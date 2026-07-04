#include "Dictionary.h"
#include "Configuration.h"

#include <iostream>
#include <fstream>
#include <sstream>

using std::cerr;
using std::endl;
using std::ifstream;
using std::istringstream;

namespace searchengine
{

Dictionary::Dictionary()
{
    auto &configMap = Configuration::getInstance().getConfigMap();

    initDict(configMap["dict"], _cnDict);
    initIndex(configMap["dictIndex"], _cnIndex);

    initDict(configMap["enDict"], _enDict);
    initIndex(configMap["enDictIndex"], _enIndex);
}

Dictionary &Dictionary::getInstance()
{
    static Dictionary singletonDict;
    return singletonDict;
}

void Dictionary::initDict(const string &dictPath, vector<pair<string, int>> &dict)
{
    ifstream ifs(dictPath);
    if (!ifs)
    {
        cerr << "ifstream open file " << dictPath << " error!" << endl;
        return;
    }

    string line;
    while (getline(ifs, line))
    {
        istringstream iss(line);
        string word;
        int freq;

        if (iss >> word >> freq)
        {
            dict.push_back({word, freq});
        }
    }
}

void Dictionary::initIndex(const string &indexPath, unordered_map<string, unordered_set<int>> &index)
{
    ifstream ifs(indexPath);
    if (!ifs)
    {
        cerr << "ifstream open file " << indexPath << " error!" << endl;
        return;
    }

    string line;
    while (getline(ifs, line))
    {
        istringstream iss(line);
        string key;
        int locate;

        if (iss >> key)
        {
            while (iss >> locate)
            {
                index[key].insert(locate);
            }
        }
    }
}

const vector<pair<string, int>> &Dictionary::getDict(DictType type) const
{
    if (type == DictType::CN)
    {
        return _cnDict;
    }
    else
    {
        return _enDict;
    }
}

const unordered_map<string, unordered_set<int>> &Dictionary::getIndexTable(DictType type) const
{
    if (type == DictType::CN)
    {
        return _cnIndex;
    }
    else
    {
        return _enIndex;
    }
}

}
