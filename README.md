# SearchEngine

基于 Linux C++ 实现的教学/学习型小型搜索引擎，支持关键词推荐与网页检索。项目包含离线数据构建与在线查询服务两部分：

- 离线阶段：解析 RSS/XML 网页数据，完成 Simhash 去重、中文分词、词典构建、网页库构建和倒排索引构建。
- 在线阶段：使用 Socket 与基于 epoll 的 Reactor 风格事件循环提供 TCP 服务；线程池处理业务任务，Redis 与本地 LRUCache 缓存热点查询结果。

> 已在 Ubuntu 24.04 WSL2（GCC 13）完成编译、基础联调及 localhost 热缓存 benchmark。hiredis、log4cpp 通过 apt 安装；redis-plus-plus 1.3.15 安装在 `$HOME/.local`；Redis 7.0 通过 Docker 运行。

## 技术栈

- 语言与平台：C++、Linux
- 网络编程：Socket、TCP、epoll、基于 epoll 的 Reactor 风格事件循环、eventfd
- 并发编程：pthread、线程池、任务队列、互斥锁、条件变量
- 搜索相关：CppJieba、Simhash、TF-IDF、倒排索引、编辑距离
- 数据与缓存：Redis、LRUCache
- 序列化与解析：nlohmann/json、tinyxml2
- 构建与工具：GCC/G++、Makefile、Git、Docker

## 目录结构

```text
.
├── 3rdparty/                 # simhash-cppjieba、nlohmann/json
├── conf/
│   └── myconf.conf
├── data/                     # 离线构建产物
├── docs/
│   └── performance/          # 性能定位与实验记录
├── include/                  # 项目头文件
├── log/                      # 日志文件
├── src/
│   ├── module1/              # 离线词典构建
│   ├── module2/              # 网页库与倒排索引构建
│   ├── module3/              # 在线搜索服务端
│   └── module4/              # 命令行客户端
├── tools/
│   └── benchmark.py          # 本机 asyncio benchmark
├── yuliao/                   # 原始语料和停用词
├── Makefile
└── README.md
```

## 核心模块

### 离线词典构建

`src/module1`：`DictProducer` 读取中英文语料并生成词典及词典索引；`SplitTool` 封装中文分词。产物包括 `dict.dat`、`dictIndex.dat`、`enDict.dat`、`enDictIndex.dat`。

### 网页库与倒排索引构建

`src/module2`：`DirScanner` 扫描 XML，`RssParser` 解析 RSS；`PageProcesser` 完成网页加载、Simhash 去重、分词和词频统计；`InvertIndexProcesser` 基于 TF-IDF 生成倒排索引，`OffsetProcesser` 生成按 docid 读取网页的偏移信息。

### 在线服务端

`src/module3`：`Acceptor` 监听并接入连接；`EventLoop` 使用 epoll 分发连接、可读和 eventfd 唤醒事件；`TcpConnection` 维护单连接状态与协议收发；`ThreadPool` / `TaskQueue` 执行业务任务。工作线程完成查询后，通过 pending callback + eventfd 唤醒 I/O 线程，由 I/O 线程回写响应。

- `KeyRecommender`：按编辑距离、词频和字典序返回候选词。
- `WebPageSearcher`：加载网页库与倒排索引，以向量相似度完成相关度排序。
- `CacheManager` / `CacheGroup` / `LRUCache`：管理网页检索本地缓存；`TimerThread` 周期同步各工作线程的本地缓存。

### 命令行客户端

`src/module4` 通过 TCP 连接服务端，支持 `1` 关键词推荐、`2` 网页检索和 `3` 退出。

## 请求处理流程

```text
Client
  |  native size_t length header + JSON body
  v
EventLoop (epoll readable event)
  v
TcpConnection::recvMessages()
  v
EchoServer::onMessage() -> ThreadPool -> MyTask::process()
  |                              | msgID=1: KeyRecommender
  |                              | msgID=2: WebPageSearcher
  v
TcpConnection::notifyLoop()
  v
pending callback + eventfd wakeup
  v
EventLoop::handlePendingCbs() -> TcpConnection::send() -> Client
```

## 通信协议

请求和响应均采用“长度头 + JSON 正文”：

```text
native size_t length
UTF-8 JSON body
```

请求示例：

```json
{
  "msgID": 1,
  "msg": "keyword"
}
```

- `msgID = 1`：关键词推荐；成功响应为 `100`
- `msgID = 2`：网页检索；成功响应为 `200`
- `msgID = 404`：业务未命中

## 环境、构建与运行

Makefile 将 `$HOME/.local/lib/pkgconfig` 导出给 `pkg-config`，并用其获取 redis-plus-plus、hiredis、log4cpp 的编译与链接参数。

