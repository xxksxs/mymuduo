#include <mymuduo/TcpServer.h>
#include <iostream>
#include <string>
#include <functional>

class EchoServer
{
public:
    EchoServer(EventLoop* loop, const InetAddress& addr, const std::string& name)
        : server_(loop, addr, name)
        , loop_(loop)
    {
        // 注册连接回调
        server_.setConnectionCallback(
            std::bind(&EchoServer::onConnection, this, std::placeholders::_1));

        // 注册消息回调
        server_.setMessageCallback(
            std::bind(&EchoServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        // 设置合适的线程数，开启多线程模型
        server_.setThreadNum(4);
    }

    void start()
    {
        server_.start();
    }

private:
    // 连接建立或者断开的回调
    void onConnection(const TcpConnectionPtr& conn)
    {
        if (conn->connected())
        {
            LOG_INFO("Connection Up: %s", conn->peerAddress().toIpPort().c_str());
        }
        else
        {
            LOG_INFO("Connection Down: %s", conn->peerAddress().toIpPort().c_str());
        }
    }

    // 可读写事件回调
    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, TimeStamp time)
    {
        std::string msg = buf->retrieveAllAsString();
        LOG_INFO("Received %zu bytes from connection %s at %s", 
                 msg.size(), conn->peerAddress().toIpPort().c_str(), time.toString().c_str());
        
        // 收到什么就回复什么 (Echo)
        conn->send(msg);
        // conn->shutdown(); // 发送完数据后关闭连接
    }

    TcpServer server_;
    EventLoop* loop_;
};

int main()
{
    LOG_INFO("EchoServer is starting...");

    EventLoop loop;
    InetAddress addr(8000); // 监听端口 8000

    EchoServer server(&loop, addr, "MyEchoServer");
    server.start(); 
    
    loop.loop(); // 启动主循环，底层是一个epoll_wait阻塞等待事件发生
    return 0;
}
