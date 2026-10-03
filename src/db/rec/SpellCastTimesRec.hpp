#ifndef DB_REC_SPELL_CAST_TIMES_REC_HPP
#define DB_REC_SPELL_CAST_TIMES_REC_HPP

#include <cstdint>

class SFile;

// SpellCastTimes.dbc: a cast time, its growth per level, and its floor, in milliseconds.
class SpellCastTimesRec {
    public:
        static const int32_t COLUMN_COUNT = 4;

        int32_t m_ID;
        int32_t m_base;
        int32_t m_perLevel;
        int32_t m_minimum;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
