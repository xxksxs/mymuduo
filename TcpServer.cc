#include "TcpServer.h"
#include "Logger.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "EventLoopThreadPool.h"
#include "EventLoopThread.h"
#include "Socket.h"
#include "TcpConnection.h"
#include <functional>
#include <string>
#include <strings.h>
#include <memory>
#include <atomic>
#include <unordered_map>

static EventLoop *checkLoopNotNULL(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("TcpServer::TcpServer() mainLoop is null\n");
    }
    return loop;
}

TcpServer::TcpServer(EventLoop *loop, const InetAddress &listenAddr, std::string nameArg, Option option)
    : loop_(checkLoopNotNULL(loop)),
      ipPort_(listenAddr.toIpPort()),
      name_(nameArg),
      acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
      threadPool_(new EventLoopThreadPool(loop, name_)),
      connectionCallback_(),
      messageCallback_(), 
      writeCompleteCallback_(),
      threadInitCallback_(),
      started_(0),
      nextConnId_(1)
{
    //当有新用户连接时，会执行TCP Server::newConnection回调函数
    acceptor_->setNewConnectionCallback(
        [this](int sockfd, const InetAddress &peerAddr) { newConnection(sockfd, peerAddr); });
}

TcpServer::~TcpServer()
{
    for (auto &item : connections_)
    {
        TcpConnectionPtr conn(item.second);
        item.second.reset(); //把TcpConnection对象的shared_ptr置空，减少引用计数，触发TcpConnection对象的析构函数
        conn->getLoop()->runInLoop([conn]() { conn->connectDestroyed(); }); //在ioLoop线程中执行TcpConnection::connectDestroyed()，完成销毁连接的过程
    }
}

void TcpServer::setThreadNum(int numThreads)
{
    threadPool_->setThreadNum(numThreads);
}

void TcpServer::start()
{
    if (started_.fetch_add(1) == 0) //防止一个TcpServer对象被调用多次
    {
        threadPool_->start(threadInitCallback_);
        loop_->runInLoop([this] { acceptor_->listen(); });
    }
}

//有一个新的客户端的连接，accptor会执行这个回调
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr)
{
    //轮询算法，选择一个subloop，来管理新连接的channel
    EventLoop* ioLoop = threadPool_->getNextLoop(); 
    char buf[64] = {0};
    snprintf(buf, sizeof(buf), "-%s#%d", na    Channel* channel = static_cast<Channel*>(events_[i].data.ptr);me_.c_str(), nextConnId_);
    ++nextConnId_;
    std::string connName = name_ + buf;
    LOG_INFO("TcpServer::newConnection() [%s] - new connection [%s] from %s\n", name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());
    //通过sockfd获取其绑定的本机的ip地址和端口信息
    sockaddr_in localAddr;
    ::bzero(&localAddr, sizeof(localAddr));
    socklen_t addrlen = sizeof(localAddr);
    if (::getsockname(sockfd, (sockaddr*)&localAddr, &addrlen) < 0)
    {
        LOG_ERROR("TcpServer::newConnection() getsockname error %d\n", errno);
    }
    InetAddress localAddrObj(localAddr);

    //根据连接成功的sockfd，创建TcpConnection对象，connName是连接的名字，localAddr是连接绑定的本机地址，peerAddr是连接对端的地址
    TcpConnectionPtr conn(new TcpConnection(ioLoop, connName, sockfd, localAddrObj, peerAddr));

    connections_[connName] = conn; //把新连接添加到connections_中
    //下面的回调都是用户设置给TcpServer->TcpConnection->Channel->Poller的，Poller给Channel通知事件发生了，Channel会回调相应的操作函数，操作函数会调用用户设置的回调函数
    conn->setConnectionCallback(connectionCallback_); //设置连接回调函数
    conn->setMessageCallback(messageCallback_); //设置消息回调函数
    conn->setWriteCompleteCallback(writeCompleteCallback_); //设置消息发送完成回调函数
    
    //设置如何关闭连接的回调函数
    conn->setCloseCallback([this](const TcpConnectionPtr& c) { removeConnection(c); }); //设置连接关闭回调函数，TcpConnection对象在销毁之前会执行这个回调函数
    
    //直接调用TcpConnection::connectEstablished()，
    ioLoop->runInLoop([conn]() { conn->connectEstablished(); }); //在ioLoop线程中执行TcpConnection::connectEstablished()，完成新连接的注册过程
}

void TcpServer::removeConnection(const TcpConnectionPtr &conn)
{
    loop_->runInLoop([this, conn]() { removeConnectionInLoop(conn); });
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
    LOG_INFO("TcpServer::removeConnectionInLoop() [%s] - connection %s\n", name_.c_str(), conn->name().c_str());
    connections_.erase(conn->name());
    EventLoop* ioLoop = conn->getLoop();
    ioLoop->queueInLoop([conn]() { conn->connectDestroyed(); }); //在ioLoop线程中执行TcpConnection::connectDestroyed()，完成销毁连接的过程
}
