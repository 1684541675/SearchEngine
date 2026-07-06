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

#include <chrono>
#include <iostream>
#include <sw/redis++/errors.h>
using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::steady_clock;
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
        bool redisAvailable = true;
        auto cacheStart = steady_clock::now();

        try
        {
            auto result = _redis.get(key);
            if (result)
            {
                response = result.value();
                auto costUs = duration_cast<microseconds>(steady_clock::now() - cacheStart).count();
                cout << "[Redis] hit keyword: " << word
                     << ", cost: " << costUs << " us" << endl;
            }
            else
            {
                LogInfo("\n\tredis miss: %s", word.c_str());
            }
        }
        catch (const sw::redis::Error &)
        {
            redisAvailable = false;
            cout << "[Redis] unavailable, fallback to dictionary" << endl;
        }

        if (response.empty())
        {
            bool storedToRedis = false;
            bool storeSkipped = false;
            response = _recommender.doQuery(word);
            if (redisAvailable)
            {
                try
                {
                    _redis.setex(key, 60, response);
                    storedToRedis = true;
                }
                catch (const sw::redis::Error &)
                {
                    storeSkipped = true;
                }
            }

            auto costUs = duration_cast<microseconds>(steady_clock::now() - cacheStart).count();
            if (redisAvailable)
            {
                cout << "[Redis] miss keyword: " << word
                     << ", cost: " << costUs << " us";
                if (storedToRedis)
                {
                    cout << ", cached";
                }
                else if (storeSkipped)
                {
                    cout << ", store skipped";
                }
                cout << endl;
            }
            else
            {
                cout << "[Redis] fallback keyword: " << word
                     << ", cost: " << costUs << " us" << endl;
            }
        }
    }
    else if (2 == msgID)
    {
        string query = root["msg"];

        auto &pManager = CacheManager::getInstance();
        auto &cacheGroup = pManager.getCacheGroup(__thread_id);
        auto cacheStart = steady_clock::now();

        if ((response = cacheGroup.getRecord(query)) == "")
        {
            LogInfo("\n\tLRU miss: %s", query.c_str());
            response = _webPageSearcher.doQuery(query);
            cacheGroup.insertRecord(query, response);
            auto costUs = duration_cast<microseconds>(steady_clock::now() - cacheStart).count();
            cout << "[LRU] miss query: " << query
                 << ", cost: " << costUs << " us, cached" << endl;
        }
        else
        {
            auto costUs = duration_cast<microseconds>(steady_clock::now() - cacheStart).count();
            cout << "[LRU] hit query: " << query
                 << ", cost: " << costUs << " us" << endl;
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
