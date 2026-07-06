#include "TcpConnection.h"
#include "EventLoop.h"

#include <sys/socket.h>   // recv, getsockname, getpeername
#include <netinet/in.h>   // sockaddr_in

#include <cerrno>         // errno, EINTR
#include <cstdio>         // perror, printf
#include <cstring>        // memcpy
#include <sstream>        // ostringstream

#include <iostream>

using std::cout;
using std::endl;
using std::ostringstream;

namespace searchengine
{

TcpConnection::TcpConnection(int fd, EventLoop *loopPtr)
:_connSock(fd)
,_sockIO(fd)
,_localAddr(getLocalAddr())
,_peerAddr(getPeerAddr())
,_isShutDownWrite(false)
,_isAlive(true)
,_loopPtr(loopPtr)
{

}

/**
 *  发送数据（按小火车协议）
 */
void TcpConnection::send(const string &msg)
{
    const size_t length = msg.size();

#ifdef __DEBUG__
    printf("\t(File:%s, Func:%s(), Line:%d)\n", __FILE__, __FUNCTION__, __LINE__);
    cout << "length = " << length << endl;
#endif

    size_t ret = _sockIO.writen(&length, sizeof(size_t)); // 发送车头

#ifdef __DEBUG__
    cout << "send train head ret = " << ret << endl;
#endif

    if (ret != sizeof(size_t))
    {
        cout<<"TcpConnection::send: send train head failed"<<endl;
        return;
    }

    ret = _sockIO.writen(msg.c_str(), length); // 发送车厢

#ifdef __DEBUG__
    cout << "send train body ret = " << ret << endl;
#endif

    if (ret != length)
    {
        cout<<"TcpConnection::send: send train body failed"<<endl;
        return;
    }
}

/**
 *  获取数据（按小火车协议）
 */
string TcpConnection::recv()
{
    size_t length = 0;

    size_t ret = _sockIO.readn(&length, sizeof(size_t)); // 接收车头

#ifdef __DEBUG__
    printf("\t(File:%s, Func:%s(), Line:%d)\n", __FILE__, __FUNCTION__, __LINE__);
    cout << "length head ret = " << ret << endl;
    cout << "length = " << length << endl;
#endif

    if (ret != sizeof(size_t))
    {
        cout << "TcpConnection::recv: read train head failed" << endl;
        return {};
    }

    string msg(length, '\0');

    if (length > 0)
    {
        ret = _sockIO.readn(&msg[0], length); // 接收车厢

#ifdef __DEBUG__
        printf("\t(File:%s, Func:%s(), Line:%d)\n", __FILE__, __FUNCTION__, __LINE__);
        cout << "body ret = " << ret << endl;
        cout << "msg = " << msg << endl;
#endif

        if (ret != length)
        {
            cout << "TcpConnection::recv: read train body failed" << endl;
            return {};
        }
    }

    return msg;
}

/**
 *  获取当前已到达的完整消息（按小火车协议）
 *
 *  1. 只读取当前 socket 接收缓冲区中已经到达的数据，不等待完整业务包
 *  2. 半包会留在 _inputBuffer 中，下一次可读事件到来后继续解析
 *  3. 粘包会被拆成多条完整消息返回
 */
vector<string> TcpConnection::recvMessages()
{
    vector<string> messages;
    char buf[65536] = {0};

    while (true)
    {
        ssize_t ret = 0;

        do
        {
            ret = ::recv(_connSock.fd(), buf, sizeof(buf), MSG_DONTWAIT);
        } while (ret == -1 && errno == EINTR);

        if (ret > 0)
        {
            _inputBuffer.append(buf, static_cast<size_t>(ret));
        }
        else if (ret == 0)
        {
            markClosed();
            break;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }

            perror("recv");
            markClosed();
            break;
        }
    }

    while (_inputBuffer.size() >= sizeof(size_t))
    {
        size_t length = 0;
        memcpy(&length, _inputBuffer.data(), sizeof(size_t));

        if (length > _inputBuffer.size() - sizeof(size_t))
        {
            break;
        }

        messages.emplace_back(_inputBuffer.data() + sizeof(size_t), length);
        _inputBuffer.erase(0, sizeof(size_t) + length);
    }

