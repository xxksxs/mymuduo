#pragma once

#include "noncopyable.h"
#include <functional>
#include <string>
#include <vector>
#include <memory>

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool : noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    EventLoopThreadPool(EventLoop* baseLoop, const std::string& nameArg);
    ~EventLoopThreadPool() = default;

    void setThreadNum(int numThreads) {numThreads_ = numThreads;}
    void start(const ThreadInitCallback& cb = ThreadInitCallback());

    //如果工作在多线程中，baseLoop_默认以轮询的方式分配Channel给subLoop
    EventLoop* getNextLoop();
    std::vector<EventLoop*> getAllLoops();
    bool started() const {return started_;}
    const std::string& name() const {return name_;}
private:
    EventLoop* baseLoop_; //用户传入的baseLoop，EventLoopThreadPool把它当做主线程的EventLoop
    std::string name_;
    bool started_;
    int numThreads_;
    std::vector<std::unique_ptr<EventLoopThread>> threads_; //线程池里面的线程对象
    std::vector<EventLoop*> loops_; //线程池里面的线程对象对应的EventLoop对象的地址
    int next_; //轮询算法的下标，记录下一个被分配的EventLoop对象在loops_中的位置
};