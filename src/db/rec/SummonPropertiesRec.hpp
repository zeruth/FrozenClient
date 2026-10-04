#ifndef DB_REC_SUMMON_PROPERTIES_REC_HPP
#define DB_REC_SUMMON_PROPERTIES_REC_HPP

#include <cstdint>

class SFile;

// SummonProperties.dbc: how a summoned creature is controlled, and the title it is named with.
class SummonPropertiesRec {
    public:
        static const int32_t COLUMN_COUNT = 6;

        int32_t m_ID;
        int32_t m_control;
        int32_t m_faction;
        int32_t m_title;
        int32_t m_slot;
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
