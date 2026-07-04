#include "CacheManager.h"
#include "Configuration.h"
#include "MutexLockGuard.h"
#include <iostream>
using std::cout;    
using std::endl;

namespace searchengine
{

CacheManager &CacheManager::getInstance()
{
    static CacheManager instance;
    return instance;
}

CacheGroup &CacheManager::getCacheGroup(size_t idx)
{
    return _caches[idx];
}

CacheManager::CacheManager()
:_cacheNums(stoul(Configuration::getInstance().getConfigMap()["workernum"]))
,_maxRecord(stoul(Configuration::getInstance().getConfigMap()["recordnum"]))
{
    for (size_t idx = 0; idx < _cacheNums; ++idx)
    {
        _caches.emplace_back(_maxRecord);
    }
}


void CacheManager::sync()
{
    if (_caches.empty())
    {
        return;
    }

    size_t pendingRecordCount = 0;
    auto &first_group = _caches[0];
    MutexLockGuard firstGuard(first_group._mutex);

    for (size_t idx = 0; idx < _caches.size(); ++idx)
    {
        auto &group = _caches[idx];
        if (idx != 0)
        {
            group._mutex.lock();
        }

        auto &pendingCache = group._pendingUpdateCache;
        pendingRecordCount += pendingCache.size();
#ifdef __DEBUG__
        cout << "group._pengdingCache.size() = " << pendingCache.size() << endl;
#endif
        for (auto it = pendingCache._resultList.rbegin(); it != pendingCache._resultList.rend(); ++it)
        {
            first_group._mainCache.insertRecord(it->first, it->second);
#ifdef __DEBUG__
            cout << "first_group._mainCache.size() = " << first_group._mainCache.size() << endl;
#endif
        }
        pendingCache.clear();

        if (idx != 0)
        {
            group._mutex.unlock();
        }
    }


    for (size_t idx = 1; idx < _caches.size(); ++idx)
    {
        auto &group = _caches[idx];
        MutexLockGuard groupGuard(group._mutex);
        group._mainCache.update(first_group._mainCache);
    }

    if (pendingRecordCount > 0)
    {
        cout << "[Cache] synchronized " << pendingRecordCount
             << " updated record(s)" << endl;
    }

#ifdef __DEBUG__
    printf("\t(File:%s, Func:%s(), Line:%d)\n", __FILE__, __FUNCTION__, __LINE__);
    cout << "timer thread: end sync" << endl;
#endif
}

}
