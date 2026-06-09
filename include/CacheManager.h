#pragma once
#include "CacheGroup.h"
#include <deque>
using std::deque;

namespace searchengine
{   

/*
************************************************************
 *
 *  cache group 管理类（单例类）
 *
 ************************************************************
 */
class CacheManager
{
public:
    static CacheManager &getInstance();

    CacheGroup &getCacheGroup(size_t);

    void sync();

private:
    CacheManager();
    ~CacheManager()=default;
    CacheManager(const CacheManager &) = delete;
    CacheManager &operator=(const CacheManager &) = delete;

private:
    size_t _cacheNums;          // cache group 总数（即工作线程总数）
    size_t _maxRecord;          // 一块 LRU cache 的最大记录数
    deque<CacheGroup> _caches;  // 所有线程的 cache group
};

}
