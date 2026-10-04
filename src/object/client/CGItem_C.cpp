#include "object/client/CGItem_C.hpp"
#include "client/ClientServices.hpp"
#include "db/Db.hpp"
#include "object/Types.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/Spell_C.hpp"
#include "object/client/SpellBook.hpp"
#include "ui/game/QuestTextParser.hpp"
#include "world/CWorld.hpp"
#include <common/DataStore.hpp>
#include <storm/Memory.hpp>
#include <cmath>
#include <ctime>

namespace {

// ref: DAT_0070757c (through the jump table at 0x00707564)
// The ItemRandomSuffix / RandPropPoints column an inventory type draws its budget from; -1 for the
// types that take no random suffix.
const int8_t s_suffixColumns[29] = {
    -1, 0, 2, 1, 0, 0, 1, 0, 1, 2, 1, 2, 1, 3, 2, 4,
    2, 0, -1, -1, 0, 3, 3, 2, -1, 4, 4, -1, -1,
};

// The reference's halving round, (int)round(2v -/+ 0.5) >> 1 under the FPU's round-to-nearest.
int32_t RoundHalfDown(float value) {
    auto twice = value < 0.0f ? value + value + 0.5f : value + value - 0.5f;

    return static_cast<int32_t>(std::nearbyint(twice)) >> 1;
}

} // namespace

CGItem_C::CGItem_C(uint32_t time, CClientObjCreate& objCreate) : CGObject_C(time, objCreate) {
    // TODO
}

CGItem_C::~CGItem_C() {
    // From FUN_0070b120: the refund information goes with the item.
    this->SetRefundInfo(nullptr);
}

void CGItem_C::PostInit(uint32_t time, const CClientObjCreate& init, bool a4) {
    this->CGObject_C::PostInit(time, init, a4);

    // TODO
}

// ref: FUN_00707220
int32_t CGItem_C::GetClassID() const {
    auto rec = g_itemDB.GetRecord(this->GetEntryID());
    return rec ? rec->m_classID : 0;
}

// ref: FUN_00707250
int32_t CGItem_C::GetSubclassID() const {
    auto rec = g_itemDB.GetRecord(this->GetEntryID());
    return rec ? rec->m_subclassID : 0;
}

// ref: FUN_00707300
int32_t CGItem_C::GetDisplayInfoID() const {
    auto rec = g_itemDB.GetRecord(this->GetEntryID());
    return rec ? rec->m_displayInfoID : 0;
}

// ref: FUN_00707280
int32_t CGItem_C::GetInventoryType() const {
    auto rec = g_itemDB.GetRecord(this->GetEntryID());
    return rec ? rec->m_inventoryType : 0;
}

void CGItem_C::SetStorage(uint32_t* storage, uint32_t* saved) {
    this->CGObject_C::SetStorage(storage, saved);

    this->m_item = reinterpret_cast<CGItemData*>(&storage[CGItem::GetBaseOffset()]);
    this->m_itemSaved = &saved[CGItem::GetBaseOffsetSaved()];
}

// ref: FUN_00706b90
// The first "on use" spell of an item's cache record, never asking for the record.
int32_t CGItem_C::GetUseSpellID(int32_t itemID) {
    auto info = g_itemCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(itemID)));

    if (info) {
        for (int32_t i = 0; i < ItemStats_C::MAX_SPELLS; i++) {
            if (info->spellID[i] != 0 && info->spellTrigger[i] == 0) {
                return info->spellID[i];
            }
        }
    }

    return 0;
}

// ref: FUN_00706bf0
int32_t CGItem_C::GetUseSpellCharges(int32_t itemID) {
    auto info = g_itemCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(itemID)));

    if (info) {
        for (int32_t i = 0; i < ItemStats_C::MAX_SPELLS; i++) {
            if (info->spellID[i] != 0 && info->spellTrigger[i] == 0) {
                return info->spellCharges[i];
            }
        }
    }

    return 0;
}

