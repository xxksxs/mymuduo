#include "Logger.h"
#include "TimeStamp.h"
#include <iostream>

// 日志类的实现
Logger::Logger() : logLevel_(INFO)
{
}

Logger &Logger::instance()
{
    static Logger logger; // 局部静态变量，保证线程安全
    return logger;
}

// 设置级别
void Logger::setloglevel(int level)
{
    logLevel_ = level;
}

// 打印信息
void Logger::Log(std::string msg)
{
    // 根据日志级别输出日志
    switch (logLevel_)
    {
    case INFO:
        std::cout << "[INFO] ";
        break;
    case ERROR:
        std::cout << "[ERROR] ";
        break;
    case FATAL:
        std::cout << "[FATAL] ";
        break;
    case DEBUG:
        std::cout << "[DEBUG] ";
        break;
    default:
        // std::cout << "[UNKNOWN] ";
        break;
    }

    // 打印时间
    std::cout << " : " << TimeStamp::now().toString() << " : " << msg << std::endl;
}

// int main()
// {
//     LOG_INFO("This is an info log, testing number: %d", 123);
//     LOG_ERROR("This is an error log, testing string: %s", "hello error");
//     LOG_FATAL("This is a fatal log");

//     return 0;
// }
