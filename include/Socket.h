#pragma once
#include "NonCopyable.h"

/*************************************************************
 *
 *  网络套接字类
 *
 *  1. 负责套接字的创建和关闭（利用 RAII 技术管理）
 *  2. 不具有复制语义
 *  3. 提供更改套接字模式/选项的方法
 *
 *************************************************************/

namespace searchengine
{
 
class Socket
:private NonCopyable
{
public:
    Socket();
    explicit Socket(int);
    ~Socket();

    int fd() const;
    void shutDownWrite();
    void setNonBlock();
    void setNoDelay();

private:
    int _fd;
};

}