#ifndef DB_REC_ITEM_SUB_CLASS_MASK_REC_HPP
#define DB_REC_ITEM_SUB_CLASS_MASK_REC_HPP

#include <cstdint>

class SFile;

// ItemSubClassMask.dbc: a name for a set of an item class's subclasses, keyed by class and mask.
class ItemSubClassMaskRec {
    public:
        static const int32_t COLUMN_COUNT = 19;

        int32_t m_classID;
        uint32_t m_mask;
        const char* m_name;
        int32_t m_ID;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
