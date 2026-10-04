#include "model/CM2Model.hpp"
#include "component/ComponentData.hpp"
#include "component/CCharacterComponent.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "ui/game/CGGameUI.hpp"
#include "object/client/CGGameObject_C.hpp"
#include "object/client/Mirror.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "ui/game/PlayerName.hpp"
#include "ui/game/PortraitButton.hpp"
#include "ui/game/ScriptEvents.hpp"
#include "world/CWorld.hpp"
#include <common/Time.hpp>
#include "db/Db.hpp"
#include "object/Types.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/FrameScript.hpp"
#include "ui/Game.hpp"
#include "client/ClientServices.hpp"
#include <common/DataStore.hpp>
#include <storm/Error.hpp>
#include "net/Connection.hpp"
#include "object/client/Spell_C.hpp"
#include "object/client/SpellHistory.hpp"
#include <cstddef>
#include "object/client/CGItem_C.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "ui/game/RuneInfo.hpp"
#include <storm/String.hpp>
#include <cstring>
#include <ctime>

CHARACTER_INFO CGPlayer_C::s_localPlayerInfo = {};
uint32_t CGPlayer_C::s_itemProficiency[17];  // ref: DAT_00c9d4f0

// ref: FUN_006cde90
uint32_t CGPlayer_C::GetItemProficiency(uint8_t itemClass) {
    if (itemClass > 16) {
        return 0;
    }

    return CGPlayer_C::s_itemProficiency[itemClass];
}

// ref: FUN_005cd920
// ref: FUN_005946c0
// The reference carries two identical copies of this accessor.
uint16_t CGPlayer_C::GetSkillLineID(uint32_t index) const {
    if (ClntObjMgrGetActivePlayer() != this->GetGUID()) {
        return 0;
    }

    return this->Player()->skillInfo[index].skillLineID;
}

// ref: FUN_006b1050
const CHARACTER_INFO* CGPlayer_C::GetLocalPlayerInfo() {
    return &CGPlayer_C::s_localPlayerInfo;
}

// ref: FUN_006b1060
// Empty rather than null when nothing has been captured: the reference returns the name pointer
// only while the first byte is non-zero, and null otherwise.
const char* CGPlayer_C::GetLocalPlayerName() {
    return CGPlayer_C::s_localPlayerInfo.name[0] ? CGPlayer_C::s_localPlayerInfo.name : nullptr;
}

// ref: FUN_006b1070
uint8_t CGPlayer_C::GetLocalPlayerRace() {
    return CGPlayer_C::s_localPlayerInfo.raceID;
}

// ref: FUN_006b1080
uint8_t CGPlayer_C::GetLocalPlayerClass() {
    return CGPlayer_C::s_localPlayerInfo.classID;
}

// ref: FUN_006b1090
uint8_t CGPlayer_C::GetLocalPlayerSex() {
    return CGPlayer_C::s_localPlayerInfo.sexID;
}

// ref: FUN_006b10a0
uint8_t CGPlayer_C::GetLocalPlayerLevel() {
    return CGPlayer_C::s_localPlayerInfo.experienceLevel;
}

void CGPlayer_C::SetLocalPlayerInfo(const CHARACTER_INFO& info) {
    CGPlayer_C::s_localPlayerInfo = info;
}

// ref: FUN_006d4450
void CGPlayer_C::SendGroupAccept(uint32_t flags) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_GROUP_ACCEPT));
    msg.Put(flags);
    msg.Finalize();
    ClientServices::Send(&msg);
}

WOWGUID CGPlayer_C::s_resurrectRequester;
WOWGUID CGPlayer_C::s_spiritHealerGUID;
WOWGUID CGPlayer_C::s_talentMasterGUID;
WOWGUID CGPlayer_C::s_binderGUID;

// ref: FUN_006d1d30
void CGPlayer_C::SendResurrectResponse(uint8_t accept) {
    if (!CGPlayer_C::s_resurrectRequester) {
        return;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_RESURRECT_RESPONSE));
    msg.Put(CGPlayer_C::s_resurrectRequester);
    msg.Put(accept);
    msg.Finalize();
    ClientServices::Send(&msg);

    CGPlayer_C::s_resurrectRequester = 0;
}

// ref: FUN_006d1e20
void CGPlayer_C::SendGossipHello(const WOWGUID& guid) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_GOSSIP_HELLO));
    msg.Put(guid);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d1ea0
void CGPlayer_C::SendQuestGiverHello(const WOWGUID& guid) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_QUEST_GIVER_HELLO));
    msg.Put(guid);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d2340
void CGPlayer_C::SendBankerActivate(const WOWGUID& guid) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_BANKER_ACTIVATE));
    msg.Put(guid);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d2480
void CGPlayer_C::SendPetitionShowList(const WOWGUID& guid) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_PETITION_SHOW_LIST));
    msg.Put(guid);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d25c0
void CGPlayer_C::SendBattlemasterHello(const WOWGUID& guid) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_BATTLEMASTER_HELLO));
    msg.Put(guid);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d2640
