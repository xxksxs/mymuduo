#include "Poller.h"
#include "EpollPoller.h"
#include <stdlib.h>

Poller* Poller::newDefaultPoller(EventLoop* loop)
{
    if (::getenv("MUDUO_USE_POLL"))
    {
        //如果环境变量MUDUO_USE_POLL被设置了，就使用PollerPoll
        return nullptr; //这里暂时返回nullptr，后续会实现PollerPoll类
    }
    else
    {
        //默认使用PollerEpoll
        return new EpollPoller(loop);
    }
}