// ref: FUN_00706c50
void CGItem_C::GetUseSpellCooldowns(int32_t itemID, int32_t* cooldown, int32_t* categoryCooldown, int32_t* category) {
    *cooldown = 0;
    *categoryCooldown = 0;
    *category = 0;

    auto info = g_itemCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(itemID)));

    if (!info) {
        return;
    }

    for (int32_t i = 0; i < ItemStats_C::MAX_SPELLS; i++) {
        if (info->spellID[i] != 0 && info->spellTrigger[i] == 0) {
            *cooldown = info->spellCooldown[i];
            *categoryCooldown = info->spellCategoryCooldown[i];
            *category = info->spellCategory[i];

            return;
        }
    }
}

// ref: FUN_00707650
// A random suffix's enchantment points in one of its five slots (7..11): the row's allocation, in
// hundredths of a percent, of the item's suffix factor.
int32_t CGItem_C::GetRandomSuffixPoints(int32_t slot, int32_t randomPropertyID, int32_t suffixFactor) {
    float points = 0.0f;

    if (randomPropertyID < 0) {
        auto suffix = g_itemRandomSuffixDB.GetRecord(-randomPropertyID);
        auto index = slot - 7;

        if (suffix && index >= 0 && index < 5) {
            points = static_cast<float>(suffix->m_allocationPct[index]) * 0.0001f * static_cast<float>(suffixFactor);
        }
    }

    return RoundHalfDown(points);
}

// ref: FUN_007086b0
// Whether `lock` keeps the player out. A skill entry is open when a known spell opens its lock type
// with enough skill (the required skill defaults to five times the item level); a spell entry when
// the spell opens locks at all; an item entry when the player carries the key. Each output names
// what was tried; `index` the entry that opened it. True when some entry applied and none opened.
bool CGItem_C::CheckLock(const LockRec* lock, int32_t itemLevel, int32_t* spellID, int32_t* skillValue, int32_t* required, int32_t* lockType, CGItem_C** key, uint32_t* index) {
    bool applies = false;

    for (uint32_t i = 0; i < 8; i++) {
        auto type = lock->m_type[i];

        if (type == 0) {
            continue;
        }

        if (type == 2) {
            applies = true;

            for (int32_t slot = 0; slot < SpellBookCount(); slot++) {
                auto knownID = static_cast<int32_t>(SpellBookSpellAt(slot));
                auto spell = g_spellDB.GetRecord(knownID);

                if (!spell) {
                    continue;
                }

                for (int32_t effect = 0; effect < 3; effect++) {
                    if (spell->m_effect[effect] != 33 || spell->m_effectMiscValue[effect] != lock->m_index[i]) {
                        continue;
                    }

                    auto needed = lock->m_skill[i];

                    if (needed == 0) {
                        needed = itemLevel * 5;
                    }

                    int32_t value = 0;
                    int32_t valueMax = 0;
                    SpellGetEffectSkillValue(spell, effect, &value, &valueMax, 0, 0, 0, 0);

                    if (spellID) {
                        *spellID = knownID;
                    }

                    if (skillValue) {
                        *skillValue = value;
                    }

                    if (required) {
                        *required = needed;
                    }

                    if (lockType) {
                        *lockType = lock->m_index[i];
                    }

                    if (needed <= value) {
                        if (index) {
                            *index = i;
                        }

                        return false;
                    }
                }
            }
        } else if (type == 3) {
            applies = true;

            auto spell = g_spellDB.GetRecord(lock->m_index[i]);

            if (spell) {
                for (int32_t effect = 0; effect < 3; effect++) {
                    if (spell->m_effect[effect] == 33) {
                        if (spellID) {
                            *spellID = spell->m_ID;
                        }

                        if (index) {
                            *index = i;
                        }

                        return false;
                    }
                }
            }
        } else if (type == 1) {
            auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, ".\\Item_C.cpp", 0x729));

            if (player) {
                applies = true;

                auto keyItem = player->m_bag.FindItemByID(lock->m_index[i], 0);

                if (keyItem) {
                    if (spellID) {
                        *spellID = keyItem->GetUseSpell(0);
                    }

                    if (key) {
                        *key = keyItem;
                    }

                    if (index) {
                        *index = i;
                    }

                    return false;
                }
            }
        }
    }

    return applies;
}

// ref: FUN_00707390
uint32_t CGItem_C::GetStatsFlags(int32_t index) const {
    auto info = g_itemCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(this->GetEntryID())));

    if (!info) {
        return 0;
    }

    return index == 0 ? info->flags : info->flags2;
}

