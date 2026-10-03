#include "object/client/AuraCache.hpp"
#include "client/ClientServices.hpp"
#include "object/Types.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/ObjMgr.hpp"
#include <common/DataStore.hpp>
#include <common/Time.hpp>

// The reference keeps a unit's auras on the unit (CGUnit_C::m_auras, filled by ReceiveAuraUpdate,
// FUN_007300a0) and its Lua aura functions read them there. Until those script functions are
// ported, the readers below are frozen's adapter from the slot array to the ClientAura shape the
// interface code was written against; they keep no state of their own.

namespace {

bool Passes(const CAuraState& aura, uint8_t required, uint8_t forbidden) {
    return aura.m_spellID != 0 && (aura.m_flags & required) == required && !(aura.m_flags & forbidden);
}

// The answers handed out, a few at a time: a caller may hold the last several while it asks for
// more, never more than this many.
ClientAura s_answers[8];
uint32_t s_nextAnswer;

CGUnit_C* GetUnit(WOWGUID guid) {
    return static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, __FILE__, __LINE__));
}

} // namespace

const ClientAura* AuraCacheGet(WOWGUID guid, int32_t index, uint8_t required, uint8_t forbidden) {
    auto unit = GetUnit(guid);

    if (!unit || index < 0) {
        return nullptr;
    }

    int32_t seen = 0;

    for (uint32_t slot = 0; slot < unit->m_auras.Count(); slot++) {
        const CAuraState& aura = unit->m_auras[slot];

        if (!Passes(aura, required, forbidden)) {
            continue;
        }

        if (seen++ != index) {
            continue;
        }

        uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());

        ClientAura& answer = s_answers[s_nextAnswer++ % 8];
        answer.spellID = aura.m_spellID;
        answer.slot = static_cast<uint8_t>(slot);
        answer.flags = aura.m_flags;
        answer.casterLevel = aura.m_level;
        answer.stacks = aura.m_stacks;
        answer.caster = aura.m_caster;
        answer.maxDuration = aura.m_maxDuration;
        answer.duration = aura.m_expireTime ? static_cast<int32_t>(aura.m_expireTime - now) : 0;
        answer.receivedMs = now;

        return &answer;
    }

    return nullptr;
}

int32_t AuraCacheCount(WOWGUID guid, uint8_t required, uint8_t forbidden) {
    auto unit = GetUnit(guid);

    if (!unit) {
        return 0;
    }

    int32_t count = 0;

    for (uint32_t slot = 0; slot < unit->m_auras.Count(); slot++) {
        if (Passes(unit->m_auras[slot], required, forbidden)) {
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
    // The auras go with their units.
}

// The aura handlers, as the unit module's start registers them (FUN_00742220, 0x00742941).
void AuraCacheRegisterHandlers() {
    ClientServices::SetMessageHandler(SMSG_AURA_UPDATE_ALL, &ReceiveAuraUpdate, nullptr);
    ClientServices::SetMessageHandler(SMSG_AURA_UPDATE, &ReceiveAuraUpdate, nullptr);
}
