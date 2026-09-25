#include "util/time/WowTime.hpp"
#include <storm/Error.hpp>
#include <storm/String.hpp>
#include <ctime>

static const char* s_weekdays[] = {
    "Sun",
    "Mon",
    "Tue",
    "Wed",
    "Thu",
    "Fri",
    "Sat",
};

void WowTime::WowDecodeTime(uint32_t value, int32_t* minute, int32_t* hour, int32_t* weekday, int32_t* monthday, int32_t* month, int32_t* year, int32_t* flags) {
    // Minute: bits 0-5 (6 bits, max 63)
    if (minute) {
        auto m = static_cast<int32_t>(value & 63);
        *minute = (m == 63) ? -1 : m;
    }

    // Hour: bits 6-10 (5 bits, max 31)
    if (hour) {
        auto h = static_cast<int32_t>((value >> 6) & 31);
        *hour = (h == 31) ? -1 : h;
    }

    // Weekday: bits 11-13 (3 bits, max 7)
    if (weekday) {
        auto wd = static_cast<int32_t>((value >> 11) & 7);
        *weekday = (wd == 7) ? -1 : wd;
    }

    // Month day: bits 14-19 (6 bits, max 63)
    if (monthday) {
        auto md = static_cast<int32_t>((value >> 14) & 63);
        *monthday = (md == 63) ? -1 : md;
    }

    // Month: bits 20-23 (4 bits, max 15)
    if (month) {
        auto mo = static_cast<int32_t>((value >> 20) & 15);
        *month = (mo == 15) ? -1 : mo;
    }

    // Year: bits 24-28 (5 bits, max 31)
    if (year) {
        auto y = static_cast<int32_t>((value >> 24) & 31);
        *year = (y == 31) ? -1 : y;
    }

    // Flags: bits 29-30 (2 bits, max 3)
    if (flags) {
        auto f = static_cast<int32_t>((value >> 29) & 3);
        *flags = (f == 3) ? -1 : f;
    }
}

void WowTime::WowDecodeTime(uint32_t value, WowTime* time) {
    WowTime::WowDecodeTime(
        value,
        &time->m_minute,
        &time->m_hour,
        &time->m_weekday,
        &time->m_monthday,
        &time->m_month,
        &time->m_year,
        &time->m_flags
    );
}

// The retail build carries no range asserts here.
// ref: FUN_0076c910
void WowTime::WowEncodeTime(uint32_t& value, int32_t minute, int32_t hour, int32_t weekday, int32_t monthday, int32_t month, int32_t year, int32_t flags) {
    value   = ((flags & 3) << 29)       // Flags: bits 29-30 (2 bits, max 3)
            | ((year & 31) << 24)       // Year: bits 24-28 (5 bits, max 31)
            | ((month & 15) << 20)      // Month: bits 20-23 (4 bits, max 15)
            | ((monthday & 63) << 14)   // Month day: bits 14-19 (6 bits, max 63)
            | ((weekday & 7) << 11)     // Weekday: bits 11-13 (3 bits, max 7)
            | ((hour & 31) << 6)        // Hour: bits 6-10 (5 bits, max 31)
            | (minute & 63);            // Minute: bits 0-5 (6 bits, max 63)
}

// ref: FUN_0076ca50
void WowTime::WowEncodeTime(uint32_t& value, const WowTime* time) {
    value   = ((time->m_flags & 3) << 29)
            | ((time->m_year & 31) << 24)
            | ((time->m_month & 15) << 20)
            | ((time->m_monthday & 63) << 14)
            | ((time->m_weekday & 7) << 11)
            | ((time->m_hour & 31) << 6)
            | (time->m_minute & 63);
}

char* WowTime::WowGetTimeString(WowTime* time, char* str, int32_t len) {
    uint32_t encoded;
    WowTime::WowEncodeTime(encoded, time);

    if (encoded == 0) {
        SStrPrintf(str, len, "Not Set");
        return str;
    }

    char yearStr[8];
    char monthStr[8];
    char monthdayStr[8];
    char weekdayStr[8];
    char hourStr[8];
    char minuteStr[8];

    if (time->m_year >= 0) {
        SStrPrintf(yearStr, sizeof(yearStr), "%i", time->m_year + 2000);
    } else {
        SStrPrintf(yearStr, sizeof(yearStr), "A");
    }

    if (time->m_month >= 0) {
        SStrPrintf(monthStr, sizeof(monthStr), "%i", time->m_month + 1);
    } else {
        SStrPrintf(monthStr, sizeof(monthStr), "A");
    }

    if (time->m_monthday >= 0) {
        SStrPrintf(monthdayStr, sizeof(monthdayStr), "%i", time->m_monthday + 1);
    } else {
        SStrPrintf(monthdayStr, sizeof(monthdayStr), "A");
    }

    if (time->m_weekday >= 0) {
        SStrPrintf(weekdayStr, sizeof(weekdayStr), s_weekdays[time->m_weekday]);
    } else {
        SStrPrintf(weekdayStr, sizeof(weekdayStr), "Any");
    }

    if (time->m_hour >= 0) {
        SStrPrintf(hourStr, sizeof(hourStr), "%i", time->m_hour);
    } else {
        SStrPrintf(hourStr, sizeof(hourStr), "A");
    }

    if (time->m_minute >= 0) {
        SStrPrintf(minuteStr, sizeof(minuteStr), "%2.2i", time->m_minute);
    } else {
        SStrPrintf(minuteStr, sizeof(minuteStr), "A");
    }

    SStrPrintf(str, len, "%s/%s/%s (%s) %s:%s", monthStr, monthdayStr, yearStr, weekdayStr, hourStr, minuteStr);

    return str;
}

