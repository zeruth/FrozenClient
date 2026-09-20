#include "model/CM2Model.hpp"
#include "component/ComponentData.hpp"
#include "component/CCharacterComponent.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "db/Db.hpp"
#include "object/Types.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/FrameScript.hpp"
#include "ui/Game.hpp"
#include <storm/Error.hpp>

CHARACTER_INFO CGPlayer_C::s_localPlayerInfo = {};

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

uint32_t CGPlayer_C::GetMoney() const {
    if (this->GetGUID() != ClntObjMgrGetActivePlayer()) {
        return 0;
    }

    return this->CGPlayer::GetMoney();
}

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
    data.raceID = unit->pad1 & 0xFF;
    data.classID = (unit->pad1 >> 8) & 0xFF;
    data.sexID = (unit->pad1 >> 16) & 0xFF;
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
    int32_t sheatheState = unit->pad3 & 0xFF;
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
