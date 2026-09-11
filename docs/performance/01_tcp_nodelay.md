# TCP_NODELAY 性能定位与第一阶段优化

## 1. Optimization Goal

本轮工作的起点不是预设某个优化方案，而是先通过 benchmark 建立当前服务的 baseline，再根据观测到的现象定位实际瓶颈。目标是用可复现的测量、代码路径检查和网络抓包缩小问题范围，而不是先假定 Redis、搜索计算或线程池一定需要优化。

## 2. Test Environment

- Ubuntu 24.04 WSL2
- GCC 13
- Redis 7.0 via Docker
- redis-plus-plus 1.3.15
- localhost TCP benchmark
- 协议：native `size_t` length header + UTF-8 JSON body
- benchmark：Python asyncio

以下结果均来自同一台机器的 localhost、热缓存场景，不代表远程网络条件或生产环境容量。协议长度头使用本机 `size_t`，测试客户端和服务端运行在兼容的 Ubuntu WSL2 环境中。

## 3. Baseline V1

统一参数：

- concurrency：1 / 5 / 10 / 20 / 50 / 100
- requests per connection：100
- warmup per connection：5
- msgID=1：query=`linux`
- msgID=2：query=`搜索`

两个 query 均已人工确认可以正常返回业务结果。

### msgID=1 关键词推荐

| Concurrency | QPS | Avg ms | P50 ms | P95 ms | P99 ms | Error |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 21.32 | 46.811 | 47.718 | 48.421 | 48.588 | 0% |
| 5 | 104.97 | 47.353 | 47.752 | 51.865 | 52.789 | 0% |
| 10 | 204.61 | 48.441 | 48.041 | 52.569 | 55.492 | 0% |
| 20 | 424.66 | 46.494 | 44.493 | 52.660 | 58.331 | 0% |
| 50 | 1064.82 | 46.249 | 44.808 | 51.350 | 60.603 | 0% |
| 100 | 2071.10 | 47.146 | 47.359 | 52.219 | 59.441 | 0% |

### msgID=2 网页检索

| Concurrency | QPS | Avg ms | P50 ms | P95 ms | P99 ms | Error |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 22.20 | 44.935 | 44.082 | 47.958 | 48.291 | 0% |
| 5 | 109.82 | 45.196 | 44.205 | 49.161 | 51.072 | 0% |
| 10 | 216.80 | 45.749 | 44.778 | 50.442 | 52.457 | 0% |
| 20 | 427.20 | 46.380 | 45.176 | 52.292 | 56.509 | 0% |
| 50 | 997.05 | 49.436 | 47.333 | 66.707 | 74.015 | 0% |
| 100 | 1932.55 | 50.800 | 46.939 | 84.691 | 114.841 | 0% |

## 4. Initial Observation

- 两类不同业务在单连接热缓存场景下都稳定出现约 44–48 ms 的 P50。
- msgID=1 使用 Redis 热缓存，msgID=2 使用本地 LRU 热缓存，但延迟非常接近。
- 因此首先怀疑两者共享的 TCP/协议路径，而不是 Redis、分词、倒排索引或搜索计算。

## 5. Hypotheses and Elimination

依次检查过以下候选原因：

- 控制台日志；
- Redis/搜索计算；
- ThreadPool / lock / eventfd / `epoll_wait`；
- timer/sleep；
- TCP 小包发送。

服务端 stdout/stderr 重定向到 `/dev/null` 后重新测试：

- msgID=1 P50 = 43.939 ms
- msgID=2 P50 = 47.003 ms

结果与 V1 的固定延迟量级一致，因此控制台日志不是主要原因。两个热缓存路径的相近延迟也削弱了 Redis 或搜索计算为主因的可能性。代码检查未发现处于每个请求必经路径上的固定 sleep；`eventfd` 用于唤醒 EventLoop，timer 用于周期任务，二者均不能直接解释稳定约 45 ms 的单请求等待。

## 6. TCP Analysis

代码检查发现：

- benchmark 请求已将 header + body 合并为一次 `writer.write(...)`；
- 服务端 `TcpConnection::send` 仍先发送 8-byte `size_t` header，再发送 JSON body；
- accepted socket 未设置 `TCP_NODELAY`。

`tcpdump` 重复观察到以下模式：

