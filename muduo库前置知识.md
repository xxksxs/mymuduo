# muduo库前置知识

> 本文档用于记录学习 muduo / mymuduo 网络库源码前必须掌握的基础概念，重点服务于后续项目面试。  
> 核心主线：**阻塞 / 非阻塞、同步 / 异步、fd / listenfd / connfd、I/O 多路复用、epoll、Reactor 模型**。

---

## 1. 为什么学习 muduo 前要先理解这些概念？

muduo 是一个基于 Reactor 模型的 C++ 网络库。它的底层依赖 Linux 网络编程中的几个关键机制：

- socket 文件描述符；
- 非阻塞 I/O；
- I/O 多路复用；
- epoll；
- 事件分发；
- 回调函数；
- one loop per thread 线程模型。

如果这些基础概念没弄清楚，直接看 `EventLoop`、`Channel`、`Poller`、`TcpConnection` 会非常容易混乱。

可以先记住一句话：

> muduo 的核心模型是：**同步非阻塞 I/O + I/O 多路复用 + Reactor 事件分发模型**。

---

## 2. 阻塞与非阻塞

阻塞 / 非阻塞关注的是：

> **调用一个函数时，如果结果还没准备好，当前线程会不会被挂起。**

### 2.1 阻塞 I/O

阻塞指的是：调用函数后，如果条件不满足，线程会一直等待，直到结果准备好。

例如阻塞 `read`：

```cpp
char buf[1024];
int n = read(fd, buf, sizeof(buf));
```

如果 `fd` 上暂时没有数据，当前线程会卡在 `read()` 这里。

流程可以理解为：

```text
线程调用 read()
    ↓
没有数据
    ↓
线程睡眠 / 阻塞
    ↓
数据到来
    ↓
read 返回
```

生活类比：

> 去食堂打饭，饭还没做好，你就站在窗口前一直等，直到饭做好。

这就是阻塞。

### 2.2 非阻塞 I/O

非阻塞指的是：调用函数后，如果条件不满足，函数不会让线程卡住，而是立刻返回。

如果把 socket fd 设置为非阻塞：

```cpp
fcntl(fd, F_SETFL, O_NONBLOCK);
```

再调用：

```cpp
int n = read(fd, buf, sizeof(buf));
```

如果此时没有数据，`read()` 不会一直等待，而是立刻返回错误，常见错误码是：

```cpp
errno == EAGAIN || errno == EWOULDBLOCK
```

流程是：

```text
线程调用 read()
    ↓
没有数据
    ↓
read 立刻返回 EAGAIN
    ↓
线程可以继续做别的事
```

生活类比：

> 去食堂打饭，窗口说“还没好”，你不站着等，而是先去买饮料、看手机，过会儿再来问。

这就是非阻塞。

### 2.3 小结

| 概念 | 关注点 | 行为 |
|---|---|---|
| 阻塞 | 调用时等不等 | 没有结果就一直等 |
| 非阻塞 | 调用时等不等 | 没有结果就立刻返回 |

---

## 3. 同步与异步

同步 / 异步关注的是：

> **I/O 结果由谁来完成，以及完成后如何通知调用者。**

这和阻塞 / 非阻塞不是同一组概念。

### 3.1 同步 I/O

同步 I/O 指的是：最终的数据读写动作仍然由调用线程自己完成。

例如：

```cpp
int n = read(fd, buf, sizeof(buf));
```

不管这个 `read()` 是阻塞还是非阻塞，只要最终是调用线程自己调用 `read()` 把数据读到用户缓冲区里，它就是同步 I/O。

同步不等于阻塞。

例如同步非阻塞：

```cpp
while (true) {
    int n = read(fd, buf, sizeof(buf));
    if (n > 0) {
        break;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // 暂时没有数据，可以去做别的事
        continue;
    }
}
```

这个过程虽然每次 `read()` 不阻塞，但仍然是调用线程自己主动检查、主动读取，所以它仍然是同步。

生活类比：

> 你点了一杯奶茶，然后自己反复去柜台问：“好了吗？”  
> 即使你每次问完马上走，这仍然是同步，因为是你主动去询问和获取结果。

