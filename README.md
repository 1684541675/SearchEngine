# SearchEngine

基于 Linux C++ 实现的小型搜索引擎服务端系统，支持关键词推荐与网页检索。项目分为离线构建和在线服务两部分：

- 离线阶段：解析 RSS/XML 网页数据，完成网页去重、中文分词、词典构建、网页库构建和倒排索引构建。
- 在线阶段：基于 Socket、epoll 和 Reactor 模型实现 TCP 服务端，通过线程池处理客户端请求，并结合本地 LRUCache 与 Redis 缓存热点查询结果。

> 说明：项目已在 Ubuntu 18.04 虚拟机环境完成编译与基础联调。外部复现需提前安装 Redis、redis-plus-plus、hiredis、log4cpp 等依赖；本文档记录的是可复现运行链路和基础验证结果，不包含高并发压测结论。

## 技术栈

- 语言与平台：C++、Linux
- 网络编程：Socket、TCP、epoll、Reactor、eventfd
- 并发编程：pthread、线程池、任务队列、互斥锁、条件变量
- 搜索相关：CppJieba、Simhash、TF-IDF、倒排索引、编辑距离
- 数据与缓存：Redis、LRUCache
- 序列化与解析：nlohmann/json、tinyxml2
- 工具：GCC/G++、Makefile 思路、Git

## 运行效果

### 客户端关键词推荐

关键词推荐命中：

![客户端关键词推荐命中](<picture/客户端关键词推荐命中.png>)

关键词推荐未命中：

![客户端关键词推荐未命中](<picture/客户端关键词推荐未命中.png>)

### 客户端网页检索

网页检索命中：

![客户端网页检索命中](<picture/客户端网页检索命中.png>)

网页检索未命中：

![客户端网页检索未命中](<picture/客户端网页检索未命中.png>)

### 服务启动与依赖验证

Redis 启动与 `PONG` 验证：

![Redis 启动与 PONG 验证截图](<picture/Redis 启动与 PONG 验证截图.png>)

服务端启动：

![服务端启动](<picture/服务端启动.png>)

## 目录结构

```text
.
├── 3rdparty/                 # 第三方库：simhash-cppjieba、nlohmann/json
├── conf/                     # 配置文件
│   └── myconf.conf
├── data/                     # 离线构建生成的数据文件
│   ├── dict.dat              # 中文词典
│   ├── dictIndex.dat         # 中文词典索引
│   ├── enDict.dat            # 英文词典
│   ├── enDictIndex.dat       # 英文词典索引
│   ├── ripepage.dat          # 网页库
│   ├── offset.dat            # 网页偏移库
│   └── invertIndex.dat       # 倒排索引库
├── include/                  # 项目头文件
├── log/                      # 日志文件
├── picture/                  # README 运行截图
├── src/
│   ├── module1/              # 离线词典构建模块
│   ├── module2/              # 离线网页库和倒排索引构建模块
│   ├── module3/              # 在线搜索服务端
│   └── module4/              # 命令行客户端
├── yuliao/                   # 原始语料和停用词
├── Makefile                  # 分模块编译辅助脚本
└── README.md                 # 项目说明文档
```

## 核心模块

### 1. 离线词典构建

对应目录：`src/module1`

主要类：

- `DictProducer`：读取中英文语料，统计词频，生成词典和词典索引。
- `SplitTool`：封装中文分词能力。
- `Configuration`：读取配置文件路径。

产物：

- `data/dict.dat`
- `data/dictIndex.dat`
- `data/enDict.dat`
- `data/enDictIndex.dat`

### 2. 离线网页库与倒排索引构建

对应目录：`src/module2`

主要类：

- `DirScanner`：扫描网页 XML 文件目录。
- `RssParser`：解析 RSS/XML 数据。
- `PageProcesser`：完成网页加载、Simhash 去重、分词和词频统计。
- `PageLib`：组织网页库、偏移库和倒排索引库的构建流程。
- `InvertIndexProcesser`：基于 TF-IDF 生成倒排索引。
- `OffsetProcesser`：生成网页偏移信息，便于按 docid 读取网页内容。

产物：

- `data/ripepage.dat`
- `data/offset.dat`
- `data/invertIndex.dat`

### 3. 在线服务端

对应目录：`src/module3`

主要类：

- `Acceptor`：负责监听 socket 的创建、地址复用、绑定、监听和 accept。
- `TcpServer`：组合 `Acceptor` 和 `EventLoop`，对外提供服务启动接口。
- `EventLoop`：基于 epoll 监听连接事件、消息事件和 eventfd 唤醒事件。
- `TcpConnection`：封装单条 TCP 连接，负责收发数据和连接状态判断。
- `ThreadPool` / `TaskQueue`：使用生产者-消费者模型处理业务任务。
- `MyTask`：根据请求类型调用关键词推荐或网页检索逻辑。
- `KeyRecommender`：基于词典索引、编辑距离、词频和字典序返回推荐词。
- `WebPageSearcher`：加载网页库和倒排索引，完成网页检索和排序。
- `CacheManager` / `CacheGroup` / `LRUCache`：管理本地缓存。
- `TimerThread` / `Timer` / `TimerTask`：定时同步各工作线程的本地缓存。

### 4. 客户端

对应目录：`src/module4`

命令行客户端通过 TCP 连接服务端，支持：

- `1`：关键词推荐
- `2`：网页检索
- `3`：退出

## 请求处理流程