void CGPlayer_C::SendAuctionHello(const WOWGUID& guid) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(MSG_AUCTION_HELLO));
    msg.Put(guid);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d26c0
void CGPlayer_C::SendListStabledPets(const WOWGUID& guid) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(MSG_LIST_STABLED_PETS));
    msg.Put(guid);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d2740
void CGPlayer_C::SendSpellClick(const WOWGUID& guid) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_SPELL_CLICK));
    msg.Put(guid);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d27c0
void CGPlayer_C::SendPlayerVehicleEnter(const WOWGUID& guid) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_PLAYER_VEHICLE_ENTER));
    msg.Put(guid);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d62a0
void CGPlayer_C::SendEnableTaxiNode(const WOWGUID& guid) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_ENABLE_TAXI_NODE));
    msg.Put(guid);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d6320
void CGPlayer_C::SendTaxiQueryAvailableNodes(const WOWGUID& guid) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_TAXI_QUERY_AVAILABLE_NODES));
    msg.Put(guid);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d6e00
void CGPlayer_C::SendPetitionShowSignatures(WOWGUID guid) {
    if (guid) {
        CDataStore msg;
        msg.Put(static_cast<uint32_t>(CMSG_PETITION_SHOW_SIGNATURES));
        msg.Put(guid);
        msg.Finalize();
        ClientServices::Send(&msg);
    }
}

// ref: FUN_006d2c20
void CGPlayer_C::SendAutostoreLootItem(uint8_t slot) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_AUTOSTORE_LOOT_ITEM));
    msg.Put(slot);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d2d40
void CGPlayer_C::SendSellItem(WOWGUID vendor, WOWGUID item, uint32_t count) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_SELL_ITEM));
    msg.Put(vendor);
    msg.Put(item);
    msg.Put(count);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d2ea0
void CGPlayer_C::SendBuyItemInSlot(WOWGUID vendor, const uint32_t* entry, uint32_t count, WOWGUID bag, uint8_t bagSlot) {
    if (!entry) {
        return;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_BUY_ITEM_IN_SLOT));
    msg.Put(vendor);
    msg.Put(entry[1]);
    msg.Put(entry[0]);
    msg.Put(bag);
    msg.Put(bagSlot);
    msg.Put(count);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d43c0
void CGPlayer_C::SendGroupUninviteGuid(WOWGUID guid, const char* reason) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_GROUP_UNINVITE_GUID));
    msg.Put(guid);
    msg.PutString(reason);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d45b0
void CGPlayer_C::SendPartySilence(WOWGUID guid, uint8_t value) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_PARTY_SILENCE));
    msg.Put(guid);
    msg.Put(value);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d4640
void CGPlayer_C::SendPartyUnsilence(WOWGUID guid, uint8_t value) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_PARTY_UNSILENCE));
    msg.Put(guid);
    msg.Put(value);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d4c10
void CGPlayer_C::SendQuestGiverQueryQuest(const WOWGUID& giver, uint32_t questID) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_QUEST_GIVER_QUERY_QUEST));
    msg.Put(giver);
    msg.Put(questID);
    msg.Put(static_cast<uint8_t>(1));
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d4e60
void CGPlayer_C::SendQuestGiverChooseReward(const WOWGUID& giver, uint32_t questID, uint32_t reward) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_QUEST_GIVER_CHOOSE_REWARD));
    msg.Put(giver);
    msg.Put(questID);
    msg.Put(reward);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d4f00
void CGPlayer_C::SendPushQuestToParty(uint32_t questID) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_PUSH_QUEST_TO_PARTY));
    msg.Put(questID);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d5c90
void CGPlayer_C::SendReadItem(uint8_t bag, uint8_t slot) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_READ_ITEM));
    msg.Put(bag);
    msg.Put(slot);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d7200
void CGPlayer_C::SendPartyAssignmentSet(uint8_t assignment, WOWGUID guid) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(MSG_PARTY_ASSIGNMENT));
    msg.Put(assignment);
    msg.Put(static_cast<uint8_t>(1));
    msg.Put(guid);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d7290
void CGPlayer_C::SendPartyAssignmentClear(uint8_t assignment, WOWGUID guid) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(MSG_PARTY_ASSIGNMENT));
    msg.Put(assignment);
    msg.Put(static_cast<uint8_t>(0));
    msg.Put(guid);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_006d1fc0
// Within the NPC's combat reach plus 4 yards (the 4.0f at 009e8d2c), compared squared,
// inclusive. The talent master and binder tests below are the same code on other guids.
int32_t CGPlayer_C::IsInSpiritHealerRange() const {
    auto object = ClntObjMgrObjectPtr(CGPlayer_C::s_spiritHealerGUID, TYPE_UNIT, __FILE__, __LINE__);

    if (object) {
        auto a = this->GetPosition();
        auto b = static_cast<CGUnit_C*>(object)->GetPosition();
        auto reach = static_cast<CGUnit_C*>(object)->Unit()->combatReach + 4.0f;
        auto distSq = (b.z - a.z) * (b.z - a.z) + (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);

        if (distSq <= reach * reach) {
            return 1;
        }
    }

    return 0;
}