### 3.2 异步 I/O

异步 I/O 指的是：调用方发起请求后，不需要自己等待或主动检查结果，等操作完成后由系统、框架或其他线程通知调用方。

伪代码：

```cpp
async_read(fd, buf, callback);
```

调用后立即返回。等数据真正读完后，系统或框架调用：

```cpp
callback(buf);
```

生活类比：

> 你点奶茶后拿了一个叫号器，不需要一直去柜台问。奶茶好了，叫号器震动通知你。

这就是异步。

### 3.3 四种组合

阻塞 / 非阻塞和同步 / 异步是两个维度，所以理论上有四种组合。

| 类型 | 含义 | 例子 |
|---|---|---|
| 同步阻塞 | 自己做，而且一直等 | 阻塞 `read()` |
| 同步非阻塞 | 自己做，但不傻等 | 非阻塞 `read()` + 轮询 / epoll |
| 异步阻塞 | 别人做，但自己等待结果 | `future.get()` 等待后台任务 |
| 异步非阻塞 | 别人做，完成后通知自己 | `async_read(..., callback)` |

### 3.4 muduo 属于哪一种？

muduo 使用的是：

```text
同步非阻塞 I/O + I/O 多路复用 + Reactor
```

原因是：

1. socket 被设置为非阻塞；
2. 通过 epoll 等待 fd 就绪；
3. fd 可读后，用户线程仍然要主动调用 `read()` / `readv()` 读取数据；
4. fd 可写后，用户线程仍然要主动调用 `write()` 发送数据。

因此，muduo 不是内核意义上的异步 I/O。

更准确地说：

> muduo 在编程接口上大量使用回调，看起来像“异步”；但在 Linux I/O 模型上，它属于同步非阻塞 I/O。

面试可以这样回答：

> 阻塞和非阻塞关注调用时线程会不会被挂起；同步和异步关注 I/O 结果由谁完成、如何通知调用方。muduo 使用的是同步非阻塞 I/O，它把 socket 设置为非阻塞，然后通过 epoll 等待事件就绪。当 fd 可读时，由 EventLoop 在线程中回调对应 Channel，最终仍然由当前线程主动调用 read 或 readv 读取数据。

---

## 4. fd、listenfd、connfd

### 4.1 fd 是什么？

`fd` 是 file descriptor，中文叫文件描述符。

在 Linux 中，很多内核对象都可以用 fd 表示，例如：

- 普通文件；
- socket；
- pipe；
- eventfd；
- timerfd；
- epoll fd。

所以 `fd` 是一个总称，本质上是一个整数句柄。

例如普通文件：

```cpp
int fd = open("a.txt", O_RDONLY);
```

例如 socket：

```cpp
int sockfd = socket(AF_INET, SOCK_STREAM, 0);
```

### 4.2 listenfd 是什么？

`listenfd` 是一种特殊的 socket fd，表示监听 socket。

服务器启动时通常会经历：

```cpp
int listenfd = socket(...);
bind(listenfd, ...);
listen(listenfd, ...);
```

它的职责是：

> 监听有没有新的客户端连接到来。

当 `listenfd` 可读时，通常表示：

> 有新连接来了，可以调用 `accept()`。

例如：

```cpp
int connfd = accept(listenfd, ...);
```

注意：`listenfd` 不是用来收发普通业务数据的。

### 4.3 connfd 是什么？

`connfd` 是通过 `accept()` 得到的已连接 socket fd。

它才是真正用来和某个客户端通信的 fd。

```cpp
int connfd = accept(listenfd, ...);
read(connfd, buf, sizeof(buf));
write(connfd, data, len);
```

可以这样记：

```text
listenfd：负责接客
connfd：负责服务客人
```

### 4.4 listenfd 和 connfd 的区别

| 名称 | 本质 | 作用 | 可读时意味着什么 | 应该调用什么 |
|---|---|---|---|---|
| fd | 文件描述符总称 | 泛指一个内核对象句柄 | 看具体类型 | 看具体类型 |
| listenfd | 监听 socket fd | 等待新连接 | 有新连接到来 | `accept()` |
| connfd | 已连接 socket fd | 和客户端通信 | 对端发来数据 | `read()` / `recv()` |

