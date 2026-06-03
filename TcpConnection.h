#pragma once
#include "noncopyable.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Buffer.h"

#include <memory>
#include <string>
#include <atomic>


class EventLoop;
class Socket;
class Channel;

/*
TcpServer -> Acceptor -> 有一个新用户连接，通过accept函数拿到connfd
-> TcpConnection 设置回调 -> Channel -> Poller -> Poller监听到connfd可读事件 
-> Channel回调TcpConnection的回调函数
*/
class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection>
{
public:
    TcpConnection(EventLoop* loop,
                  const std::string& name,
                  int sockfd,
                  const InetAddress& localAddr,
                  const InetAddress& peerAddr);
    ~TcpConnection();

    EventLoop* getLoop() const { return loop_; }
    const std::string& name() const { return name_; }
    const InetAddress& localAddress() const { return localAddr_; }
    const InetAddress& peerAddress() const { return peerAddr_; }

    bool connected() const { return state_ == kConnected; }
    bool disconnected() const { return state_ == kDisconnected; }

    void send(std::string buf);
    void sendInLoop(const void* message, int len); //发送数据，线程安全的，可以在非IO线程调用
    void shutdown(); //关闭写端，触发对端的EPOLLHUP事件

    void setConnectionCallback(const ConnectionCallback& cb){connectionCallback_ = cb;}
    void setMessageCallback(const MessageCallback& cb){messageCallback_ = cb;}
    void setWriteCompleteCallback(const WriteCompleteCallback& cb){writeCompleteCallback_ = cb;}
    void setHighWaterMarkCallback(const HighWaterMarkCallback& cb){highWaterMarkCallback_ = cb;}
    void setCloseCallback(const CloseCallback& cb){closeCallback_ = cb;}

    void connectEstablished(); //连接建立，注册到Poller中
    void connectDestroyed(); //连接销毁，从Poller中删除
    
private:
    enum StateE { kConnecting, kConnected, kDisconnecting, kDisconnected };
    void handleRead(TimeStamp receiveTime); //可读事件的回调函数
    void handleWrite(); //可写事件的回调函数
    void handleClose(); //关闭事件的回调函数
    void handleError(); //错误事件的回调函数
    void setState(StateE s) { state_ = s; }
    
    void sendInLoop(const std::string& message); //在IO线程中发送数据
    void shutdownInLoop(); //在IO线程中关闭写端

    EventLoop* loop_; //这里绝对不是baseloop，因为TcpConnection都是在subloop里面管理的
    const std::string name_;
    std::atomic_int state_;
    bool reading_;

    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;

    const InetAddress localAddr_; //本地地址
    const InetAddress peerAddr_; //对端地址

    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_; //有读与写消息时的回调函数
    WriteCompleteCallback writeCompleteCallback_; //消息发送完成后的回调函数
    HighWaterMarkCallback highWaterMarkCallback_;
    CloseCallback closeCallback_;

    size_t highWaterMark_;
    Buffer inputBuffer_; //接收缓冲区
    Buffer outputBuffer_; //发送缓冲区
};