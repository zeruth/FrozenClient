#ifndef UI_GAME_RAID_TARGET_HPP
#define UI_GAME_RAID_TARGET_HPP

#include "net/Types.hpp"
#include "util/GUID.hpp"
#include <cstdint>

class CDataStore;

// The raid target icons -- skull, cross, star and the rest -- that show on a unit frame and over
// the unit itself.
//
// Eight slots, one per icon, each holding the GUID it is currently on. The reference keeps exactly
// this: a 64-byte block at 00beb528 that RaidInfo's init memsets to zero, scanned linearly.
// Icon N in the interface is slot N-1 here.
const int32_t RAID_TARGET_COUNT = 8;

// Which slot holds this GUID, or RAID_TARGET_COUNT when none does. The out-of-range answer is the
// count itself rather than -1 because that is what the reference returns (FUN_005728c0) and what
// GetRaidTargetIndex tests for before answering nil.
int32_t RaidTargetGetIndex(WOWGUID guid);

// Put a GUID in a slot, displacing whatever was there. Pass 0 to clear the slot.
void RaidTargetSet(int32_t index, WOWGUID guid);

// Drop every icon. Used when the server sends a full list, which replaces rather than merges.
void RaidTargetClearAll();

void RaidTargetRegisterHandlers();

int32_t ReceiveRaidTargetUpdate(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

#endif
