#ifndef DB_REC_SPELL_REC_HPP
#define DB_REC_SPELL_REC_HPP

#include <cstdint>

class SFile;

// Spell.dbc, 234 columns, 936 bytes a row in 3.3.5a. Held the way the reference holds it: the file
// row with each 17-column localized string folded to one pointer, 0x2a8 bytes, so a column below
// the name block (+0x220) is at column * 4 and one past the four strings at (column - 64) * 4. The
// offsets beside the members are the reference's.
class SpellRec {
    public:
        static const int32_t COLUMN_COUNT = 234;

        int32_t m_ID;                                    // +0x000
        int32_t m_category;                              // +0x004
        int32_t m_dispel;                                // +0x008
        int32_t m_mechanic;                              // +0x00c
        uint32_t m_attributes;                           // +0x010 SPELL_ATTR0_*; 0x40 is PASSIVE
        uint32_t m_attributesEx;                         // +0x014
        uint32_t m_attributesEx2;                        // +0x018
        uint32_t m_attributesEx3;                        // +0x01c
        uint32_t m_attributesEx4;                        // +0x020
        uint32_t m_attributesEx5;                        // +0x024 bit 0x40000000 raises the effect model's 0x4 state bit (CEffect::Play)
        uint32_t m_attributesEx6;                        // +0x028
        uint32_t m_attributesEx7;                        // +0x02c
        uint32_t m_stances[2];                           // +0x030 a 64-bit form mask
        uint32_t m_stancesNot[2];                        // +0x038 a 64-bit form mask
        uint32_t m_targets;                              // +0x040
        uint32_t m_targetCreatureType;                   // +0x044
        int32_t m_requiresSpellFocus;                    // +0x048
        uint32_t m_facingCasterFlags;                    // +0x04c
        int32_t m_casterAuraState;                       // +0x050
        int32_t m_targetAuraState;                       // +0x054
        int32_t m_excludeCasterAuraState;                // +0x058
        int32_t m_excludeTargetAuraState;                // +0x05c
        int32_t m_casterAuraSpell;                       // +0x060
        int32_t m_targetAuraSpell;                       // +0x064
        int32_t m_excludeCasterAuraSpell;                // +0x068
        int32_t m_excludeTargetAuraSpell;                // +0x06c
        int32_t m_castingTimeIndex;                      // +0x070 SpellCastTimes.dbc
        int32_t m_recoveryTime;                          // +0x074
        int32_t m_categoryRecoveryTime;                  // +0x078
        uint32_t m_interruptFlags;                       // +0x07c
        uint32_t m_auraInterruptFlags;                   // +0x080
        uint32_t m_channelInterruptFlags;                // +0x084
        uint32_t m_procTypeMask;                         // +0x088
        int32_t m_procChance;                            // +0x08c
        int32_t m_procCharges;                           // +0x090
        int32_t m_maxLevel;                              // +0x094
        int32_t m_baseLevel;                             // +0x098
        int32_t m_spellLevel;                            // +0x09c
        int32_t m_durationIndex;                         // +0x0a0 SpellDuration.dbc
        int32_t m_powerType;                             // +0x0a4
        int32_t m_manaCost;                              // +0x0a8
        int32_t m_manaCostPerLevel;                      // +0x0ac
        int32_t m_manaPerSecond;                         // +0x0b0
        int32_t m_manaPerSecondPerLevel;                 // +0x0b4
        int32_t m_rangeIndex;                            // +0x0b8 SpellRange.dbc
        float m_speed;                                   // +0x0bc
        int32_t m_modalNextSpell;                        // +0x0c0
        int32_t m_cumulativeAura;                        // +0x0c4
        int32_t m_totem[2];                              // +0x0c8 items the caster must carry
        int32_t m_reagent[8];                            // +0x0d0
        int32_t m_reagentCount[8];                       // +0x0f0
        int32_t m_equippedItemClass;                     // +0x110
        uint32_t m_equippedItemSubclass;                 // +0x114 a mask over the class's subclasses
        uint32_t m_equippedItemInvTypes;                 // +0x118
        int32_t m_effect[3];                             // +0x11c
        int32_t m_effectDieSides[3];                     // +0x128
        float m_effectRealPointsPerLevel[3];             // +0x134
        int32_t m_effectBasePoints[3];                   // +0x140
        int32_t m_effectMechanic[3];                     // +0x14c
        int32_t m_effectImplicitTargetA[3];              // +0x158
        int32_t m_effectImplicitTargetB[3];              // +0x164
        int32_t m_effectRadiusIndex[3];                  // +0x170
        int32_t m_effectAura[3];                         // +0x17c EffectApplyAuraName
        int32_t m_effectAuraPeriod[3];                   // +0x188
        float m_effectAmplitude[3];                      // +0x194
        int32_t m_effectChainTargets[3];                 // +0x1a0
        int32_t m_effectItemType[3];                     // +0x1ac
        int32_t m_effectMiscValue[3];                    // +0x1b8
        int32_t m_effectMiscValueB[3];                   // +0x1c4
        int32_t m_effectTriggerSpell[3];                 // +0x1d0
        float m_effectPointsPerCombo[3];                 // +0x1dc
        uint32_t m_effectSpellClassMask[3][3];           // +0x1e8
        int32_t m_spellVisualID[2];                      // +0x20c
        int32_t m_spellIconID;                           // +0x214
        int32_t m_activeIconID;                          // +0x218 shown instead of m_spellIconID while the spell is tracked; 0 for none
        int32_t m_spellPriority;                         // +0x21c of two auras on one slot, the higher keeps its visual
        const char* m_name;                              // +0x220
        const char* m_rank;                              // +0x224 "Rank N", or ""
        const char* m_description;                       // +0x228
        const char* m_auraDescription;                   // +0x22c
        int32_t m_manaCostPct;                           // +0x230
        int32_t m_startRecoveryCategory;                 // +0x234
        int32_t m_startRecoveryTime;                     // +0x238
        int32_t m_maxTargetLevel;                        // +0x23c
        int32_t m_spellClassSet;                         // +0x240 SpellFamilyName
        uint32_t m_spellClassMask[3];                    // +0x244 SpellFamilyFlags
        int32_t m_maxTargets;                            // +0x250
        int32_t m_defenseType;                           // +0x254
        int32_t m_preventionType;                        // +0x258
        int32_t m_stanceBarOrder;                        // +0x25c
        float m_effectChainAmplitude[3];                 // +0x260
        int32_t m_minFactionID;                          // +0x26c
        int32_t m_minReputation;                         // +0x270
        int32_t m_requiredAuraVision;                    // +0x274 PLAYER_FIELD_BYTES2 byte 3 level needed to see the aura visual
        int32_t m_requiredTotemCategoryID[2];            // +0x278
        int32_t m_requiredAreasID;                       // +0x280
        uint32_t m_schoolMask;                           // +0x284
        int32_t m_runeCostID;                            // +0x288 SpellRuneCost.dbc
        int32_t m_spellMissileID;                        // +0x28c SpellMissile.dbc; 0 for none
        int32_t m_powerDisplayID;                        // +0x290 PowerDisplay.dbc
        float m_effectBonusCoefficient[3];               // +0x294
        int32_t m_descriptionVariablesID;                // +0x2a0
        int32_t m_difficulty;                            // +0x2a4

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
