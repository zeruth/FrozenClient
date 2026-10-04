#ifndef DB_REC_SPELL_DURATION_REC_HPP
#define DB_REC_SPELL_DURATION_REC_HPP

#include <cstdint>

class SFile;

// SpellDuration.dbc: an aura's duration in milliseconds, how much it grows a level, and its cap.
class SpellDurationRec {
    public:
        static const int32_t COLUMN_COUNT = 4;

        int32_t m_ID;
        int32_t m_duration;
        int32_t m_durationPerLevel;
        int32_t m_maxDuration;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
