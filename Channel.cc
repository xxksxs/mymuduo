#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"
#include <sys/epoll.h>

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop),
      fd_(fd),
      events_(0),
      revents_(0),
      index_(-1),
      tied_(false)
{
}

Channel::~Channel()
{
}
//TcpConnection -> channel
//通过tie来防止当channel被手动remove掉，channel还在执行回调操作
//一个TcpConnection新连接创建的时候，调用tie绑定一个智能指针防止被销毁掉
void Channel::tie(const std::shared_ptr<void>& obj)
{
    tie_ = obj;
    tied_ = true;
}

//当改变channel所表示的fd的events事件后，update负责在poller里面更改fd相应的事件epoll_ctl
void Channel::update()
{
    //通过channel所属的EventLoop，调用updateChannel方法，Poller最终通过epoll_ctl来注册fd的events事件
    loop_->updateChannel(this);
}

//在channel所属的EventLoop中，调用removeChannel方法把当前channel删除掉
void Channel::remove()
{
    loop_->removeChannel(this);
}

void Channel::handleEvent(TimeStamp receiveTime)
{
    LOG_INFO("Channel handleEvent\n");
    
    if (tied_)
    {
        std::shared_ptr<void> guard = tie_.lock();
        if (guard)
        {
            handleEventWithGuard(receiveTime);
        }else
        {
            //如果guard为空，说明Channel对象已经被销毁了，此时就不处理事件了
        }
    }else
    {
        handleEventWithGuard(receiveTime);
    }
}

void Channel::handleEventWithGuard(TimeStamp receiveTime)
{
    LOG_INFO("Channel handleEvent revents:%d\n", revents_);

    if (revents_ & EPOLLHUP && !(events_ & EPOLLIN))
    {
        if (closeCallback_) closeCallback_();
    }
    if (revents_ & EPOLLERR)
    {
        if (errorCallback_) errorCallback_();
    }
    if (revents_ & (EPOLLIN | EPOLLPRI))
    {
        if (readCallback_) readCallback_(receiveTime);
    }
    if (revents_ & EPOLLOUT)
    {
        if (writeCallback_) writeCallback_();
    }
}