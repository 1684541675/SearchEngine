#include "MyTask.h"
#include "MyLog.h"
#include "CacheManager.h"
#include "../../3rdparty/json-develop/include/nlohmann/json.hpp"
#include "fifo_map.hpp"
using namespace nlohmann;
/* 以下为 nlohmann/json 库使用，保证插入顺序不变 */
template <class K, class V, class dummy_compare, class A>
using my_workaround_fifo_map = fifo_map<K, V, fifo_map_compare<K>, A>;
using my_json = basic_json<my_workaround_fifo_map>;
using Json = my_json;

#include <iostream>
using std::cout;
using std::endl;

namespace searchengine
{

extern __thread size_t __thread_id; // 工作线程的编号（0, 1, 2, ... , _workerNum-1）

MyTask::MyTask(const string &msg, const TcpConnectionPtr &connPtr, WebPageSearcher &webPageSearcher, KeyRecommender &recommender, sw::redis::Redis &redis)
:_msg(msg)
,_connPtr(connPtr)
,_webPageSearcher(webPageSearcher)
,_recommender(recommender)
,_redis(redis)
{

}     

void MyTask::process() // 由子线程（TheadPool）调用！！！
{

#ifdef __DEBUG__
    printf("\t(File:%s, Func:%s(), Line:%d)\n", __FILE__, __FUNCTION__, __LINE__);
    cout << _msg << endl;
#endif

    string response;

    Json root = Json::parse(_msg); // 解析 _msg
    size_t msgID = root["msgID"];
    if (1 == msgID)
    {
        string word = root["msg"];
        string key = word;
        auto result = _redis.get(key); // 查询 redis
        if (result)
        {
            response = result.value();
            cout << "[Redis] hit keyword: " << word << endl;
        }
        else
        {
            LogInfo("\n\tredis miss: %s", word.c_str());
            response = _recommender.doQuery(word); // 查询词典（在 doQuery 中序列化）
            _redis.setex(key, 60, response);
            cout << "[Redis] store keyword: " << word << endl;
        }
    }
    else if (2 == msgID)
    {
        string query = root["msg"];

        auto &pManager = CacheManager::getInstance();
        // 查 LRU 缓存，若命中直接发送
        if ((response = pManager.getCacheGroup(__thread_id).getRecord(query)) == "")
        {
            // 若未命中
            // 将 response 插入
            LogInfo("\n\tLRU miss: %s", query.c_str());
            response = _webPageSearcher.doQuery(query);
            pManager.getCacheGroup(__thread_id).insertRecord(query, response);
            cout << "[LRU] store query: " << query << endl;
        }
        else
        {
            cout << "[LRU] hit query: " << query << endl;
        }
    }
    else
    {
        fprintf(stderr,"Error: msgID = %lu", msgID);
        response = "";
    }

    // send
    _connPtr->notifyLoop(response); // 注意，response 是已经序列化后的字符串
}

}
