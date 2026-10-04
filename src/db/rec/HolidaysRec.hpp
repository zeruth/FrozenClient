#ifndef DB_REC_HOLIDAYS_REC_HPP
#define DB_REC_HOLIDAYS_REC_HPP

#include <cstdint>

class SFile;

// Holidays.dbc: a calendar event, its dates and its name.
class HolidaysRec {
    public:
        static const int32_t COLUMN_COUNT = 55;

        int32_t m_ID;
        int32_t m_duration[10];
        int32_t m_date[26];
        int32_t m_region;
        int32_t m_looping;
        int32_t m_calendarFlags[10];
        int32_t m_holidayNameID;
        int32_t m_holidayDescriptionID;
        const char* m_textureFilename;
        int32_t m_priority;
        int32_t m_calendarFilterType;
        uint32_t m_flags;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
