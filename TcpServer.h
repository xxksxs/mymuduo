#pragma once
#include "noncopyable.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "EventLoopThreadPool.h"
#include "TcpConnection.h"
#include "Buffer.h"
#include "TimeStamp.h"
#include "Logger.h"
#include <functional> 
#include <string>
#include <memory>
#include <atomic>
#include <unordered_map>

/*
用户使用muduo编写服务器程序，主要使用TcpServer这个类，Acceptor是TcpServer的成员变量，用户不需要直接使用Acceptor。
TcpServer的主要职责是管理Acceptor，分配subloop，封装TcpConnection，并将新连接分发给subloop处理。
*/

//对外的服务器编程使用的类
class TcpServer: noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    enum Option //是否重用端口
    {
        kNoReusePort,
        kReusePort,
    };

    TcpServer(EventLoop *loop, const InetAddress &listenAddr, std::string nameArg, Option option = kNoReusePort);
    ~TcpServer();
    void setThreadInitCallback(const ThreadInitCallback &cb) { threadInitCallback_ = cb; }
    void setConnectionCallback(const ConnectionCallback &cb) { connectionCallback_ = cb; }
    void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb) { writeCompleteCallback_ = cb; }

    void setThreadNum(int numThreads); //设置底层subloop的数量
    void start(); //开启服务器监听，Acceptor开始监听listenfd，accept新连接
private:
    void newConnection(int sockfd, const InetAddress &peerAddr); //有新连接时的回调函数，负责封装TcpConnection对象，并分发给subloop处理
    void removeConnection(const TcpConnectionPtr &conn); //从connections_中删除连接
    void removeConnectionInLoop(const TcpConnectionPtr &conn); //在IO线程中删除连接

    using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;

    EventLoop *loop_; //baseloop，用户定义的线程，mainloop
    const std::string ipPort_; //服务器的ip和端口
    const std::string name_; //服务器的名字
    std::unique_ptr<Acceptor> acceptor_; //运行在mainloop，监听listenfd的事件，accept新连接
    std::shared_ptr<EventLoopThreadPool> threadPool_; //one loop per thread
    
    ConnectionCallback connectionCallback_; //有新连接时的回调函数
    MessageCallback messageCallback_; //有读写事件时的回调函数
    WriteCompleteCallback writeCompleteCallback_; //消息发送完成后的回调函数
    ThreadInitCallback threadInitCallback_; //线程初始化回调函数
    std::atomic_int started_; //原子操作，标识服务器是否已经启动
    int nextConnId_; //下一个连接的ID
    ConnectionMap connections_; //保存所有连接的map，key是连接的名字，value是连接对象
};
