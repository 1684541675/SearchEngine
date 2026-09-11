#include "Socket.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cerrno>

namespace searchengine
{

Socket::Socket()
{
    _fd = socket(AF_INET, SOCK_STREAM, 0);
    if(_fd<0)
    {
        perror("socket");
        return;
    }
}

Socket::Socket(int fd)
:_fd(fd)
{

}

Socket::~Socket()
{
    while (close(_fd) < 0 && errno == EINTR);
}

int Socket::fd() const
{
    return _fd;
}

void Socket::shutDownWrite()
{
    int ret = shutdown(_fd, SHUT_WR); // <sys/socket.h>
    if(ret<0)
    {
        perror("shutdown");
        return;
    }
}

void Socket::setNonBlock()
{
    int flags = fcntl(_fd, F_GETFL, 0);
    if (flags == -1) 
    {
        perror("fcntl F_GETFL");
        return;
    }

    flags |= O_NONBLOCK;

    if (fcntl(_fd, F_SETFL, flags) == -1) 
    {
        perror("fcntl F_SETFL");
    }
}

void Socket::setNoDelay()
{
    int on = 1;
    int ret = setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    if (ret < 0)
    {
        perror("setsockopt(TCP_NODELAY)");
    }
}

}