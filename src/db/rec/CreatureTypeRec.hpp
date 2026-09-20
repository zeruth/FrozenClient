// CreatureType.dbc -- the names UnitCreatureType answers with ("Beast", "Undead", ...).
//
// Column count and row size are the reference's own expected values, read out of its
// WowClientDB<CreatureTypeRec>::Load at 0063c240, which aborts unless the file reports
// 0x13 columns of 0x4c bytes. The shipped DBFilesClient/CreatureType.dbc agrees: 13 rows,
// 19 columns, 76-byte rows.
//
// Localized string columns collapse to a single pointer in memory, so m_name lands at
// +0x04 -- which is where FUN_00611780 (UnitCreatureType) reads the name from.
#ifndef DB_REC_CREATURE_TYPE_REC_HPP
#define DB_REC_CREATURE_TYPE_REC_HPP

#include <cstdint>

class SFile;

class CreatureTypeRec {
    public:
        int32_t m_ID;
        const char* m_name;
        int32_t m_flags;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