// ref: FUN_006d2070
int32_t CGPlayer_C::IsInTalentMasterRange() const {
    auto object = ClntObjMgrObjectPtr(CGPlayer_C::s_talentMasterGUID, TYPE_UNIT, __FILE__, __LINE__);

    if (object) {
        auto a = this->GetPosition();
        auto b = static_cast<CGUnit_C*>(object)->GetPosition();
        auto reach = static_cast<CGUnit_C*>(object)->Unit()->combatReach + 4.0f;
        auto distSq = (b.z - a.z) * (b.z - a.z) + (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);

        if (distSq <= reach * reach) {
            return 1;
        }
    }

    return 0;
}

// ref: FUN_006d2290
int32_t CGPlayer_C::IsInBinderRange() const {
    auto object = ClntObjMgrObjectPtr(CGPlayer_C::s_binderGUID, TYPE_UNIT, __FILE__, __LINE__);

    if (object) {
        auto a = this->GetPosition();
        auto b = static_cast<CGUnit_C*>(object)->GetPosition();
        auto reach = static_cast<CGUnit_C*>(object)->Unit()->combatReach + 4.0f;
        auto distSq = (b.z - a.z) * (b.z - a.z) + (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);

        if (distSq <= reach * reach) {
            return 1;
        }
    }

    return 0;
}

// ref: FUN_006d5e70
int32_t CGPlayer_C::GetBaseLanguage() const {
    auto race = g_chrRacesDB.GetRecord(this->Unit()->bytes0 & 0xFF);

    return race ? race->m_baseLanguage : 0;
}

// ref: FUN_006d6e90
int32_t CGPlayer_C::GetFactionSide() const {
    auto race = g_chrRacesDB.GetRecord(this->Unit()->bytes0 & 0xFF);

    if (race) {
        auto factionTemplate = g_factionTemplateDB.GetRecord(race->m_factionID);

        if (factionTemplate) {
            auto group = static_cast<uint32_t>(factionTemplate->m_factionGroup);

            if (group & 0x4) {
                return 0;
            }

            return (group & 0x2) ? 1 : -1;
        }
    }

    return -1;
}

// ref: FUN_006dc1c0
// The skill line is read the way GetSkillLineID reads it -- 0 for anyone but the active player --
// inline, as the reference has it.
int32_t CGPlayer_C::GetSkillIndex(uint32_t skillLine) const {
    int32_t index = 0;

    do {
        auto guid = this->GetGUID();
        uint16_t line = ClntObjMgrGetActivePlayer() == guid
            ? this->Player()->skillInfo[index].skillLineID
            : 0;

        if (line == skillLine) {
            break;
        }

        index++;
    } while (index < 128);

    if (index == 128) {
        index = -1;
    }

    return index;
}

// ref: FUN_006de330
CVisibleItemData* CGPlayer_C::GetVisibleItem(uint32_t slot) const {
    if (static_cast<int32_t>(slot) > -1 && slot < 19) {
        return &this->Player()->visibleItems[slot];
    }

    return nullptr;
}

// ref: FUN_004038f0
// One of the most-called leaves in the client: 101 call sites resolve the active player through
// it. The reference spells it exactly the same way -- the active player's guid, resolved as
// TYPE_PLAYER (0x10), with the Player_C.h file and line the allocator tracks it by.
CGPlayer_C* CGPlayer_C::GetActivePtr() {
    return static_cast<CGPlayer_C*>(
        ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__)
    );
}

// ref: FUN_006e6b40
CGPlayer_C::CGPlayer_C(uint32_t time, CClientObjCreate& objCreate) : CGUnit_C(time, objCreate) {
    // TODO the rest of the constructor

    // The inventory is the descriptor's slot block, invSlots through currencyTokenSlots
    static_assert(offsetof(CGPlayerData, currencyTokenSlots) - offsetof(CGPlayerData, invSlots) == 118 * sizeof(WOWGUID), "the inventory slots must be contiguous");

    this->m_bag.m_owner = this->GetGUID();
    this->m_bag.m_hasBankSlots = 1;
    this->m_bag.m_numSlots = this->GetGUID() == ClntObjMgrGetActivePlayer() ? 150 : 0;
    this->m_bag.m_slots = this->Player()->invSlots;
}

CGPlayer_C::~CGPlayer_C() {
    // TODO
}

// ref: FUN_00578210
int32_t CGPlayer_C::GetModDamageDonePos(uint32_t school) const {
    if (this->GetGUID() != ClntObjMgrGetActivePlayer()) {
        return 0;
    }

    return this->Player()->modDamageDonePos[school];
}

