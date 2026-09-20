#include "object/client/AuraCache.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/game/CGMinimapFrame.hpp"
#include "ui/game/Types.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/ScriptUtil.hpp"
#include "client/ClientServices.hpp"
#include <common/Time.hpp>
#include <common/DataStore.hpp>
#include <map>

namespace {

// slot -> aura, per unit. A map rather than a fixed array because the slot is a wire-supplied byte:
// an array sized to today's slot count would silently drop anything past it, and sizing to 256 per
// unit wastes memory on every creature in view.
std::map<WOWGUID, std::map<uint8_t, ClientAura>> s_auras;

// Server->client packed guid: a mask byte, then one byte per set bit, low to high.
WOWGUID GetPackedGuid(CDataStore* msg) {
    uint8_t mask = 0;
    msg->Get(mask);

    WOWGUID guid = 0;

    for (int32_t i = 0; i < 8; i++) {
        if (mask & (1 << i)) {
            uint8_t b = 0;
            msg->Get(b);
            guid |= static_cast<WOWGUID>(b) << (i * 8);
        }
    }

    return guid;
}

bool Passes(const ClientAura& aura, uint8_t required, uint8_t forbidden) {
    return (aura.flags & required) == required && !(aura.flags & forbidden);
}

// One aura record, shared by both opcodes.
//
//   uint8  slot
//   uint32 spellID                       0 means the slot was cleared
//   uint8  flags
//   uint8  casterLevel
//   uint8  stacks                        stack count, or charges when the spell does not stack
//   packed caster guid                   only when AURA_FLAG_CASTER is clear
//   uint32 maxDuration, uint32 duration  only when AURA_FLAG_DURATION is set
//
// Read from AzerothCore's AuraApplication::BuildUpdatePacket rather than from memory.
bool ReadAura(CDataStore* msg, WOWGUID target, uint32_t now) {
    if (msg->Tell() + 5 > msg->Size()) {
        return false;
    }

    uint8_t slot = 0;
    uint32_t spellID = 0;

    msg->Get(slot);
    msg->Get(spellID);

    auto& slots = s_auras[target];

    if (!spellID) {
        slots.erase(slot);

        // An empty map would otherwise keep every unit that ever had an aura alive in the cache.
        if (slots.empty()) {
            s_auras.erase(target);
        }

        return true;
    }

    ClientAura aura;
    aura.slot = slot;
    aura.spellID = static_cast<int32_t>(spellID);
    aura.receivedMs = now;

    msg->Get(aura.flags);
    msg->Get(aura.casterLevel);
    msg->Get(aura.stacks);

    if (!(aura.flags & AURA_FLAG_CASTER)) {
        aura.caster = GetPackedGuid(msg);
    } else {
        // The flag means the target cast it on itself, which is why no guid is on the wire.
        aura.caster = target;
    }

    if (aura.flags & AURA_FLAG_DURATION) {
        uint32_t maxDuration = 0;
        uint32_t duration = 0;

        msg->Get(maxDuration);
        msg->Get(duration);

        aura.maxDuration = static_cast<int32_t>(maxDuration);
        aura.duration = static_cast<int32_t>(duration);
    }

    slots[slot] = aura;

    return true;
}

} // namespace

const ClientAura* AuraCacheGet(WOWGUID guid, int32_t index, uint8_t required, uint8_t forbidden) {
    auto unit = s_auras.find(guid);

    if (unit == s_auras.end() || index < 0) {
        return nullptr;
    }

    int32_t seen = 0;

    for (const auto& entry : unit->second) {
        if (!Passes(entry.second, required, forbidden)) {
            continue;
        }

        if (seen == index) {
            return &entry.second;
        }

        seen++;
    }

    return nullptr;
}

int32_t AuraCacheCount(WOWGUID guid, uint8_t required, uint8_t forbidden) {
    auto unit = s_auras.find(guid);

    if (unit == s_auras.end()) {
        return 0;
    }

    int32_t count = 0;

    for (const auto& entry : unit->second) {
        if (Passes(entry.second, required, forbidden)) {
            count++;
        }
    }

    return count;
}

// ref: the 0x136 branch of FUN_00802f80
void AuraCacheCancel(uint32_t spellID) {
    if (!spellID) {
        return;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_CANCEL_AURA));
    msg.Put(spellID);
    msg.Finalize();

    ClientServices::Send(&msg);
}

void AuraCacheClear() {
    s_auras.clear();
}

// SMSG_AURA_UPDATE: one unit, one slot.
// UNIT_AURA, once per packet rather than once per aura: a full update rewrites every slot, and
// FrameXML rebuilds the whole buff row from one event anyway.
//
// This is the event the descriptors cannot carry. CGUnitData::auraState looks like the source and
// is not -- it is a bitfield of aura *categories* for spell requirements, not the aura list -- so
// the mirror deliberately leaves UNIT_AURA alone and it belongs here, where the auras arrive.
static void SignalAuraChange(WOWGUID target) {
    // Which tracking spell is active is an aura on the player, so it is recomputed here rather
    // than tracked by the minimap on its own. Does nothing unless the answer changed.
    if (target == ClntObjMgrGetActivePlayer()) {
        CGMinimapFrame::RefreshTrackingSpell();
    }

    auto token = Script_GetTokenFromGUID(target);

    if (token) {
        FrameScript_SignalEvent(SCRIPT_UNIT_AURA, "%s", token);
    }
}

int32_t ReceiveAuraUpdate(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    if (!msg) {
        return 1;
    }

    WOWGUID target = GetPackedGuid(msg);

    ReadAura(msg, target, static_cast<uint32_t>(OsGetAsyncTimeMs()));

    SignalAuraChange(target);

    return 1;
}

// SMSG_AURA_UPDATE_ALL: one unit, every visible slot it has. The count is not on the wire, so the
// records run to the end of the packet.
int32_t ReceiveAuraUpdateAll(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    if (!msg) {
        return 1;
    }

    WOWGUID target = GetPackedGuid(msg);

    // A full update replaces what was there: a slot the server no longer lists is gone, and without
    // this an aura that expired while out of view would linger.
    s_auras.erase(target);

    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());

    while (msg->Tell() < msg->Size()) {
        if (!ReadAura(msg, target, now)) {
            break;
        }
    }

    SignalAuraChange(target);

    return 1;
}

void AuraCacheRegisterHandlers() {
    ClientServices::SetMessageHandler(SMSG_AURA_UPDATE, &ReceiveAuraUpdate, nullptr);
    ClientServices::SetMessageHandler(SMSG_AURA_UPDATE_ALL, &ReceiveAuraUpdateAll, nullptr);
}
