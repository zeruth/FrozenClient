#ifndef DB_REC_SPELL_REC_HPP
#define DB_REC_SPELL_REC_HPP

#include <cstdint>

class SFile;

// Spell.dbc: only the three fields the action bar needs.
//
// 49839 rows, 234 columns, 936 bytes a row in 3.3.5a. Almost all of it is combat data this client
// has no use for yet, so the row is read as a flat block of 234 dwords and the wanted columns are
// picked out by index rather than by declaring 234 members.
//
// The indices were confirmed against the shipped file rather than taken from a wiki: spell 133 reads
// name "Fireball" at column 136 and icon 185 at column 133, and 585/2050 agree ("Smite",
// "Lesser Heal").
class SpellRec {
    public:
        static const int32_t COLUMN_COUNT = 234;
        static const int32_t COLUMN_ATTRIBUTES = 4;   // SPELL_ATTR0_*; 0x40 is PASSIVE
        static const int32_t COLUMN_VISUAL = 131;     // SpellVisualID[2]: 131, 132
        // The three Effect columns. The reference reads Effect[0] at +0x11c: 0x11c / 4 is 71, and
        // the eight three-wide effect arrays from there end exactly at COLUMN_EFFECT_AURA.
        static const int32_t COLUMN_EFFECT = 71;       // 71, 72, 73
        // The three EffectApplyAuraName columns. Identified from the reference reading its spell
        // record at +0x17c: 0x17c / 4 is 95, and 95 is where this array starts in the 234-column
        // 3.3.5a layout, which the constants either side of it already assume.
        static const int32_t COLUMN_EFFECT_AURA = 95;   // 95, 96, 97

        // The reference's in-memory record is the file row with each 17-column localized string
        // folded to one pointer, so an offset below the name block (+0x220) is column * 4 and one
        // past the four strings is (column - 64) * 4. Every column below was located that way from
        // an offset the reference reads.
        static const int32_t COLUMN_DISPEL = 2;                     // +0x08
        static const int32_t COLUMN_MECHANIC = 3;                   // +0x0c
        static const int32_t COLUMN_ATTRIBUTES_EX = 5;              // +0x14
        static const int32_t COLUMN_ATTRIBUTES_EX2 = 6;             // +0x18
        static const int32_t COLUMN_STANCES = 12;                   // +0x30, a 64-bit mask
        static const int32_t COLUMN_STANCES_NOT = 14;               // +0x38, a 64-bit mask
        static const int32_t COLUMN_TARGETS = 16;                   // +0x40
        static const int32_t COLUMN_EFFECT_MECHANIC = 83;           // +0x14c, 3 columns
        static const int32_t COLUMN_EFFECT_IMPLICIT_TARGET_A = 86;  // +0x158, 3 columns
        static const int32_t COLUMN_EFFECT_IMPLICIT_TARGET_B = 89;  // +0x164, 3 columns
        static const int32_t COLUMN_EFFECT_MISC_VALUE = 110;        // +0x1b8, 3 columns
        static const int32_t COLUMN_EFFECT_SPELL_CLASS_MASK = 122;  // +0x1e8, 3 x 3 columns
        static const int32_t COLUMN_SCHOOL_MASK = 225;              // +0x284

        static const int32_t COLUMN_ICON = 133;
        static const int32_t COLUMN_ACTIVE_ICON = 134;  // immediately after the normal icon
        static const int32_t COLUMN_NAME = 136;       // 17 locale columns, then
        static const int32_t COLUMN_RANK = 153;       // "Rank N", or "" for an unranked spell

        int32_t m_ID;
        int32_t m_spellIconID;

        // Shown instead of m_spellIconID while the spell is the one being tracked. Zero means the
        // spell has no separate active icon and the normal one is used.
        int32_t m_activeIconID;

        // EffectApplyAuraName[3]. Only used to recognise the tracking spells so far: 44 and 45 are
        // TRACK_CREATURES and TRACK_RESOURCES, 151 is TRACK_STEALTHED.
        int32_t m_effectAura[3];
        int32_t m_effect[3];
        const char* m_name;
        const char* m_rank;
        int32_t m_spellVisualID[2];
        uint32_t m_attributes;
        int32_t m_dispel;
        int32_t m_mechanic;
        uint32_t m_attributesEx;
        uint32_t m_attributesEx2;
        uint32_t m_stances[2];
        uint32_t m_stancesNot[2];
        uint32_t m_targets;
        int32_t m_effectMechanic[3];
        int32_t m_effectImplicitTargetA[3];
        int32_t m_effectImplicitTargetB[3];
        int32_t m_effectMiscValue[3];
        uint32_t m_effectSpellClassMask[3][3];
        uint32_t m_schoolMask;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