// ref: FUN_00578250
int32_t CGPlayer_C::GetModDamageDoneNeg(uint32_t school) const {
    if (this->GetGUID() != ClntObjMgrGetActivePlayer()) {
        return 0;
    }

    return this->Player()->modDamageDoneNeg[school];
}

// ref: FUN_00578290
float CGPlayer_C::GetModDamageDonePct(uint32_t school) const {
    if (this->GetGUID() != ClntObjMgrGetActivePlayer()) {
        return 0.0f;
    }

    return this->Player()->modDamageDonePct[school];
}

// ref: FUN_006d7070
// stat is 0-based (0 strength, 1 agility). The class record's second field decides whether
// strength counts double; a form flagged 0x20 adds the raw agility on top.
int32_t CGPlayer_C::GetAttackPowerForStat(int32_t stat, int32_t value) const {
    auto classRec = g_chrClassesDB.GetRecord((this->Unit()->bytes0 >> 8) & 0xFF);

    if (!classRec) {
        return 0;
    }

    auto notAgility = classRec->m_damageBonusStat != 1;

    auto base = value - 10;

    if (base < 0) {
        base = 0;
    }

    int32_t attackPower = 0;

    if (stat == 0) {
        attackPower = base;

        if (notAgility) {
            return base * 2;
        }
    } else if (stat == 1) {
        if (!notAgility) {
            attackPower = base;
        }

        auto form = g_spellShapeshiftFormDB.GetRecord(this->GetShapeshiftForm());

        if (form && (form->m_flags & 0x20)) {
            attackPower += value;
        }
    }

    return attackPower;
}

// ref: FUN_0051a250
uint32_t CGPlayer_C::GetSkillRank(uint32_t index) const {
    if (this->GetGUID() != ClntObjMgrGetActivePlayer()) {
        return 0;
    }

    uint32_t rank = this->Player()->skillInfo[index].skillRank;

    if (rank) {
        rank += static_cast<uint16_t>(this->Player()->skillInfo[index].skillPermModifier);
    }

    return rank;
}

// ref: FUN_004f7310
// The object the player is viewing the world through; the active player's copy only.
WOWGUID CGPlayer_C::GetFarsightObject() const {
    if (ClntObjMgrGetActivePlayer() != this->GetGUID()) {
        return 0;
    }

    return this->Player()->farsightObject;
}

// ref: FUN_0051a2b0
uint32_t CGPlayer_C::GetMoney() const {
    if (this->GetGUID() != ClntObjMgrGetActivePlayer()) {
        return 0;
    }

    return this->CGPlayer::GetMoney();
}

// ref: FUN_0060a600
uint32_t CGPlayer_C::GetNextLevelXP() const {
    if (this->GetGUID() != ClntObjMgrGetActivePlayer()) {
        return 0;
    }

    return this->CGPlayer::GetNextLevelXP();
}

uint32_t CGPlayer_C::GetXP() const {
    if (this->GetGUID() != ClntObjMgrGetActivePlayer()) {
        return 0;
    }

    return this->CGPlayer::GetXP();
}

void CGPlayer_C::PostInit(uint32_t time, const CClientObjCreate& init, bool a4) {
    // TODO

    this->CGUnit_C::PostInit(time, init, a4);

    // TODO

    if (this->GetGUID() == ClntObjMgrGetActivePlayer()) {
        this->PostInitActivePlayer();
    } else {
        this->UpdatePartyMemberState();
    }

    // TODO
}

// ref: FUN_006e7f50
void CGPlayer_C::PostInitActivePlayer() {
    // The player stands still until the server's first time sync (event 0x31 clears it).
    this->m_localMove.m_moveFlags |= 0x200;

    // TODO

    if (ClntObjMgrGetPlayerType() == PLAYER_NORMAL) {
        // TODO

        FrameScript_SignalEvent(SCRIPT_ACTIONBAR_SLOT_CHANGED, "%d", 0);
    }

    // TODO

    if (ClntObjMgrGetPlayerType() == PLAYER_NORMAL) {
        // TODO

        // The player is the active mover -- or the vehicle it controls, which the vehicle port
        // brings in.
        GameUIInitPlayerControl(this->GetGUID());

        // TODO

        CGGameUI::EnterWorld();

        // TODO FUN_00520f70

        // The character login is done and the character is in the world (0x006e8199).
        ClientServices::Connection()->SetInWorld(1);
    }

    // The player's class decides which spells take the spell modifiers (0x006e81e3).
    SpellSetClassSet((this->m_unit->bytes0 >> 8) & 0xFF);

    // FUN_006e5180's watcher on the invisibility glow (PARTIAL: the rest of that function's
    // per-object watchers are not ported), then the screen effect a ghost or an aura logs in with.
    MirrorRegisterObjectHandler(this->GetGUID(), ID_PLAYER, offsetof(CGPlayerData, field_bytes_2_4), 1,
                                &PlayerOnFieldBytesChanged, nullptr, 0, 0);
    CGWorldFrame::UpdateScreenEffect();

    // TODO
}