在 muduo 中：

```text
listenfd
    ↓
Acceptor 持有
    ↓
对应 acceptChannel
    ↓
可读时调用 Acceptor::handleRead()
    ↓
accept() 得到 connfd
```

然后：

```text
connfd
    ↓
TcpConnection 持有
    ↓
对应 connectionChannel
    ↓
可读时调用 TcpConnection::handleRead()
    ↓
read / readv 读取客户端数据
```

面试可以这样回答：

> fd 是文件描述符的总称，listenfd 是其中一种，表示监听 socket。服务器通过 socket、bind、listen 创建 listenfd。当 listenfd 在 epoll 中变为可读，说明有新连接到来，此时应该调用 accept，得到新的 connfd。真正和客户端进行业务读写的是 connfd，而不是 listenfd。

---

## 5. I/O 多路复用

I/O 多路复用的核心是：

> **用一个线程，同时监听多个 fd。一旦其中某些 fd 就绪，就通知程序去处理。**

其中：

```text
多路 = 多个 fd / 多个 socket 连接
复用 = 复用一个线程 / 一个事件循环 / 一个等待点
```

### 5.1 为什么需要 I/O 多路复用？

最朴素的服务器模型是：

```cpp
while (true) {
    int connfd = accept(listenfd, ...);
    read(connfd, buf, sizeof(buf));
    write(connfd, buf, n);
}
```

问题是：`read()` 可能阻塞。

如果某个客户端连接后一直不发数据，线程就卡在这个连接上，无法处理其他连接。

一种简单改进是：

```text
一个连接一个线程
```

但高并发下会变成：

```text
10000 个连接 ≈ 10000 个线程
```

这会带来：

- 内存开销大；
- 上下文切换频繁；
- 调度成本高；
- 系统稳定性差。

I/O 多路复用就是为了解决这个问题。

### 5.2 I/O 多路复用的基本思路

把多个 fd 交给内核，让内核帮我们监控。

以 epoll 为例：

```cpp
epoll_wait(epfd, events, maxevents, timeout);
```

当前线程阻塞在 `epoll_wait()` 上。

一旦某些 fd 就绪，`epoll_wait()` 返回，例如告诉程序：

```text
connfd13 可读
connfd25 可读
connfd999 可写
```

然后程序再处理这些 fd：

```cpp
for (int i = 0; i < n; ++i) {
    int fd = events[i].data.fd;

    if (events[i].events & EPOLLIN) {
        read(fd, buf, sizeof(buf));
    }

    if (events[i].events & EPOLLOUT) {
        write(fd, data, len);
    }
}
```

整体流程：

```text
注册多个 fd
    ↓
统一阻塞在 epoll_wait
    ↓
内核返回就绪 fd
    ↓
用户线程处理这些 fd
```

### 5.3 select、poll、epoll

Linux 常见的 I/O 多路复用机制有：

- `select`；
- `poll`；
- `epoll`。

它们都解决同一类问题：

> 监听多个 fd，返回其中已经就绪的 fd。

区别在于性能和实现方式。

#### select

`select` 使用 `fd_set` 保存 fd 集合。

缺点：

- fd 数量有限；
- 每次调用都要重新传 fd 集合；
- 返回后还要遍历所有 fd，才能知道谁就绪。

#### poll

`poll` 使用数组保存 fd。

相比 `select`，它没有固定的 fd 数量限制。

但它仍然存在问题：

- 每次调用都要传整个 fd 数组；
- 返回后仍然要遍历所有 fd。

#### epoll

`epoll` 是 Linux 下高性能网络库常用的机制。

它的使用分为三步：

```cpp
int epfd = epoll_create1(0);
```

```cpp
struct epoll_event ev;
ev.events = EPOLLIN;
ev.data.fd = connfd;
epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev);
```

```cpp
int n = epoll_wait(epfd, events, maxevents, timeout);
```

`epoll` 的优势是：

1. 注册和等待分离；
2. 注册过的 fd 不需要每次重新传入；
3. `epoll_wait()` 返回的是就绪事件集合；
4. 高并发下更适合大量连接、少量活跃的场景。

