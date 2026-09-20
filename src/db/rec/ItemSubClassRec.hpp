// ItemSubClass.dbc -- the item's subtype name, which GetItemInfo answers with as itemSubType
// ("Cloth", "Sword", "Potion", ...) and the tooltip shows beside the equip slot.
//
// The shipped DBFilesClient/ItemSubClass.dbc reports 119 rows of 44 columns at 176 bytes each.
//
// This table has NO id column: a row is identified by the (classID, subClassID) pair, so it is
// loaded with an assigned id and looked up by scanning. 119 rows makes that a non-issue.
//
// Both name columns are localized blocks and so collapse to one pointer each. The display name is
// the short one the tooltip wants; the verbose name is empty for most rows in the shipped table.
#ifndef DB_REC_ITEM_SUB_CLASS_REC_HPP
#define DB_REC_ITEM_SUB_CLASS_REC_HPP

#include <cstdint>

class SFile;

class ItemSubClassRec {
    public:
        int32_t m_ID;
        int32_t m_classID;
        int32_t m_subClassID;
        int32_t m_prerequisiteProficiency;
        int32_t m_postrequisiteProficiency;
        int32_t m_flags;
        int32_t m_displayFlags;
        int32_t m_weaponParrySeq;
        int32_t m_weaponReadySeq;
        int32_t m_weaponAttackSeq;
        int32_t m_weaponSwingSize;
        const char* m_displayName;
        const char* m_verboseName;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
