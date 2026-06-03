#include "Buffer.h"
#include <errno.h>
#include <unistd.h>
#include <sys/uio.h>

ssize_t Buffer::writeFd(int fd, int *savedErrno)
{
    ssize_t n = ::write(fd, peek(), readableBytes()); //将缓冲区中可读数据写入文件描述符fd中
    if (n < 0)
    {
        *savedErrno = errno; //如果写入失败，将错误码保存到savedErrno中
    }
    return n;
}

// 从fd上读取数据， Poller工作在LT模式
// buffer缓冲区是由大小的，但是从fd上读数据的时候，无法提前知道tcp数据最终的大小
ssize_t Buffer::readFd(int fd, int *savedErrno)
{
    char extrabuf[65536] = {0}; //栈上的内存空间
    struct iovec vec[2];
    const size_t writable = writableBytes(); //缓冲区中剩余的可写空间大小
    vec[0].iov_base = begin() + writeIndex_; //第一个iovec指向缓冲区中可写空间的起始地址
    vec[0].iov_len = writable; //第一个iovec的长度为缓冲区中剩余的可写空间大小
    vec[1].iov_base = extrabuf; //第二个iovec指向栈上的extrabuf
    vec[1].iov_len = sizeof(extrabuf); //第二个iovec的长度为extrabuf的大小

    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1; //如果缓冲区中剩余的可写空间小于extrabuf的大小，则使用两个iovec，否则只使用一个iovec
    const ssize_t n = ::readv(fd, vec, iovcnt); //从fd上读取数据到vec数组中，iovec数组的长度为iovcnt
    if (n < 0)
    {
        *savedErrno = errno; //如果读取失败，将错误码保存到savedErrno中
    }
    else if (static_cast<size_t>(n) <= writable) //如果读取的数据长度小于等于缓冲区中剩余的可写空间大小
    {
        writeIndex_ += n; //移动写指针，表示已经写入了n字节数据
    }
    else
    {
        writeIndex_ = buffer_.size(); //将写指针移动到缓冲区的末尾，表示缓冲区已经被写满了
        append(extrabuf, n - writable); //将extrabuf中剩余的数据添加到缓冲区中
    }
    return n;
}Buffer::~Buffer() = default;
