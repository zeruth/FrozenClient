// CreatureFamily.dbc -- hunter pet families, and the names UnitCreatureFamily answers with.
//
// Column count and row size are the reference's own expected values, read out of its
// WowClientDB<CreatureFamilyRec>::Load at 0063b6a0, which aborts unless the file reports
// 0x1c columns of 0x70 bytes. The shipped DBFilesClient/CreatureFamily.dbc agrees: 40 rows,
// 28 columns, 112-byte rows.
//
// Localized string columns collapse to a single pointer in memory, so m_name lands at
// +0x28 -- which is where FUN_00611820 (UnitCreatureFamily) reads the name from.
#ifndef DB_REC_CREATURE_FAMILY_REC_HPP
#define DB_REC_CREATURE_FAMILY_REC_HPP

#include <cstdint>

class SFile;

class CreatureFamilyRec {
    public:
        int32_t m_ID;
        float m_minScale;
        int32_t m_minScaleLevel;
        float m_maxScale;
        int32_t m_maxScaleLevel;
        int32_t m_skillLine[2];
        int32_t m_petFoodMask;
        int32_t m_petTalentType;
        int32_t m_categoryEnumID;
        const char* m_name;
        const char* m_iconFile;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
