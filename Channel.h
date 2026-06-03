#pragma once
#include "noncopyable.h"
#include "TimeStamp.h"
#include <functional>
#include <memory>

/*
Channel类理解为通道，封装了sockfd和它感兴趣的事件，如EPOLLIN、EPOLLOUT等。
还绑定了poller返回的具体事件。它负责调用具体事件发生时的回调函数。
*/

class EventLoop;

class Channel : noncopyable
{
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(TimeStamp)>;

    Channel(EventLoop* loop, int fd); //这里只包含指针，所以可以用前置声明
    ~Channel();

    //fd得到poller通知以后，调用相应的方法处理事件
    void handleEvent(TimeStamp receiveTime); //这里直接用到了对象，在编译期需要知道对象的大小，所以需要包含头文件

    //设置回调函数对象
    void setReadCallback(ReadEventCallback cb) {readCallback_ = std::move(cb);}
    void setWriteCallback(EventCallback cb) {writeCallback_ = std::move(cb);}
    void setCloseCallback(EventCallback cb) {closeCallback_ = std::move(cb);}
    void setErrorCallback(EventCallback cb) {errorCallback_ = std::move(cb);}

    //防止Channel被手动remove掉，Channel还在执行回调操作
    void tie(const std::shared_ptr<void>&); 

    int fd() const {return fd_;}
    int events() const {return events_;}
    int revents() const {return revents_;}
    void set_revents(int revt) {revents_ = revt;} //提供一个公用的对外接口，供Poller去监听发生的事件

    //设置fd相应的事件状态
    void enableReading() {events_ |= kReadEvent; update();}
    void disableReading() {events_ &= ~kReadEvent; update();}
    void enableWriting() {events_ |= kWriteEvent; update();}
    void disableWriting() {events_ &= ~kWriteEvent; update();}
    void disableAll() {events_ = kNoneEvent; update();}

    bool isNoneEvent() const {return events_ == kNoneEvent;}
    bool isReading() const {return events_ & kReadEvent;}
    bool isWriting() const {return events_ & kWriteEvent;}

    int index() {return index_;}
    void set_index(int idx) {index_ = idx;}

    EventLoop* ownerLoop() {return loop_;}
    void remove();

private:
    void update(); //更新Channel所绑定的fd的事件状态
    void handleEventWithGuard(TimeStamp receiveTime); //事件发生时的具体回调函数

    static const int kNoneEvent; //没有事件
    static const int kReadEvent; //读事件
    static const int kWriteEvent; //写事件
    EventLoop* loop_; //事件循环对象
    const int fd_; //sockfd监听的对象
    int events_; //注册的事件
    int revents_; //poller返回的实际发生的事件
    int index_; //在poller中的状态

    std::weak_ptr<void> tie_; //当Channel被持有时，tie_保存着它的shared_ptr对象
    bool tied_; //是否绑定了shared_ptr对象

    //因为Channel通道里面可以获知fd最终发生的具体的事件revents_，所以它负责调用具体事件发生时的回调函数
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};