### 5.4 I/O 多路复用是不是异步 I/O？

不是。

`epoll` 只是告诉你：

```text
某个 fd 现在可以读了
某个 fd 现在可以写了
```

但数据并不会自动进入你的用户缓冲区。

你仍然需要自己调用：

```cpp
read(fd, buf, sizeof(buf));
write(fd, data, len);
```

所以：

> epoll 是就绪通知机制，不是数据搬运机制。

面试可以这样回答：

> I/O 多路复用是一种用一个线程同时监听多个 fd 的机制。程序把多个 socket fd 注册到 select、poll 或 epoll 这样的多路复用器中，然后阻塞在统一的等待函数上，比如 epoll_wait。当某些 fd 就绪时，内核返回这些就绪事件，用户线程再针对这些 fd 调用 read 或 write 进行处理。epoll 不是异步 I/O，它只是就绪通知，真正的数据读写仍然由用户线程完成。

---

## 6. EPOLLIN 是什么？

`EPOLLIN` 是 epoll 的可读事件标志。

注册 fd 时写：

```cpp
ev.events = EPOLLIN;
epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
```

意思是：

> 我关心这个 fd 的可读事件。一旦这个 fd 上有东西可以读了，请通知我。

### 6.1 对 connfd 来说

如果 fd 是已连接 socket，也就是 `connfd`：

```cpp
ev.events = EPOLLIN;
ev.data.fd = connfd;
epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev);
```

这表示：

```text
connfd 上如果有数据可以读，就通知我
```

当 `epoll_wait()` 返回 `EPOLLIN` 时，通常说明：

> 客户端发来了数据。

此时应该调用：

```cpp
read(connfd, buf, sizeof(buf));
```

在 muduo 中，对应流程是：

```text
connfd 可读
    ↓
TcpConnection 对应的 Channel 触发读事件
    ↓
Channel::handleEvent()
    ↓
TcpConnection::handleRead()
    ↓
read / readv 读取数据
    ↓
调用用户设置的 messageCallback
```

### 6.2 对 listenfd 来说

如果 fd 是监听 socket，也就是 `listenfd`：

```cpp
ev.events = EPOLLIN;
ev.data.fd = listenfd;
epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);
```

这时 `EPOLLIN` 的含义不是“有普通业务数据可读”，而是：

> 有新的客户端连接来了，可以调用 `accept()`。

此时应该调用：

```cpp
int connfd = accept(listenfd, ...);
```

在 muduo 中，对应流程是：

```text
listenfd 可读
    ↓
Acceptor 对应的 Channel 触发读事件
    ↓
Channel::handleEvent()
    ↓
Acceptor::handleRead()
    ↓
accept()
    ↓
TcpServer::newConnection()
```

### 6.3 EPOLLIN 和 EPOLLOUT

`EPOLLIN` 表示可读事件。

`EPOLLOUT` 表示可写事件。

```cpp
ev.events = EPOLLIN | EPOLLOUT;
```

表示同时关心：

```text
这个 fd 能不能读
这个 fd 能不能写
```

但在网络库中，`EPOLLOUT` 通常不会一直打开。

原因是 socket 大多数时候都是可写的，如果一直监听 `EPOLLOUT`，`epoll_wait()` 会频繁返回，造成 CPU 空转。

所以 muduo 的常见策略是：

```text
默认关注 EPOLLIN
只有发送缓冲区还有数据没写完时，才临时关注 EPOLLOUT
写完后再关闭 EPOLLOUT
```

### 6.4 Channel 中的封装

在 muduo / mymuduo 中，一般不会在业务层直接操作 `EPOLLIN`，而是通过 `Channel` 封装。

类似：

```cpp
void Channel::enableReading() {
    events_ |= kReadEvent;
    update();
}
```

其中：

```cpp
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
```

也就是说：

```text
Channel::enableReading()
    ↓
EventLoop::updateChannel(this)
    ↓
Poller::updateChannel(channel)
    ↓
EpollPoller::epoll_ctl(...)
```

看到 `enableReading()`，就应该联想到：

