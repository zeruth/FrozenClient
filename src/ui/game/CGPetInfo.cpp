#include "ui/game/CGPetInfo.hpp"
#include "client/ClientServices.hpp"
#include "net/Types.hpp"
#include "ui/game/CGGameUI.hpp"
#include "object/client/CGUnit_C.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/Types.hpp"
#include <common/DataStore.hpp>

uint32_t CGPetInfo::s_numPets;
WOWGUID* CGPetInfo::s_pets;
uint32_t CGPetInfo::s_attacking;
uint8_t CGPetInfo::s_comboPoints;
WOWGUID CGPetInfo::s_comboTarget;
uint32_t CGPetInfo::s_numSpells;
uint32_t* CGPetInfo::s_spells;
uint32_t CGPetInfo::s_mode;

// ref: FUN_005d3390
WOWGUID CGPetInfo::GetPet(uint32_t index) {
    if (index < CGPetInfo::s_numPets) {
        return CGPetInfo::s_pets[index];
    }

    return 0;
}

// ref: FUN_005d2ff0
void CGPetInfo::SetReactState(uint32_t state) {
    CGPetInfo::s_mode = (CGPetInfo::s_mode & 0xFFFFFF00) | state;
    FrameScript_SignalEvent(SCRIPT_PET_BAR_UPDATE, nullptr);
}

// ref: FUN_005d3020
void CGPetInfo::SetCommandState(uint32_t state) {
    CGPetInfo::s_mode = (CGPetInfo::s_mode & 0x080000FF) | (state << 8);
    FrameScript_SignalEvent(SCRIPT_PET_BAR_UPDATE, nullptr);
}

// ref: FUN_005d3080
void CGPetInfo::HideGrid() {
    FrameScript_SignalEvent(SCRIPT_PET_BAR_HIDEGRID, nullptr);
}

// ref: FUN_005d3090
void CGPetInfo::UpdateCooldowns() {
    FrameScript_SignalEvent(SCRIPT_PET_BAR_UPDATE_COOLDOWN, nullptr);
}

// ref: FUN_005d3310
int32_t CGPetInfo::CanAct(CGUnit_C* unit) {
    auto flags = unit->Unit()->flags;

    if (!(flags & 0x40000) && !((flags >> 23) & 1) && !((flags >> 22) & 1)) {
        return 1;
    }

    return 0;
}

// ref: FUN_005d3410
uint32_t* CGPetInfo::FindSpell(uint32_t spellID) {
    auto index = CGPetInfo::s_numSpells;

    do {
        if (index == 0) {
            return nullptr;
        }

        index--;
    } while ((CGPetInfo::s_spells[index] & 0xFFFFFF) != spellID);

    return &CGPetInfo::s_spells[index];
}

// ref: FUN_005d3520
uint8_t CGPetInfo::GetComboPoints(WOWGUID target) {
    if (target == 0) {
        target = CGGameUI::GetLockedTarget();
    }

    if (target == CGPetInfo::s_comboTarget) {
        return CGPetInfo::s_comboPoints;
    }

    return 0;
}

// ref: FUN_005d4650
void CGPetInfo::StopAttack() {
    if (!CGPetInfo::s_attacking) {
        return;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_PET_STOP_ATTACK));
    msg.Put(CGPetInfo::s_numPets ? CGPetInfo::s_pets[0] : WOWGUID(0));
    msg.Finalize();
    ClientServices::Send(&msg);

    CGPetInfo::s_attacking = 0;
}

// ref: FUN_005d4a00
void CGPetInfo::SendRename(const char* name, const char* declined) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_PET_RENAME));
    msg.Put(CGPetInfo::s_numPets ? CGPetInfo::s_pets[0] : WOWGUID(0));
    msg.PutString(name);

    if (!declined) {
        msg.Put(static_cast<uint8_t>(0));
    } else {
        msg.Put(static_cast<uint8_t>(1));

        for (int32_t i = 5; i != 0; i--) {
            msg.PutString(declined);
            declined += 0x60;
        }
    }

    msg.Finalize();
    ClientServices::Send(&msg);
}
