#include "object/client/SpellHistory.hpp"
#include "client/ClientServices.hpp"
#include "db/Db.hpp"
#include "object/Types.hpp"
#include "object/client/CGItem_C.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/SpellBook.hpp"
#include "object/client/Spell_C.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/CGActionBar.hpp"
#include "ui/game/CGPetInfo.hpp"
#include "ui/game/ContainerFrameScript.hpp"
#include "ui/game/RuneInfo.hpp"
#include "ui/game/Types.hpp"
#include <common/DataStore.hpp>
#include <common/Time.hpp>

// The player's and the pet's cooldowns.
SpellHistoryList g_spellHistory[2];                 // ref: DAT_00d3f5ac

static int32_t SpellHistoryNow() {
    return static_cast<int32_t>(OsGetAsyncTimeMs());
}

SPELLHISTORY* SpellHistoryList::NewEntry() {
    auto entry = this->m_free.Head();

    if (entry) {
        this->m_free.UnlinkNode(entry);
        this->m_active.LinkToTail(entry);
    } else {
        entry = this->m_active.NewNode(2, 0, 0x8);
    }

    return entry;
}

// ref: FUN_00805230
// Nothing is recorded for a spell with no cooldown, no category cooldown, no hold and no global
// cooldown.
void SpellHistoryList::Add(int32_t spellID, int32_t itemID, int32_t start, int32_t duration, int32_t category, int32_t categoryStart, int32_t categoryDuration, bool onHold, int32_t gcdCategory, int32_t gcdDuration) {
    if (duration == 0 && categoryDuration == 0 && !onHold && gcdDuration == 0) {
        return;
    }

    auto entry = this->NewEntry();

    entry->spellID = spellID;
    entry->itemID = itemID;
    entry->start = start;
    entry->duration = duration;
    entry->category = category;
    entry->categoryStart = categoryStart;
    entry->categoryDuration = categoryDuration;
    entry->onHold = onHold;
    entry->gcdCategory = gcdCategory;
    entry->gcdDuration = gcdDuration;
}

// ref: FUN_00802970
// SMSG_COOLDOWN_EVENT and SMSG_CLEAR_COOLDOWN: a held cooldown starts now, or the spell's entries
// go entirely.
void SpellHistoryList::Release(int32_t spellID, int32_t time, bool remove) {
    for (auto entry = this->m_active.Head(); entry; ) {
        auto next = this->m_active.Next(entry);

        if (entry->spellID == spellID) {
            if (remove) {
                this->m_active.UnlinkNode(entry);
                this->m_free.LinkToTail(entry);
            } else if (entry->onHold) {
                entry->gcdDuration = 0;
                entry->start = time;
                entry->categoryStart = time;
                entry->onHold = false;
            }
        }

        entry = next;
    }
}

// ref: FUN_00802a10
// SMSG_MODIFY_COOLDOWN: move a spell's cooldowns by `delta` milliseconds.
void SpellHistoryList::Shift(int32_t spellID, int32_t delta) {
    for (auto entry = this->m_active.Head(); entry; entry = this->m_active.Next(entry)) {
        if (entry->spellID == spellID) {
            entry->start += delta;
            entry->categoryStart += delta;
        }
    }
}

// ref: FUN_00802a50
void SpellHistoryList::Clear() {
    while (auto entry = this->m_active.Head()) {
        this->m_active.UnlinkNode(entry);
        this->m_free.LinkToTail(entry);
    }
}

// ref: FUN_00802b00
// Retire the entries whose cooldowns have all run out by `time`; held ones stay.
void SpellHistoryList::Expire(int32_t time) {
    for (auto entry = this->m_active.Head(); entry; ) {
        auto next = this->m_active.Next(entry);

        if (!entry->onHold
            && (entry->duration == 0 || time - entry->start - entry->duration >= 0)
            && (entry->categoryDuration == 0 || time - entry->categoryStart - entry->categoryDuration >= 0)) {
            this->m_active.UnlinkNode(entry);
            this->m_free.LinkToTail(entry);
        }

        entry = next;
    }
}

