#ifndef DB_REC_SPELL_CATEGORY_REC_HPP
#define DB_REC_SPELL_CATEGORY_REC_HPP

#include <cstdint>

class SFile;

// SpellCategory.dbc: flags a spell cooldown category carries.
class SpellCategoryRec {
    public:
        static const int32_t COLUMN_COUNT = 2;

        int32_t m_ID;
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