> 给这个 fd 注册或更新 EPOLLIN 事件。

面试可以这样回答：

> EPOLLIN 是 epoll 的可读事件标志。注册 fd 时指定 EPOLLIN，表示希望 epoll 在该 fd 可读时通知我们。如果这个 fd 是监听 socket，那么可读表示有新连接到来，需要调用 accept；如果这个 fd 是已连接 socket，那么可读表示对端发送了数据，需要调用 read 或 recv。在 muduo 中，Channel::enableReading() 本质上就是给对应 fd 添加 EPOLLIN 关注，然后通过 EventLoop 和 Poller 更新到 epoll 中。

---

## 7. Reactor 模型

Reactor 模型可以用一句话概括：

> Reactor 是一种事件驱动模型：程序不主动阻塞等待某个连接的数据，而是把 socket 注册到 I/O 多路复用器中，由 epoll 统一监听哪些 fd 就绪；一旦某个 fd 可读、可写或出错，就把这个事件分发给对应的回调函数处理。

### 7.1 Reactor 解决的问题

传统阻塞 I/O 是：

```cpp
while (true) {
    int connfd = accept(listenfd, ...);
    read(connfd, buf, sizeof(buf));
    write(connfd, buf, n);
}
```

问题是一个连接可能阻塞整个线程。

Reactor 的思路是：

```text
不要阻塞在某一个 fd 上
而是把所有 fd 都交给 epoll
让 epoll 告诉我们哪些 fd 就绪
```

核心循环类似：

```cpp
while (true) {
    activeChannels = epoll_wait(...);

    for (Channel* channel : activeChannels) {
        channel->handleEvent();
    }
}
```

### 7.2 Reactor 的三件套

Reactor 模型中通常有三类核心角色：

| Reactor 概念 | muduo / mymuduo 中的对应类 |
|---|---|
| 事件循环 | `EventLoop` |
| I/O 多路复用器 | `Poller` / `EpollPoller` |
| 事件源 | socket fd |
| 事件封装 | `Channel` |
| 新连接处理器 | `Acceptor` |
| 连接封装 | `TcpConnection` |
| 服务器管理器 | `TcpServer` |
| 线程池 | `EventLoopThreadPool` |

### 7.3 EventLoop

`EventLoop` 是 Reactor 的核心。

它负责：

1. 调用 `Poller::poll()` 等待事件；
2. 获取活跃的 `Channel`；
3. 遍历活跃 `Channel`；
4. 调用 `Channel::handleEvent()` 分发事件；
5. 执行跨线程投递过来的任务。

伪代码：

```cpp
while (!quit_) {
    activeChannels_ = poller_->poll(timeout);

    for (Channel* channel : activeChannels_) {
        channel->handleEvent();
    }

    doPendingFunctors();
}
```

面试可以这样说：

> EventLoop 是 muduo 中 Reactor 模型的核心。每个 EventLoop 绑定一个线程，内部通过 Poller 调用 epoll_wait 等待 I/O 事件，然后把活跃 fd 对应的 Channel 取出来，逐个执行事件回调。

### 7.4 Poller / EpollPoller

`Poller` 是 I/O 多路复用器的抽象。

`EpollPoller` 是 Linux 下基于 epoll 的具体实现。

它封装了：

```text
epoll_create
epoll_ctl
epoll_wait
```

调用链可以理解为：

```text
EventLoop::loop()
    ↓
Poller::poll()
    ↓
EpollPoller::poll()
    ↓
epoll_wait()
```

这样做的好处是解耦。

`EventLoop` 不直接依赖 epoll 细节，而是依赖 `Poller` 抽象。

### 7.5 Channel

`Channel` 不是 socket。

`Channel` 是对 fd 及其事件回调的封装。

一个 `Channel` 通常包含：

```cpp
int fd_;
int events_;   // 关心的事件，比如 EPOLLIN / EPOLLOUT
int revents_;  // epoll 返回的实际发生事件

ReadEventCallback readCallback_;
EventCallback writeCallback_;
EventCallback closeCallback_;
EventCallback errorCallback_;
```

可以记成：

```text
fd + events + callbacks = Channel
```