bool CGItem_C::IsCharter() const {
    return (this->GetStatsFlags(0) >> 13) & 1;
}

// ref: FUN_00706e60
// The slot of the item's first "on use" spell, -1 when it has none (or its record is not here).
int32_t CGItem_C::GetUseSpellIndex() const {
    auto info = g_itemCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(this->GetEntryID())));

    if (info) {
        for (int32_t i = 0; i < ItemStats_C::MAX_SPELLS; i++) {
            if (info->spellID[i] != 0 && info->spellTrigger[i] == 0) {
                return i;
            }
        }
    }

    return -1;
}

// ref: FUN_00706ec0
// The first enchantment on the item that gives it an "on use" spell (effect 7 with a spell).
int32_t CGItem_C::GetUseEnchantment() const {
    auto data = this->Item();

    for (int32_t slot = 0; slot < 12; slot++) {
        auto enchant = data->enchantments[slot].id ? g_spellItemEnchantmentDB.GetRecord(data->enchantments[slot].id) : nullptr;

        if (!enchant) {
            continue;
        }

        for (int32_t effect = 0; effect < 3; effect++) {
            if (enchant->m_effectArg[effect] != 0 && enchant->m_effect[effect] == 7) {
                return data->enchantments[slot].id;
            }
        }
    }

    return 0;
}

// ref: FUN_00706f40
int32_t CGItem_C::GetMaxCharges() const {
    auto enchantID = this->GetUseEnchantment();
    auto enchant = enchantID ? g_spellItemEnchantmentDB.GetRecord(enchantID) : nullptr;

    if (enchant) {
        for (int32_t effect = 0; effect < 3; effect++) {
            if (enchant->m_effectArg[effect] != 0 && enchant->m_effect[effect] == 7) {
                return enchant->m_charges;
            }
        }
    }

    return CGItem_C::GetUseSpellCharges(this->GetEntryID());
}

// ref: FUN_00707dc0
// The charges left on the item's use. For a use enchantment the reference reads the charges of the
// enchantment slot numbered like the matching EFFECT, not the slot the enchantment sits in.
int32_t CGItem_C::GetCharges() const {
    auto enchant = this->GetUseEnchantment() ? g_spellItemEnchantmentDB.GetRecord(this->GetUseEnchantment()) : nullptr;

    if (enchant) {
        for (int32_t effect = 0; effect < 3; effect++) {
            if (enchant->m_effect[effect] == 7 && enchant->m_effectArg[effect] != 0) {
                if (this->IsCharter()) {
                    return 0;
                }

                return static_cast<int16_t>(this->Item()->enchantments[effect].chargesRemaining);
            }
        }
    }

    auto index = this->GetUseSpellIndex();

    if (index < 0) {
        return 0;
    }

    return this->Item()->spellCharges[index];
}

// ref: FUN_00707db0
bool CGItem_C::HasUseEnchantment() const {
    return this->GetUseEnchantment() != 0;
}

// ref: FUN_00707c60
// The spell the item casts when used: its own "on use" spell (one with an effect of the given type
// when `effect` is not 0), else a use enchantment's spell.
int32_t CGItem_C::GetUseSpell(int32_t effect) const {
    auto info = g_itemCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(this->GetEntryID())));

    if (!info) {
        return 0;
    }

    int32_t spellID = 0;

    for (int32_t i = 0; i < ItemStats_C::MAX_SPELLS; i++) {
        if (info->spellID[i] == 0 || info->spellTrigger[i] != 0) {
            continue;
        }

        bool matches = effect == 0;

        if (!matches) {
            auto spell = g_spellDB.GetRecord(info->spellID[i]);

            for (int32_t e = 0; spell && e < 3; e++) {
                if (spell->m_effect[e] == effect) {
                    matches = true;
                    break;
                }
            }
        }

        if (matches) {
            spellID = info->spellID[i];

            if (spellID) {
                return spellID;
            }

            break;
        }
    }

    auto data = this->Item();

    for (int32_t slot = 0; slot < 12; slot++) {
        auto enchant = data->enchantments[slot].id ? g_spellItemEnchantmentDB.GetRecord(data->enchantments[slot].id) : nullptr;

        if (enchant) {
            for (int32_t e = 0; e < 3; e++) {
                if (enchant->m_effect[e] == 7) {
                    spellID = enchant->m_effectArg[e];
                    break;
                }
            }
        }

        if (spellID) {
            break;
        }
    }

    return spellID;
}

