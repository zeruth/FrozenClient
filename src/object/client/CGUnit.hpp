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
    // UNIT_FIELD_BYTES_0, the companion to bytes1 further down, and unnamed here for the same
    // reason: four bytes in one field, with CGPlayer_C already unpacking three of them. The 3.3.5
    // field order puts it between the channel spell and health, which is exactly this slot.
    //
    //   byte 0  race
    //   byte 1  class        -- what UnitHasRelicSlot looks up in ChrClasses
    //   byte 2  gender
    //   byte 3  power type
    uint32_t bytes0;
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
    // UNIT_FIELD_BYTES_1, four bytes packed into one field, which is why it sat here unnamed as
    // padding while one of its bytes was already being read. Its position is not guesswork: the
    // 3.3.5 field order puts it exactly here, after the offhand damage pair and before the pet
    // block, and CGUnit_C already pulled the stand state out of byte 0.
    //
    //   byte 0  stand state          (sitting, kneeling, sleeping ...)
    //   byte 1  pet loyalty / talents, unused by this client
    //   byte 2  visibility flags     0x02 is the stealth creep flag IsStealthed tests
    //   byte 3  animation tier       (ground, swim, hover, fly)
    uint32_t bytes1;
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
    // UNIT_FIELD_BYTES_2, the third of the packed byte fields, sitting between the base health
    // and the attack power exactly as the 3.3.5 field order has it.
    //
    // Three of the four bytes were already being read here under the old name, which is the same
    // pattern bytes0 and bytes1 turned out to have:
    //
    //   byte 0  sheath state      -- read by CGObject_C and CGPlayer_C
    //   byte 1  pvp flags         -- bit 3 read by Script_UnitIsPVP
    //   byte 2  pet flags         -- documented layout, nothing reads it here yet
    //   byte 3  shapeshift form   -- the reference has a one-line accessor for it (FUN_0071af70,
    //                                ported as CGUnit_C::GetShapeshiftForm) and a dozen call sites
    //                                reading descriptor +0x1d3 directly
    uint32_t bytes2;
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
    float powerCostMultiplier[7];
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
        int32_t GetStatNonNegative(int32_t index) const;

        // ref: FUN_005ee050
        // The smallest cost modifier among the schools in the mask; 0 when the mask is empty.
        int32_t GetPowerCostModifier(uint32_t schoolMask) const;

        // ref: FUN_005ee0b0
        // The smallest cost multiplier among the schools in the mask, plus one.
        float GetPowerCostMultiplier(uint32_t schoolMask) const;

        // The unit fields, for the script layer's unit queries
        CGUnitData* Unit() const;

        int32_t HasCharmer() const;
        const WOWGUID& GetCharmerOrCreator() const;
        int32_t IsControlledBy(const WOWGUID& guid) const;
        int32_t IsPetitionerAndTabardDesigner() const;

    protected:
        // Protected member variables
        CGUnitData* m_unit;
        uint32_t* m_unitSaved;
        CMovement_C* m_move;
};

#endif
