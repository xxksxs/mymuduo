#include "EventLoopThreadPool.h"
#include "EventLoopThread.h"
#include "EventLoop.h"
#include <memory>

EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg)
    : baseLoop_(baseLoop),
      name_(nameArg),
      started_(false),
      numThreads_(0),
      next_(0)
{
}

void EventLoopThreadPool::start(const ThreadInitCallback &cb)
{
    started_ = true;
    for (int i = 0; i < numThreads_; i++)
    {
        char buf[name_.size() + 32];
        snprintf(buf, sizeof buf, "%s%d", name_.c_str(), i);
        EventLoopThread* t = new EventLoopThread(cb, buf);
        threads_.emplace_back(std::unique_ptr<EventLoopThread>(t));
        //底层创建线程，绑定一个新的EventLoop对象，并返回这个EventLoop对象的地址，保存到loops_中
        loops_.emplace_back(t->startLoop()); 
    }

    //整个服务端只有一个线程，baseLoop_就是唯一的EventLoop对象，用户传入的回调函数就直接在baseLoop_上执行
    if (numThreads_ == 0 && cb)
    {
        cb(baseLoop_);
    }
}

EventLoop *EventLoopThreadPool::getNextLoop()
{
    EventLoop* loop = baseLoop_; //如果工作在单线程中，baseLoop_默认以轮询的方式分配Channel给subLoop
    if (!loops_.empty())
    {
        loop = loops_[next_]; //如果工作在多线程中，baseLoop_默认以轮询的方式分配Channel给subLoop
        ++next_;
        if (static_cast<size_t>(next_) >= loops_.size())
        {
            next_ = 0;
        }
    }
    return loop;
    
}

std::vector<EventLoop *> EventLoopThreadPool::getAllLoops()
{
    if (loops_.empty())
    {
        return std::vector<EventLoop*>(1, baseLoop_);
    }
    else
    {
        return loops_;
    }
}
