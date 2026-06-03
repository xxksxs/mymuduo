#include "TimeStamp.h"
#include <sys/time.h>

TimeStamp::TimeStamp() : microSecondsSinceEpoch_(0)
{
}

TimeStamp::TimeStamp(int64_t microSecondsSinceEpoch) : microSecondsSinceEpoch_(microSecondsSinceEpoch)
{
}

TimeStamp TimeStamp::now()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    int64_t seconds = tv.tv_sec;
    return TimeStamp(seconds * 1000 * 1000 + tv.tv_usec);
}

std::string TimeStamp::toString() const
{
    char buf[128] = {0};
    struct tm tm_time;
    time_t seconds = static_cast<time_t>(microSecondsSinceEpoch_ / (1000 * 1000));
    localtime_r(&seconds, &tm_time);
    int microseconds = static_cast<int>(microSecondsSinceEpoch_ % (1000 * 1000));
    snprintf(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d.%06d",
             tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
             tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec, microseconds);
    return buf;
}
