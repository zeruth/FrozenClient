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

    this->m_characterComponent = CCharacterComponent::AllocComponent();
    this->m_characterComponent->Init(&data, nullptr);

    // RenderPrep(1) is what actually composites the skin sections and binds the body texture;
    // RenderPrep(0) only defers. The glue's loading step does the same.
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
