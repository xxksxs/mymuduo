# MyMuduo 网络库

MyMuduo 是一个基于 C++11 编写的轻量级、高性能的并发网络库。其核心架构参考了陈硕大神编写的 [muduo](https://github.com/chenshuo/muduo) 网络库。

本项目摒弃了原版 muduo 中的 Boost 依赖，完全采用 C++11 标准库（如 `std::thread`, `std::function`, `std::bind`, `std::shared_ptr`, `std::atomic` 等）进行重构，代码更加精简，非常适合用来学习高并发网络编程以及 Reactor 模型！

## 🌟 核心特性

- **基于 Reactor 模式**：采用 non-blocking I/O + Epoll 基于事件驱动的网络模型。
- **One Loop Per Thread**：一个线程一个事件循环，主线程（MainLoop）只负责接收新连接的 Accept 事件，随后以轮询（Round Robin）的方式分发给工作线程（SubLoop）处理后续读写业务。
- **完全使用 C++11 替代 Boost**：抛弃了重量级的 Boost 依赖，使用标准库，降低了阅读和编译门槛。
- **核心组件**：
  - `TcpServer`：管理 Acceptor 和 EventLoopThreadPool，是对外提供服务的核心接口。
  - `TcpConnection`：封装了一个连接的所有信息及读写缓冲区。
  - `EventLoop`：反应堆的核心，负责处理 Epoll 分发的处于活跃状态的 Channel。
  - `Channel`：封装了 fd（文件描述符）及相关的感兴趣事件与回调函数。
  - `Poller / EpollPoller`：I/O 多路复用分发器的底层实现，默认使用 Epoll。
  - `Buffer`：非阻塞 I/O 专用的应用层读写缓冲区（模仿 Netty 设计）。

## 🛠️ 编译与安装

本项目提供了一个一键自动编译与安装的脚本 `autobuild.sh`。它会通过 CMake 进行构建，并将生成的头文件和动态库拷贝到系统目录，方便后续在任何业务开发中直接引入。

```bash
# 给予脚本执行权限
chmod +x autobuild.sh

# 执行一键编译安装（需要 sudo 权限将文件写入 /usr/include 和 /usr/lib）
sudo ./autobuild.sh
```

- **头文件安装路径**：`/usr/include/mymuduo`
- **动态库安装路径**：`/usr/lib/libmymuduo.so`

## 🚀 快速上手 (Example)

你可以查看 `example` 目录下的回显服务器（Echo Server）例子，快速了解如何使用 `MyMuduo` 构建服务器程序。

### 1. 编写服务端代码
包含头文件 `<mymuduo/TcpServer.h>` 即可获取暴露的所有接口层。

```cpp
#include <mymuduo/TcpServer.h>
#include <iostream>
#include <string>

class EchoServer {
public:
    EchoServer(EventLoop* loop, const InetAddress& addr, const std::string& name)
        : server_(loop, addr, name), loop_(loop) {
        // 注册连接事件回调
        server_.setConnectionCallback(
            std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        
        // 注册读写消息事件回调
        server_.setMessageCallback(
            std::bind(&EchoServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        // 设置底层的 subloop 的数量 (工作线程数)
        server_.setThreadNum(4);
    }

    void start() {
        server_.start();
    }

private:
    void onConnection(const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            std::cout << "Connection Up: " << conn->peerAddress().toIpPort() << std::endl;
        } else {
            std::cout << "Connection Down: " << conn->peerAddress().toIpPort() << std::endl;
        }
    }

    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, TimeStamp time) {
        std::string msg = buf->retrieveAllAsString();
        std::cout << "Receive: " << msg << std::endl;
        conn->send(msg); // 收到什么就回复什么
    }

    TcpServer server_;
    EventLoop* loop_;
};

int main() {
    EventLoop loop;
    InetAddress addr(8000);
    EchoServer server(&loop, addr, "EchoServer");
    server.start();
    loop.loop(); 
    return 0;
}
```

### 2. 编译你的程序
你可以按如下方式进行链接：
```bash
g++ -std=c++11 -g -o testserver testserver.cc -lmymuduo -lpthread
```
> **注意**：链接时不要忘记加上 `-lmymuduo` 和 `-lpthread`。因为我们的库使用了标准线程库，所以需要挂载 pthread。你同样可以使用 `example` 内提供的 `Makefile` 直接进行编译 `make`。

### 3. 测试
启动服务端：
```bash
./testserver
```
打开一个新的终端，使用 `telnet` 命令进行测试：
```bash
telnet 127.0.0.1 8000
```
此时你输入任何信息，服务端都会回显对应的字符串过来。