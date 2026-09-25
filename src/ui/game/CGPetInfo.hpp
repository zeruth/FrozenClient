#ifndef UI_GAME_C_G_PET_INFO_HPP
#define UI_GAME_C_G_PET_INFO_HPP

#include "util/GUID.hpp"
#include <cstdint>

class CGUnit_C;

class CGPetInfo {
    public:
        // Static variables. Nothing fills any of them yet: the pet packet handlers that do are
        // not ported, so there is never a pet, a pet spell or a pet combo target.
        static uint32_t s_numPets;          // ref: DAT_00c234fc
        static WOWGUID* s_pets;             // ref: DAT_00c23500
        static uint32_t s_attacking;        // ref: DAT_00c234dc
        static uint8_t s_comboPoints;       // ref: DAT_00c234e4
        static WOWGUID s_comboTarget;       // ref: DAT_00c234e8
        static uint32_t s_numSpells;        // ref: DAT_00c23534
        static uint32_t* s_spells;          // ref: DAT_00c23538
        // The pet's mode word: byte 0 is set by SetReactState, bits 8 and up by SetCommandState,
        // and bit 27 survives the latter.
        static uint32_t s_mode;             // ref: DAT_00c234c8

        // Static functions
        // ref: FUN_005d3390
        // A pet guid by index, 0 past the last.
        static WOWGUID GetPet(uint32_t index);

        // ref: FUN_005d2ff0
        static void SetReactState(uint32_t state);

        // ref: FUN_005d3020
        static void SetCommandState(uint32_t state);

        // ref: FUN_005d3080
        // Signals PET_BAR_HIDEGRID.
        static void HideGrid();

        // ref: FUN_005d3090
        // Signals PET_BAR_UPDATE_COOLDOWN.
        static void UpdateCooldowns();

        // ref: FUN_005d3310
        // Whether a unit is free to act: not stunned (0x40000), confused (0x400000) or fleeing
        // (0x800000).
        static int32_t CanAct(CGUnit_C* unit);

        // ref: FUN_005d3410
        // The pet spell entry whose low 24 bits are this spell, searched from the last entry
        // back; null when there is none. The top byte carries the entry's flags.
        static uint32_t* FindSpell(uint32_t spellID);

        // ref: FUN_005d3520
        // The combo points held on a target, 0 unless it is the tracked combo target. A null
        // guid means the locked target.
        static uint8_t GetComboPoints(WOWGUID target);

        // ref: FUN_005d4650
        // CMSG_PET_STOP_ATTACK for the first pet, only while it is attacking.
        static void StopAttack();

        // ref: FUN_005d4a00
        // CMSG_PET_RENAME for the first pet. declined, when given, is five 0x60-byte names.
        static void SendRename(const char* name, const char* declined);
};

#endif
