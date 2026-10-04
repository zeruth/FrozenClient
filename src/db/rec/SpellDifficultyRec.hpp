#ifndef DB_REC_SPELL_DIFFICULTY_REC_HPP
#define DB_REC_SPELL_DIFFICULTY_REC_HPP

#include <cstdint>

class SFile;

// SpellDifficulty.dbc: the spell cast in place of another at each dungeon or raid difficulty.
class SpellDifficultyRec {
    public:
        static const int32_t COLUMN_COUNT = 5;

        int32_t m_ID;
        int32_t m_difficultySpellID[4];

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
