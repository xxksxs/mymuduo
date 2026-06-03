#pragma once 

#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>

namespace CurrentThread
{
    extern __thread int t_cachedTid; //线程ID缓存，初始值为0，表示未缓存

    void cacheTid(); //获取线程ID并缓存到t_cachedTid中
    inline int tid() //获取线程ID，如果未缓存则调用cacheTid函数获取并
    {
        if (__builtin_expect(t_cachedTid == 0, 0)) //如果t_cachedTid为0，说明未缓存
        {
            cacheTid(); //获取线程ID并缓存到t_cachedTid中
        }
        return t_cachedTid; //返回线程ID
    }
}