void CGPlayer_C::SetStorage(uint32_t* storage, uint32_t* saved) {
    this->CGUnit_C::SetStorage(storage, saved);

    this->m_player = reinterpret_cast<CGPlayerData*>(&storage[CGPlayer::GetBaseOffset()]);
    this->m_playerSaved = &saved[CGPlayer::GetBaseOffsetSaved()];
}


void CGPlayer_C::UpdatePartyMemberState() {
    // TODO
}

uint32_t Player_C_GetDisplayId(uint32_t race, uint32_t sex) {
    STORM_ASSERT(sex < UNITSEX_LAST);

    auto raceRec = g_chrRacesDB.GetRecord(race);

    if (!raceRec) {
        return 0;
    }

    if (sex == UNITSEX_MALE) {
        return raceRec->m_maleDisplayID;
    }

    if (sex == UNITSEX_FEMALE) {
        return raceRec->m_femaleDisplayID;
    }

    return 0;
}

const CreatureModelDataRec* Player_C_GetModelName(uint32_t race, uint32_t sex) {
    STORM_ASSERT(sex < UNITSEX_LAST);

    auto displayID = Player_C_GetDisplayId(race, sex);

    if (!displayID) {
        return nullptr;
    }

    auto displayInfo = g_creatureDisplayInfoDB.GetRecord(displayID);

    if (!displayInfo) {
        // TODO this becomes nullsub in retail build -- might be variation of macro
        STORM_APP_FATAL("Error, unknown displayInfo %d specified for player race %d sex %d!", displayID, race, sex);
    }

    auto modelData = g_creatureModelDataDB.GetRecord(displayInfo->m_modelID);

    if (!modelData) {
        // TODO this becomes nullsub in retail build -- might be variation of macro
        STORM_APP_FATAL("Error, unknown model record %d specified for player race %d sex %d!", displayInfo->m_modelID, race, sex);
    }

    return modelData;
}

// ref: FUN_00753a50
bool InventorySlotInLocations(uint32_t slot, uint32_t mask) {
    if ((slot > 0x12 || (mask & 0x1) == 0)
        && (slot - 0x17 > 0xF || (mask & 0x4) == 0)
        && (slot - 0x13 > 3 || (mask & 0x2) == 0)
        && (((slot < 0x27 || slot > 0x42) && slot - 0x43 > 6) || (mask & 0x8) == 0)
        && (slot - 0x56 > 0x1F || (mask & 0x40) == 0)
        && (slot - 0x76 > 0x1F || (mask & 0x200) == 0)
    ) {
        return false;
    }

    return true;
}

// ref: FUN_006dc010
// SMSG_TIME_SYNC_REQUEST: the counter, answered through the player's movement queue so the answer
// carries the time the movement had reached.
int32_t PlayerTimeSyncRequestHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    (void)param;
    (void)msgId;
    (void)time;

    uint32_t counter = 0;
    msg->Get(counter);

    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, ".\\Player_C.cpp", 0xa0));

    if (player) {
        player->m_localMove.QueueTimeSync(static_cast<int32_t>(OsGetAsyncTimeMs()), counter);
    }

    return 1;
}

// ref: FUN_006e8ee0
// PARTIAL: the reference registers some ninety Player_C message handlers here (FUN_006e83b0);
// the time sync is the one movement needs, and the item time, socket and refund messages the item
// tooltip reads. The refund answer reaches FUN_006d1650 through the reference's shared Player_C
// dispatcher (the switch at 0x006defce); frozen registers it directly.
void PlayerInitialize() {
    ClientServices::SetMessageHandler(SMSG_TIME_SYNC_REQUEST, PlayerTimeSyncRequestHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_ITEM_TIME_UPDATE, ItemTimeUpdateHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_ITEM_ENCHANT_TIME_UPDATE, ItemTimeUpdateHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_SOCKET_GEMS_RESULT, ItemTimeUpdateHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_ITEM_REFUND_INFO_RESPONSE, ItemRefundInfoHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_CONVERT_RUNE, RuneConvertHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_RESYNC_RUNES, RuneResyncHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_ADD_RUNE_POWER, RuneAddPowerHandler, nullptr);
}

// Where the player's hearthstone returns them, from SMSG_BIND_POINT_UPDATE.
static C3Vector s_bindPosition;                         // ref: DAT_00c9eb2c
static uint32_t s_bindMapID;                            // ref: DAT_00c9d538
static uint32_t s_bindAreaID;                           // ref: DAT_00c9d534
static int32_t s_bindPointKnown;                        // ref: DAT_00c9d53c

// ref: FUN_006ceec0
void PlayerReadBindPoint(CDataStore* msg) {
    msg->Get(s_bindPosition.x);
    msg->Get(s_bindPosition.y);
    msg->Get(s_bindPosition.z);
    msg->Get(s_bindMapID);
    msg->Get(s_bindAreaID);
    s_bindPointKnown = 1;
}

