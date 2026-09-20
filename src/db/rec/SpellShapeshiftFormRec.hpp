// SpellShapeshiftForm.dbc -- what a shapeshifted unit counts as.
//
// Column count and row size are the reference's own expected values, read out of its
// WowClientDB<SpellShapeshiftFormRec>::Load at 00650df0, which aborts unless the file
// reports 0x23 columns of 0x8c bytes. The shipped file agrees: 32 rows, 35 columns,
// 140-byte rows.
//
// m_creatureType is the reason this record exists: a druid in Bear Form reports Beast
// rather than Humanoid. With the 17 localized name columns collapsed to one pointer it
// lands at +0x10, which is the offset FUN_0071f300 reads.
#ifndef DB_REC_SPELL_SHAPESHIFT_FORM_REC_HPP
#define DB_REC_SPELL_SHAPESHIFT_FORM_REC_HPP

#include <cstdint>

class SFile;

class SpellShapeshiftFormRec {
    public:
        int32_t m_ID;
        int32_t m_bonusActionBar;
        const char* m_name;
        int32_t m_flags;
        int32_t m_creatureType;
        int32_t m_attackIconID;
        int32_t m_combatRoundTime;
        int32_t m_creatureDisplayID[4];
        int32_t m_presetSpellID[8];

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