// ref: FUN_00707070
void CGItem_C::SetExpiration(int32_t seconds) {
    this->m_expiration = seconds > 0 ? static_cast<int32_t>(std::time(nullptr)) + seconds : 0;
}

// ref: FUN_007070b0
int32_t CGItem_C::GetExpirationTimeLeft() const {
    if (!this->m_expiration) {
        return 0;
    }

    auto left = this->m_expiration - static_cast<int32_t>(std::time(nullptr));

    return left < 1 ? 0 : left;
}

// ref: FUN_007070e0
void CGItem_C::SetEnchantExpiration(int32_t slot, int32_t seconds) {
    this->m_enchantExpiration[slot] = seconds > 0 ? seconds * 1000 + static_cast<int32_t>(CWorld::GetCurTimeMs()) : 0;
}

// ref: FUN_00707120
// Milliseconds left on an enchantment, 0 when it does not expire or has.
int32_t CGItem_C::GetEnchantTimeLeft(int32_t slot) const {
    auto expiration = this->m_enchantExpiration[slot];
    auto now = static_cast<int32_t>(CWorld::GetCurTimeMs());

    if (expiration != 0 && now - expiration < 0) {
        return expiration - now;
    }

    return 0;
}

// ref: FUN_007073e0
bool CGItem_C::HasSoulboundEnchantment() const {
    if (this->IsCharter()) {
        return false;
    }

    auto data = this->Item();

    for (int32_t slot = 0; slot < 12; slot++) {
        auto enchant = data->enchantments[slot].id ? g_spellItemEnchantmentDB.GetRecord(data->enchantments[slot].id) : nullptr;

        if (enchant && (enchant->m_flags & 1)) {
            return true;
        }
    }

    return false;
}

// ref: FUN_00708520
bool CGItem_C::IsSoulbound() const {
    return (this->Item()->flags & 1) || this->HasSoulboundEnchantment();
}

// ref: FUN_00708ac0
// The permanent enchantment, the first two sockets and the prismatic socket -- not the third.
bool CGItem_C::HasEnchantments() const {
    auto data = this->Item();

    if (!this->IsCharter() && data->enchantments[0].id != 0) {
        return true;
    }

    for (int32_t slot = 2; slot < 4; slot++) {
        if (!this->IsCharter() && data->enchantments[slot].id != 0) {
            return true;
        }
    }

    return !this->IsCharter() && data->enchantments[6].id != 0;
}

// ref: FUN_00708b40
// A soulbound item's window to be traded to whoever was present when it dropped: open for two hours
// of the owner's played time after it was looted (descriptor flag 0x100).
bool CGItem_C::IsTradeWindowExpired() const {
    if (!this->IsSoulbound()) {
        return false;
    }

    auto data = this->Item();

    if ((data->flags >> 8) & 1) {
        auto player = CGPlayer_C::GetActivePtr();

        if (player && data->owner == player->GetGUID()) {
            return data->createPlayedTime - player->GetPlayedTime() + 7200 <= 0;
        }
    }

    return true;
}

// ref: FUN_00707500
int32_t CGItem_C::GetSuffixAllocationColumn() const {
    auto rec = g_itemDB.GetRecord(this->GetEntryID());
    uint32_t type = rec ? static_cast<uint32_t>(rec->m_inventoryType) : 0;

    if (type > 28) {
        return -1;
    }

    return s_suffixColumns[type];
}

// ref: FUN_007075a0
// The RandPropPoints budget of the item's level and quality for its slot group.
int32_t CGItem_C::GetSuffixAllocation() const {
    auto info = g_itemCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(this->GetEntryID())));

    if (!info) {
        return 0;
    }

    auto column = static_cast<uint32_t>(this->GetSuffixAllocationColumn());
    auto points = g_randPropPointsDB.GetRecord(info->itemLevel);

    if (column < 5 && points) {
        if (info->quality == 2) {
            return points->m_good[column];
        }

        if (info->quality == 3) {
            return points->m_superior[column];
        }

        if (info->quality == 4) {
            return points->m_epic[column];
        }
    }

    return 0;
}

