#include "EventLoop.h"
#include "Logger.h"
#include "Poller.h"
#include "Channel.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>

__thread EventLoop* t_loopInThisThread = 0; //线程局部变量，指向当前线程的EventLoop对象
const int kPollTimeMs = 10000; //定义默认的Poller IO接口的超时时间

int createEventfd()
{
    //创建一个eventfd对象，初始值为0，非阻塞，关闭时自动关闭文件描述符
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); 
    //创建wakeupfd，用来notify唤醒subReactor处理新来的channel
    if (evtfd < 0)
    {
        LOG_FATAL("create eventfd error:%d\n", errno);
    }
    return evtfd;
}

EventLoop::EventLoop()
:looping_(false),
 quit_(false),
 callingPendingFunctors_(false),
 threadId_(CurrentThread::tid()),
 poller_(Poller::newDefaultPoller(this)),
 weakfd_(createEventfd()),
 weakupChannel_(new Channel(this, weakfd_)), 
 currentActiveChannel_(nullptr)
{
    LOG_DEBUG("EventLoop created %p in thread %d\n", this, threadId_);
    if (t_loopInThisThread)
    {
        LOG_FATAL("Another EventLoop %p exists in this thread %d\n", t_loopInThisThread, threadId_);
    }
    else
    {
        t_loopInThisThread = this;
    }
    //设置wakeupfd的事件类型以及发生事件后的回调操作
    weakupChannel_->setReadCallback([this](TimeStamp) { handleRead(); });
    //每一个eventloop都将监听wakeupchannel的EPOLLIN读事件
    weakupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    weakupChannel_->disableAll();
    weakupChannel_->remove();
    ::close(weakfd_);
}

void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;
    LOG_INFO("EventLoop %p start looping\n", this);

    while(quit_ == false)
    {
        activeChannels_.clear();
        //监听两类fd上发生的事件，一类是client connection fd（与客户端通信），另一类是wakeupfd（mainloop与subloop通信）
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_); //监听fd上发生的事件，返回发生事件的fd列表
        for (Channel* channel : activeChannels_)
        {
            // Poller监听哪些channel发生事件了，然后上报给eventloop，通知channel处理事件
            channel->handleEvent(pollReturnTime_); 
        }
        //执行当前eventloop事件循环需要处理的回调操作
        //IO线程mainLoop接受新用户的channel，储存相应的fd
        //通过轮询算法选择一个subLoop，把新用户的channel分发给它，然后通过该成员唤醒subloop处理channel
        //mainloop会事先注册一个回调cb，然后唤醒subloop，subloop执行cb，cb会把新用户的channel添加到subloop中
        doPendingFunctors(); 
    }
    LOG_INFO("EventLoop %p stop looping\n", this);
}

//情况1、loop在自己的线程中调用quit()，此时直接设置标志位quit_为true，loop就会退出
//情况2、loop在非自己的线程中调用quit()，此时设置标志位quit_为true，并且调用wakeup()唤醒loop所在的线程，loop所在的线程被唤醒后会检测到quit_为true，loop就会退出
void EventLoop::quit()
{
    quit_ = true;
    if (!isInLoopThread())
    {
        wakeup();
    }
}

void EventLoop::runInLoop(Functor cb)
{
    //如果在自己的线程中调用runInLoop()，直接执行cb
    //如果在非自己的线程中调用runInLoop()，把cb放入队列中，并且唤醒loop所在的线程，让loop所在的线程执行cb
    if (!isInLoopThread())
    {
        queueInLoop(std::move(cb));
        wakeup();
    }
    else
    {
        cb();
    }
}

void EventLoop::queueInLoop(Functor cb)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(std::move(cb));
    }

    //唤醒loop所在的线程，让loop所在的线程执行cb
    if (!isInLoopThread() || callingPendingFunctors_)
    {
        wakeup();
    }
}

//和handleRead作用一样，用一个读事件来唤醒loop所在的线程
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = ::write(weakfd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8\n", n);
    }
}

void EventLoop::updateChannel(Channel *channel)
{
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel *channel)
{
    return poller_->hasChannel(channel);
}

void EventLoop::handleRead()
{
    uint64_t one = 1;
    ssize_t n = ::read(weakfd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::handleRead() reads %lu bytes instead of 8\n", n);
    }
}

void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;
    //把需要执行的回调操作从pendingFunctors_中取出，放入functors中
    //之所以要交换而不是直接执行pendingFunctors_中的回调操作，是因为有部分的操作可能事件很长，会导致长时间持有锁，导致时延
    //将相应的cb交换到局部变量functors中，减少锁的持有时间，提升性能
    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_); 
    }
    for (const Functor& functor : functors)
    {
        functor(); //执行回调操作
    }
    callingPendingFunctors_ = false;
}
