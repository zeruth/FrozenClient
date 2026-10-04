#ifndef DB_REC_ITEM_LIMIT_CATEGORY_REC_HPP
#define DB_REC_ITEM_LIMIT_CATEGORY_REC_HPP

#include <cstdint>

class SFile;

// ItemLimitCategory.dbc: how many items of a category a character may carry or equip.
class ItemLimitCategoryRec {
    public:
        static const int32_t COLUMN_COUNT = 20;

        int32_t m_ID;
        const char* m_name;
        int32_t m_quantity;
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