// ref: FUN_007089b0
int32_t CGItem_C::GetEnchantPoints(int32_t slot) const {
    return CGItem_C::GetRandomSuffixPoints(slot, this->Item()->randomPropertiesID, this->GetSuffixAllocation());
}

// ref: FUN_00708140
// The sockets the prismatic enchantment adds (an effect 8 enchantment in slot 6).
int32_t CGItem_C::GetPrismaticSocketCount() const {
    if (this->IsCharter()) {
        return 0;
    }

    auto enchant = this->Item()->enchantments[6].id ? g_spellItemEnchantmentDB.GetRecord(this->Item()->enchantments[6].id) : nullptr;

    if (enchant) {
        for (int32_t effect = 0; effect < 3; effect++) {
            if (enchant->m_effect[effect] == 8) {
                return enchant->m_effectPointsMin[effect];
            }
        }
    }

    return 0;
}

// ref: FUN_007094e0
int32_t CGItem_C::GetSocketCount() const {
    int32_t count = 0;
    auto info = g_itemCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(this->GetEntryID())));

    if (info) {
        for (int32_t i = 0; i < ItemStats_C::MAX_SOCKETS; i++) {
            if (info->socketColor[i] != 0) {
                count++;
            }
        }
    }

    return this->GetPrismaticSocketCount() + count;
}

// ref: FUN_00708540
// The repair cost of a damaged weapon or armor piece: the durability lost, times the quality's
// multiplier, times the item level's cost for the subclass; at least 1 copper.
int32_t CGItem_C::GetRepairCost() const {
    auto info = g_itemCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(this->GetEntryID())));

    if (!info) {
        return 0;
    }

    auto data = this->Item();

    if (((data->flags >> 3) & 1) || data->maxDurability == 0 || data->durability >= data->maxDurability) {
        return 0;
    }

    auto costs = g_durabilityCostsDB.GetRecord(info->itemLevel);
    auto itemClass = this->GetClassID();

    if (!costs || (itemClass != 2 && itemClass != 4)) {
        return 0;
    }

    auto subclass = static_cast<uint32_t>(this->GetSubclassID());
    int32_t cost;

    if (itemClass == 2) {
        if (subclass > 0x15) {
            return 0;
        }

        cost = costs->m_weaponSubClassCost[subclass];
    } else {
        if (subclass > 8) {
            return 0;
        }

        cost = costs->m_armorSubClassCost[subclass];
    }

    auto quality = g_durabilityQualityDB.GetRecordByIndex(info->quality * 2 + 1);

    if (!quality) {
        return 0;
    }

    auto lost = this->GetMaxDurability() - this->GetDurability();
    auto repair = RoundToInt(static_cast<float>(lost) * quality->m_data * static_cast<float>(cost));

    return repair == 0 ? 1 : repair;
}

// ref: FUN_00706cf0
// Takes ownership of `info`; a new one ends the outstanding request.
void CGItem_C::SetRefundInfo(ITEMREFUNDINFO* info) {
    if (this->m_refundInfo) {
        SMemFree(this->m_refundInfo, "delete", -1, 0);
    }

    this->m_refundInfo = info;

    if (info) {
        this->m_itemFlags &= ~0x4u;
    }
}

// ref: FUN_007089e0
// CMSG_GET_ITEM_PURCHASE_DATA, once until the answer arrives.
void CGItem_C::RequestRefundInfo() {
    if (this->m_itemFlags & 0x4) {
        return;
    }

    this->m_itemFlags |= 0x4;

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_GET_ITEM_PURCHASE_DATA));
    msg.Put(this->GetGUID());
    msg.Finalize();

    ClientServices::Send(&msg);
}

