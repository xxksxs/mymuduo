#pragma once 
#include "noncopyable.h"
#include "Channel.h"
#include "TimeStamp.h"
#include <vector>
#include <unordered_map>

//muduo库中多路事件分发器的核心IO复用模块
class Poller : noncopyable
{
public:
    using ChannelList = std::vector<Channel*>;

    Poller(EventLoop* loop);
    virtual ~Poller();

    //给所有IO复用保留统一的接口
    virtual TimeStamp poll(int timeoutMs, ChannelList* activeChannels) = 0; //监听fd上发生的事件，返回发生事件的fd列表
    virtual void updateChannel(Channel* channel) = 0; //注册fd的events事件
    virtual void removeChannel(Channel* channel) = 0; //删除fd的events事件

    bool hasChannel(Channel* channel) const; //判断参数Channel*是否在poller中
    static Poller* newDefaultPoller(EventLoop* loop); //根据操作系统的不同，返回不同的Poller对象
protected:
    //map的key是fd，value是fd对应的Channel*，所以可以通过fd快速找到对应的Channel*，从而找到fd发生的事件
    using ChannelMap = std::unordered_map<int, Channel*>; //fd到Channel的映射
    ChannelMap channels_; //保存fd到Channel的映射
private:
    EventLoop* ownerLoop_;
    // ChannelMap channels_; //保存fd到Channel的映射
};