// ref: FUN_0076c190
WowTime::WowTime() {
    this->m_minute = -1;
    this->m_hour = -1;
    this->m_weekday = -1;
    this->m_monthday = -1;
    this->m_month = -1;
    this->m_year = -1;
    this->m_flags = 0;
    this->m_holidayOffset = 0;
}

void WowTime::AddDays(int32_t days, bool includeTime) {
    // Validate date

    if (this->m_year < 0 || this->m_month < 0 || this->m_monthday < 0) {
        return;
    }

    // Convert WowTime to tm

    tm t = {};

    t.tm_year = this->m_year + 100;     // WowTime year is years since 2000; tm_year is years since 1900
    t.tm_mon = this->m_month;           // WowTime month and tm_mon are both 0-based
    t.tm_mday = this->m_monthday + 1;   // WowTime monthday is 0-based; tm_mday is 1-based
    t.tm_isdst = -1;                    // Let mktime determine DST

    if (includeTime) {
        t.tm_hour = this->m_hour;
        t.tm_min = this->m_minute;
    }

    // Convert tm to time_t and add the specified days

    auto time = mktime(&t);
    time += days * 86400;


    if (!includeTime) {
        time += 3600;                   // Tack on hour to ensure DST boundaries don't muck with days added
    }

    // Convert adjusted time back to tm

    auto t_ = localtime(&time);
    if (t_) {
        t = *t_;
    } else {
        t = {};
    }

    // Convert adjusted tm back to WowTime

    this->m_year = t.tm_year - 100;
    this->m_month = t.tm_mon;
    this->m_monthday = t.tm_mday - 1;
    this->m_weekday = t.tm_wday;

    if (includeTime) {
        this->m_hour = t.tm_hour;
        this->m_minute = t.tm_min;
    }
}

// ref: FUN_0076c670
int32_t WowTime::CompareHour(const WowTime& other) const {
    if (other.m_hour < this->m_hour) {
        return 1;
    }

    return (other.m_hour <= this->m_hour) - 1;
}

// ref: FUN_0076c6a0
int32_t WowTime::CompareMinute(const WowTime& other) const {
    if (other.m_minute < this->m_minute) {
        return 1;
    }

    return (other.m_minute <= this->m_minute) - 1;
}

// ref: FUN_0076c5e0
int32_t WowTime::CompareMonth(const WowTime& other) const {
    if (other.m_month < this->m_month) {
        return 1;
    }

    return (other.m_month <= this->m_month) - 1;
}

// ref: FUN_0076c610
int32_t WowTime::CompareMonthday(const WowTime& other) const {
    if (other.m_monthday < this->m_monthday) {
        return 1;
    }

    return (other.m_monthday <= this->m_monthday) - 1;
}

// ref: FUN_0076c640
int32_t WowTime::CompareWeekday(const WowTime& other) const {
    if (other.m_weekday < this->m_weekday) {
        return 1;
    }

    return (other.m_weekday <= this->m_weekday) - 1;
}

// ref: FUN_0076c360
int32_t WowTime::GetHourAndMinutes() {
    if (this->m_hour < 0 || this->m_minute < 0) {
        return 0;
    }

    return this->m_minute + this->m_hour * 60;
}

// A negative field on either side is a wildcard and matches anything.
// ref: FUN_0076c890
bool WowTime::Matches(const WowTime& other) const {
    return (other.m_year < 0 || this->m_year < 0 || other.m_year == this->m_year)
        && (other.m_month < 0 || this->m_month < 0 || other.m_month == this->m_month)
        && (other.m_monthday < 0 || this->m_monthday < 0 || other.m_monthday == this->m_monthday)
        && (other.m_weekday < 0 || this->m_weekday < 0 || other.m_weekday == this->m_weekday)
        && (other.m_hour < 0 || this->m_hour < 0 || other.m_hour == this->m_hour)
        && (other.m_minute < 0 || this->m_minute < 0 || other.m_minute == this->m_minute);
}

// ref: FUN_0076c480
bool WowTime::SetDate(uint32_t month, uint32_t monthday, uint32_t year) {
    if (month >= 12 || monthday >= 32) {
        return false;
    }

    if (year >= 2000) {
        year -= 2000;
    }

    if (year >= 32) {
        return false;
    }

    this->m_month = month;
    this->m_monthday = monthday;
    this->m_year = year;

    return true;
}

// ref: FUN_0076c380
void WowTime::SetHourAndMinutes(int32_t minutes) {
    this->m_minute = minutes % 60;
    this->m_hour = minutes / 60;
}

// ref: FUN_0076c3c0
bool WowTime::SetHourAndMinutes(uint32_t hour, uint32_t minutes) {
    if (hour >= 24 || minutes >= 60) {
        return false;
    }

    this->m_hour = hour;
    this->m_minute = minutes;

    return true;
}
