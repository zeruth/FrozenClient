#include "model/CM2Model.hpp"
#include "component/ComponentData.hpp"
#include "component/CCharacterComponent.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "db/Db.hpp"
#include "object/Types.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/FrameScript.hpp"
#include "ui/Game.hpp"
#include "client/ClientServices.hpp"
#include <common/DataStore.hpp>
#include <storm/Error.hpp>

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

CGPlayer_C::CGPlayer_C(uint32_t time, CClientObjCreate& objCreate) : CGUnit_C(time, objCreate) {
    // TODO
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

void CGPlayer_C::PostInitActivePlayer() {
    // TODO

    if (ClntObjMgrGetPlayerType() == PLAYER_NORMAL) {
        // TODO

        FrameScript_SignalEvent(SCRIPT_ACTIONBAR_SLOT_CHANGED, "%d", 0);
    }

    // TODO

    if (ClntObjMgrGetPlayerType() == PLAYER_NORMAL) {
        // TODO

        CGGameUI::EnterWorld();
    }

    // TODO
}

void CGPlayer_C::SetStorage(uint32_t* storage, uint32_t* saved) {
    this->CGUnit_C::SetStorage(storage, saved);

    this->m_player = reinterpret_cast<CGPlayerData*>(&storage[CGPlayer::GetBaseOffset()]);
    this->m_playerSaved = &saved[CGPlayer::GetBaseOffsetSaved()];
}

// Assembles the composited body texture (skin, face, hair, facial hair) for the player's model
// from its appearance fields, the same component pipeline character creation uses. Equipment is
// not applied yet. (0x???? in the original CGPlayer_C::Initialize)
void CGPlayer_C::BuildCharacterComponent() {
    if (!this->m_model || !this->m_player) {
        return;
    }

    auto unit = this->Unit();

    if (!unit) {
        return;
    }

    ComponentData data;
    data.raceID = unit->bytes0 & 0xFF;
    data.classID = (unit->bytes0 >> 8) & 0xFF;
    data.sexID = (unit->bytes0 >> 16) & 0xFF;
    data.skinColorID = this->m_player->skinID;
    data.faceID = this->m_player->faceID;
    data.hairStyleID = this->m_player->hairStyleID;
    data.hairColorID = this->m_player->hairColorID;
    data.facialHairStyleID = this->m_player->facialHairStyleID;

    // The component borrows the model; keep our own reference balanced
    this->m_model->AddRef();
    data.model = this->m_model;
    data.flags |= 0x2;

    // A unit can be built more than once (respawn, tile reload, display change). Free any component
    // it already carries first: the component heap is a fixed-size ObjectAlloc pool, and leaking
    // into it eventually makes AllocComponent return null -- which then crashed in Init, since
    // neither call site checked. Release the model reference taken just above if it does fail.
    if (this->m_characterComponent) {
        CCharacterComponent::FreeComponent(this->m_characterComponent);
        this->m_characterComponent = nullptr;
    }

    this->m_characterComponent = CCharacterComponent::AllocComponent();

    if (!this->m_characterComponent) {
        this->m_model->Release();
        return;
    }

    this->m_characterComponent->Init(&data, nullptr);

    // UNIT_FIELD_BYTES_2 low byte is the sheathe state: 0 = stowed (weapons on the back/hip),
    // 1 = melee drawn, 2 = ranged drawn. Idle units stow their weapons, like the reference.
    int32_t sheatheState = unit->bytes2 & 0xFF;
    bool sheathed = sheatheState == 0;

    // Apply the equipped items. Armour slots are composited onto the body; weapons and shields are
    // separate models attached to the hand/shield points, exactly as the character-select display
    // does it in the reference.
    for (int32_t slot = 0; slot < 19; slot++) {
        auto entryID = this->m_player->visibleItems[slot].entryID;

        if (!entryID) {
            continue;
        }

        auto itemRec = g_itemDB.GetRecord(entryID);

        if (!itemRec || itemRec->m_displayInfoID <= 0) {
            continue;
        }

        bool isHand = slot == INVSLOT_MAINHAND || slot == INVSLOT_OFFHAND || slot == INVSLOT_RANGED;

        if (isHand) {
            // Hunters carry the ranged weapon; everyone else carries their melee weapons
            if ((slot == INVSLOT_MAINHAND || slot == INVSLOT_OFFHAND) && data.classID == 3) {
                continue;
            }

            if (slot == INVSLOT_RANGED && data.classID != 3) {
                continue;
            }

            auto displayRec = g_itemDisplayInfoDB.GetRecord(itemRec->m_displayInfoID);

            if (!displayRec) {
                continue;
            }

            bool shield = itemRec->m_inventoryType == INVTYPE_SHIELD;
            bool heldRight = itemRec->m_inventoryType == INVTYPE_RANGEDRIGHT || itemRec->m_inventoryType == INVTYPE_THROWN;

            CCharacterComponent::AddHandItem(
                this->m_model,
                displayRec,
                static_cast<INVENTORY_SLOTS>(slot),
                static_cast<SHEATHE_TYPE>(itemRec->m_sheatheType),
                sheathed,
                shield,
                heldRight,
                0
            );

            continue;
        }

        this->m_characterComponent->AddItemByInventoryType(itemRec->m_inventoryType, itemRec->m_displayInfoID);
    }

    // RenderPrep(1) is what actually composites the skin and item sections and binds the body
    // texture; RenderPrep(0) only defers. The glue's loading step does the same.
    this->m_characterComponent->RenderPrep(1);
    this->m_model->IsDrawable(1, 1);
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
