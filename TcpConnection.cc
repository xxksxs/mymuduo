#include "TcpConnection.h"
#include "TimeStamp.h"
#include "Logger.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"

#include <functional>
#include <errno.h>
#include <sys/types.h>         
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <strings.h>

static EventLoop *checkLoopNotNULL(EventLoop *loop) //改成静态函数，防止其他文件重复调用
{
    if (loop == nullptr)
    {
        LOG_FATAL("TcpServer::TcpServer() mainLoop is null\n");
    }
    return loop;
}

TcpConnection::TcpConnection(EventLoop *loop, const std::string &name, int sockfd, const InetAddress &localAddr, const InetAddress &peerAddr)
: loop_(checkLoopNotNULL(loop)),
  name_(name),
  state_(kConnecting),
  reading_(true),
  socket_(new Socket(sockfd)),
  channel_(new Channel(loop, sockfd)),
  localAddr_(localAddr),
  peerAddr_(peerAddr),
  highWaterMark_(64*1024*1024) // 64M
{
    //下面给Channel设置相应的回调函数，poller给channel通知感兴趣的事件发生了，channel会回调相应的操作函数
    channel_->setReadCallback([this](TimeStamp receiveTime) { handleRead(receiveTime); });
    channel_->setWriteCallback([this]() { handleWrite(); });
    channel_->setCloseCallback([this]() { handleClose(); });
    channel_->setErrorCallback([this]() { handleError(); });
    LOG_INFO("TcpConnection::ctor[%s] at fd=%d\n", name_.c_str(), sockfd);
    socket_->setKeepAlive(true); //设置TCP keep-alive属性
}

TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::dtor[%s] at fd=%d state=%d\n", name_.c_str(), channel_->fd(), (int)state_);
}

//发送数据 应用层写的快，内核层发送数据慢。需要把待发送数据写入缓冲区，而且设置了水位回调函数
void TcpConnection::sendInLoop(const void *message, int len)
{
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;
    if (state_ == kDisconnected) //之前调用过该connected的shutdown，不能再发送了
    {
        LOG_ERROR("disconnected, give up writing!\n");
        return;
    }

    //表示channel_第一次开始写数据，而且缓冲区没有待发送数据
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote = ::write(channel_->fd(), message, len);
        if (nwrote >= 0)
        {
            remaining = len - nwrote;
            //既然在这里数据全部发送完成，就不用再给channel设置EPOLLOUT事件了
            if (remaining == 0 && writeCompleteCallback_)
            {
                loop_->queueInLoop([this] { writeCompleteCallback_(shared_from_this()); });
            }
        }else // nwrote < 0
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK) //EWOULDBLOCK表示由于非阻塞没有数据，正常返回
            {
                LOG_ERROR("TcpConnection::sendInLoop() failed\n");
                if (errno == EPIPE || errno == ECONNRESET) //EPIPE表示对端已经关闭连接，ECONNRESET表示连接被重置
                {
                    faultError = true;
                }
            }
        }
    }

    //说明当前这一次write并没有把数据全部发送出去，剩余数据需要保存到缓冲区当中，
    //然后给channel注册EPOLLOUT事件，poller发现tcp的发送缓冲区有空间，会通知相应的sock->channel，调用writeCallback回调函数，
    //调用TcpConnection::handleWrite()把发送缓冲区当中的数据发送出去
    if (!faultError && remaining > 0)
    {
        //目前发送缓冲区剩余的带发送数据的长度
        size_t oldLen = outputBuffer_.readableBytes(); 
        if (oldLen + remaining >= highWaterMark_ && 
            oldLen < highWaterMark_ && highWaterMarkCallback_)
        {
            loop_->queueInLoop([this, oldLen, remaining] { highWaterMarkCallback_(shared_from_this(), oldLen + remaining); });
        }
        outputBuffer_.append((char*)message + nwrote, remaining);
        if (!channel_->isWriting())
        {
            channel_->enableWriting(); //注册channel的写事件，否则poller不会给channel通知EPOLLOUT
        }
    }
}

void TcpConnection::send(std::string buf)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())
        {
            sendInLoop(buf.c_str(), buf.size());
        }
        else
        {
            loop_->runInLoop([this, buf] { sendInLoop(buf.c_str(), buf.size()); });
        }
    }
}

void TcpConnection::shutdown()
{
    if (state_ == kConnected)
    {
        setState(kDisconnecting);
        loop_->runInLoop([this] { shutdownInLoop(); });
    }
}

void TcpConnection::connectEstablished()
{
    setState(kConnected);
    channel_->tie(shared_from_this()); 
    channel_->enableReading(); //注册channel的读事件，poller给channel通知EPOLLIN事件时，channel会调用TcpConnection::handleRead()来处理读事件

    //新连接建立，执行回调
    connectionCallback_(shared_from_this());
}

void TcpConnection::connectDestroyed()
{
    if (state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll(); //把channel感兴趣的事件全部删除掉

        //连接销毁，执行回调
        connectionCallback_(shared_from_this());
    }
    channel_->remove(); //把channel从Poller中删除掉
}

void TcpConnection::handleRead(TimeStamp receiveTime)
{
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0)
    {
        //已建立连接的用户，有可读事件发生了，调用用户传入的回调操作onMessage
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }else if (n == 0)
    {
        handleClose();
    }else
    {
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead() error %d\n", errno);
        handleError();
    }
}

void TcpConnection::handleWrite()
{
    if (channel_->isWriting())
    {
        int savedErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if (n > 0)
        {
            outputBuffer_.retrieve(n);
            if (outputBuffer_.readableBytes() == 0)
            {
                channel_->disableWriting();
                if (writeCompleteCallback_)
                {
                    loop_->queueInLoop([this] { writeCompleteCallback_(shared_from_this()); });
                }
                if (state_ == kDisconnecting)
                {
                    shutdownInLoop();
                }
            }
        }else
        {
            LOG_ERROR("TcpConnection::handleWrite() error %d\n", errno);
        }
    }else
    {
        LOG_ERROR("TcpConnection fd=%d is down, no more writing\n", channel_->fd());
    }
}

void TcpConnection::handleClose()
{
    LOG_INFO("TcpConnection::handleClose() fd=%d state=%d\n", channel_->fd(), (int)state_);
    setState(kDisconnected);
    channel_->disableAll();

    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr); //执行用户注册的连接关闭回调操作
    closeCallback_(connPtr); //执行用户注册的连接关闭回调操作
}

void TcpConnection::handleError()
{
    int optVal;
    socklen_t optLen = sizeof(optVal);
    int err = 0;
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optVal, &optLen) < 0)
    {
        err = errno;
        // LOG_ERROR("TcpConnection::handleError() getsockopt error %d\n", errno);
    }
    else
    {
        err = optVal;
        // LOG_ERROR("TcpConnection::handleError() SO_ERROR = %d\n", optVal);
    }
    LOG_ERROR("TcpConnection::handleError() name:%s - SO_ERROR = %d\n", name_.c_str(), err);
}

void TcpConnection::sendInLoop(const std::string &message)
{
}

void TcpConnection::shutdownInLoop()
{
    if (!channel_->isWriting())
    {
        socket_->shutdownWrite();
    }
}