例如：

- `listenfd` 对应一个 `Channel`，读事件回调是 `Acceptor::handleRead()`；
- `connfd` 对应一个 `Channel`，读事件回调是 `TcpConnection::handleRead()`。

所以 `Channel` 的作用是：

> 当某个 fd 上有事件发生时，知道应该调用哪个对象的哪个函数。

---

## 8. muduo 的多 Reactor 线程模型

muduo 采用：

```text
one loop per thread
```

也就是：

> 一个线程一个 EventLoop。

典型结构是：

```text
Main Thread
  └── Main EventLoop
        └── Acceptor
              └── accept 新连接
                    ↓
          分发给某个 Sub EventLoop

Worker Thread 1
  └── Sub EventLoop
        └── TcpConnection A, B, C

Worker Thread 2
  └── Sub EventLoop
        └── TcpConnection D, E, F
```

### 8.1 MainLoop 做什么？

MainLoop 主要负责：

- 监听 `listenfd`；
- 处理新连接到来事件；
- 调用 `accept()` 得到 `connfd`；
- 把新连接分发给某个 SubLoop。

### 8.2 SubLoop 做什么？

SubLoop 主要负责：

- 管理已经建立的连接；
- 监听 `connfd` 的读写事件；
- 处理客户端数据读取；
- 处理发送缓冲区中的数据写出；
- 执行用户设置的连接回调和消息回调。

### 8.3 为什么这样设计？

这样设计的好处是：

1. MainLoop 不被具体连接的读写拖慢；
2. 多个 SubLoop 可以分摊大量连接；
3. 一个连接固定归属于一个 EventLoop，避免多个线程同时操作同一个连接；
4. 线程模型清晰，便于管理生命周期。

一句话：

> MainLoop 负责接收连接，SubLoop 负责连接建立后的读写 I/O。

---

## 9. 一次新连接的完整流程

### 9.1 服务器启动阶段

服务器创建监听 socket：

```text
socket()
    ↓
bind()
    ↓
listen()
    ↓
得到 listenfd
```

然后 `Acceptor` 把 `listenfd` 封装成 `Channel`，并注册到 MainLoop 的 epoll 中，关注 `EPOLLIN`。

```text
listenfd
    ↓
acceptChannel
    ↓
MainLoop::updateChannel()
    ↓
EpollPoller::epoll_ctl(EPOLL_CTL_ADD)
```

### 9.2 新连接到来

客户端连接服务器后，`listenfd` 变为可读。

```text
MainLoop::epoll_wait()
    ↓
listenfd 可读
    ↓
acceptChannel::handleEvent()
    ↓
Acceptor::handleRead()
```

### 9.3 accept 得到 connfd

`Acceptor::handleRead()` 调用：

```cpp
int connfd = accept(listenfd, ...);
```

得到真正用于通信的连接 fd。

### 9.4 TcpServer 创建 TcpConnection

`TcpServer` 为新连接创建 `TcpConnection` 对象。

`TcpConnection` 内部会为 `connfd` 创建对应的 `Channel`。

```text
connfd
    ↓
TcpConnection
    ↓
connectionChannel
```

### 9.5 分发到 SubLoop

如果设置了多个工作线程，`TcpServer` 会从 `EventLoopThreadPool` 中选择一个 SubLoop。

通常采用轮询方式：

```text
SubLoop 1
SubLoop 2
SubLoop 3
SubLoop 4
```

新连接被分配给某个 SubLoop 后，后续读写事件都在这个 SubLoop 所在线程中处理。

### 9.6 客户端发送数据

客户端发送数据后，`connfd` 变为可读。

```text
SubLoop::epoll_wait()
    ↓
connectionChannel 可读
    ↓
TcpConnection::handleRead()
    ↓
read / readv 读取数据
    ↓
调用 messageCallback
```

如果是 echo server，则用户回调中可能调用：

```cpp
conn->send(msg);
```

然后 muduo 再负责把数据写回客户端。

---

## 10. 面试高频问题总结

### 10.1 什么是阻塞和非阻塞？

