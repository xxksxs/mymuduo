#include "EpollPoller.h"
#include "Logger.h"
#include "Channel.h"
#include <errno.h>
#include <unistd.h>
#include <string.h>

//表示Channel在poller中的状态，和Channel类中的index_成员变量的值等价
const int kNew = -1; //表示一个新的Channel
const int kAdded = 1; //表示一个已经添加到poller中的Channel
const int kDeleted = 2; //表示一个已经从poller中删除的Channel

EpollPoller::EpollPoller(EventLoop* loop)
    : Poller(loop),  //调用基类的构造函数来初始化从基类继承而来的成员
      epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
      events_(kInitEventListSize)
{
    if (epollfd_ < 0)
    {
        LOG_FATAL("epoll_create1 failed: %d\n", errno);
    }
}

EpollPoller::~EpollPoller()
{
    ::close(epollfd_);
}

//这个函数的作用就是通过epoll_wait来监听发生事件的channel，并把发生事件的cahnnel填充到activeChannels中，告知给EventLoop
TimeStamp EpollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    LOG_INFO("func=%s => fd total count:%lu\n", __FUNCTION__, channels_.size());
    //epoll_wait的第二个参数要求一个指向epoll_event数组的指针，所以通过begin（）获取首个元素的迭代器，然后*运算符获取到首个元素的值，再取地址就得到了一个指向epoll_event数组的指针
    //现代C++提供vector的data（）函数来直接获取指向vector底层数组的指针
    int numEvents = ::epoll_wait(epollfd_, events_.data(), static_cast<int>(events_.size()), timeoutMs);
    int saveErrno = errno;
    auto now = TimeStamp::now();
    if (numEvents > 0)
    {
        LOG_INFO("func=%s => %d events happened\n", __FUNCTION__, numEvents);
        fillActiveChannels(numEvents, activeChannels); //把发生事件的fd列表填充到activeChannels中
        if (numEvents == events_.size())
        {
            events_.resize(events_.size() * 2); //如果发生事件的数量等于当前events_数组的大小，说明events_数组太小了，需要扩大一倍
        }
    }else if (numEvents == 0)
    {
        LOG_INFO("func=%s => Timeout\n", __FUNCTION__);
    }
    else
    {
        if (saveErrno != EINTR)
        {
            errno = saveErrno; // 用于打印错误信息
            LOG_ERROR("func=%s => epoll_wait error: %d\n", __FUNCTION__, saveErrno);
        }
    }
    return now;
}

//channel update/remove => EventLoop updateChannel/removeChannel => Poller updateChannel/removeChannel => EpollPoller updateChannel/removeChannel 
void EpollPoller::updateChannel(Channel *channel)
{
    const int index = channel->index();
    LOG_INFO("func=%s => fd=%d events=%d index=%d\n", __FUNCTION__, channel->fd(), channel->events(), index);
    
    if (index == kNew || index == kDeleted)
    {
        // 尚未添加 或 曾被删除过，现在要 (重新) 添加到 epoll 中
        int fd = channel->fd();
        if (index == kNew)
        {
            // 如果是全新 fd 则加入字典
            channels_[fd] = channel;
        }

        channel->set_index(kAdded);  // 状态切为已添加
        update(EPOLL_CTL_ADD, channel); // 使用 ADD 操作
    }
    else
    {
        // index == kAdded，说明已经在 epoll 树上了
        int fd = channel->fd();
        
        // 如果该 channel 没有关心的事件，说明我们可以把它从 epoll 树上摘下去了
        if (channel->isNoneEvent())
        {
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted); // 状态切回已删除
        }
        else
        {
            // 还有关心的事件，可能是从读变成写，更新 epoll 里面的内核事件集合
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

void EpollPoller::removeChannel(Channel *channel)
{
    int fd = channel->fd();
    int index = channel->index();
    channels_.erase(fd); //从字典中删除这个fd对应的Channel对象

    LOG_INFO("func=%s => fd=%d events=%d index=%d\n", __FUNCTION__, channel->fd(), channel->events(), index);
    if (index == kAdded)
    {
        update(EPOLL_CTL_DEL, channel); //从epoll树上删除这个fd
    }
    channel->set_index(kNew); //状态切回全新

}

void EpollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
    for (int i = 0; i < numEvents; i++)
    {
        Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
        channel->set_revents(events_[i].events); //把发生事件的fd列表填充到activeChannels中
        activeChannels->push_back(channel);
    }
}

void EpollPoller::update(int operation, Channel *channel)
{
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = channel->events();
    event.data.ptr = channel; //把Channel*保存到event.data.ptr中，这样当poller监听到事件发生时，就可以通过event.data.ptr找到对应的Channel对象，从而调用它的回调函数来处理事件
    int fd = channel->fd();
    // 移除 event.data.fd = fd; 否则会导致联合体中的 ptr 变量被覆盖发生段错误
    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0) //小于0表示出错了
    {
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_ERROR("epoll_ctl del error: %d\n", errno);
        }
        else
        {
            LOG_FATAL("epoll_ctl add/mod error: %d\n", errno);
        }
    }
}