// ref: FUN_00804720
// A spell's entries stop holding the global cooldown.
void SpellHistoryList::ClearGlobalCooldown(int32_t spellID) {
    bool found = false;

    for (auto entry = this->m_active.Head(); entry; entry = this->m_active.Next(entry)) {
        if (entry->spellID == spellID) {
            entry->gcdCategory = 0;
            entry->gcdDuration = 0;
            found = true;
        }
    }

    if (found) {
        this->Expire(SpellHistoryNow());
        SpellSignalCooldownUpdate();
    }
}

// ref: FUN_00807980
// The cooldown that ends last among the spell's own, its category's, the global cooldown it shares
// and -- for a rune spell -- its runes. `enabled` is cleared when the latest is held. False when
// nothing is cooling down. Trade skill spells (effects 47 and 78) never are.
bool SpellHistoryList::GetCooldown(int32_t spellID, int32_t itemID, int32_t* duration, int32_t* start, uint32_t* enabled) {
    if (enabled) {
        *enabled = 1;
    }

    auto spell = g_spellDB.GetRecord(spellID);

    if (!spell || spell->m_effect[0] == 78 || spell->m_effect[0] == 47) {
        return false;
    }

    auto category = spell->m_category;
    auto gcdCategory = spell->m_startRecoveryCategory;

    if (itemID) {
        auto info = g_itemCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(itemID)));

        if (info) {
            for (int32_t i = 0; i < ItemStats_C::MAX_SPELLS; i++) {
                if (info->spellID[i] == spellID && info->spellCategory[i] > 0) {
                    category = info->spellCategory[i];
                }
            }
        }
    }

    auto now = SpellHistoryNow();
    auto latest = now;

    for (auto entry = this->m_active.Head(); entry; entry = this->m_active.Next(entry)) {
        if (entry->spellID == spellID && entry->itemID == itemID && (entry->duration != 0 || entry->onHold)) {
            auto end = (entry->onHold ? now + 1 : entry->start) + entry->duration;

            if (end - latest >= 0) {
                if (duration) {
                    *duration = entry->onHold ? 1 : entry->duration;
                }

                if (start) {
                    *start = entry->onHold ? now : entry->start;
                }

                latest = end;

                if (enabled && *enabled) {
                    *enabled = !entry->onHold;
                }
            }
        }

        auto categoryRec = g_spellCategoryDB.GetRecord(entry->category);

        if (((entry->category != 0 && entry->category == category) || (categoryRec && (categoryRec->m_flags & 2))) && entry->categoryDuration != 0) {
            auto end = (entry->onHold ? now : entry->categoryStart) + entry->categoryDuration;

            if (end - latest >= 0) {
                if (duration) {
                    *duration = entry->categoryDuration;
                }

                if (start) {
                    *start = entry->onHold ? now : entry->categoryStart;
                }

                latest = end;

                if (enabled && *enabled) {
                    *enabled = !entry->onHold;
                }
            }
        }

        if (entry->gcdCategory == gcdCategory && entry->gcdDuration != 0 && entry->start + entry->gcdDuration - latest >= 0) {
            if (duration) {
                *duration = entry->gcdDuration;
            }

            latest = entry->start + entry->gcdDuration;

            if (start) {
                *start = entry->start;
            }
        }

        if (spell->m_runeCostID == 0) {
            continue;
        }

        // A rune spell also waits on the runes it shares with the spell this entry is for.
        bool shares = spellID == entry->spellID;

        if (!shares) {
            auto other = g_spellDB.GetRecord(entry->spellID);

            if (other && other->m_runeCostID) {
                auto mine = g_spellRuneCostDB.GetRecord(spell->m_runeCostID);
                auto theirs = g_spellRuneCostDB.GetRecord(other->m_runeCostID);

                if (mine && theirs && ((mine->m_blood && theirs->m_blood) || (mine->m_unholy && theirs->m_unholy) || (mine->m_frost && theirs->m_frost))) {
                    shares = true;
                }
            }

            if (!shares) {
                continue;
            }
        }

        uint32_t ready = 0;

        if (!SpellGetRuneCooldown(spell, &ready) || static_cast<int32_t>(ready) - latest < 0) {
            continue;
        }

        if (start) {
            *start = entry->start;
        }

        latest = static_cast<int32_t>(ready);

        if (!duration || !start) {
            continue;
        }

        if (!SpellHasRunes(spellID, nullptr)) {
            *start = static_cast<int32_t>(ready) - 10000;
            *duration = 10000;

            continue;
        }

        *duration = entry->gcdDuration;
    }

    return latest != now;
}

