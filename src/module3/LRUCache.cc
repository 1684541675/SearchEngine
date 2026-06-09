#include "LRUCache.h"
#include "Thread.h"
#include <iostream>
using std::cout;
using std::endl;


namespace searchengine
{

extern __thread size_t __thread_id; // 工作线程的编号（0, 1, 2, ... , _workerNum-1）

LRUCache::LRUCache(size_t capacity)
:_capacity(capacity)
{

}

bool LRUCache::isHit(const string &query)
{
    return _hashMap.count(query) > 0;
}

string LRUCache::getRecord(const string &query)
{
    if (!isHit(query))
    {
        return "";
    }
    cout << "No." << __thread_id << " cache hit!" << endl;
    // 将 query 的记录移至 _resultList 头部
    _resultList.splice(_resultList.begin(), _resultList, _hashMap[query]);
    return _hashMap[query]->second;
}

void LRUCache::insertRecord(const string &query, const string &result)
{
    if (isHit(query))
    {
        // 将 query 的记录移至 _resultList 头部
        _resultList.splice(_resultList.begin(), _resultList, _hashMap[query]);
        _hashMap[query]->second = result;
    }
    else
    {
        if (_resultList.size() == _capacity)      // _resultList 已满
        {
            auto back_key = _resultList.back().first; // 暂存尾部记录的 query
            _resultList.pop_back();                   // 删除 _resultList 尾部记录
            _hashMap.erase(back_key);                 // 更新 _hashMap
        }
        _resultList.push_front({query, result}); // 将记录 <query, string> 插入 _resultList 头部
        _hashMap[query] = _resultList.begin();   // 更新 _hashMap
    }
}

void LRUCache::clear()
{
    _resultList.clear();
    _hashMap.clear();
}

void LRUCache::update(const LRUCache &cache)
{
    for (auto it = cache._resultList.rbegin(); it != cache._resultList.rend(); ++it)
    {
        insertRecord(it->first, it->second);
    }
}

size_t LRUCache::size() const
{
    return _resultList.size();
}

}
