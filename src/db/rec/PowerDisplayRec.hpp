#ifndef DB_REC_POWER_DISPLAY_REC_HPP
#define DB_REC_POWER_DISPLAY_REC_HPP

#include <cstdint>

class SFile;

// PowerDisplay.dbc: a power shown under another name and colour.
class PowerDisplayRec {
    public:
        static const int32_t COLUMN_COUNT = 6;

        int32_t m_ID;
        int32_t m_actualType;
        const char* m_globalStringBaseTag;
        uint8_t m_red;
        uint8_t m_green;
        uint8_t m_blue;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