// ref: FUN_006cef10
int32_t PlayerGetBindAreaID() {
    return static_cast<int32_t>(s_bindAreaID);
}

// ref: FUN_006de230
// The equipped item, in the slots the mask allows, of the class and subclass the spell needs; a
// broken one does not count. A hidden hand's slot is left out, as is the ranged slot for a spell
// marked AttributesEx2 0x400. The reference keeps this on the player.
CGItem_C* CGPlayer_C::GetEquippedItemForSpell(const SpellRec* spell, uint32_t slots) {
    auto data = this->Unit();

    if ((data->flags & 0x200000) || (data->flags2 & 0x80)) {
        if (this->IsHandHidden(0)) {
            slots &= ~0x8000u;
        }

        if (this->IsHandHidden(1)) {
            slots &= ~0x10000u;
        }
    }

    if (data->flags2 & 0x400) {
        slots &= ~0x20000u;
    }

    for (uint32_t slot = 0; slot < 23; slot++) {
        if (!(slots & (1u << slot))) {
            continue;
        }

        auto item = this->m_bag.GetItem(slot);

        if (!item) {
            continue;
        }

        auto itemData = item->Item();

        if (((itemData->flags & 0x8) || itemData->maxDurability == 0 || itemData->durability != 0) && !(itemData->flags & 0x10)) {
            if (spell->m_equippedItemClass == item->GetClassID() && (spell->m_equippedItemSubclass & (1u << (item->GetSubclassID() & 0x1F)))) {
                return item;
            }
        }
    }

    return nullptr;
}

// The inspected player's gear. SMSG_INSPECT_RESULTS fills it; until that handler is ported nothing
// does, and every other player's gear is read from their visible items instead.
INSPECTDATA* g_inspectData;                         // ref: DAT_00c9eae0

// The scratch record GetInventoryItemInfo builds from a visible item.
static INVENTORYITEMINFO s_visibleItemInfo;         // ref: DAT_00c9ec78

// ref: FUN_006d6f00
const char* CGPlayer_C::GetPvpRankName(int32_t rank) const {
    char key[32];
    SStrPrintf(key, sizeof(key), "PVP_RANK_%d_%d", rank, this->GetFactionSide());

    return this->GetGenderedText(key, -1);
}

// ref: FUN_006cf440
// Seconds played: what the server last reported plus the time since. 0 before it has reported.
int32_t CGPlayer_C::GetPlayedTime() const {
    if (this->m_playedTime < 0) {
        return 0;
    }

    return static_cast<int32_t>(std::time(nullptr)) - this->m_playedTimeReceived + this->m_playedTime;
}

// ref: FUN_006cf470
void CGPlayer_C::SetPlayedTime(int32_t seconds) {
    this->m_playedTime = seconds;
    this->m_playedTimeReceived = static_cast<int32_t>(std::time(nullptr));
}

// ref: FUN_006dc2c0
// The rank with the temporary bonus too (the active player's own skill block only), never negative.
int32_t CGPlayer_C::GetSkillValue(uint32_t skillLine) const {
    auto index = this->GetSkillIndex(skillLine);

    if (index < 0) {
        return 0;
    }

    int16_t bonus = this->GetGUID() == ClntObjMgrGetActivePlayer() ? this->Player()->skillInfo[index].skillTempModifier : 0;
    auto value = static_cast<int32_t>(this->GetSkillRank(index)) + bonus;

    return value >= 0 ? value : 0;
}

static void PlayerOnGemItemArrived(uint32_t id, const WOWGUID* guid, void* param, bool found) {
    // ref: FUN_006de490
    if (!found) {
        return;
    }

    if (auto player = CGPlayer_C::GetActivePtr()) {
        player->UpdateGemColorCounts();
    }
}

// ref: FUN_006de4d0
// Counts the gems socketed in the nineteen equipped slots by the colours they match. The active
// player's own items are read for a broken one not to count; anyone else's from their inspect or
// visible-item record.
void CGPlayer_C::UpdateGemColorCounts() {
    for (auto& count : this->m_gemColorCounts) {
        count = 0;
    }

    bool active = ClntObjMgrGetActivePlayer() == this->GetGUID();

    for (uint32_t slot = 0; slot < 19; slot++) {
        CGItem_C* item = nullptr;
        const INVENTORYITEMINFO* info = nullptr;

        if (ClntObjMgrGetActivePlayer() == this->GetGUID()) {
            item = this->m_bag.GetItem(slot);

            if (!item) {
                continue;
            }

            auto data = item->Item();

            if (!(((data->flags >> 3) & 1) || data->maxDurability == 0 || data->durability != 0) || ((data->flags >> 4) & 1)) {
                continue;
            }
        } else {
            info = this->GetInventoryItemInfo(slot);

            if (!info || !(info->flags & 1)) {
                continue;
            }
        }

        for (int32_t socket = 2; socket < 5; socket++) {
            int32_t enchantID;

            if (active) {
                enchantID = item && !item->IsCharter() ? item->Item()->enchantments[socket].id : 0;
            } else {
                enchantID = info->enchant[socket];
            }

            auto enchant = g_spellItemEnchantmentDB.GetRecord(enchantID);

            if (!enchant || !enchant->m_srcItemID) {
                continue;
            }

            WOWGUID none = 0;
            auto gem = g_itemCache.GetRecord(DBCACHEKEY32(enchant->m_srcItemID), &none, &PlayerOnGemItemArrived, nullptr, false);

            if (!gem) {
                continue;
            }

            auto properties = g_gemPropertiesDB.GetRecord(gem->gemProperties);

            if (!properties || !properties->m_type) {
                continue;
            }

            for (int32_t color = 0; color < 4; color++) {
                if (properties->m_type & (1u << color)) {
                    this->m_gemColorCounts[color]++;
                }
            }
        }
    }
}

