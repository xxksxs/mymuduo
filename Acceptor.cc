#include "Acceptor.h"
#include "Logger.h"
#include "InetAddress.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

static int createNonblocking()
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd < 0)
    {
        LOG_FATAL("%s:%s:%d: createnonblocking socket failed: %d\n", __FILE__, __FUNCTION__, __LINE__, errno);
    }
    return sockfd;
}

Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
:loop_(loop),
acceptSocket_(createNonblocking()),
acceptChannel_(loop, acceptSocket_.fd()),
listening_(false)
{
    acceptSocket_.setReuseAddr(true);
    acceptSocket_.setReusePort(reuseport); 
    acceptSocket_.bindAddress(listenAddr); //绑定套接字
    //当有新用户连接时，执行handleRead回调函数 connfd->channel->subloop
    //baseloop通过acceptChannel上的listenfd监听到是否有事件发生，如有就调用相应回调函数
    acceptChannel_.setReadCallback([this](TimeStamp) { handleRead(); }); 
}

Acceptor::~Acceptor()
{
    acceptChannel_.disableAll();
    acceptChannel_.remove();
}

void Acceptor::listen()
{
    listening_ = true;
    acceptSocket_.listen(); //listenfd开始监听
    acceptChannel_.enableReading(); //让baseloop监听listenfd的读事件
}

// 当channel收到读事件的时候执行
// listenfd有事件发生了，就是有新用户连接了，acceptChannel就会调用handleRead()，从而调用accept()接受连接
void Acceptor::handleRead()
{
    InetAddress peerAddr;
    int connfd = acceptSocket_.accept(&peerAddr); //accept()函数会返回一个新的文件描述符，代表了这个新连接
    if (connfd >= 0)
    {
        if (newConnectionCallback_)
        {
            // 此处通过回调（通常绑定为 TcpServer::newConnection）将原生连接上报给上层建筑。
            // 后续的负载均衡（轮询 SubLoop）、封装 TcpConnection 以及跨线程分发，均由上层全权负责。
            newConnectionCallback_(connfd, peerAddr); 
        }
        else
        {
            ::close(connfd); //没有回调函数，说明没有subloop，无法处理这个新连接，所以关闭它
        }
    }
    else
    {
        LOG_ERROR("%s:%s:%d: accept failed: %d\n", __FILE__, __FUNCTION__, __LINE__, errno);
        if (errno == EMFILE)
        {
            //如果文件描述符达到上限，要不然就改当前服务器的文件描述符上限，要不然就关闭一个文件描述符来接受这个新连接
            LOG_ERROR("%s:%s:%d: sockfd reached limit\n", __FILE__, __FUNCTION__, __LINE__);
        }
    }
}