// ref: FUN_00807da0
bool SpellHistoryList::IsOnHold(int32_t spellID, int32_t itemID) {
    auto spell = g_spellDB.GetRecord(spellID);

    if (!spell) {
        return false;
    }

    auto category = spell->m_category;

    if (itemID) {
        auto info = g_itemCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(itemID)));

        if (info) {
            for (int32_t i = 0; i < ItemStats_C::MAX_SPELLS; i++) {
                if (info->spellID[i] == spellID && info->spellCategory[i] > 0) {
                    category = info->spellCategory[i];
                }
            }
        }
    }

    for (auto entry = this->m_active.Head(); entry; entry = this->m_active.Next(entry)) {
        if (entry->spellID == spellID && entry->itemID == itemID && (entry->duration != 0 || entry->onHold) && entry->onHold) {
            return true;
        }

        if (entry->category == category && entry->categoryDuration != 0 && entry->onHold) {
            return true;
        }
    }

    return false;
}

// ref: FUN_00807d40
// Cast within the last 1.5 seconds.
bool SpellHistoryList::IsRecent(int32_t spellID, int32_t itemID) {
    auto now = SpellHistoryNow();

    for (auto entry = this->m_active.Head(); entry; entry = this->m_active.Next(entry)) {
        if (entry->spellID == spellID && entry->itemID == itemID && now - entry->start - 1500 < 0) {
            return true;
        }
    }

    return false;
}

// ref: FUN_00805b10
// A cooldown the server reports with time left (SMSG_INITIAL_SPELLS): the full length is the
// item's or spell's own, modified, and never less than what is left; it started that much ago.
void SpellHistoryList::AddServerCooldown(int32_t spellID, int32_t itemID, int32_t category, int32_t cooldown, int32_t categoryCooldown, bool onHold, int32_t pet) {
    auto now = SpellHistoryNow();
    int32_t start = 0;
    int32_t duration = 0;
    int32_t categoryStart = 0;
    int32_t categoryDuration = 0;

    auto spell = g_spellDB.GetRecord(spellID);

    if (!spell) {
        return;
    }

    const ItemStats_C* info = nullptr;
    int32_t slot = 0;

    if (itemID) {
        info = g_itemCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(itemID)));

        if (info) {
            for (slot = 0; slot < ItemStats_C::MAX_SPELLS; slot++) {
                if (info->spellID[slot] == spellID) {
                    break;
                }
            }

            if (slot >= ItemStats_C::MAX_SPELLS) {
                info = nullptr;
            }
        }
    }

    if (cooldown > 0) {
        auto full = info && info->spellCooldown[slot] >= 0 ? info->spellCooldown[slot] : spell->m_recoveryTime;

        if (full <= cooldown) {
            full = cooldown;
        }

        int32_t flat;
        int32_t pct;

        if (SpellGetModifier(spell, 0xb, &flat, &pct)) {
            full = (flat + full) * pct / 100;
        }

        start = now - (full - cooldown);
        duration = full;
    }

    if (categoryCooldown > 0) {
        int32_t full;

        if (info && info->spellCategoryCooldown[slot] >= 0) {
            full = info->spellCategoryCooldown[slot];
        } else {
            full = spell->m_categoryRecoveryTime;
            SpellApplyCooldownModifiers(&full, spell, pet);
        }

        if (full <= categoryCooldown) {
            full = categoryCooldown;
        }

        categoryStart = now - (full - categoryCooldown);
        categoryDuration = full;
    }

    if (duration != 0 || categoryDuration != 0 || onHold) {
        auto entry = this->NewEntry();

        entry->spellID = spellID;
        entry->itemID = itemID;
        entry->start = start;
        entry->duration = duration;
        entry->category = category;
        entry->categoryStart = categoryStart;
        entry->categoryDuration = categoryDuration;
        entry->onHold = onHold;
        entry->gcdCategory = 0;
        entry->gcdDuration = 0;
    }
}

