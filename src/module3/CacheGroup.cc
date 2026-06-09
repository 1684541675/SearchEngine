#include "CacheGroup.h"
#include "MutexLockGuard.h"

namespace searchengine
{

CacheGroup::CacheGroup(size_t capacity)
:_mainCache(capacity)
,_pendingUpdateCache(capacity)
{

}

string CacheGroup::getRecord(const string &query)
{
    MutexLockGuard guard(_mutex);
    return _mainCache.getRecord(query);
}

void CacheGroup::insertRecord(const string &query, const string &result)
{
    MutexLockGuard guard(_mutex);
    _mainCache.insertRecord(query, result);
    _pendingUpdateCache.insertRecord(query, result);
}

void CacheGroup::update(const CacheGroup &group)
{
    MutexLockGuard guard(_mutex);
    _mainCache.update(group._mainCache);
}

}