// ref: FUN_006de360
// The inspected player's record for the slot, or one built from the player's visible item (entry,
// permanent and temporary enchantment) when this is not the inspected player.
const INVENTORYITEMINFO* CGPlayer_C::GetInventoryItemInfo(uint32_t slot) const {
    if (g_inspectData && g_inspectData->guid == this->GetGUID()) {
        if (static_cast<int32_t>(slot) < 0) {
            return nullptr;
        }

        if (slot < 19) {
            return &g_inspectData->items[slot];
        }
    }

    if (slot >= 19) {
        return nullptr;
    }

    auto visible = &this->Player()->visibleItems[slot];

    memset(&s_visibleItemInfo, 0, sizeof(s_visibleItemInfo));
    s_visibleItemInfo.itemID = visible->entryID < 0 ? -visible->entryID : visible->entryID;
    s_visibleItemInfo.enchant[0] = static_cast<uint16_t>(visible->enchantment);
    s_visibleItemInfo.enchant[1] = static_cast<uint16_t>(visible->enchantment >> 16);

    if (visible->entryID < 1) {
        s_visibleItemInfo.flags |= 1;
    }

    return &s_visibleItemInfo;
}

// ref: FUN_006e61b0
void CGPlayer_C::AddPendingItemExpiration(WOWGUID item, int32_t slot, int32_t seconds) {
    auto pending = this->m_pendingItemExpirations.NewNode(2, 0, 0x8);

    pending->item = item;
    pending->slot = slot;
    pending->seconds = seconds;
}

// ref: FUN_006e6250
void CGPlayer_C::ApplyPendingItemExpirations(CGItem_C* item) {
    if (!item) {
        return;
    }

    for (auto pending = this->m_pendingItemExpirations.Head(); pending; ) {
        auto next = this->m_pendingItemExpirations.Next(pending);

        if (pending->item == item->GetGUID()) {
            item->SetEnchantExpiration(pending->slot, pending->seconds);
            this->m_pendingItemExpirations.DeleteNode(pending);
        }

        pending = next;
    }
}

// ref: FUN_004f7290
float CGPlayer_C::GetDrunkenness() const {
    auto data = this->Player();

    int32_t drunk = data->bytes_3_2;
    int32_t fake = data->fakeInebriation;
    int32_t larger = drunk <= fake ? fake : drunk;

    uint32_t value;

    if (larger < 100) {
        value = fake < drunk ? static_cast<uint32_t>(drunk) : static_cast<uint32_t>(fake);
    } else {
        value = 100;
    }

    return static_cast<float>(value & 0xFF) * 0.01f;
}

// ref: FUN_006de750
void CGPlayer_C::UpdatePvPFlagTimer(uint32_t oldFlags) {
    uint32_t flags = this->Player()->flags;

    // 0x200: PvP turned on by the player, which never lapses.
    if (flags & 0x200) {
        this->m_pvpFlagExpire = 0;
        return;
    }

    // 0x40000: the flag is on its timer already, and stays on the one it has.
    if (!(flags & 0x40000) || !(oldFlags & 0x40000)) {
        this->m_pvpFlagExpire = CWorld::GetCurTimeMs() + 300000;
    }
}

namespace {

// ref: FUN_006d5290
// A game object whose highlight depends on the player's state takes the new one.
int32_t PlayerUpdateObjectHighlight(WOWGUID guid, void* param) {
    auto object = ClntObjMgrObjectPtr(guid, TYPE_OBJECT, __FILE__, __LINE__);

    if (object && object->IsA(TYPE_GAMEOBJECT)) {
        static_cast<CGGameObject_C*>(object)->UpdateHighlightModel();
    }

    return 1;
}

}

