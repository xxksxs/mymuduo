#pragma once

#include "noncopyable.h"
#include <string>

//LOG_INFO("%s %d", arg1, arg2)
#define LOG_INFO(LogmsgFormat, ...) \
    do { \
        Logger &logger = Logger::instance(); \
        logger.setloglevel(INFO); \
        char buf[1024] = {0}; \
        snprintf(buf, 1024, LogmsgFormat, ##__VA_ARGS__); \
        logger.Log(buf); \
    } while(0)
#define LOG_ERROR(LogmsgFormat, ...) \
    do { \
        Logger &logger = Logger::instance(); \
        logger.setloglevel(ERROR); \
        char buf[1024] = {0}; \
        snprintf(buf, 1024, LogmsgFormat, ##__VA_ARGS__); \
        logger.Log(buf); \
    } while(0)
#define LOG_FATAL(LogmsgFormat, ...) \
    do { \
        Logger &logger = Logger::instance(); \
        logger.setloglevel(FATAL); \
        char buf[1024] = {0}; \
        snprintf(buf, 1024, LogmsgFormat, ##__VA_ARGS__); \
        logger.Log(buf); \
        exit(-1); \
    } while(0)
#ifdef MUDEBUG
#define LOG_DEBUG(LogmsgFormat, ...) \
    do { \
        Logger &logger = Logger::instance(); \
        logger.setloglevel(DEBUG); \
        char buf[1024] = {0}; \
        snprintf(buf, 1024, LogmsgFormat, ##__VA_ARGS__); \
        logger.Log(buf); \
    } while(0)
#else
#define LOG_DEBUG(LogmsgFormat, ...) do {} while(0)
#endif


//定义日志的级别 INFO ERROR FATAL DEBUG
enum LogLevel
{
    INFO, //普通信息
    ERROR, //错误信息
    FATAL, //核心错误，程序无法继续运行
    DEBUG //调试信息
};

//输出一个日志类
class Logger : noncopyable
{
public:
    static Logger& instance(); //单例模式，获取日志对象的唯一实例
    void setloglevel(int level); //设置日志级别
    void Log(std::string msg); //输出日志
private:
    Logger(); //构造函数私有化，禁止外部创建对象
    int logLevel_; //日志级别
};