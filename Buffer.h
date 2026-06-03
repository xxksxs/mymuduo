#pragma once
#include <cstddef>
#include <vector>
#include <string>


//+-------------------+--------------------+------------------+
//| prependable bytes |  readable bytes    |  writable bytes  |
//|                   |                    |                  |
//+-------------------+--------------------+------------------+
//0      <=       readIndex   <=       writeIndex    <=      size


//网络库底层的缓冲器类型定义
class Buffer
{
public:
    explicit Buffer(size_t initialSize = kInitialSize)
    :buffer_(kCheapPrepend + initialSize), //预留空间 + 初始大小
     readIndex_(kCheapPrepend), //读指针从预留空间开始
     writeIndex_(kCheapPrepend) //写指针从预留空间开始
    {
    }
    ~Buffer();

    size_t readableBytes() const { return writeIndex_ - readIndex_; } //可读字节数
    size_t writableBytes() const { return buffer_.size() - writeIndex_; } //可写字节数
    size_t prependableBytes() const { return readIndex_; } //预留空间字节数
    const char* peek() const { return begin() + readIndex_; } //返回缓冲区中可读数据的起始地址
    void retrieve(size_t len) { 
        if (len < readableBytes()) //如果要读取的字节数小于可读字节数
        {
            readIndex_ += len; //移动读指针，表示已经读取了len字节
        }
        else
        {
            retrieveAll(); //否则，重置缓冲区，表示已经读取了所有可读数据
        }
    } //onMessage string <- Buffer，移动读指针，表示已经读取了len字节
    void retrieveUntil(const char* end) { retrieve(end - peek()); } //移动读指针，直到end位置，表示已经读取了从peek()到end的字节数
    void retrieveAll() { readIndex_ = writeIndex_ = kCheapPrepend; } //重置读写指针，表示缓冲区已经被完全读取
    std::string retrieveAllAsString() { return retrieveAsString(readableBytes()); } //将缓冲区中所有可读数据转换为字符串返回，并重置缓冲区
    std::string retrieveAsString(size_t len) //将缓冲区中len字节的可读数据转换为字符串，并移动读指针
    {
        std::string result(peek(), len); //从peek()位置开始，长度为len的字符串
        retrieve(len); //上面把缓冲区中可读的数据读取出来，这里要对缓冲区进行复位操作
        return result; //返回转换后的字符串
    }
    void ensureWritableBytes(size_t len){
        if (writableBytes() < len) //如果可写字节数小于len
        {
            makeSpace(len); //扩展缓冲区，确保有足够的空间写入len字节
        }
    } //确保缓冲区有足够的可写空间，如果没有则扩展缓冲区
    void append(const char* data, size_t len) //将[data, data + len]数据添加到缓冲区中
    {
        ensureWritableBytes(len); //确保有足够的可写空间
        std::copy(data, data + len, beginWrite()); //将data指向的数据复制到缓冲区的可写位置
        hasWritten(len); //移动写指针，表示已经写入了len字节
    }
    ssize_t readFd(int fd, int* savedErrno); //从文件描述符fd中读取数据

    char* beginWrite() { return begin() + writeIndex_; } //返回缓冲区中可写数据的起始地址
    const char* beginWrite() const { return begin() + writeIndex_; } //返回缓冲区中可写数据的起始地址（常量版本）
    void hasWritten(size_t len) { writeIndex_ += len; } //移动写指针，表示已经写入了len字节
    void unwrite(size_t len) { writeIndex_ -= len; } //移动写指针，表示撤销了len字节的写入
    
    static const size_t kInitialSize = 1024; //初始缓冲区大小
    static const size_t kCheapPrepend = 8; //预留的空间大小，方便在头部添加数据

    ssize_t writeFd(int fd, int* savedErrno); //将缓冲区中可读数据写入文件描述符fd中
private:
    char* begin() { return &*buffer_.begin(); } //返回缓冲区的起始地址
    const char* begin() const { return &*buffer_.begin(); } //返回缓冲区的起始地址（常量版本）
    void makeSpace(size_t len) //扩展缓冲区，确保有足够的空间写入len字节
    {
        //
        if (writableBytes() + prependableBytes() < len + kCheapPrepend) 
        {
            buffer_.resize(writeIndex_ + len); //扩展缓冲区大小为当前写指针位置加上len
        }
        else
        {
            size_t readable = readableBytes(); //当前可读字节数
            std::copy(begin() + readIndex_, begin() + writeIndex_, begin() + kCheapPrepend); //将可读数据移动到预留空间之后
            readIndex_ = kCheapPrepend; //重置读指针位置为预留空间之后
            writeIndex_ = readIndex_ + readable; //重置写指针位置为读指针位置加上可读字节数
        }
    }

    // 缓冲器的内部实现细节
    std::vector<char> buffer_; // 用于存储数据的缓冲区
    size_t readIndex_;         // 读指针索引
    size_t writeIndex_;        // 写指针索引
};