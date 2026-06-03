#include "EventLoopThread.h"
#include "EventLoop.h"

EventLoopThread::EventLoopThread(const ThreadInitCallback &cb, const std::string &name)
    : loop_(nullptr),
      exiting_(false),
      thread_([this](){threadFunc();}, name),
      callback_(cb),
      mutex_(),
      cond_()
{
}

EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if (loop_ != nullptr)
    {
        loop_->quit();
        thread_.join();
    }
}

EventLoop *EventLoopThread::startLoop()
{
    thread_.start(); //启动底层的新线程，调用threadFunc函数
    EventLoop* loop = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (loop_ == nullptr) //等待新线程创建好EventLoop对象
        {
            cond_.wait(lock); //等待条件变量的通知，直到loop_不再是nullptr
        }
        loop = loop_; //获取新线程里面创建的EventLoop对象的地址
    }
    return loop;
}

//下面这个方法，是在单独的新线程里面运行的
void EventLoopThread::threadFunc()
{
    EventLoop loop; //在新线程里面创建一个EventLoop对象，和上面的线程是一一对应的=>one loop per thread
    if (callback_)
    {
        callback_(&loop); //如果用户传入了回调函数，就调用用户的回调函数，进行一些用户自定义的初始化操作
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop; //把新线程里面创建的EventLoop对象的地址，赋值给成员变量loop_
        cond_.notify_one(); //唤醒等待的线程，通知它loop_已经创建好了
    }

    loop.loop(); //调用EventLoop对象的loop方法，进入事件循环，开始处理事件
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = nullptr; //事件循环退出后，把loop_置空，表示这个EventLoop对象已经销毁了
}
