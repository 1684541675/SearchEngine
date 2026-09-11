#include "TaskQueue.h"
#include "MutexLockGuard.h"
#include <utility>
using std::move;

namespace searchengine
{

TaskQueue::TaskQueue(size_t capacity)
:_capacity(capacity)
,_mutex() // 调用MutexLock的构造函数，保证_mutex先于_full和_empty构造
,_full(_mutex)
,_empty(_mutex)
,_isExiting(false)
{

}

bool TaskQueue::full() const
{
    return _queue.size() == _capacity;
}

bool TaskQueue::empty() const
{
    return _queue.empty();
}

void TaskQueue::push(Task &&task)
{
    MutexLockGuard autolock(_mutex); // 保证在本函数退出前一定完成解锁，即防止了死锁的发生
    while (full())                   // 仓库满，阻塞生产者
    {
        _full.wait();
    }

    _queue.push(move(task)); // 任务入队

    _empty.notify();
    
}

TaskQueue::Task TaskQueue::pop()
{
    MutexLockGuard autolock(_mutex); // 保证在本函数退出前一定完成解锁，即防止了死锁的发生
    while (!_isExiting && empty())   // 仓库空，阻塞消费者
    {
        _empty.wait();
    }

    if (_isExiting)
    {
        return nullptr;
    }

    Task tmp = move(_queue.front());
    _queue.pop(); // 任务出队

   _full.notify();
    
    return tmp;
}

void TaskQueue::wakeupEmpty()
{
    _isExiting = true;
    _empty.notifyAll();
}

}
