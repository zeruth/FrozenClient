#ifndef OBJECT_CLIENT_CG_UNIT_HPP
#define OBJECT_CLIENT_CG_UNIT_HPP

#include "util/GUID.hpp"
#include <tempest/Vector.hpp>
#include <cstdint>

class CMovement_C;

struct CGUnitData {
    WOWGUID charm;
    WOWGUID summon;
    WOWGUID critter;
    WOWGUID charmedBy;
    WOWGUID summonedBy;
    WOWGUID createdBy;
    WOWGUID target;
    WOWGUID channelObject;
    int32_t channelSpell;
    // Not padding: the packed byte field. Script_UnitPowerTypeOf reads the power type out of
    // its top byte, and UNIT_DISPLAYPOWER is signalled when this dword moves.
    int32_t pad1;
    int32_t health;
    int32_t power[7];
    int32_t maxHealth;
    int32_t maxPower[7];
    float powerRegenFlatModifier[7];
    int32_t powerRegenInterruptedFlatModifier[7];
    int32_t level;
    int32_t factionTemplate;
    int32_t virtualItemSlotID[3];
    uint32_t flags;
    uint32_t flags2;
    uint32_t auraState;
    uint32_t attackRoundBaseTime[2];
    uint32_t rangedAttackTime;
    float boundingRadius;
    float combatReach;
    int32_t displayID;
    int32_t nativeDisplayID;
    int32_t mountDisplayID;
    float minDamage;
    float maxDamage;

    // Floats, like the main-hand pair above. These were uint32_t, which no caller had read yet --
    // the reference takes all four through a float load.
    float minOffhandDamage;
    float maxOffhandDamage;
    int32_t pad2;
    uint32_t petNumber;
    uint32_t petNameTimestamp;
    uint32_t petExperience;
    uint32_t petNextLevelExperience;
    uint32_t dynamicFlags;
    float modCastingSpeed;
    int32_t createdBySpell;
    uint32_t npcFlags;
    uint32_t emoteState;
    int32_t stats[5];
    int32_t posStats[5];
    int32_t negStats[5];
    int32_t resistance[7];
    int32_t resistanceBuffModsPositive[7];
    int32_t resistanceBuffModsNegative[7];
    int32_t baseMana;
    int32_t baseHealth;
    int32_t pad3;
    int32_t attackPower;

    // A PACKED PAIR, not one number: the positive modifier is the low int16 and the negative one
    // the high int16. The reference reads them as two shorts off 0x1d8 and 0x1da.
    int32_t attackPowerMods;

    // A float, in a block whose fields are otherwise integers. It was declared int32_t here, which
    // would have reported the raw bit pattern as a multiplier. The scale callers want is this
    // PLUS ONE, so zero means unscaled.
    float attackPowerMultiplier;

    int32_t rangedAttackPower;
    int32_t rangedAttackPowerMods;
    float rangedAttackPowerMultiplier;
    float minRangedDamage;
    float maxRangedDamage;
    int32_t powerCostModifier[7];
    int32_t powerCostMultiplier[7];
    int32_t maxHealthModifier;
    float hoverHeight;
    int32_t pad4;
};

class CGUnit {
    public:
        // Public static functions
        static uint32_t GetBaseOffset();
        static uint32_t GetBaseOffsetSaved();
        static uint32_t GetDataSize();
        static uint32_t GetDataSizeSaved();
        static uint32_t TotalFields();
        static uint32_t TotalFieldsSaved();

        // Public member functions
        CGUnit(CMovement_C& move)
            : m_move(&move) {};
        int32_t GetDisplayID() const;
        float GetFacing() const;
        int32_t GetNativeDisplayID() const;
        C3Vector GetPosition() const;
        float GetRawFacing() const;

        // The unit fields, for the script layer's unit queries
        CGUnitData* Unit() const;

    protected:
        // Protected member variables
        CGUnitData* m_unit;
        uint32_t* m_unitSaved;
        CMovement_C* m_move;
};

#endif
