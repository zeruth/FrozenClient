#ifndef UI_GAME_GAME_SCRIPT_HPP
#define UI_GAME_GAME_SCRIPT_HPP

#include "util/guid/Types.hpp"
#include <tempest/Vector.hpp>
#include <cstdint>

class AreaTableRec;

#define NUM_TOTEM_SLOTS 4
#define NUM_MIRROR_TIMERS 3

// One totem slot, 0x20 bytes in the reference. The guid is the totem's; the rest is cleared
// with it when the totem is destroyed.
struct TOTEMSLOT {
    uint32_t unk00;
    uint32_t unk04;
    WOWGUID guid;
    uint32_t unk10;
    uint32_t unk14;
    uint32_t unk18;
    uint32_t unk1C;
};

void DestroyTotem(uint32_t slot);

void DisplayNameError(int32_t result);

void DisplayReferAFriendError(int32_t result, const char* name);

void DisplayUIMessage(const char* text, int32_t error);

void GameScriptRegisterFunctions();

uint8_t GetComboPoints(WOWGUID guid);

AreaTableRec* GetCurrentAreaRec();

void GetDeathReleaseLocation(int32_t* mapID, C3Vector* position);

int32_t* GetMirrorTimer(uint32_t index);

TOTEMSLOT* GetTotemSlot(uint32_t slot);

bool InstanceMapHasFlag100();

bool IsCurrentAreaFlyable();

uint32_t MirrorTimerIndexFromName(const char* name);

void SendSetSelection(WOWGUID guid);

void SetBoundTradeableItem(WOWGUID item);

void SetBoundTradeableValue(uint32_t value);

#endif
