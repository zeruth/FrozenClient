#ifndef UTIL_TIME_WOW_TIME_HPP
#define UTIL_TIME_WOW_TIME_HPP

#include <cstdint>

class WowTime {
    public:
        // Static functions
        static void WowDecodeTime(uint32_t value, int32_t* minute, int32_t* hour, int32_t* weekday, int32_t* monthday, int32_t* month, int32_t* year, int32_t* flags);
        static void WowDecodeTime(uint32_t value, WowTime* time);
        static void WowEncodeTime(uint32_t& value, int32_t minute, int32_t hour, int32_t weekday, int32_t monthday, int32_t month, int32_t year, int32_t flags);
        static void WowEncodeTime(uint32_t& value, const WowTime* time);
        static char* WowGetTimeString(WowTime* time, char* str, int32_t len);

        // Member variables
        int32_t m_minute;
        int32_t m_hour;
        int32_t m_weekday;
        int32_t m_monthday;
        int32_t m_month;
        int32_t m_year;
        int32_t m_flags;
        int32_t m_holidayOffset;

        // Member functions
        WowTime();
        void AddDays(int32_t days, bool includeTime);
        int32_t CompareHour(const WowTime& other) const;
        int32_t CompareMinute(const WowTime& other) const;
        int32_t CompareMonth(const WowTime& other) const;
        int32_t CompareMonthday(const WowTime& other) const;
        int32_t CompareWeekday(const WowTime& other) const;
        int32_t GetHourAndMinutes();
        bool Matches(const WowTime& other) const;
        bool SetDate(uint32_t month, uint32_t monthday, uint32_t year);
        void SetHourAndMinutes(int32_t minutes);
        bool SetHourAndMinutes(uint32_t hour, uint32_t minutes);
};

#endif
