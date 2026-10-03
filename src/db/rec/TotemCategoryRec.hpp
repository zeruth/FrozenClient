#ifndef DB_REC_TOTEM_CATEGORY_REC_HPP
#define DB_REC_TOTEM_CATEGORY_REC_HPP

#include <cstdint>

class SFile;

// TotemCategory.dbc: the tool a spell needs, any item of the category serving.
class TotemCategoryRec {
    public:
        static const int32_t COLUMN_COUNT = 20;

        int32_t m_ID;
        const char* m_name;
        int32_t m_totemCategoryType;
        uint32_t m_totemCategoryMask;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
