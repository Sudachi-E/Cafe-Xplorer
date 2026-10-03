#pragma once

#include <coreinit/time.h>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

#include "Settings.hpp"

inline const char* WeekdayAbbreviation(int dayOfWeek) {
    static const char* const names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    if (dayOfWeek < 0 || dayOfWeek > 6) {
        return "---";
    }
    return names[dayOfWeek];
}

inline std::string FormatCalendarTime(const struct tm& time, bool includeWeekday = false) {
    const bool dayFirst = (Settings::GetDateFormat() == DateFormat::DayMonthYear);
    const int first  = dayFirst ? time.tm_mday : time.tm_mon + 1;
    const int second = dayFirst ? time.tm_mon + 1 : time.tm_mday;

    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << first << '/'
        << std::setw(2) << second << '/'
        << std::setw(4) << time.tm_year + 1900 << ' '
        << std::setw(2) << time.tm_hour << ':'
        << std::setw(2) << time.tm_min << ' '
        << (time.tm_hour < 12 ? "AM" : "PM");
    if (includeWeekday) {
        oss << " (" << WeekdayAbbreviation(time.tm_wday) << ')';
    }
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