// ref: FUN_007081b0
// SMSG_SOCKET_GEMS_RESULT: the four socket enchantments (slots 2..5) the server settled on; a zero
// leaves the slot as it is.
// PARTIAL: the reference then refreshes the item's inventory slot, its tooltip and the socketing
// frame (FUN_00707f50); those UI refreshes are ported with their frames.
void CGItem_C::SetSocketedGems(uint32_t gem1, uint32_t gem2, uint32_t gem3, uint32_t bonus) {
    auto data = this->Item();

    if (gem1) {
        data->enchantments[2].id = gem1;
    }

    if (gem2) {
        data->enchantments[3].id = gem2;
    }

    if (gem3) {
        data->enchantments[4].id = gem3;
    }

    if (bonus) {
        data->enchantments[5].id = bonus;
    }
}

// ref: FUN_006e6330
// SMSG_ITEM_TIME_UPDATE, SMSG_ITEM_ENCHANT_TIME_UPDATE and SMSG_SOCKET_GEMS_RESULT. An enchantment
// time for an item not here yet waits on the player until the item arrives.
int32_t ItemTimeUpdateHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WOWGUID guid = 0;
    msg->Get(guid);

    auto item = static_cast<CGItem_C*>(ClntObjMgrObjectPtr(guid, TYPE_ITEM, ".\\Player_C.cpp", 0x519));

    if (msgId == SMSG_ITEM_TIME_UPDATE) {
        uint32_t seconds = 0;
        msg->Get(seconds);

        if (item) {
            item->SetExpiration(static_cast<int32_t>(seconds));
        }

        return 1;
    }

    if (msgId == SMSG_ITEM_ENCHANT_TIME_UPDATE) {
        uint32_t slot = 0;
        uint32_t seconds = 0;
        WOWGUID owner = 0;
        msg->Get(slot);
        msg->Get(seconds);
        msg->Get(owner);

        if (item) {
            item->SetEnchantExpiration(static_cast<int32_t>(slot), static_cast<int32_t>(seconds));

            return 1;
        }

        auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(owner, TYPE_PLAYER, ".\\Player_C.cpp", 0x540));

        if (!player) {
            player = CGPlayer_C::GetActivePtr();
        }

        if (player) {
            player->AddPendingItemExpiration(guid, static_cast<int32_t>(slot), static_cast<int32_t>(seconds));
        }

        return 1;
    }

    if (msgId != SMSG_SOCKET_GEMS_RESULT) {
        return 0;
    }

    uint32_t gems[4] = {};

    for (auto& gem : gems) {
        msg->Get(gem);
    }

    if (item) {
        item->SetSocketedGems(gems[0], gems[1], gems[2], gems[3]);
    }

    return 1;
}

// ref: FUN_006d1650
// SMSG_ITEM_REFUND_INFO_RESPONSE: what the item cost, and each item price's cache record asked for.
int32_t ItemRefundInfoHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WOWGUID guid = 0;
    msg->Get(guid);

    auto item = static_cast<CGItem_C*>(ClntObjMgrObjectPtr(guid, TYPE_ITEM, ".\\Player_C.cpp", 0x12d0));

    if (!item) {
        msg->Seek(msg->Size());

        return 1;
    }

    auto info = item->m_refundInfo;

    if (!info) {
        info = static_cast<ITEMREFUNDINFO*>(SMemAlloc(sizeof(ITEMREFUNDINFO), ".\\Player_C.cpp", 0x12da, 0));

        if (!info) {
            msg->Seek(msg->Size());

            return 1;
        }

        item->SetRefundInfo(info);
    }

    msg->Get(info->money);
    msg->Get(info->honor);
    msg->Get(info->arenaPoints);

    for (int32_t i = 0; i < 5; i++) {
        uint32_t itemID = 0;
        msg->Get(itemID);
        msg->Get(info->itemCount[i]);
        info->itemID[i] = static_cast<int32_t>(itemID);

        WOWGUID none = 0;
        g_itemCache.GetRecord(DBCACHEKEY32(itemID), &none, nullptr, nullptr, true);
    }

    msg->Get(info->unk34);
    msg->Get(info->refundTimeLeft);

    return 1;
}

// ref: FUN_00707180
// The page text a readable item opens, never asking for the record.
int32_t CGItem_C::GetPageText() const {
    auto info = g_itemCache.Peek(DBCACHEKEY32(static_cast<uint32_t>(this->GetEntryID())));

    return info ? info->pageText : 0;
}
