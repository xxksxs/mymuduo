#include "Thread.h"
#include "CurrentThread.h"
#include <sys/syscall.h>
#include <semaphore.h>

std::atomic<int> Thread::numCreated_{0};

Thread::Thread(ThreadFunc func, const std::string &name)
: started_(false),
  joined_(false),
  tid_(0),
  func_(std::move(func)),
  name_(name)
{
    setDefaultName();
}

Thread::~Thread()
{
    //如果线程已经启动但没有被join，则分离线程
    //joined_表示这是一个普通的工作线程，还是一个守护线程
    if (started_ && !joined_) 
    {
        //thread类提供的设置分离线程的方法
        thread_->detach();
    }
}

//一个thread对象，记录的就是一个新线程的详细信息
void Thread::start() 
{
    started_ = true;
    sem_t sem;
    sem_init(&sem, false, 0);
    //创建线程，传入线程函数和线程对象的指针
    thread_ = std::make_shared<std::thread>([&]{
        //获取线程id
        tid_ = CurrentThread::tid();
        sem_post(&sem); //通知主线程线程id已经获取到
        //执行线程函数
        func_();
    });

    //这里必须获取到线程id之后才能返回，否则可能出现主线程获取到的线程id为0的情况
    sem_wait(&sem); //等待子线程获取到线程id
}

void Thread::join()
{
    joined_ = true;
    thread_->join();
}

void Thread::setDefaultName()
{
    int num = ++numCreated_;
    if (name_.empty())
    {
        char buf[32];
        snprintf(buf, sizeof buf, "Thread%d", num);
        name_ = buf;
    }
}
