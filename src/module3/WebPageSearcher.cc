#include "WebPageSearcher.h"
#include "Configuration.h"
#include "MyLog.h"
#include "MultiBytesCharacter.h"
#include "../../3rdparty/json-develop/include/nlohmann/json.hpp"
#include "fifo_map.hpp"
using namespace nlohmann;
/* 以下为 nlohmann/json 库使用，保证插入顺序不变 */
template <class K, class V, class dummy_compare, class A>
using my_workaround_fifo_map = fifo_map<K, V, fifo_map_compare<K>, A>;
using my_json = basic_json<my_workaround_fifo_map>;
using Json = my_json;

#include <fstream>
#include <sstream>
#include <utility>

using std::ifstream;
using std::stringstream;
using std::move;
using std::pair;

namespace searchengine
{

WebPageSearcher::WebPageSearcher()
{
    loadFromFile();
}

/**
 *  从磁盘中读入三个库（网页库，倒排索引库，停用词库）
 *
 *  1. 偏移库无需存入内存，仅在读入网页库时借用该库信息而已
 */
void WebPageSearcher::loadFromFile()
{
    // 读入停用词库
    ifstream stopWordsLib(Configuration::getInstance().getConfigMap()["stopwords"]);
    if (!stopWordsLib)
    {
        perror("can not open stop_words.utf8");
        exit(EXIT_FAILURE);
    }
    string word;
    while (getline(stopWordsLib, word))
    {
        _stopWords.insert(word);
    }

    // 读入网页库
    ifstream offsetLib(Configuration::getInstance().getConfigMap()["offset"]);
    if (!offsetLib)
    {
        perror("can not open offset.dat");
        exit(EXIT_FAILURE);
    }
    ifstream ripepageLib(Configuration::getInstance().getConfigMap()["ripepage"]);
    if (!ripepageLib)
    {
        perror("can not open ripepage.dat");
        exit(EXIT_FAILURE);
    }
    string offsetLine;
    int docid;
    size_t beg, offset;
    while (getline(offsetLib, offsetLine))
    {
        stringstream ss(offsetLine);
        ss >> docid >> beg >> offset;
        string doc(offset, '\0');
        ripepageLib.seekg(beg);
        ripepageLib.read(&doc[0], offset);
        WebPage tmp(doc);
        _pageList.push_back(move(tmp));
    }

    // 读入倒排索引库
    ifstream invertIndexLib(Configuration::getInstance().getConfigMap()["invertIndex"]);
    if (!invertIndexLib)
    {
        perror("can not open invertIndex.dat");
        exit(EXIT_FAILURE);
    }
    string invertIndexLine;
    string keyWord;
    double weight;
    while (getline(invertIndexLib, invertIndexLine))
    {
        stringstream ss(invertIndexLine);

        ss >> keyWord;
        while (ss)
        {
            ss >> docid >> weight;
            _invertIndexTable[keyWord][docid] = weight;
        }
    }

    stopWordsLib.close();
    offsetLib.close();
    ripepageLib.close();
    invertIndexLib.close();
}
/**
 *  查询网页信息
 *
 *  1. msg 已经序列化后的，由客户发送的查询语句
 *  2. 返回所有网页信息，并且已经序列化为 json 格式
 */
string WebPageSearcher::doQuery(string msg)
{
    WebPage pageX;
    pageX.setPageContent(msg);               // 将 msg 作为 content 创建网页 pageX
    pageX.splitWord(_splitTool, _stopWords); // 对 pageX 分词并统计词频

    unordered_map<string, double> vecX = getVectorX(pageX); // 获取向量 vecX

    string response;
    unordered_set<PageID> IDs = getIDs(pageX); // 获取候选文章的编号集合 IDs（取交集）
   
    if (IDs.empty())
    {
        LogInfo("webPageSearcher miss: %s", msg.c_str());
        response = serializeForNothing(); // 获取未找到网页的序列化信息
    }
    else
    {
        vector<PageID> sortedIDs = getSortedIDs(vecX, IDs); // 获取排序后的候选文章的编号集合 sortedIDs

        setSummarys(sortedIDs, pageX); // 为所有候选文章设置摘要信息

        if (sortedIDs.empty())
        {
            response = serializeForNothing();
        }
        else
        {
            response = serialize(sortedIDs);
        }
    }

    return response;
}
/**
 *  求网页 pageX 的向量 vexX
 */
unordered_map<string, double> WebPageSearcher::getVectorX(WebPage &pageX)
{
    unordered_map<string, double> vecX;                          // 查询向量 X
    unordered_map<string, int> &wordsMapX = pageX.getWordsMap(); // <word, freq>

    int totalWords = pageX.getTotalwords();
    if (totalWords == 0)
    {
        return vecX;
    }

    int N = _pageList.size();
    if (N == 0)
    {
        return vecX;
    }

    double sumWeight = 0.0;

    for (auto &wordPair : wordsMapX)
    {
        const string &word = wordPair.first;
        int freq = wordPair.second;

        auto it = _invertIndexTable.find(word);
        if (it == _invertIndexTable.end())
        {
            continue;
        }

        double TF = static_cast<double>(freq) / totalWords;
        int DF = it->second.size();

        double IDF = log10(static_cast<double>(N + 1) / (DF + 1));
        double w = TF * IDF;

        vecX.insert({word, w});
        sumWeight += w * w;
    }

    if (sumWeight == 0.0)
    {
        return vecX;
    }

    double norm = sqrt(sumWeight);

    for (auto &wordPair : vecX)
    {
        wordPair.second /= norm;
    }

    return vecX;
}

/**
 *  获取候选文章的编号集合
 */
unordered_set<PageID> WebPageSearcher::getIDs(WebPage &pageX)
{
    unordered_map<string, int> &wordsMapX = pageX.getWordsMap();

    unordered_set<PageID> result;
    bool first = true;

    for (auto &wordPair : wordsMapX)
    {
        const string &word = wordPair.first;

        auto it = _invertIndexTable.find(word);
        if (it == _invertIndexTable.end())
        {
            return {};
        }

        // 第一个词：直接把它对应的所有 docID 放进 result
        if (first)
        {
            for (auto &docWeightPair : it->second)
            {
                result.insert(docWeightPair.first);
            }
            first = false;
        }
        else
        {
            // 后续词：保留 result 中也出现在当前词倒排表里的 docID
            unordered_set<PageID> tmp;

            for (auto &id : result)
            {
                if (it->second.count(id) != 0)
                {
                    tmp.insert(id);
                }
            }

            result.swap(tmp);
        }

        if (result.empty())
        {
            return {};
        }
    }

    return result;
}

/**
 *  根据余弦相似度算法，对候选文章的 ID 集合进行排序
 *
 *  1. vecX 为向量 X
 *  2. IDs 为候选文章的 ID 集合
 */
struct MyGreater
{
    bool operator()(const pair<double, PageID> &lhs, const pair<double, PageID> &rhs) const
    {
        if (lhs.first != rhs.first)
        {
            return lhs.first > rhs.first;
        }
        else
        {
            return lhs.second < rhs.second;
        }
    }
};

vector<PageID> WebPageSearcher::getSortedIDs(
    const unordered_map<string, double> &vecX,
    const unordered_set<PageID> &IDs)
{
    vector<pair<double, PageID>> scores;
    scores.reserve(IDs.size());

    for (auto &id : IDs)
    {
        double score = 0.0;

        for (auto it = vecX.begin(); it != vecX.end(); ++it)
        {
            const string &word = it->first;
            double xWeight = it->second;

            auto wordIt = _invertIndexTable.find(word);
            if (wordIt == _invertIndexTable.end())
            {
                continue;
            }

            auto docIt = wordIt->second.find(id);
            if (docIt == wordIt->second.end())
            {
                continue;
            }

            score += xWeight * docIt->second;
        }
        scores.emplace_back(score, id);
    }

    sort(scores.begin(), scores.end(), MyGreater());

    vector<PageID> result;
    result.reserve(scores.size());

    for (auto &scorePair : scores)
    {
        result.push_back(scorePair.second);
    }

    return result;
}
/**
 *  设置所有候选网页的摘要信息
 */
void WebPageSearcher::setSummarys(vector<PageID> &sortedIDs, WebPage &pageX)
{
    const size_t STEP = 40;
    unordered_map<string, int> &wordsMapX = pageX.getWordsMap();

    // 最多只需要生成 maxpagenum 个摘要
    size_t maxPageNum = 0;
    auto it = Configuration::getInstance().getConfigMap().find("maxpagenum");
    if (it != Configuration::getInstance().getConfigMap().end())
    {
        maxPageNum = stoul(it->second);
    }
    if (maxPageNum == 0)
    {
        maxPageNum = sortedIDs.size();
    }

    vector<PageID> validIDs;
    validIDs.reserve(std::min(maxPageNum, sortedIDs.size()));

    for (auto ID : sortedIDs)
    {
        if (validIDs.size() >= maxPageNum)
        {
            break;
        }

  
        const string content = _pageList[ID].getContent();

        size_t first_pos = string::npos;

        for (auto &wordPair : wordsMapX)
        {
            const string &word = wordPair.first;

            size_t pos = content.find(word);
            if (pos != string::npos && pos < first_pos)
            {
                first_pos = pos;
            }
        }

        // 正文中没有查询词，跳过这篇网页
        if (first_pos == string::npos)
        {
            continue;
        }

        size_t first_to_end = content.size() - first_pos;
        size_t right_pos = first_pos + howManyBytesWithNCharacter(
                                            content, first_pos,
                                            first_to_end,
                                            STEP);

        vector<size_t> ppc = getPosPerCharactor(content, first_pos);
        size_t left_pos = ppc.size() >= STEP ? ppc[ppc.size() - STEP] : 0;

        string summary;
        if (left_pos != 0)
        {
            summary += " ... ";
        }

        summary += content.substr(left_pos, right_pos - left_pos);

        if (right_pos < content.size())
        {
            summary += " ... ";
        }

        _pageList[ID].setPageSummary(summary);
        validIDs.push_back(ID);
    }

    sortedIDs.swap(validIDs);
}


/**
 *  返回搜索失败后的 json 格式字符串
 */
string WebPageSearcher::serializeForNothing()
{
    Json root;
    root["msgID"] = 404;
    root["msg"] = json("未能找到相关文章");
    // root["msg"] = "未能找到相关文章"; // 一样的效果

    return root.dump(4);
}
/**
 *  返回使用 json 序列化后的所有网页信息
 */
string WebPageSearcher::serialize(const vector<PageID> &sortedIDs)
{
    Json root;
    root["msgID"] = 200;

    Json msg = Json::array();

    for (auto ID : sortedIDs)
    {
        const WebPage &page = _pageList[ID];

        Json file;
        file["title"] = page.getTitle();
        file["url"] = page.getUrl();
        file["summary"] = page.getSummary();

        msg.push_back(file);
    }

    root["msg"] = msg;

    return root.dump(4);
}

}
