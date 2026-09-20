// ItemClass.dbc -- the top-level item category name GetItemInfo answers with as itemType
// ("Weapon", "Armor", "Consumable", ...).
//
// The shipped DBFilesClient/ItemClass.dbc reports 17 rows of 20 columns at 80 bytes each.
//
// Look this table up by its ID, not by the classID column beside it: the two agree for the first
// eleven rows and then stop. Row 11 is Quiver with classID 6, row 12 Quest with classID 0, and an
// item whose record says class 11 is a quiver -- so the ID is the item's class and classID is a
// coarser grouping that would name a quiver "Projectile".
//
// Localized string columns collapse to a single pointer in memory, so m_name lands at +0x0c.
#ifndef DB_REC_ITEM_CLASS_REC_HPP
#define DB_REC_ITEM_CLASS_REC_HPP

#include <cstdint>

class SFile;

class ItemClassRec {
    public:
        int32_t m_ID;
        int32_t m_classID;
        int32_t m_flags;
        const char* m_name;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