    return messages;
}

/**
 *  获取一行数据
 */
string TcpConnection::recvLine()
{
    char buf[65536] = {0};

    size_t ret = _sockIO.readline(buf, sizeof(buf));

#ifdef __DEBUG__
    printf("\t(File:%s, Func:%s(), Line:%d)\n", __FILE__, __FUNCTION__, __LINE__);
    cout << "recvLine ret = " << ret << endl;
#endif

    if (ret == 0)
    {
        return {};
    }

    if (ret > sizeof(buf))
    {
        cout << "TcpConnection::recvLine: readline failed" << endl;
        return {};
    }

    if (buf[ret - 1] != '\n')
    {
        fprintf(stderr, "TcpConnection::recvLine: message does not contain newline\n");
        return {};
    }

    string res(buf, ret);

    res.pop_back(); // 去掉末尾 '\n'

    return res;
}

string TcpConnection::show()
{
    ostringstream oss;

    oss << "[tcp] (local)" << _localAddr.ip() << ":" << _localAddr.port()
        << " --> "
        << "(peer)" << _peerAddr.ip() << ":" << _peerAddr.port();

    return oss.str();
}

bool TcpConnection::isClosed() const
{
    char tmpBuf[128] = {0};
    ssize_t ret = 0;

    do
    {
        ret = ::recv(_connSock.fd(), tmpBuf, sizeof(tmpBuf), MSG_PEEK);
    } while (ret == -1 && errno == EINTR);

    if (ret == -1)
    {
        perror("recv");
        return true;
    }

    return ret == 0;
}

bool TcpConnection::isAlive() const
{
    return _isAlive;
}

void TcpConnection::markClosed()
{
    _isAlive = false;
}

InetAddress TcpConnection::getLocalAddr()
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(struct sockaddr);

    int ret = getsockname(_connSock.fd(), (struct sockaddr *)&addr, &len);
    if(ret==-1)
    {
        perror("getsockname");
    }

    return InetAddress(addr);
}

InetAddress TcpConnection::getPeerAddr()
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(struct sockaddr);

    int ret = getpeername(_connSock.fd(), (struct sockaddr *)&addr, &len);
    if(ret==-1)
    {
        perror("getpeername");
    }
    return InetAddress(addr);
}

void TcpConnection::setConnectionCallBack(const TcpConnectionCallBack &onConnectionCb)
{
    _onConnectionCb = onConnectionCb;
}

void TcpConnection::setMessageCallBack(const TcpConnectionCallBack &onMessageCb)
{
    _onMessageCb = onMessageCb;
}

void TcpConnection::setCloseCallBack(const TcpConnectionCallBack &onCloseCb)
{
    _onCloseCb = onCloseCb;
}

void TcpConnection::handleConnectionCallBack()
{
    if (_onConnectionCb)
    {
        _onConnectionCb(shared_from_this());
    }
}

void TcpConnection::handleMessageCallBack()
{
    if (_onMessageCb)
    {
        _onMessageCb(shared_from_this());
    }
}

void TcpConnection::handleCloseCallBack()
{
    if (_onCloseCb)
    {
        _onCloseCb(shared_from_this());
    }
}

/**
 *  将 send(response) 作为回调函数传入 loop，并将主线程从 loop 中唤醒
 *
 *  1. 子线程调用
 *  2. 流程：notifyLoop -> setPendingCallBack -> writeCounter
 *  3. 这里只能先注册、后写 counter，顺序不能调转，否则会出现 BUG
 */
void TcpConnection::notifyLoop(const string &msg)
{
    // cout << "Worker Thread: notifyLoop" << endl;

    TcpConnectionPtr self = shared_from_this();


    // 注册 onPendingCb（即插入一个 onPendingCb）
     _loopPtr->setPendingCallBack([self, msg]() {
        self->send(msg);
    });
    // 向内核计数器中写值
    _loopPtr->writeCounter();
}

} // namespace searchengine