### 1. 启动 Redis

```bash
docker start searchengine-redis
docker exec searchengine-redis redis-cli ping
```

预期输出：`PONG`。以上命令直接在容器内运行 `redis-cli`，不要求 WSL 主机额外安装该客户端。

### 2. 构建离线数据

```bash
cd ~/projects/searchengine
make module1
cd src/module1 && ./a.out

cd ~/projects/searchengine
make module2
cd src/module2 && ./a.out
```

### 3. 启动服务端

```bash
cd ~/projects/searchengine
make run-server
```

### 4. 启动命令行客户端

另开终端：

```bash
cd ~/projects/searchengine
make run-client
```

可分别验证关键词推荐、网页检索以及 Redis/LRUCache 重复查询命中行为。

## Benchmark

`tools/benchmark.py` 是独立的 Python asyncio 本机压测客户端，严格复用当前 native `size_t` 长度头 + UTF-8 JSON body 协议，不修改服务端、线程池、缓存或协议实现。

先启动服务端：

```bash
cd ~/projects/searchengine
make run-server
```

再在另一终端执行：

```bash
cd ~/projects/searchengine
python3 tools/benchmark.py --concurrency 5 --requests-per-connection 20 --msg-id 1 --query linux --warmup 2 --show-sample
python3 tools/benchmark.py --concurrency 5 --requests-per-connection 20 --msg-id 2 --query 搜索 --warmup 2 --show-sample
```

可通过 `--host`、`--port`、`--timeout`、`--max-response-bytes`、`--show-sample` 调整目标与保护阈值。输出包含 total requests、protocol success、business success、business miss / 404、响应 msgID 计数、错误样本、QPS、平均延迟和 P50/P95/P99；warmup 请求不计入统计。

## 性能优化

第一阶段 benchmark 定位并修复了服务端响应链路中的固定等待：仅对服务端 accept 后的连接设置 `TCP_NODELAY`，未合并 header/body，也未改动线程池、缓存、eventfd/epoll 或协议。

代表性 V1/V2 对照：

| 场景 | V1 | V2 |
| --- | ---: | ---: |
| Keyword recommendation，concurrency=1，P50 | 47.718 ms | 0.575 ms |
| Web search，concurrency=1，P50 | 44.082 ms | 0.457 ms |
| Keyword recommendation，concurrency=100，QPS | 2071.10 | 3814.66 |
| Web search，concurrency=100，QPS | 1932.55 | 2722.70 |

数据来自 Ubuntu 24.04 WSL2 的 localhost、warm-cache、固定 query 场景；V1/V2 每个并发档仅测试一次，不代表远程网络或生产环境容量。完整定位过程、tcpdump 证据、全部数据和限制见 [TCP_NODELAY 性能记录](docs/performance/01_tcp_nodelay.md)。

## 配置文件

主要配置位于 `conf/myconf.conf`，包括 CppJieba 词典、语料、停用词、离线产物、服务端 IP/端口、工作线程数、缓存容量、最大返回数量和缓存同步间隔。

配置文件路径以各模块运行目录为基准；例如服务端从 `src/module3` 启动时使用 `../../conf/myconf.conf`。当前默认监听 `127.0.0.1:1234`，工作线程数为 `5`。

## 项目亮点

- 将网络 I/O 与业务计算分离：epoll 事件循环负责连接/读事件和回写调度，线程池执行搜索任务。
- 使用 pending callback + eventfd 将工作线程结果安全交回 I/O 线程。
- 离线阶段组合 Simhash、分词、TF-IDF 与倒排索引；在线阶段提供关键词推荐与网页相关度排序。
- Redis 缓存关键词推荐结果，本地 LRUCache 缓存网页检索结果；本地缓存不是分布式缓存架构。
- 通过 asyncio benchmark、代码路径检查和 tcpdump 定位 TCP 小包等待，并以单变量 `TCP_NODELAY` 实验完成验证。

## 当前限制

- 项目用于学习 Linux C++ 后端开发流程，是小型搜索引擎，不是工业级或生产级搜索系统。
- 当前协议使用 native `size_t` 长度头，未处理跨架构、跨字节序兼容性。
- 当前实现是基于 epoll 的 Reactor 风格事件循环，不是完整的全非阻塞 Reactor：连接写回尚未实现基于 `EPOLLOUT` 的输出缓冲与背压处理。
- Redis 与本地 LRUCache 用于热点查询缓存，不构成分布式缓存架构。
- benchmark 是本机 localhost 测试；后续性能比较应维持相同参数，并在多次运行后再报告稳定统计。