#ifndef DB_REC_ITEM_REC_HPP
#define DB_REC_ITEM_REC_HPP

#include <cstdint>

class SFile;

// Item.dbc: the client's basic item table, enough to turn an equipped item's entry into the
// display info and inventory type the character component needs
class ItemRec {
    public:
        int32_t m_ID;
        int32_t m_classID;
        int32_t m_subclassID;
        int32_t m_soundOverrideSubclassID;
        int32_t m_material;
        int32_t m_displayInfoID;
        int32_t m_inventoryType;
        int32_t m_sheatheType;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