// ref: FUN_006df710
// PARTIAL: the corpse half is not ported -- clearing the spirit healer the player was talking to
// (FUN_00523eb0, CMSG 0x2e2), forgetting the corpse's map position (FUN_0051f690), the resurrect
// request and its CMSG 0x216 (FUN_00524a30), and the corpse query with its CMSG 0x417
// (FUN_006dc5a0). The screen effect, the object highlights, the reactions and the interface event
// are here.
void CGPlayer_C::OnGhostChanged() {
    if (!(this->Player()->flags & 0x10)) {
        FrameScript_SignalEvent(SCRIPT_PLAYER_UNGHOST, nullptr);
    }

    // The ghost's sky and grey come from the screen effect (ScreenEffect 1, which forces the
    // light's death params and their skybox).
    CGWorldFrame::UpdateScreenEffect();

    ClntObjMgrEnumVisibleObjects(&PlayerUpdateObjectHighlight, nullptr);

    PlayerNameInvalidateAllReactions();
    this->UpdateReaction(0);
}

// ref: FUN_006e0fd0
// PARTIAL: four arms are not ported -- the AFK flag the chat frame keeps (DAT_00bcefec), the helm
// and cloak showing toggles (FUN_006dd0f0 / FUN_006e0e00 and FUN_006dd1b0 / FUN_006e0ef0, which
// redress the model and send CMSG 0x2b9 / 0x2ba), the resting flag's two GetGUID calls, and the
// PvP toggle's line in the chat frame (FUN_00509dd0). The PvP toggle's error text is shown.
void CGPlayer_C::OnFlagsChanged(uint32_t oldFlags) {
    uint32_t flags = this->Player()->flags;
    uint32_t changed = flags ^ oldFlags;
    WOWGUID guid = this->GetGUID();

    if (changed & 0x800E) {
        PlayerNameInvalidate(this->m_nameDesc);
    }

    if (changed & 0x40300) {
        this->UpdatePvPFlagTimer(oldFlags);
    }

    if (changed & 0xC00) {
        PortraitRefresh(guid, 3);
    }

    ScriptEventsSignalUnitEvent(guid, SCRIPT_PLAYER_FLAGS_CHANGED);

    if (guid != ClntObjMgrGetActivePlayer()) {
        return;
    }

    if (changed & 0x10) {
        this->OnGhostChanged();
    }

    if (changed & 0x20) {
        FrameScript_SignalEvent(SCRIPT_PLAYER_UPDATE_RESTING, nullptr);
    }

    if (changed & 0x200) {
        CGGameUI::DisplayError((flags & 0x200) ? 0x1DA : 0x1DB);
    }

    if (changed & 0x3000) {
        FrameScript_SignalEvent(SCRIPT_PLAYTIME_CHANGED, nullptr);
    }

    if (changed & 0x20000) {
        FrameScript_SignalEvent((flags & 0x20000) ? SCRIPT_ENABLE_TAXI_BENCHMARK : SCRIPT_DISABLE_TAXI_BENCHMARK, nullptr);
    }

    if (changed & 0x1800000) {
        SpellSignalUsableUpdate();
    }

    if (changed & 0x2000000) {
        FrameScript_SignalEvent((flags & 0x2000000) ? SCRIPT_DISABLE_XP_GAIN : SCRIPT_ENABLE_XP_GAIN, nullptr);
    }

    if (changed & 0x10000) {
        FrameScript_SignalEvent((flags & 0x10000) ? SCRIPT_ENABLE_LOW_LEVEL_RAID : SCRIPT_DISABLE_LOW_LEVEL_RAID, nullptr);
    }
}

namespace {

// ref: FUN_006e1c20
int32_t PlayerOnFlagsField(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(guid, TYPE_PLAYER, __FILE__, __LINE__));

    if (player) {
        player->OnFlagsChanged(*static_cast<const uint32_t*>(old));
    }

    return 1;
}

}

// ref: FUN_006e45d0
// PARTIAL: the reference registers some forty player field handlers here; this is the flags one.
void PlayerRegisterFieldHandlers() {
    MirrorRegisterHandler(ID_PLAYER, offsetof(CGPlayerData, flags), 4, &PlayerOnFlagsField, nullptr, 0, 0);
}

// ref: FUN_006da770
// PLAYER_FIELD_BYTES byte 3 moved: bit 0x40, the glow of an invisible player, picks the screen
// effect. PARTIAL: the walk over the visible units after it (FUN_006d7030 -> FUN_00727a70, the
// units' invisibility shading) is not ported.
int32_t PlayerOnFieldBytesChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(guid, TYPE_PLAYER, __FILE__, __LINE__));

    if (player) {
        uint8_t changed = player->Player()->field_bytes_2_4 ^ *static_cast<const uint8_t*>(old);

        if (changed & 0x40) {
            CGWorldFrame::UpdateScreenEffect();
        }
    }

    return 1;
}

static_assert(offsetof(CGPlayerData, field_bytes_2_4) == 0x10E7, "PLAYER_FIELD_BYTES byte 3 sits at 0x10e7");

// The reference fills both slots 0x24 and 0x28 with FUN_006e6fd0, tagged on GetBag below.
void* CGPlayer_C::Virtual024() {
    return &this->m_bag;
}

// ref: FUN_006e6fd0
CGBag_C* CGPlayer_C::GetBag() {
    return &this->m_bag;
}
