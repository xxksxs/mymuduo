#include "CurrentThread.h"


namespace CurrentThread
{
    //__thread是GCC提供的一个关键字，用于声明线程局部存储（Thread Local Storage）变量。每个线程都会有自己独立的实例，互不干扰。
    __thread int t_cachedTid = 0; //线程ID缓存，初始值为0，表示未缓存

    void cacheTid()
    {
        if (t_cachedTid == 0) //如果t_cachedTid为0，说明未缓存
        {
            //通过Linux系统调用获取当前线程的tID值
            t_cachedTid = static_cast<pid_t>(::syscall(SYS_gettid)); //获取线程ID并缓存到t_cachedTid中
            // t_tidStringLength = snprintf(t_tidString, sizeof(t_tidString), "%5d ", t_cachedTid); //把线程ID格式化成字符串，存储在t_tidString中，并记录字符串长度
        }
    }
}