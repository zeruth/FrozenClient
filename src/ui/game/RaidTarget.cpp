#include "ui/game/RaidTarget.hpp"
#include "client/ClientServices.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/Types.hpp"

#include <common/DataStore.hpp>

namespace {

// ref: the table at 00beb528, cleared by RaidInfo's init (FUN_005756c0) with a 0x40-byte memset --
// which is what fixes the length at eight GUIDs rather than leaving it to be guessed.
WOWGUID s_raidTargets[RAID_TARGET_COUNT] = { 0 };

} // namespace

// ref: FUN_005728c0
int32_t RaidTargetGetIndex(WOWGUID guid) {
    for (int32_t i = 0; i < RAID_TARGET_COUNT; i++) {
        if (s_raidTargets[i] == guid) {
            return i;
        }
    }

    return RAID_TARGET_COUNT;
}

// ref: FUN_00572ed0
void RaidTargetSet(int32_t index, WOWGUID guid) {
    if (index < 0 || index >= RAID_TARGET_COUNT) {
        return;
    }

    // DIVERGENCE: the reference looks up the object that held this icon and the one taking it, and
    // calls a per-unit notify on each (FUN_007198d0) so their nameplates redraw immediately.
    // Frozen has no counterpart for that notify yet, so both redraws wait for the event below.
    // Recorded rather than silently dropped: if an icon lingers on the old unit for a frame, this
    // is why.
    s_raidTargets[index] = guid;
}

void RaidTargetClearAll() {
    for (int32_t i = 0; i < RAID_TARGET_COUNT; i++) {
        s_raidTargets[i] = 0;
    }
}

// MSG_RAID_TARGET_UPDATE (0x321), server -> client. ref: FUN_005743b0.
//
//   u8    full
//   guid  setter        -- only when full == 0
//   then, until the message runs out:
//   u8    index
//   guid  target
//
// A full list REPLACES the table rather than merging into it, so the reference clears all eight
// slots before reading any pairs. The trailing pairs are read the same way either way, which is
// why the loop sits outside the branch.
int32_t ReceiveRaidTargetUpdate(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint8_t full = 0;
    msg->Get(full);

    if (full) {
        RaidTargetClearAll();
    } else {
        // Who set it. The reference uses this only for the chat line naming the setter and the
        // target, which frozen does not build yet.
        uint64_t setter = 0;
        msg->Get(setter);
    }

    // The message carries as many pairs as it carries -- one for a single change, up to eight for
    // a full list -- so it is read to exhaustion rather than by a count.
    while (!msg->IsRead()) {
        uint8_t index = 0;
        uint64_t target = 0;

        msg->Get(index);
        msg->Get(target);

        if (index < RAID_TARGET_COUNT) {
            RaidTargetSet(index, target);
        }
    }

    FrameScript_SignalEvent(SCRIPT_RAID_TARGET_UPDATE, nullptr);

    return 1;
}

void RaidTargetRegisterHandlers() {
    ClientServices::SetMessageHandler(MSG_RAID_TARGET_UPDATE, &ReceiveRaidTargetUpdate, nullptr);
}
