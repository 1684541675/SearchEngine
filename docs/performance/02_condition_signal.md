# 优化 2：减少 TaskQueue 条件变量广播唤醒

## 1. 问题发现

在完成 TCP_NODELAY 优化后，使用 perf 对服务端进行 CPU profiling。

热点调用栈中发现：

pthread_cond_broadcast
→ Condition::notifyAll()
→ TaskQueue::pop()
→ ThreadPool::getTask()
→ ThreadPool::doTask()

同时存在较明显的 futex wake、try_to_wake_up 和内核线程唤醒开销。

## 2. 原因分析

TaskQueue 在正常生产和消费路径中均使用 notifyAll：

- push()：任务入队后 `_empty.notifyAll()`
- pop()：任务出队后 `_full.notifyAll()`

一次 push 只新增一个任务，通常只需要唤醒一个等待的 worker。
广播唤醒可能造成多个 worker 同时被唤醒并竞争 mutex，产生额外的线程调度和同步开销。

pop() 每次也只释放一个队列槽位，因此正常路径没有必要广播所有等待线程。

线程池退出时需要唤醒所有 worker，因此 wakeupEmpty() 中的 notifyAll 保持不变。

## 3. 修改方案

将正常生产/消费路径：

`notifyAll()`

修改为：

`notify()`

即：

- `_empty.notifyAll()` → `_empty.notify()`
- `_full.notifyAll()` → `_full.notify()`

退出路径：

`_empty.notifyAll()`

保持不变。

## 4. A/B Benchmark

测试条件：

- concurrency: 100
- requests per connection: 100
- total requests: 10000
- msgID: 1
- query: linux
- warmup: 5
- 每种版本连续运行 5 次

| 指标 | 优化前均值 | 优化后均值 | 变化 |
| --- | ---: | ---: | ---: |
| QPS | 3318.15 | 3430.38 | +3.38% |
| Avg latency | 29.995 ms | 29.006 ms | -3.30% |
| P50 | 28.610 ms | 27.499 ms | -3.88% |
| P95 | 39.673 ms | 37.590 ms | -5.25% |
| P99 | 46.664 ms | 43.366 ms | -7.07% |
| Error rate | 0% | 0% | 不变 |

## 5. perf 复测

修改前正常请求路径中存在：

pthread_cond_broadcast
→ Condition::notifyAll()
→ TaskQueue::pop()

修改后变为：

pthread_cond_signal
→ Condition::notify()
→ TaskQueue::pop()

正常请求采样中未再观察到 TaskQueue 的 notifyAll / pthread_cond_broadcast 路径。

## 6. 结论

通过将 TaskQueue 正常生产/消费路径的广播唤醒改为单线程唤醒，
减少了不必要的线程唤醒和锁竞争。

5 轮 A/B 测试中：

- QPS 提升约 3.38%
- 平均延迟下降约 3.30%
- P95 延迟下降约 5.25%
- P99 延迟下降约 7.07%
- 错误率保持 0%