// ref: FUN_00809000
bool SpellGetCooldown(int32_t spellID, int32_t pet, int32_t* duration, int32_t* start, uint32_t* enabled) {
    return g_spellHistory[pet].GetCooldown(spellID, 0, duration, start, enabled);
}

// ref: FUN_008090c0
bool ItemGetCooldown(CGItem_C* item, int32_t* duration, int32_t* start, uint32_t* enabled) {
    if (!item) {
        return false;
    }

    return g_spellHistory[0].GetCooldown(item->GetUseSpell(0), item->GetEntryID(), duration, start, enabled);
}

static void ItemIDOnCooldownItemArrived(uint32_t id, const WOWGUID* guid, void* param, bool found) {
    // ref: FUN_0080abe0
    if (found && ItemIDGetCooldown(static_cast<int32_t>(id), nullptr, nullptr, nullptr)) {
        SpellSignalCooldownUpdate();
        ContainerSignalBagUpdateCooldown();
    }
}

// ref: FUN_00809030
bool ItemIDGetCooldown(int32_t itemID, int32_t* duration, int32_t* start, uint32_t* enabled) {
    auto player = ClntObjMgrGetActivePlayer();
    auto info = g_itemCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(itemID)), &player, &ItemIDOnCooldownItemArrived, nullptr, true);

    if (info) {
        for (int32_t i = 0; i < ItemStats_C::MAX_SPELLS; i++) {
            if (info->spellID[i] > 0 && info->spellTrigger[i] == 0) {
                return g_spellHistory[0].GetCooldown(info->spellID[i], itemID, duration, start, enabled);
            }
        }
    }

    return false;
}

// ref: FUN_00801220
// An item whose use spell's category is flagged (SpellCategory flag 4) holds its cooldown, as does
// a spell flagged SPELL_ATTR0 0x2000000.
bool SpellIsCooldownHeld(const SpellRec* spell, WOWGUID caster) {
    auto object = ClntObjMgrObjectPtr(caster, TYPE_OBJECT, ".\\Spell_C.cpp", 0x6ec);

    if (object && object->IsA(TYPE_ITEM)) {
        auto item = static_cast<CGItem_C*>(object);
        auto owner = ClntObjMgrObjectPtr(item->Item()->owner, TYPE_UNIT, ".\\Spell_C.cpp", 0x6ef);
        auto info = g_itemCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(item->GetEntryID())));
        auto slot = item->GetUseSpellIndex();

        if (info && owner && slot >= 0) {
            auto category = g_spellCategoryDB.GetRecord(info->spellCategory[slot]);

            if (category && (category->m_flags & 4)) {
                return true;
            }
        }
    }

    return (spell->m_attributes >> 25) & 1;
}

// ref: FUN_0053bac0
// PARTIAL: the reference also signals UPDATE_SHAPESHIFT_COOLDOWN while the stance bar has forms
// (0x00be8e28); frozen keeps no stance bar list yet (GetNumShapeshiftForms answers 0).
void SpellSignalCooldownUpdate() {
    CGActionBar::SignalUpdateCooldown();
    FrameScript_SignalEvent(SCRIPT_SPELL_UPDATE_COOLDOWN, nullptr);
}

// ref: FUN_0053cf10
// PARTIAL: the reference first re-checks every action button's usability (FUN_005aa470), then
// every tracked spell's and stance's (0x00be8e58, 0x00be8e28), signalling their events on a
// change; frozen keeps none of those lists yet, so only SPELL_UPDATE_USABLE is sent.
void SpellSignalUsableUpdate() {
    FrameScript_SignalEvent(SCRIPT_SPELL_UPDATE_USABLE, nullptr);
}

