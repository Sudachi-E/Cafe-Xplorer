#pragma once

#include <coreinit/time.h>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

inline std::string FormatCalendarTime(const struct tm& time) {
    int hour12 = time.tm_hour % 12;
    if (hour12 == 0) {
        hour12 = 12;
    }

    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << time.tm_year + 1900 << '-'
        << std::setw(2) << time.tm_mon + 1 << '-'
        << std::setw(2) << time.tm_mday << ' '
        << std::setw(2) << hour12 << ':'
        << std::setw(2) << time.tm_min << ' '
        << (time.tm_hour < 12 ? "AM" : "PM");
    return oss.str();
}

inline struct tm GetConsoleLocalTime() {
    OSCalendarTime consoleTime;
    OSTicksToCalendarTime(OSGetTime(), &consoleTime);

    struct tm result = {};
    result.tm_sec   = consoleTime.tm_sec;
    result.tm_min   = consoleTime.tm_min;
    result.tm_hour  = consoleTime.tm_hour;
    result.tm_mday  = consoleTime.tm_mday;
    result.tm_mon   = consoleTime.tm_mon;
    result.tm_year  = consoleTime.tm_year - 1900;
    result.tm_wday  = consoleTime.tm_wday;
    result.tm_yday  = consoleTime.tm_yday;
    result.tm_isdst = 0;
    return result;
}