> 阻塞和非阻塞关注调用函数时线程会不会被挂起。阻塞 I/O 中，如果数据没准备好，线程会一直等待；非阻塞 I/O 中，如果数据没准备好，函数会立刻返回，通常返回 `EAGAIN` 或 `EWOULDBLOCK`。

### 10.2 什么是同步和异步？

> 同步和异步关注 I/O 结果由谁完成、如何通知调用方。同步 I/O 中，最终数据读写由调用线程自己调用 `read` 或 `write` 完成；异步 I/O 中，调用方发起请求后立即返回，真正完成后由系统或框架通知调用方。

### 10.3 muduo 是异步 I/O 吗？

> 严格来说不是。muduo 使用的是同步非阻塞 I/O。它通过 epoll 监听 fd 就绪事件，当 fd 可读或可写时，再由 EventLoop 所在线程主动调用 `read`、`readv` 或 `write` 完成数据读写。muduo 的编程方式是回调式的，但底层 I/O 模型不是内核意义上的异步 I/O。

### 10.4 fd、listenfd、connfd 的关系是什么？

> fd 是文件描述符的统称。listenfd 是监听 socket，用来接收新连接；当 listenfd 可读时，说明有新连接到来，需要调用 accept。accept 返回的 connfd 是已连接 socket，真正用于和客户端进行业务数据读写。

### 10.5 I/O 多路复用是什么？

> I/O 多路复用是一种用一个线程同时监听多个 fd 的机制。程序把多个 fd 注册到 select、poll 或 epoll 中，然后阻塞在统一的等待函数上，比如 epoll_wait。当某些 fd 就绪时，内核返回这些事件，用户线程再去处理对应 fd。

### 10.6 epoll 是异步 I/O 吗？

> 不是。epoll 是就绪通知机制。它只告诉用户线程某个 fd 可以读或可以写，真正的数据读写仍然需要用户线程自己调用 read 或 write 完成。

### 10.7 EPOLLIN 是什么？

> EPOLLIN 是 epoll 的可读事件标志。对 listenfd 来说，EPOLLIN 表示有新连接到来，需要调用 accept；对 connfd 来说，EPOLLIN 表示对端发来了数据，需要调用 read 或 recv。

### 10.8 Channel 是 socket 吗？

> 不是。Channel 是对 fd、感兴趣事件和事件回调的封装。socket fd 是内核资源，Channel 是 muduo 在用户态对这个 fd 的事件管理对象。Channel 负责在事件发生时调用对应的 readCallback、writeCallback、closeCallback 或 errorCallback。

### 10.9 EventLoop 是什么？

> EventLoop 是 muduo 中 Reactor 的核心。每个 EventLoop 绑定一个线程，内部通过 Poller 调用 epoll_wait 等待事件，然后遍历活跃 Channel，调用 Channel::handleEvent() 分发事件。

### 10.10 muduo 的 Reactor 模型是什么？

> muduo 使用基于 Reactor 的事件驱动模型。它将 socket fd 封装为 Channel，并把 Channel 注册到 Poller 中。Poller 在 Linux 下通过 epoll_wait 等待事件就绪，EventLoop 拿到活跃 Channel 后调用 handleEvent 分发到对应回调。线程模型上采用 one loop per thread，主 EventLoop 负责 accept 新连接，工作线程中的 SubLoop 负责已连接 socket 的读写事件。

---

## 11. 一句话总览

可以把 muduo 的核心前置知识串成一条线：

```text
Linux 中一切皆 fd
    ↓
服务器用 listenfd 监听新连接
    ↓
accept 得到 connfd
    ↓
connfd 设置为非阻塞
    ↓
把 listenfd / connfd 注册到 epoll
    ↓
epoll_wait 返回就绪事件
    ↓
EventLoop 获取活跃 Channel
    ↓
Channel 根据事件调用回调
    ↓
Acceptor 处理新连接，TcpConnection 处理连接读写
    ↓
形成 Reactor 事件驱动网络库
```

最终总结：

> muduo 不是简单封装 epoll，而是以 `EventLoop + Poller + Channel` 为骨架，把 Linux 同步非阻塞 I/O 和 I/O 多路复用机制组织成清晰的 Reactor 网络编程模型。