static void SpellHistoryUpdateOwner(bool pet) {
    if (pet) {
        CGPetInfo::UpdateCooldowns();
    } else {
        ContainerSignalBagUpdateCooldown();
    }
}

// ref: FUN_00806dd0
// SMSG_SPELL_COOLDOWN: the caster, a flags byte (1 the global cooldown applies, 2 nothing is held),
// then pairs of spell and cooldown until the message ends. A zero cooldown means the spell's own.
int32_t SpellCooldownHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WOWGUID caster = 0;
    uint8_t flags = 0;
    msg->Get(caster);
    msg->Get(flags);

    bool global = flags & 1;
    bool noHold = flags & 2;
    int32_t pet;

    if (ClntObjMgrGetActivePlayer() == caster) {
        pet = 0;
    } else {
        if (CGPetInfo::GetPet(0) != caster) {
            return 1;
        }

        pet = 1;
    }

    while (!msg->IsRead()) {
        uint32_t spellID = 0;
        int32_t cooldown = 0;
        msg->Get(spellID);
        msg->Get(reinterpret_cast<uint32_t&>(cooldown));

        auto spell = g_spellDB.GetRecord(spellID);

        if (spell) {
            bool held = noHold ? false : SpellIsCooldownHeld(spell, caster);
            auto duration = cooldown;
            int32_t flat;
            int32_t pct;

            if (cooldown == 0) {
                duration = spell->m_recoveryTime;

                if (SpellGetModifier(spell, 0xb, &flat, &pct)) {
                    duration = (flat + duration) * pct / 100;
                }
            }

            int32_t categoryDuration = 0;

            if (cooldown == 0) {
                categoryDuration = spell->m_categoryRecoveryTime;
                SpellApplyCooldownModifiers(&categoryDuration, spell, pet);
            }

            int32_t gcdCategory = 0;
            int32_t gcdDuration = 0;

            if (global && !held) {
                gcdCategory = spell->m_startRecoveryCategory;
                gcdDuration = spell->m_startRecoveryTime;

                if (SpellGetModifier(spell, 0x15, &flat, &pct)) {
                    gcdDuration = (flat + gcdDuration) * pct / 100;
                }
            }

            g_spellHistory[pet].Add(spellID, 0, static_cast<int32_t>(time), duration, spell->m_category, static_cast<int32_t>(time), categoryDuration, held, gcdCategory, gcdDuration);
        }
    }

    SpellSignalCooldownUpdate();
    SpellHistoryUpdateOwner(pet != 0);

    return 1;
}

// ref: FUN_00807060
// SMSG_ITEM_COOLDOWN: the item and the spell it cast; thirty seconds from now.
int32_t ItemCooldownHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WOWGUID guid = 0;
    uint32_t spellID = 0;
    msg->Get(guid);
    msg->Get(spellID);

    auto spell = g_spellDB.GetRecord(spellID);
    auto item = spell ? static_cast<CGItem_C*>(ClntObjMgrObjectPtr(guid, TYPE_ITEM, ".\\Spell_C.cpp", 0x1b06)) : nullptr;

    if (item) {
        auto entry = g_spellHistory[0].NewEntry();

        entry->spellID = static_cast<int32_t>(spellID);
        entry->itemID = item->GetEntryID();
        entry->start = static_cast<int32_t>(time);
        entry->duration = 30000;
        entry->category = 0;
        entry->categoryStart = 0;
        entry->categoryDuration = 0;
        entry->onHold = false;
        entry->gcdCategory = 0;
        entry->gcdDuration = 0;
    }

    SpellSignalCooldownUpdate();
    ContainerSignalBagUpdateCooldown();

    return 1;
}