```text
server -> client: 8-byte length header
~41–44 ms
client -> server: ACK
~25–30 us
server -> client: JSON body
```

这说明服务端第二段 body 在前一个小包尚未 ACK 时被暂缓，与 Nagle algorithm + delayed ACK 的组合行为一致。

## 7. Optimization

第一阶段严格采用单变量修改：

- 只对服务端 accept 得到的连接启用 `TCP_NODELAY`；
- 不合并 header/body；
- 不修改 ThreadPool；
- 不修改 TaskQueue；
- 不修改 eventfd/epoll；
- 不修改缓存；
- 不修改协议。

实现路径为 `EventLoop::handleNewConnection()` 获取已连接 fd，构造 `TcpConnection`，随后由其持有的 `Socket` 调用封装的 `setNoDelay()`。这样 V1/V2 的差异能够归因于这一项 socket 选项修改。

## 8. V2 Results

### msgID=1 关键词推荐

| Concurrency | QPS | Avg ms | P50 ms | P95 ms | P99 ms | Error |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1506.26 | 0.605 | 0.575 | 0.801 | 0.901 | 0% |
| 5 | 3761.56 | 1.270 | 1.243 | 1.761 | 2.208 | 0% |
| 10 | 2596.94 | 3.746 | 3.133 | 7.483 | 11.902 | 0% |
| 20 | 3763.06 | 5.236 | 5.124 | 6.385 | 7.247 | 0% |
| 50 | 3684.45 | 13.444 | 12.859 | 20.492 | 29.852 | 0% |
| 100 | 3814.66 | 26.093 | 24.567 | 31.987 | 33.302 | 0% |

### msgID=2 网页检索

| Concurrency | QPS | Avg ms | P50 ms | P95 ms | P99 ms | Error |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1450.26 | 0.488 | 0.457 | 0.714 | 0.773 | 0% |
| 5 | 3039.50 | 1.474 | 1.434 | 1.836 | 2.289 | 0% |
| 10 | 2969.99 | 3.176 | 3.081 | 3.760 | 4.480 | 0% |
| 20 | 2954.73 | 6.557 | 6.443 | 7.681 | 8.478 | 0% |
| 50 | 2739.28 | 17.980 | 17.664 | 21.608 | 23.430 | 0% |
| 100 | 2722.70 | 36.371 | 35.252 | 46.018 | 59.203 | 0% |

## 9. Before / After

- msgID=1、concurrency=1：P50 `47.718 ms → 0.575 ms`；QPS `21.32 → 1506.26`。
- msgID=2、concurrency=1：P50 `44.082 ms → 0.457 ms`；QPS `22.20 → 1450.26`。
- msgID=1、concurrency=100：QPS `2071.10 → 3814.66`；P50 `47.359 ms → 24.567 ms`。
- msgID=2、concurrency=100：QPS `1932.55 → 2722.70`；P50 `46.939 ms → 35.252 ms`。

其中 60–70 倍级的变化只发生在 localhost、热缓存、单连接测试场景，不能概括为系统在任意网络或生产负载下均有同等倍数提升。

## 10. Conclusion

benchmark 首先暴露了约 45 ms 的固定延迟；通过代码路径分析、日志排除和 tcpdump 抓包，确认服务端拆分发送且未设置 `TCP_NODELAY` 导致 Nagle/Delayed ACK 等待。采用单变量修改启用 `TCP_NODELAY` 后，固定延迟消失，单连接热缓存请求进入亚毫秒级，同时高并发吞吐得到明显提升。

## 11. Remaining Bottleneck

优化后观察到：

- msgID=1 在更高并发下吞吐约进入 3.7k–3.8k QPS 平台；
- msgID=2 大致进入 2.7k–3.0k QPS 平台；
- 并发提高后主要表现为排队延迟增加。

下一阶段需要重新定位真实瓶颈；ThreadPool、TaskQueue、eventfd 回写、Redis 路径或单 EventLoop 都只是候选方向，目前不应写成已确认根因。

## 12. Methodology / Limitations

- V1 与 V2 每个并发档均只执行一次；
- 测试在同一台机器 localhost 环境完成；
- 使用热缓存和固定 query；
- 数据用于项目阶段性性能分析，不代表生产环境容量；
- 后续比较必须继续保持相同 benchmark 参数。
