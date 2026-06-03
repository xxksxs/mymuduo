#pragma once
#include "noncopyable.h"
#include "TimeStamp.h"
#include "CurrentThread.h"
#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>

class Poller;
class Channel;
//事件循环类，主要包含了两个大模块Channel和Poller（epoll的抽象类）
class EventLoop : noncopyable
{
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    void loop(); //事件循环的主函数，负责不断地监听事件并处理事件
    void quit(); //退出事件循环

    TimeStamp pollReturnTime() const { return pollReturnTime_; } //获取Poller返回发生事件的channels的时间点
    void runInLoop(Functor cb); //在当前loop线程执行cb
    void queueInLoop(Functor cb); //把cb放入队列

    void wakeup(); //唤醒loop所在的线程
    void updateChannel(Channel* channel); //更新channel
    void removeChannel(Channel* channel); //删除channel
    bool hasChannel(Channel* channel); //判断是否有这个channel

    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); } //判断当前线程是否是loop所在的线程

private:
    void handleRead(); //唤醒loop所在的线程，处理wakeup事件
    void doPendingFunctors(); //执行回调操作

    using ChannelList = std::vector<Channel*>;
    std::atomic_bool looping_; //标识事件循环是否正在运行，原子操作，使用CAS实现
    std::atomic_bool quit_; //标识事件循环是否退出

    const pid_t threadId_; //记录创建当前EventLoop对象所在的线程ID，保证线程安全
    TimeStamp pollReturnTime_; //Poller返回发生事件的channels的时间点
    std::unique_ptr<Poller> poller_; //EventLoop使用的Poller对象，使用智能指针管理资源

    int weakfd_; //主要作用，当mainloop获取一个新用户的channel，通过轮询算法选择一个subloop，通过该成员唤醒subloop处理channel
    std::unique_ptr<Channel> weakupChannel_; //把weakfd_和感兴趣的事件封装成一个channel
    ChannelList activeChannels_; //eventloop保存的全部channel
    Channel* currentActiveChannel_; //当前正在处理的channel

    std::atomic_bool callingPendingFunctors_; //当前loop是否有需要执行的回调操作
    std::vector<Functor> pendingFunctors_; //保存loop需要执行的回调操作
    std::mutex mutex_; //互斥锁用来保护pendingFunctors_的线程安全

};