// ref: FUN_00804010
// SMSG_COOLDOWN_EVENT (a held cooldown starts), SMSG_CLEAR_COOLDOWN (it goes) and
// SMSG_MODIFY_COOLDOWN (it moves): the spell and the caster, the player or their pet.
int32_t CooldownEventHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint32_t spellID = 0;
    WOWGUID caster = 0;
    msg->Get(spellID);
    msg->Get(caster);

    bool pet = CGPetInfo::GetPet(0) == caster;

    // The reference's arm for SMSG_COOLDOWN_CHEAT here is unreachable: that message is registered
    // to FUN_00804110.

    if (msgId == SMSG_COOLDOWN_EVENT || msgId == SMSG_CLEAR_COOLDOWN) {
        // FUN_00802ba0
        g_spellHistory[pet].Release(static_cast<int32_t>(spellID), static_cast<int32_t>(time), msgId == SMSG_CLEAR_COOLDOWN);
        SpellSignalCooldownUpdate();
        SpellSignalUsableUpdate();
        SpellHistoryUpdateOwner(pet);

        return 1;
    }

    if (msgId == SMSG_MODIFY_COOLDOWN) {
        int32_t delta = 0;
        msg->Get(reinterpret_cast<uint32_t&>(delta));

        if (spellID) {
            // FUN_00802bf0
            g_spellHistory[pet].Shift(static_cast<int32_t>(spellID), delta);
            SpellSignalCooldownUpdate();
            SpellSignalUsableUpdate();
            SpellHistoryUpdateOwner(pet);
        }
    }

    return 1;
}

// ref: FUN_00804110
// SMSG_COOLDOWN_CHEAT: every cooldown of the player, or of their pet, ends.
int32_t CooldownCheatHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WOWGUID guid = 0;
    msg->Get(guid);

    if (ClntObjMgrGetActivePlayer() != guid) {
        if (CGPetInfo::GetPet(0) == guid) {
            g_spellHistory[1].Clear();
            SpellSignalCooldownUpdate();
            CGPetInfo::UpdateCooldowns();
        }

        return 1;
    }

    g_spellHistory[0].Clear();
    SpellSignalCooldownUpdate();
    ContainerSignalBagUpdateCooldown();

    return 1;
}

// ref: FUN_008071c0
// SMSG_RESET_RANGED_COMBAT_TIMER: every known ranged spell (SPELL_ATTR0 0x2, not an auto-repeat
// exception) cools down for the given time.
int32_t ResetRangedCombatTimerHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    int32_t duration = 0;
    msg->Get(reinterpret_cast<uint32_t&>(duration));

    for (int32_t slot = 0; slot < SpellBookCount(); slot++) {
        auto spell = g_spellDB.GetRecord(static_cast<int32_t>(SpellBookSpellAt(slot)));

        if (!spell) {
            continue;
        }

        if ((spell->m_attributes & 2) && !(spell->m_attributesEx2 & 0x20000) && !(spell->m_attributesEx4 & 0x10000000)) {
            if (duration != 0) {
                auto entry = g_spellHistory[0].NewEntry();

                entry->spellID = spell->m_ID;
                entry->itemID = 0;
                entry->start = SpellHistoryNow();
                entry->duration = duration;
                entry->category = 0;
                entry->categoryStart = 0;
                entry->categoryDuration = 0;
                entry->onHold = false;
                entry->gcdCategory = 0;
                entry->gcdDuration = 0;
            }

            SpellSignalCooldownUpdate();
        }
    }

    return 1;
}

// Part of FUN_00810050's registrations: the cooldown messages. The cast messages it also
// registers are the spell cast port's.
void SpellHistoryRegisterHandlers() {
    ClientServices::SetMessageHandler(SMSG_SPELL_COOLDOWN, &SpellCooldownHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_ITEM_COOLDOWN, &ItemCooldownHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_COOLDOWN_EVENT, &CooldownEventHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_CLEAR_COOLDOWN, &CooldownEventHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_MODIFY_COOLDOWN, &CooldownEventHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_COOLDOWN_CHEAT, &CooldownCheatHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_RESET_RANGED_COMBAT_TIMER, &ResetRangedCombatTimerHandler, nullptr);
}
