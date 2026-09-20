#ifndef OBJECT_CLIENT_AURA_CACHE_HPP
#define OBJECT_CLIENT_AURA_CACHE_HPP

#include "net/Types.hpp"
#include "util/guid/Types.hpp"
#include <cstdint>

class CDataStore;

// The aura (buff/debuff) state the server publishes for each visible unit.
//
// Auras arrive on two opcodes and are never part of the object update blocks, so without these
// handlers the packets were read off the wire and dropped -- UnitAura had nothing to answer with and
// the buff frame stayed empty.
//
// Flags as the server sends them (AzerothCore SpellAuraDefines.h). Only CASTER, POSITIVE, DURATION
// and NEGATIVE change how the client reads the record.
enum AURA_FLAGS {
    AURA_FLAG_EFF_INDEX_0 = 0x01,
    AURA_FLAG_EFF_INDEX_1 = 0x02,
    AURA_FLAG_EFF_INDEX_2 = 0x04,
    AURA_FLAG_CASTER      = 0x08, // the target cast it on itself; no caster guid follows
    AURA_FLAG_POSITIVE    = 0x10,
    AURA_FLAG_DURATION    = 0x20, // a duration pair follows
    AURA_FLAG_AMOUNT_SENT = 0x40,
    AURA_FLAG_NEGATIVE    = 0x80,
};

struct ClientAura {
    int32_t spellID = 0;
    uint8_t slot = 0;
    uint8_t flags = 0;
    uint8_t casterLevel = 0;
    uint8_t stacks = 0;
    WOWGUID caster = 0;
    int32_t maxDuration = 0; // milliseconds, 0 when the aura does not expire
    int32_t duration = 0;    // milliseconds left at the moment the packet arrived
    uint32_t receivedMs = 0; // OsGetAsyncTimeMs() at receipt -- the same clock GetTime() exposes
};

// Auras on a unit, in slot order. index is 0-based over the ones that pass the filter.
//
// requiredFlags and forbiddenFlags implement UnitAura's HELPFUL / HARMFUL filtering: pass 0 for both
// to iterate everything.
const ClientAura* AuraCacheGet(WOWGUID guid, int32_t index, uint8_t requiredFlags, uint8_t forbiddenFlags);

int32_t AuraCacheCount(WOWGUID guid, uint8_t requiredFlags, uint8_t forbiddenFlags);

// ref: the 0x136 branch of FUN_00802f80
// Ask the server to remove one of the player's own auras. Only the spell id goes on the wire --
// the server knows whose aura it is.
//
// A pet's aura is a different opcode carrying the pet's guid, and a totem a third; neither is
// ported, so this is the player's own buffs only.
void AuraCacheCancel(uint32_t spellID);

void AuraCacheClear();

void AuraCacheRegisterHandlers();

int32_t ReceiveAuraUpdate(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

int32_t ReceiveAuraUpdateAll(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

#endif
