#include "EchoServer.h"
#include "MyTask.h"
#include "MyLog.h"
#include "TimerTask.h"
#include "Configuration.h"

#include <iostream>
#include <functional>

using std::bind;
using std::cout;
using std::endl;

namespace searchengine
{

EchoServer::EchoServer(const string &ip, unsigned short port)
:_pool(INIT_WORKER_NUM, INIT_TASKQUEUE_CAPACITY)
,_server(ip, port)
,_webPageSearcher()
,_recommender()
,_redis("tcp://127.0.0.1:6379")
,_timerThread(bind(&TimerTask::process, TimerTask()),
                   stoi(Configuration::getInstance().getConfigMap()["initTime"]),
                   stoi(Configuration::getInstance().getConfigMap()["periodicTime"]))
{

}

void EchoServer::start()
{
    auto &config = Configuration::getInstance().getConfigMap();
    cout << "[Server] starting on " << config["ip"] << ":" << config["port"] << endl;
    cout << "[Server] worker threads: " << INIT_WORKER_NUM
         << ", cache records per worker: " << config["recordnum"] << endl;

    _pool.start();

    _timerThread.start();


    _server.setConnectionCallBack([this](const TcpConnectionPtr &connPtr) {
        onConnection(connPtr);
    });

    _server.setMessageCallBack([this](const TcpConnectionPtr &connPtr) {
        onMessage(connPtr);
    });

    _server.setCloseCallBack([this](const TcpConnectionPtr &connPtr) {
        onClose(connPtr);
    });
    _server.start();
}

void EchoServer::stop()
{
    _server.stop();

    _timerThread.stop();

    _pool.stop();
}

void EchoServer::onConnection(const TcpConnectionPtr &connPtr)
{
    LogInfo("\n\t%s connected", connPtr->show().c_str());
    cout << connPtr->show() << " connected!" << endl;
}

void EchoServer::onMessage(const TcpConnectionPtr &connPtr)
{
    auto messages = connPtr->recvMessages();

    for (const auto &msg : messages)
    {
        // decode -> compute -> encode -> send
        MyTask task(msg, connPtr, _webPageSearcher, _recommender, _redis);
        _pool.addTask([task]() mutable { task.process(); }); // 也可以 lambda 表达式
    }
}

void EchoServer::onClose(const TcpConnectionPtr &connPtr)
{
    LogInfo("\n\t%s disconnected", connPtr->show().c_str());
    cout << connPtr->show() << " disconnected!" << endl;
}

}
