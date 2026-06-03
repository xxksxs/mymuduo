#pragma once

#include "Poller.h"
#include "TimeStamp.h"
#include <vector>
#include <sys/epoll.h>


/*
EpollPoller是Poller的一个具体实现类，使用了Linux的epoll机制来实现IO复用。
它负责监听fd上发生的事件，并把发生事件的fd列表填充到activeChannels中。
当Channel对象改变了fd的events事件时，EpollPoller会通过update方法来更新fd在epoll中的事件状态。
*/
class EpollPoller : public Poller
{
public:
    EpollPoller(EventLoop* loop);
    ~EpollPoller() override;

    //重现基类Poller的抽象方法
    TimeStamp poll(int timeoutMs, ChannelList* activeChannels) override;
    void updateChannel(Channel* channel) override;
    void removeChannel(Channel* channel) override;

private:
    //填写活跃的事件，把发生事件的fd列表填充到activeChannels中
    void fillActiveChannels(int numEvents, ChannelList* activeChannels) const; 
    //更新fd的events事件，operation参数表示是添加、修改还是删除事件
    void update(int operation, Channel* channel); 

    using EventList = std::vector<struct epoll_event>;
    static const int kInitEventListSize = 16; //初始事件列表的大小
    int epollfd_; //epoll_create返回的文件描述符
    EventList events_; //保存发生事件的fd列表
};