```text
Client
  |
  |  length + JSON body
  v
TcpServer / EventLoop
  |
  |  epoll 监听到可读事件
  v
TcpConnection::recv()
  |
  |  EchoServer::onMessage()
  v
ThreadPool
  |
  |  MyTask::process()
  |    ├── msgID = 1：KeyRecommender
  |    └── msgID = 2：WebPageSearcher
  v
TcpConnection::notifyLoop()
  |
  |  pending callback + eventfd 唤醒 IO 线程
  v
EventLoop::handlePendingCbs()
  |
  |  TcpConnection::send()
  v
Client
```

## 通信协议

客户端和服务端采用简单的“长度头 + JSON 正文”格式：

```text
size_t length
JSON body
```

请求类型：

```json
{
    "msgID": 1,
    "msg": "keyword"
}
```

- `msgID = 1`：关键词推荐
- `msgID = 2`：网页检索

响应类型：

- `msgID = 100`：关键词推荐成功
- `msgID = 200`：网页检索成功
- `msgID = 404`：未查询到结果

## 编译与运行

以下命令基于个人 Linux 虚拟机环境，服务端依赖本机已安装的 Redis、redis-plus-plus、hiredis、log4cpp 等库。

推荐按下面链路复现：

1. 启动 Redis，并用 `redis-cli ping` 验证返回 `PONG`。
2. 构建中英文词典，生成 `dict.dat`、`dictIndex.dat`、`enDict.dat` 和 `enDictIndex.dat`。
3. 构建网页库、网页偏移库和倒排索引库。
4. 启动在线搜索服务端。
5. 启动命令行客户端，分别验证关键词推荐和网页检索。

项目根目录提供了一个简单的 `Makefile`，用于保存各模块的 `g++` 编译命令：

```bash
make module1   # 构建中英文词典生成程序
make module2   # 构建网页库和倒排索引生成程序
make server    # 构建在线搜索服务端
make run-server # 构建并启动服务端，自动设置 LD_LIBRARY_PATH
make client    # 构建命令行客户端
```

也可以按下面步骤进入各模块目录手动编译和运行。

### 1. 启动 Redis

```bash
cd redis
sudo redis-server redis.conf
```

重新打开一个终端验证：

```bash
cd redis
redis-cli ping
```

若返回 `PONG`，说明 Redis 已启动。

如需关闭 Redis：

```bash
redis-cli shutdown
```

### 2. 构建中英文词典

```bash
cd src/module1
g++ *.cc -I../../include
./a.out
```

该步骤会生成或更新中英文词典及索引文件。

### 3. 构建网页库和倒排索引

```bash
cd src/module2
g++ *.cc -I../../include
./a.out
```

该步骤会生成或更新网页库、网页偏移库和倒排索引库。

### 4. 启动服务端

```bash
cd src/module3
g++ *.cc -o server -I../../include -std=c++17 -L/usr/local/lib -lredis++ -lhiredis -llog4cpp -lpthread
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
./server
```

如果使用根目录的 `Makefile`，可以直接执行：

```bash
make run-server
```

### 5. 启动客户端

```bash
cd src/module4
g++ *.cc -I../../include -std=c++17
./a.out
```

## 配置文件

主要配置位于 `conf/myconf.conf`，包括：

- CppJieba 词典路径
- 中英文语料路径
- 停用词路径
- 离线构建产物路径
- 服务端 IP 和端口
- 工作线程数量
- 缓存容量
- 最大返回结果数量
- 定时缓存同步间隔

注意：当前配置文件中的路径以各模块运行目录为基准，例如从 `src/module3` 启动服务端时，默认配置路径为 `../../conf/myconf.conf`。

当前配置中服务端默认监听 `127.0.0.1:1234`，工作线程数为 `5`，关键词推荐最大返回数量为 `10`，网页检索最大返回数量为 `30`，缓存同步的初始时间和周期分别为 `1` 秒和 `3` 秒。

## 基础联调与验证记录

当前已保留的基础联调材料包括：

- Redis 启动后通过 `redis-cli ping` 返回 `PONG`。
- 服务端完成启动并加载配置。
- 客户端关键词推荐支持命中与未命中场景。
- 客户端网页检索支持命中与未命中场景。

后续如果补充测试记录，优先记录 Redis 缓存命中日志、本地 LRUCache 命中日志和多客户端简单联调结果；在没有真实测试数据前，不写 QPS、延迟或高并发压测结论。

## 项目亮点

- 基于 epoll 封装 Reactor 风格的 TCP 服务端。
- 使用线程池和任务队列将网络 IO 与业务计算解耦。
- 使用 pending callback + eventfd 机制，让工作线程完成业务后通知 IO 线程统一发送响应。
- 离线阶段使用 Simhash 对网页内容去重。
- 基于 TF-IDF 构建倒排索引，并通过向量相似度完成网页相关度排序。
- 关键词推荐模块综合编辑距离、词频和字典序进行候选词排序。
- 使用本地 LRUCache 缓存网页检索结果，并使用 Redis 缓存部分热点查询结果。

## 当前限制

- 项目主要用于学习 Linux C++ 后端开发流程，不是工业级搜索引擎系统。
- 服务端依赖本地 Redis、redis-plus-plus、hiredis、log4cpp 环境，未提供一键安装脚本。
- 当前未提供 CMake 工程化构建，仍以分模块 g++ 命令编译为主。
- 暂未提供系统化压测数据，因此不对 QPS、延迟或高并发能力做夸大承诺。
- 本地缓存和 Redis 的使用以项目演示和热点查询优化为主，不涉及分布式缓存架构。
