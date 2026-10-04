#include "world/map/MapFootprints.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/CVehicleCamera_C.hpp"
#include "object/client/UnitVehicle_C.hpp"
#include "model/CM2ParticleEmitter.hpp"
#include <new>
#include <storm/Memory.hpp>
#include "ui/game/CGGameUI.hpp"
#include "util/Zlib.hpp"
#include "ui/game/ScriptEvents.hpp"
#include <storm/String.hpp>
#include "ui/game/PortraitButton.hpp"
#include "component/ComponentData.hpp"
#include "client/ClientServices.hpp"
#include "net/Types.hpp"
#include "object/client/SpellVisuals.hpp"
#include "object/client/Mirror.hpp"
#include "util/Random.hpp"
#include <tempest/random/CRandom.hpp>
#include "console/CVar.hpp"
#include "console/Types.hpp"
#include "model/CM2Scene.hpp"
#include "net/Connection.hpp"
#include "sound/SI2.hpp"
#include "sound/SOUNDKITOBJECT.hpp"
#include "sound/SoundKitProperties.hpp"
#include "ui/game/PlayerName.hpp"
#include "ui/game/CGMinimapFrame.hpp"
#include "world/CWorld.hpp"
#include "world/map/CMap.hpp"
#include "world/MapWeather.hpp"
#include "component/CCharacterComponent.hpp"
#include "db/Db.hpp"
#include "model/Model2.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Data.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "object/client/CEffect.hpp"
#include "object/client/CGGameObject_C.hpp"
#include "object/client/GameObjectTypes.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/CVehicle_C.hpp"
#include "object/client/CVehiclePassenger_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/ObjectEffect.hpp"
#include "ui/Game.hpp"
#include "ui/InputControl.hpp"
#include "object/client/Spell_C.hpp"
#include "object/movement/CMovementStatus.hpp"
#include "util/DataStore.hpp"
#include "ui/game/CGCamera.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "ui/game/CGPartyInfo.hpp"
#include "ui/game/CGRaidInfo.hpp"
#include "ui/game/ScriptEvents.hpp"
#include "ui/game/Types.hpp"
#include "ui/FrameScript.hpp"
#include <storm/Error.hpp>
#include <tempest/Math.hpp>
#include <cstring>
#include <cmath>

WOWGUID CGUnit_C::s_activeMover;

// ref: FUN_00715440
// Pure FactionTemplate.dbc logic, and the answer everything else falls back to. Each side carries a
// group mask, a friend mask, an enemy mask and four-entry friend and enemy lists of faction ids;
// each list stops at its first zero entry.
int32_t CGUnit_C::GetFactionTemplateReaction(const FactionTemplateRec* a, const FactionTemplateRec* b) {
    if (a->m_enemyGroup & b->m_factionGroup) {
        return 1;
    }

    for (int32_t i = 0; i < 4 && a->m_enemies[i]; i++) {
        if (a->m_enemies[i] == b->m_faction) {
            return 1;
        }
    }

    if (!(a->m_friendGroup & b->m_factionGroup)) {
        for (int32_t i = 0; i < 4 && a->m_friend[i]; i++) {
            if (a->m_friend[i] == b->m_faction) {
                return 4;
            }
        }

        if (!(b->m_friendGroup & a->m_factionGroup)) {
            for (int32_t i = 0; i < 4; i++) {
                if (!b->m_friend[i]) {
                    // Neither side claims the other. Neutral, unless this template's flag 0x2000
                    // says it attacks anything it is not explicitly friendly with.
                    return (a->m_flags & 0x2000) ? 1 : 3;
                }

                if (b->m_friend[i] == a->m_faction) {
                    break;
                }
            }
        }
    }

    return 4;
}

// ref: FUN_0071f770
// TODO the reference reaches for the reputation system first when the other unit is a player: an
// explicitly set standing wins, and a faction at war is hostile regardless of the templates. None
// of that is ported, so this is the faction-template answer alone, which is what the reference
// itself falls back to for everything that is not a player with a reputation entry.
int32_t CGUnit_C::GetReaction(const CGUnit_C* other) const {
    if (!other) {
        return 3;
    }

    if (this == other) {
        return 4;
    }

    auto data = this->Unit();
    auto otherData = other->Unit();

    if (!data || !otherData) {
        return 3;
    }

    auto a = g_factionTemplateDB.GetRecord(data->factionTemplate);
    auto b = g_factionTemplateDB.GetRecord(otherData->factionTemplate);

    // A template the client cannot resolve is neutral to everything.
    if (!a || !b) {
        return 3;
    }

    return CGUnit_C::GetFactionTemplateReaction(a, b);
}

const char* CGUnit_C::GetDisplayClassNameFromRecord(const ChrClassesRec* classRec, UNIT_SEX sex, UNIT_SEX* displaySex) {
    if (displaySex) {
        *displaySex = sex;
    }

    if (!classRec) {
        return nullptr;
    }

    if (sex == UNITSEX_MALE) {
        if (*classRec->m_nameMale) {
            return classRec->m_nameMale;
        }

        if (*classRec->m_nameFemale) {
            if (displaySex) {
                *displaySex = UNITSEX_FEMALE;
            }

            return classRec->m_nameFemale;
        }

        return classRec->m_name;
    }

    if (sex == UNITSEX_FEMALE) {
        if (*classRec->m_nameFemale) {
            return classRec->m_nameFemale;
        }

        if (*classRec->m_nameMale) {
            if (displaySex) {
                *displaySex = UNITSEX_MALE;
            }

            return classRec->m_nameMale;
        }

        return classRec->m_name;
    }

    return classRec->m_name;
}

const char* CGUnit_C::GetDisplayRaceNameFromRecord(const ChrRacesRec* raceRec, UNIT_SEX sex, UNIT_SEX* displaySex) {
    if (displaySex) {
        *displaySex = sex;
    }

    if (!raceRec) {
        return nullptr;
    }

    if (sex == UNITSEX_MALE) {
        if (*raceRec->m_nameMale) {
            return raceRec->m_nameMale;
        }

        if (*raceRec->m_nameFemale) {
            if (displaySex) {
                *displaySex = UNITSEX_FEMALE;
            }

            return raceRec->m_nameFemale;
        }

        return raceRec->m_name;
    }

    if (sex == UNITSEX_FEMALE) {
        if (*raceRec->m_nameFemale) {
            return raceRec->m_nameFemale;
        }

        if (*raceRec->m_nameMale) {
            if (displaySex) {
                *displaySex = UNITSEX_MALE;
            }

            return raceRec->m_nameMale;
        }

        return raceRec->m_name;
    }

    return raceRec->m_name;
}

CGUnit_C::CGUnit_C(uint32_t time, CClientObjCreate& objCreate)
    : CGObject_C(time, objCreate)
    , CGUnit(this->m_localMove)
    , m_localMove(objCreate.move.status.position28, objCreate.move.status.facing34, this->m_obj->m_guid, this)
{
    this->m_mountSound = STORM_NEW(SOUNDKITOBJECT);
    this->m_petSound = STORM_NEW(SOUNDKITOBJECT);
    this->m_speechSound = STORM_NEW(SOUNDKITOBJECT);

    // What each hand holds, from the virtual items (0x0073fbd3 .. 0x0073fc88); later changes come
    // through OnVirtualItemChanged.
    for (int32_t hand = 0; hand < 3; hand++) {
        auto item = g_itemDB.GetRecord(this->m_unit ? this->m_unit->virtualItemSlotID[hand] : 0);

        UNIT_WEAPON_INFO info;

        if (item) {
            this->m_weaponDisplays[hand] = item->m_displayInfoID;
            info.m_class = static_cast<uint8_t>(item->m_classID);
            info.m_subclass = static_cast<uint8_t>(item->m_subclassID);
            info.m_soundOverride = static_cast<uint8_t>(item->m_soundOverrideSubclassID);
            info.m_material = static_cast<uint8_t>(item->m_material);
            info.m_inventoryType = static_cast<uint8_t>(item->m_inventoryType);
            info.m_sheathType = static_cast<uint8_t>(item->m_sheatheType);
        } else {
            this->m_weaponDisplays[hand] = 0;
        }

        this->m_weaponInfo[hand] = info;
    }

    this->m_targetChangeTime = CWorld::GetCurTimeMs() - 1000;

    // TODO

    this->RefreshDataPointers();

    // FUN_0073c330: the movement the create block carries.
    this->PostMovementUpdate(objCreate, 0);

    // TODO
}

CGUnit_C::~CGUnit_C() {
    // FUN_00734b50: the vehicle this unit is goes (its passengers kept where they are), and its
    // own ride with it.
    this->m_stateFlags &= 0xfeffffff;
    UnitDestroyVehicle(this, 0);
    UnitDestroyVehicleCamera(this);

    if (this->m_vehiclePassenger) {
        this->m_vehiclePassenger->Free();
        this->m_vehiclePassenger = nullptr;
    }

    if (this->m_mountSound) {
        SI2::StopOrFadeOut(this->m_mountSound, 1, 0.0f, 1);
        this->m_mountSound->~SOUNDKITOBJECT();
        STORM_FREE(this->m_mountSound);
        this->m_mountSound = nullptr;
    }

    for (auto sound : { &this->m_petSound, &this->m_speechSound }) {
        if (*sound) {
            SI2::StopOrFadeOut(*sound, 1, 0.0f, 1);
            (*sound)->~SOUNDKITOBJECT();
            STORM_FREE(*sound);
            *sound = nullptr;
        }
    }

    this->m_threatList.Clear();

    // Free the composited body built for a humanoid NPC (players keep their own component in
    // CGPlayer_C, so this only ever fires for NPCs). Units stream in and out with the tiles, so a
    // leak here would grow unbounded.
    if (this->m_characterComponent) {
        CCharacterComponent::FreeComponent(this->m_characterComponent);
        this->m_characterComponent = nullptr;
    }
}

int32_t CGUnit_C::CanHighlight() {
    if (this->m_unit->flags & 0x2000000) {
        if (this->m_unit->createdBy != ClntObjMgrGetActivePlayer() || this->GetGUID() != CGPetInfo::GetPet(0)) {
            return false;
        }
    }

    return true;
}

int32_t CGUnit_C::CanBeTargetted() {
    return this->CanHighlight();
}

// ref: FUN_0074b810
// A passenger aboard this unit joins its vehicle's passengers.
int32_t CGUnit_C::Virtual0F4(CPassenger* passenger, int32_t mode) {
    (void)mode;

    if (!this->m_vehicle) {
        return 0;
    }

    return this->m_vehicle->AddPassenger(passenger);
}

// ref: FUN_004d43c0
int32_t CGUnit_C::IsActiveMover() const {
    if (this->GetGUID() == CGUnit_C::s_activeMover) {
        return 1;
    }

    return 0;
}

// ref: FUN_00616b10
int32_t CGUnit_C::GetDisplayID() const {
    // Prefer local display ID if set and unit's display ID hasn't been overridden from unit's
    // native display ID.
    //
    // The middle test used to read `this->GetDisplayID()`, which is THIS function: unbounded
    // recursion and a stack overflow. It never fired only because `m_localDisplayID` is still
    // initialised to 0 and nothing writes it, so the `&&` short-circuits before reaching it -- a
    // landmine rather than a live crash, and one that goes off the moment anything sets a local
    // display id, which is what the reference uses for transform and shapeshift effects.
    //
    // The comparison the reference makes is between the OBJECT's display id and its native one
    // (FUN_00717a20 reads them at unit data +0xf4 and +0xf8), so the base accessor is what belongs
    // here.
    // The branch below is still DEAD, separately from the recursion that used to be in it:
    // m_localDisplayID is never written, so GetLocalDisplayID() is always 0 and the && never gets
    // past its first term. Porting whatever sets a local display id is what makes this live.
    // tools/deaddata.py reports it under DEAD GUARDS, through the accessor.
    if (this->GetLocalDisplayID() && this->CGUnit::GetDisplayID() == this->GetNativeDisplayID()) {
        return this->GetLocalDisplayID();
    }

    return this->CGUnit::GetDisplayID();
}

float CGUnit_C::GetFacing() const {
    return this->CGUnit::GetFacing();
}

int32_t CGUnit_C::GetLocalDisplayID() const {
    return this->m_localDisplayID;
}

// Resolve this unit to its CreatureModelData record, through CreatureDisplayInfo. The reference
// picks the display id the same way GetDisplayID above does -- the cached local one unless the
// object's display id has been overridden away from its native one -- and reports the same two
// failures, which are the strings this function already carries as comments:
// NOCREATUREDISPLAYIDFOUND when the display id resolves to nothing, and INVALIDDISPLAYMODELRECORD
// when it does but its m_modelID does not.
// ref: FUN_00717a20
CreatureModelDataRec* CGUnit_C::GetModelData() const {
    auto displayID = this->GetDisplayID();

    auto creatureDisplayInfoRec = g_creatureDisplayInfoDB.GetRecord(displayID);

    if (!creatureDisplayInfoRec) {
        // TODO SysMsgPrintf(1, 2, "NOCREATUREDISPLAYIDFOUND|%d", displayID);
        return nullptr;
    }

    auto creatureModelDataRec = g_creatureModelDataDB.GetRecord(creatureDisplayInfoRec->m_modelID);

    if (!creatureModelDataRec) {
        // TODO SysMsgPrintf(1, 16, "INVALIDDISPLAYMODELRECORD|%d|%d", creatureDisplayInfoRec->m_modelID, creatureDisplayInfoRec->m_ID);
        return nullptr;
    }

    return creatureModelDataRec;
}

int32_t CGUnit_C::GetModelFileName(const char*& name) const {
    auto modelDataRec = this->GetModelData();

    // Model data not found
    if (!modelDataRec) {
        name = "Spells\\ErrorCube.mdx";

        return true;
    }

    name = modelDataRec->m_modelName;

    return modelDataRec->m_modelName ? true : false;
}

float CGUnit_C::GetModelScale() const {
    // The reference scales a creature by its display's model scale times the model data's scale
    auto disp = g_creatureDisplayInfoDB.GetRecord(this->GetDisplayID());

    if (!disp) {
        return 1.0f;
    }

    float scale = disp->m_creatureModelScale;

    auto model = g_creatureModelDataDB.GetRecord(disp->m_modelID);

    if (model) {
        scale *= model->m_modelScale;
    }

    return scale > 0.0f ? scale : 1.0f;
}


C3Vector CGUnit_C::GetPosition() const {
    return this->CGUnit::GetPosition();
}

float CGUnit_C::GetRawFacing() const {
    return this->CGUnit::GetRawFacing();
}

float CGUnit_C::GetRawSmoothFacing() const {
    return this->m_smoothFacing;
}

WOWGUID CGUnit_C::GetTransportGUID() const {
    return this->m_localMove.GetTransportGUID();
}

namespace {

// DAT_00ca1248 / DAT_00ca1250: an SMSG_CONTROL_UPDATE for a unit not yet in view, which its
// PostInit takes up (FUN_00716060 stores it).
WOWGUID s_pendingControl = 0;
bool s_pendingControlHas = false;

} // namespace

// ref: FUN_0073fcc0
// PARTIAL: the movement start (FUN_006ea520, FUN_0074d070, FUN_0074b380), the stand state's posture (FUN_006e9920), the name plate (FUN_007e5f60), the
// party and raid slots (FUN_005139b0, FUN_0054d1c0) and the vehicle and passenger starts are the
// Movement, PlayerName and party ports'.
void CGUnit_C::PostInit(uint32_t time, const CClientObjCreate& init, bool a4) {
    this->m_displayScale = this->GetDisplayScale(this->m_unit->displayID);

    this->UpdateCollisionBox(1, 1);

    if (auto race = g_chrRacesDB.GetRecord(static_cast<uint8_t>(this->m_unit->bytes0))) {
        this->m_splashSoundID = race->m_splashSoundID;
    }

    this->UpdateShadowRadius();
    this->UpdateEffectAttachments();

    this->CGObject_C::PostInit(time, init, a4);

    if (this->m_displayInfo) {
        CCharacterComponent::ApplyMonsterGeosets(this->m_model, this->m_displayInfo);
        CCharacterComponent::ReplaceMonsterSkin(this->m_model, this->m_displayInfo, this->m_modelData);

        if (this->m_modelData) {
            this->m_model->m_flag4 = (this->m_modelData->m_flags & 0x200) ? true : false;
        }
    }

    this->m_mountDisplayID = this->m_unit->mountDisplayID;

    if (this->m_modelData) {
        this->m_footprintTexture = this->m_modelData->m_footprintTextureID;
        this->m_footprintLength = this->m_modelData->m_footprintTextureWidth * 0.02777777798473835f;
        this->m_footprintWidth = 0.02777777798473835f * this->m_modelData->m_footprintTextureLength;
        this->m_footprintParticleScale = this->m_modelData->m_footprintParticleScale;
    }

    this->UpdateMountSound();

    // The component is built once the model is in (GetHidden -> BuildComponent).
    if (this->m_characterComponent) {
        CCharacterComponent::FreeComponent(this->m_characterComponent);
        this->m_characterComponent = nullptr;
    }

    this->m_stateFlags = (this->m_stateFlags & ~0x20000u) | 0x400000;

    this->PlaceModel(0.0f);

    if (this->m_unit->health < 1) {
        this->StopAllEffects(0);
    } else if (0 < this->m_unit->mountDisplayID) {
        this->Mount(1, 1);
    }

    this->m_smoothFacing = CMath::normalizeangle0to2pi(this->GetRawFacing());
    this->m_smoothFacingStep = 0.0f;
    this->m_smoothFacingHistory[0] = 0.0f;
    this->m_smoothFacingHistory[1] = 0.0f;
    this->m_smoothFacingHistory[2] = 0.0f;
    this->m_smoothFacingHistory[3] = 0.0f;
    this->m_lowerBodyFacing = this->GetRawFacing();
    this->m_lowerBodyFacingStep = 0.0f;
    this->m_lowerBodyBlend = 1.0f;

    this->UpdateObjectEffectPackage();

    // 0x007401c5: an SMSG_CONTROL_UPDATE that arrived before the unit did.
    if (s_pendingControl == this->GetGUID()) {
        this->OnControlUpdate(s_pendingControl, s_pendingControlHas);
        s_pendingControl = 0;
    }

    // TODO
}

// ref: FUN_0073c260
// A movement block from the server: the create's, or SMSG_MOVE_UPDATE's for a unit someone else
// moves. The active mover keeps its own prediction and takes nothing from it.
// NOT PORTED: FUN_0098e560 (the block's guid at +0x2b8 copied into a target the decompiler lost),
// and the active player's position copied into DAT_00cd7544 +0x17c for update flag 1.
void CGUnit_C::PostMovementUpdate(const CClientObjCreate& init, int32_t activeMover) {
    // Update flag 0x80: the unit is a vehicle.
    if (init.flags & 0x80) {
        UnitCreateVehicle(this, &init, static_cast<int32_t>(init.uint2C4));
    }

    if (activeMover) {
        return;
    }

    this->m_localMove.InitFromCreate(static_cast<int32_t>(OsGetAsyncTimeMs()), init.move, init.flags & 0x1);

    if (this->m_localMove.m_moveFlags & 0x2000) {
        this->UpdateFallAnimation();
    }

    if (init.flags & 0x1) {
        this->m_stateFlags |= 0x80;
    }

    if (init.flags & 0x400) {
        this->m_stateFlags |= 0x40000000;
    }
}

// The half-extent of the CURRENT animation's authored box, which is what the blob shadow uses as
// the caster footprint -- that is why a footprint grows as something rears up and shrinks as it
// crouches, instead of being a fixed circle.
//
// This used to walk m_data->sequences itself, matching the applied id against sequences[i].id, and
// returned 0 when no sequence carried that id. That is wrong in the case that matters: a model
// frequently does NOT carry the animation it is asked for, and the reference does not give up when
// that happens -- CM2Model::GetSequenceInfo (FUN_0082ced0) walks the model's fallback chain first
// and reports the box of the sequence the model WOULD actually play. The hand-rolled loop skipped
// the chain, so every such caster silently fell back to the model's global box and drew a footprint
// that did not follow the animation at all.
//
// Using GetSequenceInfo also gives it its first caller in frozen. It was ported and correct but
// unreached, which is how tools/deaddata.py came to report M2SequenceInfo::extent, center and
// moveSpeed as written-and-never-read; extent is now read.
//
// Variation 0, deliberately and not as a guess: CGUnit_C tracks the animation id it applied but
// not which variation the model chose, and Sub8260C0 counts variations along a chain whose head is
// 0. Variations of one animation are alternate takes of the same motion, so their authored boxes
// agree closely, and variation 0 is the one that exists whenever the animation does.
// The box the reference projects as this unit's blob shadow. It is the CreatureModelData
// geoBox -- NOT the model's own bounds and not an animated box -- raised to the mount's height
// when the unit is riding something.
//
// Offsets, all confirmed against frozen's generated record rather than assumed: the reference
// copies six floats from the model record at +0x44, which is m_geoBoxMinX through m_geoBoxMaxZ in
// the 32-bit layout, and reads the mount height at +0x40, which is m_mountHeight immediately
// before them. The gate is `unit data +0xfc > 0`, and +0xfc is the field right after displayID and
// nativeDisplayID, which is mountDisplayID -- so the branch is simply "is this unit mounted".
//
// The shift centres the box on the mount height rather than offsetting by it:
// `mountHeight - halfZ` added to both Z components moves the box so its middle sits at the height
// the mount carries the rider. That is what keeps a mounted player's shadow the right size and in
// the right place instead of sunk into the ground.
//
// ONE SUBSTITUTION, stated because it is not a straight port: the reference looks the MOUNT's
// record up from a cached field at unit +0x9c0 whose frozen counterpart has not been identified.
// mountDisplayID is used instead, which is the id the gate itself tests and reaches the same
// record through the same two-level chain. If +0x9c0 turns out to hold something else, the height
// this picks is wrong -- but the gate would still be right.
//
// Nothing calls this yet. It is the missing half of the reference's unit shadow path: the blob
// caster in CGWorldFrame currently sizes from GetAnimFootprint below, which is a different box
// from a different source. Wiring them together changes what is drawn and wants a run.
// ref: FUN_0071ed80
CAaBox& CGUnit_C::GetShadowBox(CAaBox& box) const {
    box.b = { 0.0f, 0.0f, 0.0f };
    box.t = { 0.0f, 0.0f, 0.0f };

    auto rec = this->m_modelData ? this->m_modelData : this->GetModelData();

    if (rec) {
        box.b = { rec->m_geoBoxMinX, rec->m_geoBoxMinY, rec->m_geoBoxMinZ };
        box.t = { rec->m_geoBoxMaxX, rec->m_geoBoxMaxY, rec->m_geoBoxMaxZ };
    }

    int32_t mountDisplayID = this->Unit()->mountDisplayID;

    if (mountDisplayID > 0) {
        auto mountDisplay = g_creatureDisplayInfoDB.GetRecord(mountDisplayID);
        auto mountModel = mountDisplay
            ? g_creatureModelDataDB.GetRecord(mountDisplay->m_modelID)
            : nullptr;

        if (mountModel) {
            float shift = mountModel->m_mountHeight - (box.t.z - box.b.z) * 0.5f;

            box.b.z += shift;
            box.t.z += shift;
        }
    }

    return box;
}

float CGUnit_C::GetAnimFootprint() const {
    auto model = this->m_model;

    if (!model || !model->m_shared || !model->m_shared->m_m2DataLoaded || !model->m_shared->m_data) {
        return 0.0f;
    }

    int32_t animID = static_cast<int32_t>(this->GetCurrentAnimationId());

    if (animID < 0) {
        return 0.0f;
    }

    // GetSequenceInfo blocks in WaitForLoad when the model itself is not loaded yet. This runs
    // inside the per-frame shadow pass, so take the miss and let the caller use the model box
    // rather than stall a frame on a disc read.
    if (!model->m_loaded) {
        return 0.0f;
    }

    M2SequenceInfo info = {};
    model->GetSequenceInfo(static_cast<uint32_t>(animID), 0, info);

    const CAaBox& e = info.extent;
    float ex = (e.t.x - e.b.x) * 0.5f;
    float ey = (e.t.y - e.b.y) * 0.5f;
    float extent = ex > ey ? ex : ey;

    // Degenerate bounds are common, and GetSequenceInfo also reports an all-zero box when the
    // model has no such variation. Both mean the same thing here: let the caller fall back.
    return extent > 0.01f ? extent : 0.0f;
}

// ref: FUN_0071af70
uint8_t CGUnit_C::GetShapeshiftForm() const {
    auto data = this->Unit();

    return data ? static_cast<uint8_t>((data->bytes2 >> 24) & 0xFF) : 0;
}

// ref: FUN_00719950
const char* CGUnit_C::GetSubName() const {
    auto data = this->Unit();

    // A pet has no title whatever it was tamed from, gated on the descriptor's pet number exactly
    // as the classification is.
    if (!data || data->petNumber != 0) {
        return nullptr;
    }

    auto info = this->m_creatureStats;

    if (!info || !info->m_subName || !info->m_subName[0]) {
        return nullptr;
    }

    return info->m_subName;
}

// ref: FUN_0071f300
int32_t CGUnit_C::GetCreatureType() const {
    // A shapeshifted unit counts as whatever the form says: a druid in Bear Form is a Beast, not
    // a Humanoid. Forms that declare no type (Ambient, and the several stance rows) leave the
    // answer to the creature template below -- which is why the >= 1 test is here rather than a
    // plain null check.
    //
    // DIVERGENCE: the reference skips this branch entirely when a byte at CGUnit_C +0x9f4 is set,
    // and reads the cached template instead. FUN_0071f300 is the only code in the image that
    // touches that byte -- nothing writes it that a displacement scan can find -- so there was no
    // behaviour to port. Frozen acts as though it is clear, which is the reference's own path for
    // every unit whose flag was never set.
    auto form = g_spellShapeshiftFormDB.GetRecord(this->GetShapeshiftForm());

    if (form && form->m_creatureType >= 1) {
        return form->m_creatureType;
    }

    auto info = this->m_creatureStats;

    if (info) {
        return info->m_type;
    }

    // No creature template means a player, whose type comes from its race instead.
    auto data = this->Unit();
    auto race = data ? g_chrRacesDB.GetRecord(static_cast<int32_t>(data->bytes0 & 0xFF)) : nullptr;

    return (race && race->m_creatureType >= 1) ? race->m_creatureType : 0;
}

// ref: FUN_007153e0
int32_t CGUnit_C::GetCreatureFamily() const {
    auto info = this->m_creatureStats;

    return info ? info->m_family : 0;
}

// ref: FUN_00718a00
int32_t CGUnit_C::GetClassification() const {
    auto data = this->Unit();

    // A pet reports normal whatever it was tamed from, so taming an elite does not hand the player
    // an elite pet frame. The reference gates on the descriptor's pet number for exactly this.
    if (!data || data->petNumber != 0) {
        return 0;
    }

    auto info = this->m_creatureStats;

    return info ? info->m_rank : 0;
}

// ref: FUN_0073ab90
// A one-shot emote (SMSG_EMOTE, or the player's own): its animation plays unless it is already
// playing, the unit channels, or it is busy (animation flag 0x400) -- and a unit in combat only
// plays the two emotes meant for it (0xc0 and 0xc8).
void CGUnit_C::PlayEmote(uint32_t emoteID) {
    auto emote = g_emotesDB.GetRecord(static_cast<int32_t>(emoteID));

    if (!emote) {
        return;
    }

    int32_t animID = emote->m_animID;

    if (animID < 0 || 0x1FA <= animID || static_cast<int32_t>(this->GetCurrentAnimationId()) == animID) {
        return;
    }

    bool combatEmote = animID == 0xC0 || animID == 200;

    if (this->m_unit->channelSpell == 0 && !(this->m_animFlags & 0x400)
        && (combatEmote || !this->IsAttackingOrPetInCombat())) {
        this->SetAnimation(static_cast<uint32_t>(animID), 0);
    }
}

void CGUnit_C::RefreshDataPointers() {
    auto displayID = this->GetDisplayID();

    // Display info

    this->m_displayInfo = g_creatureDisplayInfoDB.GetRecord(displayID);

    if (!this->m_displayInfo) {
        // TODO auto name = this->GetUnitName(0, 1);
        // TODO SysMsgPrintf(2, 2, "NOUNITDISPLAYID|%d|%s", displayID, name);

        this->m_displayInfo = g_creatureDisplayInfoDB.GetRecordByIndex(0);

        if (!this->m_displayInfo) {
            STORM_APP_FATAL("Error, NO creature display records found");
        }
    }

    // Display info extra

    this->m_displayInfoExtra = g_creatureDisplayInfoExtraDB.GetRecord(this->m_displayInfo->m_extendedDisplayInfoID);

    // Model data

    this->m_modelData = g_creatureModelDataDB.GetRecord(this->m_displayInfo->m_modelID);

    // Sound data

    this->m_soundData = g_creatureSoundDataDB.GetRecord(this->m_displayInfo->m_soundID);

    if (!this->m_soundData) {
        this->m_soundData = g_creatureSoundDataDB.GetRecord(this->m_modelData->m_soundID);
    }

    // Blood levels

    this->m_bloodRec = g_unitBloodLevelsDB.GetRecord(this->m_displayInfo->m_bloodID);

    if (!this->m_bloodRec) {
        this->m_bloodRec = g_unitBloodLevelsDB.GetRecord(this->m_modelData->m_bloodID);

        if (!this->m_bloodRec) {
            this->m_bloodRec = g_unitBloodLevelsDB.GetRecordByIndex(0);
        }
    }

    // Creature stats. A unit that is not something more (a player) has a creature template, and
    // the cache is asked for it with the callback that installs it when it lands.
    if (this->GetType() == HIER_TYPE_UNIT) {
        WOWGUID guid = this->GetGUID();

        this->m_creatureStats = g_creatureCache.GetRecord(
            DBCACHEKEY32(static_cast<uint32_t>(this->GetEntryID())), &guid,
            &CGUnit_C::OnCreatureStatsArrived, nullptr, false);
    }

    // Flags

    // TODO set flags
}

void CGUnit_C::SetStorage(uint32_t* storage, uint32_t* saved) {
    this->CGObject_C::SetStorage(storage, saved);

    this->m_unit = reinterpret_cast<CGUnitData*>(&storage[CGUnit::GetBaseOffset()]);
    this->m_unitSaved = &saved[CGUnit::GetBaseOffsetSaved()];
}

// SMSG_EMOTE (0x103): uint32 emoteID, then the caster's guid as a plain 8-byte value.
//
// The opcode was declared in src/net/Types.hpp and never handled, so every scripted gesture a
// creature makes -- the Lich King planting his sword, a guard saluting -- was read off the wire and
// dropped. UNIT_NPC_EMOTESTATE was already handled; that is the looping pose, not this.
int32_t ReceiveEmote(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    if (!msg) {
        return 1;
    }

    uint32_t emoteID = 0;
    uint64_t guid = 0;

    msg->Get(emoteID);
    msg->Get(guid);

    if (!emoteID || !guid) {
        return 1;
    }

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, __FILE__, __LINE__));

    if (unit && static_cast<uint8_t>(unit->Unit()->bytes1) != 3 && !(unit->m_localMove.m_moveFlags & 0x200000)) {
        unit->PlayEmote(emoteID);
    }

    return 1;
}

// ref: FUN_00990370
bool IsMovementAckOpcode(int32_t opcode) {
    switch (opcode) {
        case 0x0E9:
        case 0x0EB:
        case 0x0C7:
        case 0x0E3:
        case 0x0E5:
        case 0x0E7:
        case 0x2DD:
        case 0x382:
        case 0x384:
        case 0x2DB:
        case 0x2DF:
        case 0x45D:
        case 0x0F6:
        case 0x345:
        case 0x2CF:
        case 0x2D0:
        case 0x0F0:
        case 0x4CF:
        case 0x4D1:
        case 0x340:
        case 0x517:
            return true;

        default:
            return false;
    }
}

// ref: FUN_00990420
bool IsMovementAckOrNotActiveMoverOpcode(int32_t opcode) {
    switch (opcode) {
        case 0x0E9:
        case 0x0EB:
        case 0x0C7:
        case 0x0E3:
        case 0x0E5:
        case 0x0E7:
        case 0x2DD:
        case 0x382:
        case 0x384:
        case 0x2DB:
        case 0x2DF:
        case 0x45D:
        case 0x0F6:
        case 0x345:
        case 0x2CF:
        case 0x2D0:
        case 0x0F0:
        case 0x2D1:
        case 0x4CF:
        case 0x4D1:
        case 0x340:
        case 0x517:
            return true;

        default:
            return false;
    }
}

// ref: FUN_009904e0
bool IsMovementStateOpcode(int32_t opcode) {
    switch (opcode) {
        case 0x0CA:
        case 0x0CB:
        case 0x0D9:
        case 0x2D8:
        case 0x2D9:
        case 0x346:
        case 0x46D:
        case 0x49B:
            return true;

        default:
            return false;
    }
}

// ref: FUN_00715f70
uint32_t CGUnit_C::GetCreatureTypeFlag11() const {
    auto info = this->m_creatureStats;

    if (info) {
        return (info->m_typeFlags >> 11) & 1;
    }

    return 0;
}

// ref: FUN_00715f90
uint32_t CGUnit_C::GetCreatureTypeFlag12() const {
    auto info = this->m_creatureStats;

    if (info) {
        return (info->m_typeFlags >> 12) & 1;
    }

    return 0;
}

// ref: FUN_00715df0
uint32_t CGUnit_C::GetCreatureTypeFlag26() const {
    auto info = this->m_creatureStats;

    if (info) {
        return (info->m_typeFlags >> 26) & 1;
    }

    return 0;
}

// ref: FUN_00715e50
int32_t CGUnit_C::GetCreatureSkinningType() const {
    auto info = this->m_creatureStats;

    if (info) {
        if (info->m_typeFlags & 0x100) {
            return 1;
        }

        if (info->m_typeFlags & 0x200) {
            return 2;
        }

        if (info->m_typeFlags & 0x8000) {
            return 3;
        }
    }

    return 0;
}

// ref: FUN_007151f0
bool IsMovementAckWithValueOpcode(int32_t opcode) {
    if (opcode != 0x0E3 && opcode != 0x0E5 && opcode != 0x0E7
        && opcode != 0x2DD && opcode != 0x382 && opcode != 0x384 && opcode != 0x2DB && opcode != 0x2DF
        && opcode != 0x45D
        && opcode != 0x0F6 && opcode != 0x345 && opcode != 0x2CF && opcode != 0x2D0 && opcode != 0x340
        && opcode != 0x517
    ) {
        return false;
    }

    return true;
}

// ref: FUN_00714ce0
void ConvertAnimationFlags(uint32_t src, uint32_t* dst) {
    if (!dst) {
        return;
    }

    if (src & 0x4) {
        *dst |= 0x2;
    }

    if (src & 0x8) {
        *dst |= 0x4;
    }

    if (src & 0x10) {
        *dst |= 0x8;
    }

    if (src & 0x100) {
        *dst |= 0x20;
    }
}

// ref: FUN_00714c80
bool IsTwoHandedWeapon(const uint8_t* weapon) {
    if (weapon && weapon[0] == 2) {
        switch (weapon[1]) {
            case 1:
            case 5:
            case 6:
            case 8:
            case 10:
            case 12:
            case 17:
            case 20:
                return true;
        }
    }

    return false;
}

// ref: FUN_00714e80
bool IsMovementAnimation(uint32_t animID) {
    switch (animID) {
        case 0x04:
        case 0x05:
        case 0x0B:
        case 0x0C:
        case 0x0D:
        case 0x25:
        case 0x26:
        case 0x27:
        case 0x2A:
        case 0x2B:
        case 0x2C:
        case 0x2D:
        case 0x77:
        case 0x87:
        case 0x8F:
        case 0xBB:
        case 0xDF:
            return true;

        default:
            return false;
    }
}

// ref: FUN_007152b0
void ReadPackedMovementOffset(CDataStore* msg, const C3Vector& base, C3Vector& out) {
    uint32_t packed;
    msg->Get(packed);

    float y = static_cast<float>(static_cast<int32_t>((packed >> 11) << 21) >> 21) * 0.25f;
    float z = static_cast<float>(static_cast<int32_t>(packed) >> 22) * 0.25f;

    out.x = base.x - static_cast<float>(static_cast<int32_t>(packed << 21) >> 21) * 0.25f;
    out.y = base.y - y;
    out.z = base.z - z;
}

// ref: FUN_00715d00
int32_t AdjustSheathState(int32_t state, const uint8_t* first, const uint8_t* second) {
    if (state == 1) {
        if (!first && (!second || second[4] == 0x17)) {
            return 0;
        }
    } else if (state == 0) {
        if ((second && second[4] == 0x0E) || (first && first[0] == 2) || (second && second[0] == 2)) {
            state = 1;
        }
    }

    return state;
}

// ref: FUN_00743320
void CGUnit_C::SetFlag21() {
    this->m_flag21 = 1;
}

// ref: FUN_0074b9a0
// Reads unit +0x7d0, which is +0x48 of the embedded movement block at +0x788: the low bit of the
// second movement flag word.
uint8_t CGUnit_C::HasMoveFlags2Bit0() const {
    return this->m_localMove.GetMoveFlags2() & 1;
}

// ref: FUN_00716710
// Unit flag 0x2 clear with any of 0xc00004 set answers no outright. Without flag 0x1000000 the unit
// itself must be an uncharmed player; with it, the charmer (or the creator, when nothing charms it)
// must be. Either way the player must not carry unit flag 0x1.
bool CGUnit_C::IsPlayerControlled() const {
    uint32_t flags = this->m_unit->flags;

    if (!(flags & 0x2) && (flags & 0xC00004)) {
        return false;
    }

    auto data = this->m_unit;

    if (!(data->flags & 0x1000000)) {
        if (!this->IsA(TYPE_PLAYER)) {
            return false;
        }

        if (data->charmedBy != 0) {
            return false;
        }

        return !(data->flags & 0x1);
    }

    const WOWGUID& controllerGUID = data->charmedBy != 0 ? data->charmedBy : data->createdBy;
    auto controller = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(controllerGUID, TYPE_UNIT, __FILE__, __LINE__));

    if (!controller) {
        return false;
    }

    if (!controller->IsA(TYPE_PLAYER)) {
        return false;
    }

    return !(controller->m_unit->flags & 0x1);
}

// ref: FUN_00716fa0
// The reference reads the walk speed at CGUnit_C +0x818 and the move flags at +0x7cc, which are
// +0x90 and +0x44 of the movement block the unit embeds (m_localMove): the offsets CMovementShared
// documents for m_walkSpeed and m_moveFlags. Ghidra drops GetCurrentSpeed's receiver; it is taken
// as that same block, which is also what m_move points at in frozen.
bool CGUnit_C::IsMovingAtWalkPace() const {
    return this->m_localMove.GetCurrentSpeed(0) <= this->m_localMove.GetWalkSpeed() + this->m_localMove.GetWalkSpeed();
}

// ref: FUN_00718a90
// Players answer 0; a creature whose template has not arrived answers 1.
uint32_t CGUnit_C::GetCreatureTypeFlag10() const {
    if (this->IsA(TYPE_PLAYER)) {
        return 0;
    }

    auto info = this->m_creatureStats;

    if (!info) {
        return 1;
    }

    return (info->m_typeFlags >> 10) & 1;
}

// ref: FUN_00718b70
// The player behind this unit: the unit's charmer (or creator), or the unit itself when it has
// neither, if that is a player; otherwise that one's own charmer (or creator), if a player.
CGUnit_C* CGUnit_C::GetControllingPlayer() {
    CGUnit_C* unit = this;
    auto data = this->m_unit;
    const WOWGUID& ownerGUID = data->charmedBy != 0 ? data->charmedBy : data->createdBy;

    if (ownerGUID != 0) {
        const WOWGUID& guid = data->charmedBy != 0 ? data->charmedBy : data->createdBy;
        unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, __FILE__, __LINE__));
    }

    if (unit) {
        if (unit->IsA(TYPE_PLAYER)) {
            return unit;
        }

        auto unitData = unit->m_unit;
        const WOWGUID& guid = unitData->charmedBy != 0 ? unitData->charmedBy : unitData->createdBy;
        auto owner = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, __FILE__, __LINE__));

        if (owner && owner->IsA(TYPE_PLAYER)) {
            return owner;
        }
    }

    return nullptr;
}

// ref: FUN_0071b770
// Base value by power type: mana the descriptor's base mana, health (-2) its base health.
int32_t CGUnit_C::GetBasePower(int32_t powerType) const {
    switch (powerType) {
        case 0:
            return this->m_unit->baseMana;

        case 1:
        case 6:
            return 1000;

        case 2:
        case 3:
            return 100;

        case 5:
            return 1;

        case -2:
            return this->m_unit->baseHealth;

        default:
            return 0;
    }
}

// ref: FUN_0071b810
// Move flag 0x1000000 of the embedded movement block (see IsMovingAtWalkPace).
uint32_t CGUnit_C::CanFly() const {
    return this->m_localMove.GetMoveFlags() & 0x1000000;
}

// ref: FUN_0071c500
// Not the active player, and a player whose model data carries flag 0x4 and whose extended display
// carries flag 0x1 -- or has no extended display at all.
bool CGUnit_C::IsOtherPlayerWithFlaggedModel() const {
    WOWGUID guid = this->GetGUID();

    if (ClntObjMgrGetActivePlayer() != guid) {
        bool player = this->IsA(TYPE_PLAYER);

        if (player && this->m_modelData && (this->m_modelData->m_flags & 0x4)
            && this->m_displayInfoExtra && (this->m_displayInfoExtra->m_flags & 0x1)
        ) {
            return true;
        }

        if (!this->m_displayInfoExtra && player && (this->m_modelData->m_flags & 0x4)) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0071c570
// NPC flag 0x1000000 (spell click) without unit flag2 0x2000; with flag2 0x1000 only for a party or
// raid member or pet.
bool CGUnit_C::IsSpellClickable() const {
    if ((this->m_unit->npcFlags & 0x1000000) && !(this->m_unit->flags2 & 0x2000)) {
        if (this->m_unit->flags2 & 0x1000) {
            if (!CGPartyInfo::IsMemberOrPet(this->GetGUID()) && !CGRaidInfo::IsMemberOrPet(this->GetGUID())) {
                return false;
            }
        }

        return true;
    }

    return false;
}

// ref: FUN_0071c8b0
bool CGUnit_C::IsMovingFasterThanWalkPace() const {
    if (this->m_localMove.GetCurrentSpeed(0) <= this->m_localMove.GetWalkSpeed() + this->m_localMove.GetWalkSpeed()) {
        return false;
    }

    return true;
}

// ref: FUN_00721ca0
// In a shapeshift form whose SpellShapeshiftForm row lacks flag 0x1 (a stance). Like the reference,
// this reads the row without a null check.
//
// DIVERGENCE: the reference answers false when the byte at CGUnit_C +0x9f4 is set; nothing that
// writes it is ported, so frozen treats it as clear (as GetCreatureType does).
bool CGUnit_C::IsInNonStanceForm() const {
    uint32_t form = (this->m_unit->bytes2 >> 24) & 0xFF;

    if (form == 0) {
        return false;
    }

    auto formRec = g_spellShapeshiftFormDB.GetRecord(static_cast<int32_t>(form));

    return !(formRec->m_flags & 0x1);
}

// ref: FUN_00717540
bool ResolveAnimationBehavior(int32_t animID, int32_t tier, int32_t* out) {
    if (animID < g_animationDataDB.GetNumRecords()) {
        if (tier == 0 && animID >= 0) {
            auto rec = g_animationDataDB.GetRecordByIndex(animID);

            if (rec && rec->m_behaviorID == animID && rec->m_behaviorTier == 0) {
                *out = rec->m_ID;
                return true;
            }
        }

        auto rec = g_animationDataDB.GetRecord(animID);

        if (rec && rec->m_behaviorTier != 0) {
            *out = animID;
            return true;
        }

        for (int32_t i = 0; i < g_animationDataDB.GetNumRecords(); i++) {
            auto row = g_animationDataDB.GetRecordByIndex(i);

            if (row->m_behaviorID == animID && row->m_behaviorTier == tier) {
                *out = row->m_ID;
                return true;
            }
        }
    }

    return false;
}

// ref: FUN_007176b0
int32_t GetAnimationBehavior(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        return rec->m_behaviorID;
    }

    return 0x1FA;
}

// ref: FUN_00718b30
bool FactionHasReputation(int32_t factionID) {
    auto rec = g_factionDB.GetRecord(factionID);

    if (rec) {
        return rec->m_reputationIndex >= 0;
    }

    return false;
}

// ref: FUN_0071c050
float GetNativeRaceModelScale(const CreatureDisplayInfoRec* display) {
    auto extra = g_creatureDisplayInfoExtraDB.GetRecord(display->m_extendedDisplayInfoID);

    if (extra) {
        auto race = g_chrRacesDB.GetRecord(extra->m_displayRaceID);

        if (race) {
            int32_t displayID = 0;

            if (extra->m_displaySexID == 0) {
                displayID = race->m_maleDisplayID;
            } else if (extra->m_displaySexID == 1) {
                displayID = race->m_femaleDisplayID;
            }

            auto native = g_creatureDisplayInfoDB.GetRecord(displayID);

            if (native) {
                return native->m_creatureModelScale;
            }
        }
    }

    return 1.0f;
}

// ref: FUN_0071d2a0
// 39 JumpEnd, 187 JumpLandRun.
bool IsJumpLandAnimation(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;

        if (behavior == 0x27 || behavior == 0xBB) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0071d2e0
// 46 AttackBow, 49 AttackRifle, 105 through 112.
bool IsRangedAttackAnimation(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);
    int32_t behavior = rec ? rec->m_behaviorID : 0x1FA;

    switch (behavior) {
        case 0x2E:
        case 0x31:
        case 0x69:
        case 0x6A:
        case 0x6B:
        case 0x6C:
        case 0x6D:
        case 0x6E:
        case 0x6F:
        case 0x70:
            return true;

        default:
            return false;
    }
}

// ref: FUN_0071d380
// 2 Spell, 32 SpellCast, 33 SpellCastArea, 53 SpellCastDirected, 54 SpellCastOmni.
bool IsSpellCastAnimation(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);
    int32_t behavior = rec ? rec->m_behaviorID : 0x1FA;

    switch (behavior) {
        case 0x02:
        case 0x20:
        case 0x21:
        case 0x35:
        case 0x36:
            return true;

        default:
            return false;
    }
}

// ref: FUN_0071d410
// 51 ReadySpellDirected, 52 ReadySpellOmni.
bool IsReadySpellAnimation(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;

        if (behavior > 0x32 && behavior < 0x35) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0071d450
// 16 AttackUnarmed, 20 ParryUnarmed, 25 ReadyUnarmed, 117, 118.
bool IsUnarmedAnimation(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);
    int32_t behavior = rec ? rec->m_behaviorID : 0x1FA;

    switch (behavior) {
        case 0x10:
        case 0x14:
        case 0x19:
        case 0x75:
        case 0x76:
            return true;

        default:
            return false;
    }
}

// ref: FUN_0071d550
// 57 Special1H, 58 Special2H, 118.
bool IsSpecialAttackAnimation(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;

        if (behavior > 0x38 && (behavior < 0x3B || behavior == 0x76)) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0071d590
bool IsCombatAnimation(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);
    int32_t behavior = rec ? rec->m_behaviorID : 0x1FA;

    switch (behavior) {
        case 0x0A:
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x1E:
        case 0x24:
        case 0x39:
        case 0x3A:
        case 0x3B:
        case 0x55:
        case 0x56:
        case 0x57:
        case 0x58:
        case 0x5F:
        case 0x75:
        case 0x76:
        case 0xAA:
        case 0xAB:
        case 0xAC:
        case 0xAD:
        case 0xAE:
        case 0xAF:
        case 0xB0:
        case 0xB1:
        case 0xB2:
        case 0xB3:
        case 0xD4:
            return true;

        default:
            return false;
    }
}

// ref: FUN_0071d6b0
bool IsBaseStateAnimation(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        switch (rec->m_behaviorID) {
            case 0x00:
            case 0x01:
            case 0x03:
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x08:
            case 0x09:
            case 0x0A:
            case 0x0B:
            case 0x0C:
            case 0x0D:
            case 0x25:
            case 0x26:
            case 0x27:
            case 0x28:
            case 0x29:
            case 0x2A:
            case 0x2B:
            case 0x2C:
            case 0x2D:
            case 0x5C:
            case 0x5D:
            case 0x5E:
            case 0x5F:
            case 0x77:
            case 0x78:
            case 0x7F:
            case 0x83:
            case 0x84:
            case 0x87:
            case 0x8F:
            case 0xBB:
            case 0xC1:
                return true;
        }
    }

    return false;
}

// ref: FUN_0071d7c0
// 37 JumpStart, 38 Jump, 39 JumpEnd, 187 JumpLandRun.
bool IsJumpAnimation(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;

        if (behavior > 0x24 && (behavior < 0x28 || behavior == 0xBB)) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0071d800
bool IsActionAnimation(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);
    int32_t behavior = rec ? rec->m_behaviorID : 0x1FA;

    switch (behavior) {
        case 0x02:
        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0E:
        case 0x0F:
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1A:
        case 0x1B:
        case 0x1C:
        case 0x1D:
        case 0x1E:
        case 0x1F:
        case 0x20:
        case 0x21:
        case 0x22:
        case 0x23:
        case 0x24:
        case 0x2E:
        case 0x2F:
        case 0x30:
        case 0x31:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
        case 0x38:
        case 0x39:
        case 0x3A:
        case 0x3B:
        case 0x3C:
        case 0x3D:
        case 0x3E:
        case 0x3F:
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43:
        case 0x44:
        case 0x45:
        case 0x46:
        case 0x47:
        case 0x48:
        case 0x49:
        case 0x4A:
        case 0x4C:
        case 0x4D:
        case 0x4E:
        case 0x50:
        case 0x51:
        case 0x52:
        case 0x53:
        case 0x54:
        case 0x55:
        case 0x56:
        case 0x57:
        case 0x58:
        case 0x59:
        case 0x5A:
        case 0x69:
        case 0x6A:
        case 0x6B:
        case 0x6C:
        case 0x6D:
        case 0x6E:
        case 0x6F:
        case 0x70:
        case 0x71:
        case 0x75:
        case 0x76:
        case 0x7A:
        case 0x7B:
        case 0x7C:
        case 0x7D:
        case 0x80:
        case 0x81:
        case 0x82:
        case 0x85:
        case 0x86:
        case 0x88:
        case 0x89:
        case 0x8A:
        case 0x99:
        case 0x9A:
        case 0x9B:
        case 0x9C:
        case 0xB9:
        case 0xBA:
        case 0xC3:
        case 0xD5:
        case 0xD6:
        case 0xD7:
        case 0xD8:
        case 0xD9:
        case 0xDA:
        case 0xDB:
        case 0xDC:
        case 0xDD:
        case 0xDE:
        case 0xE1:
            return true;

        default:
            return false;
    }
}

// ref: FUN_0071d940
bool IsEmoteAnimation(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);
    int32_t behavior = rec ? rec->m_behaviorID : 0x1FA;

    switch (behavior) {
        case 0x3C:
        case 0x3D:
        case 0x3E:
        case 0x3F:
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x43:
        case 0x44:
        case 0x45:
        case 0x46:
        case 0x47:
        case 0x48:
        case 0x49:
        case 0x4A:
        case 0x4C:
        case 0x4D:
        case 0x4E:
        case 0x50:
        case 0x51:
        case 0x52:
        case 0x53:
        case 0x54:
        case 0x71:
        case 0x88:
        case 0x89:
        case 0x8A:
        case 0xB2:
        case 0xB9:
        case 0xBA:
        case 0xC3:
            return true;

        default:
            return false;
    }
}

// ref: FUN_0071da20
// 107, 111, 112: the members of IsRangedAttackAnimation's set that are neither bow nor rifle.
bool IsThrownAnimation(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;

        if (behavior == 0x6B || (behavior > 0x6E && behavior < 0x71)) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0071da60
// 46 AttackBow, 105, 109.
bool IsBowAnimation(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;

        if (behavior == 0x2E || behavior == 0x69 || behavior == 0x6D) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0071daa0
// 49 AttackRifle, 106, 110.
bool IsRifleAnimation(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;

        if (behavior == 0x31 || behavior == 0x6A || behavior == 0x6E) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0071dae0
// 25 ReadyUnarmed through 29 ReadyBow.
bool IsReadyAnimation(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;

        if (behavior > 0x18 && behavior < 0x1E) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0071db20
bool IsAnimationBehavior127Or201To202(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;

        if (behavior == 0x7F || (behavior > 200 && behavior < 0xCB)) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0071db70
bool IsAnimationBehavior466To468Or472(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;

        if (behavior > 0x1D1 && (behavior < 0x1D5 || behavior == 0x1D8)) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0071dbc0
bool IsAnimationBehavior6Or132Or467To468Or472(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;
        bool match;

        if (behavior < 0x1D5) {
            if (behavior > 0x1D2) {
                return true;
            }

            if (behavior == 6) {
                return true;
            }

            match = behavior == 0x84;
        } else {
            match = behavior == 0x1D8;
        }

        if (match) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0071dc20
bool IsAnimationBehavior37To40Or467(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;

        if (behavior > 0x24 && (behavior < 0x29 || behavior == 0x1D3)) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0071dd80
bool IsAnimationBehavior1Or131Or466To467(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;

        if (behavior < 0x84) {
            if (behavior == 0x83 || behavior == 1) {
                return true;
            }
        } else if (behavior > 0x1D1 && behavior < 0x1D4) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0071dde0
// 1 Death, 6 Dead, 131, 132, 466 through 468, 472.
bool IsDeathAnimation(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;

        if (behavior < 0x85) {
            if (behavior > 0x82 || behavior == 1 || behavior == 6) {
                return true;
            }
        } else if (behavior > 0x1D1) {
            if (behavior < 0x1D5) {
                return true;
            }

            if (behavior == 0x1D8) {
                return true;
            }
        }
    }

    return false;
}

// ref: FUN_0071de50
bool IsAnimationBehavior133To134(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;

        if (behavior > 0x84 && behavior < 0x87) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0071d510
bool IsWoundAnimation(int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;

        if (behavior > 0x07 && behavior < 0x0B) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0071dc70
bool IsSpellCastOrReadySpellAnimation(int32_t animID) {
    if (IsSpellCastAnimation(animID)) {
        return true;
    }

    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;

        if (behavior > 0x32 && behavior < 0x35) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0071dcc0
bool IsSpellCastOrRangedAttackAnimation(int32_t animID) {
    if (IsSpellCastAnimation(animID)) {
        return true;
    }

    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec && rec->m_behaviorID == 0x6B) {
        return true;
    }

    // The reference looks the row up a second time here instead of reusing the one it has, and
    // reaches the last test through GetAnimationBehavior rather than the row.
    rec = g_animationDataDB.GetRecord(animID);

    if (rec && rec->m_behaviorID == 0x2E) {
        return true;
    }

    return GetAnimationBehavior(animID) == 0x31;
}

// ref: FUN_0071dd30
bool IsCombatOrReadyAnimation(int32_t animID) {
    if (IsCombatAnimation(animID)) {
        return true;
    }

    auto rec = g_animationDataDB.GetRecord(animID);

    if (rec) {
        int32_t behavior = rec->m_behaviorID;

        if (behavior > 0x18 && behavior < 0x1E) {
            return true;
        }
    }

    return false;
}

// The reference's animation tier fallback table at 0x00a349b4, read in the outer loop of
// ResolveAnimation: hover (2) drops to fly (3), and every other tier to ground (0).
static const int32_t s_animTierFallback[4] = { 0, 0, 3, 0 };

// ref: FUN_00717260
uint32_t CGUnit_C::GetCurrentAnimationId() const {
    if (!this->m_model || !this->m_model->IsLoaded(0, 0)) {
        return 0xFFFFFFFF;
    }

    if (this->m_upperBodyBoneId != 0xFFFFFFFF) {
        uint32_t animID = this->m_model->GetBoneUint90(this->m_upperBodyBoneId);

        if (animID != 0xFFFFFFFF) {
            return animID;
        }
    }

    return this->m_model->GetBoneUint90(0xFFFFFFFF);
}

// ref: FUN_0071b000
// The animation playing holds the unit in place (AnimationData flag 0x80).
int32_t CGUnit_C::IsAnimationRooting() const {
    uint32_t animID = this->GetCurrentAnimationId();

    if (0x1fa <= animID) {
        return 0;
    }

    auto rec = g_animationDataDB.GetRecord(static_cast<int32_t>(animID));

    return rec && (rec->m_flags & 0x80) ? 1 : 0;
}

// ref: FUN_00717600
bool CGUnit_C::GetDefaultAnimationForTier(int32_t tier, int32_t* out) const {
    int32_t count = g_animationDataDB.GetNumRecords();

    if (count <= 0) {
        return false;
    }

    int32_t animID;
    auto first = g_animationDataDB.GetRecordByIndex(0);

    if (tier == 0 && first && first->m_behaviorID == 0 && first->m_behaviorTier == 0) {
        animID = first->m_ID;
    } else {
        // Row id 0 exists but is not a tier-0 behaviour: the reference still takes id 0 here,
        // spelling the literal out rather than reading the row it just looked up.
        auto zero = g_animationDataDB.GetRecord(0);

        if (zero && zero->m_behaviorTier != 0) {
            animID = 0;
        } else {
            int32_t i = 0;

            for (; i < count; i++) {
                auto row = g_animationDataDB.GetRecordByIndex(i);

                if (row && row->m_behaviorID == 0 && row->m_behaviorTier == tier) {
                    break;
                }
            }

            if (i >= count) {
                return false;
            }

            animID = g_animationDataDB.GetRecordByIndex(i)->m_ID;
        }
    }

    if (!this->m_model || !this->m_model->HasSequence(static_cast<uint32_t>(animID))) {
        return false;
    }

    *out = animID;

    return true;
}

// ref: FUN_007176f0
uint32_t CGUnit_C::ResolveAnimation(uint32_t animID, CM2Model* model) {
    int32_t tier = this->m_animTier;

    // m_postInited and m_inReenable are bits 18 and 17 of the flag word the reference tests as
    // 0x40000 and 0x20000: an object still being created, or on its way out, keeps the id it asked
    // for.
    if (!this->m_postInited || this->m_inReenable || !this->GetObjectModel()) {
        return animID;
    }

    if (!this->GetObjectModel()->IsLoaded(0, 0)) {
        return animID;
    }

    if (model) {
        if (!model->IsLoaded(0, 0)) {
            return animID;
        }
    } else {
        model = this->GetObjectModel();
    }

    int32_t resolved = static_cast<int32_t>(animID & 0xFFFF);
    int32_t visited[0x1FA];
    int32_t tiersTried = 0;

    while (true) {
        memset(visited, 0, sizeof(visited));

        uint32_t cursor = animID;

        while (true) {
            if (ResolveAnimationBehavior(static_cast<int32_t>(cursor), tier, &resolved)
                && model->HasSequenceResolved(static_cast<uint32_t>(resolved))) {
                return static_cast<uint32_t>(resolved);
            }

            auto rec = g_animationDataDB.GetRecord(static_cast<int32_t>(cursor));

            if (cursor >= 0x1FA || visited[cursor] || !rec
                || rec->m_fallback == static_cast<int32_t>(cursor)) {
                break;
            }

            visited[cursor] = 1;
            cursor = static_cast<uint32_t>(rec->m_fallback);
        }

        if (this->GetDefaultAnimationForTier(tier, &resolved)) {
            return static_cast<uint32_t>(resolved);
        }

        // The reference loops here until a tier answers, and the ground tier falls back to itself,
        // so a model carrying neither the animation nor its tier default would spin forever. Four
        // passes is every tier the table can reach; frozen gives up instead of hanging.
        if (tier < 0 || tier > 3 || ++tiersTried > 4) {
            return static_cast<uint32_t>(resolved);
        }

        tier = s_animTierFallback[tier];
    }
}

// ref: FUN_007173f0
void CGUnit_C::GetBoneSequenceStates(M2BoneSequenceState* mount, M2BoneSequenceState* body,
                                     M2BoneSequenceState* upper, int32_t keepFinished) const {
    *mount = M2BoneSequenceState{};

    if (this->m_mountModel && this->m_mountModel->IsLoaded(0, 0)) {
        this->m_mountModel->GetBoneSequenceState(0xFFFFFFFF, mount);
    } else {
        mount->finished = 1;
    }

    if (mount->pastDuration) {
        mount->finished = 1;
    }

    // The mount slot is cleared whatever keepFinished says -- only the two model slots below honour
    // it, which is how a finished mount sequence always gives way to the unit's own.
    if (mount->finished) {
        mount->uint90 = 0xFFFFFFFF;
        mount->speed = 1.0f;
        mount->uint94 = 0xFFFFFFFF;
        mount->currentTime = 0;
    }

    *body = M2BoneSequenceState{};
    this->m_model->GetBoneSequenceState(0xFFFFFFFF, body);

    if (body->pastDuration) {
        body->finished = 1;
    }

    if (body->finished && keepFinished == 0) {
        body->uint90 = 0xFFFFFFFF;
        body->speed = 1.0f;
        body->uint94 = 0xFFFFFFFF;
        body->currentTime = 0;
    }

    *upper = M2BoneSequenceState{};

    if (this->m_upperBodyBoneId == 0xFFFFFFFF) {
        upper->finished = 1;
    } else {
        this->m_model->GetBoneSequenceState(this->m_upperBodyBoneId, upper);
    }

    if (upper->pastDuration) {
        upper->finished = 1;
    }

    if (upper->finished && keepFinished == 0) {
        upper->uint90 = 0xFFFFFFFF;
        upper->speed = 1.0f;
        upper->uint94 = 0xFFFFFFFF;
        upper->currentTime = 0;
    }
}

// ref: FUN_0071df30
bool CGUnit_C::ReplaceIdleWithHover(M2BoneSequenceState* state) const {
    if (state->uint90 == 0xFFFFFFFF) {
        return false;
    }

    M2SequenceInfo info = {};
    this->m_model->GetSequenceInfo(state->uint90, static_cast<int32_t>(state->uint94), info);

    // The id the chain actually landed on, not the one asked for: a model without the airborne
    // animation resolves it down to one of these three grounded idles.
    if (info.sequenceId == 0x00 || info.sequenceId == 0x08 || info.sequenceId == 0x19) {
        state->uint90 = 0xC1;
        state->uint94 = 0xFFFFFFFF;

        return true;
    }

    return false;
}

// The three ways a unit drives a sequence onto a model. Each one applies the change to the model it
// is given and then repeats it on every passenger riding this unit, so a vehicle's riders animate
// with it. The passenger half is NOT ported: it walks the passenger table the reference keeps on
// CVehicle_C +0x170, and nothing in frozen fills that table yet (see CVehicle_C.hpp). Where the
// reference would walk it there is a comment, not a silent omission -- the call-order score in
// docs/recomp/REPORT.md is what says these are incomplete.

// ref: FUN_00735820
void CGUnit_C::SetBoneSequence(CM2Model* model, uint32_t boneId, uint32_t animID, uint32_t variation,
                               uint32_t time, float speed, int32_t a8, int32_t a9,
                               int32_t fromPassenger) {
    if (!model) {
        return;
    }

    // A passenger being animated by its vehicle takes the vehicle's sequence; one asked directly
    // while it is riding an alive vehicle keeps its own.
    if (!fromPassenger && this->m_vehiclePassenger
        && this->m_vehiclePassenger->IsRidingLiveVehicle()) {
        return;
    }

    if (this->m_animFlags & 0x8000000) {
        a8 = 0;
    }

    model->SetBoneSequence(boneId, animID, variation, time, speed, a8, a9);

    M2BoneSequenceState state = {};

    if (model->IsLoaded(0, 0) && model->HasBone(boneId)
        && (model->GetBoneSequenceState(boneId, &state), state.uint90 == animID)
        && time > 0 && std::fabs(speed) > 0.001f && state.finished) {
        // The model is already holding this sequence at its end: let it go rather than restart it.
        // Bone 0x1a is the one exception -- it is put back on its idle instead of released.
        if (boneId == 0xFFFFFFFF || boneId == 0x1A) {
            model->SetBoneSequence(boneId, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);
        } else {
            model->UnsetBoneSequence(boneId, a8, a9);
        }

        return;
    }

    // PASSENGER PROPAGATION, reference 0x73591d, deliberately still out. What it does is now
    // fully read: if this unit IS a vehicle (m_vehicle), its Vehicle.dbc row is present, it
    // HasStateBits, and the model being animated is the unit's own, then it walks the vehicle's
    // rider list and re-issues the same sequence to each rider that is itself riding a live
    // vehicle. Each rider gets this model's current variation, or -1 when
    // GetSequenceVariationCount says the sequence has fewer variations than that -- which is
    // what the model-side wrapper FUN_008262f0 was ported for.
    //
    // TWO reasons it is not written. The rider list lives at vehicle +0x170 and +0x178, past
    // every field CVehicle_C declares (it stops at m_stateBits, +0x16c), and it is an offset-
    // linked list rather than a pointer one, so the layout has to be worked out first. And
    // nothing in frozen constructs a CVehicle_C at all, so the walk could not run even with the
    // layout -- it would be a loop behind a pointer that is always null.
}

// ref: FUN_00735a60
bool CGUnit_C::UnsetBoneSequence(CM2Model* model, uint32_t boneId, int32_t a4, int32_t a5,
                                int32_t fromPassenger) {
    if (!model) {
        return false;
    }

    if (!fromPassenger && this->m_vehiclePassenger
        && this->m_vehiclePassenger->IsRidingLiveVehicle()) {
        return false;
    }

    bool unset = false;

    if (model->IsLoaded(0, 0) && model->BoneHasParent(boneId)) {
        unset = true;
        model->UnsetBoneSequence(boneId, a4, a5);
    }

    // Passenger propagation goes here (reference 0x735b0a).

    return unset;
}

// ref: FUN_00735cc0
void CGUnit_C::SetBoneSequenceSpeed(CM2Model* model, uint32_t boneId, float speed,
                                    int32_t fromPassenger) {
    if (!model) {
        return;
    }

    if (!fromPassenger && this->m_vehiclePassenger
        && this->m_vehiclePassenger->IsRidingLiveVehicle()) {
        return;
    }

    model->SetBoneSequenceSpeed(boneId, speed);

    // Passenger propagation goes here (reference 0x735d3e), the same walk as SetBoneSequence's
    // and blocked on the same two things -- see the longer note there.
}

// ref: FUN_00737ef0
void CGUnit_C::ApplySequence(M2BoneSequenceState* state, uint32_t currentAnimID, int32_t upperBody,
                             int32_t targetAnimID, int32_t a8, int32_t skipInfoCheck) {
    uint32_t animID = state->uint90;

    bool busy = this->m_unit->channelSpell != 0 || (this->m_animFlags & 0x400) != 0;

    // A unit that is attacking, channelling, casting or standing ready restarts these sequences from
    // their first variation instead of keeping whichever one it was showing: the behaviour range the
    // reference tests inline here is IsReadyAnimation's, and 0 is Stand.
    if (this->m_attackTarget || busy || IsSpellCastOrReadySpellAnimation(targetAnimID)
        || IsCombatOrReadyAnimation(targetAnimID)) {
        auto rec = g_animationDataDB.GetRecord(static_cast<int32_t>(animID));

        if ((rec && rec->m_behaviorID > 0x18 && rec->m_behaviorID < 0x1E) || animID == 0) {
            state->uint94 = 0;
        }
    }

    if (IsDeathAnimation(static_cast<int32_t>(animID))) {
        state->speed = 1.0f;
    }

    if (!(this->m_stateFlags & 0x80000)) {
        a8 = 0;
    }

    // The variation the state carries may belong to a different sequence than the one the fallback
    // chain lands on; when it does, start at variation 0 rather than an index that is not there.
    if (skipInfoCheck == 0 && animID != 0xFFFFFFFF && state->uint94 != 0xFFFFFFFF) {
        M2SequenceInfo info = {};
        this->m_model->GetSequenceInfo(animID, 0, info);

        if (info.sequenceId != animID) {
            state->uint94 = 0;
        }
    }

    uint32_t boneId = upperBody ? this->m_upperBodyBoneId : 0xFFFFFFFF;

    // Already playing it: re-time rather than restart, and do not even do that when the speed is
    // where it should be.
    if (currentAnimID == animID) {
        if (std::fabs(this->m_model->GetBoneSequenceSpeed(boneId) - state->speed) < 2.3841858e-07f) {
            return;
        }

        this->SetBoneSequenceSpeed(this->m_model, boneId, state->speed, 0);

        return;
    }

    if (animID == 0xFFFFFFFF) {
        return;
    }

    if (upperBody != 0 && this->m_upperBodyBoneId == 0xFFFFFFFF) {
        return;
    }

    this->SetBoneSequence(this->m_model, boneId, animID, state->uint94,
                          static_cast<uint32_t>(state->currentTime), state->speed, a8, 1, 0);

    // Record what class of animation is now playing, by behaviour id.
    int32_t behavior = GetAnimationBehavior(static_cast<int32_t>(animID));

    if (behavior < 0xC1) {
        if (behavior == 0xC0) {
            this->m_animFlags |= 0x40000;
        } else if (behavior == 0x27) {
            this->m_animFlags |= 0x4;
        } else if (behavior == 0x79) {
            this->m_animFlags |= 0x80000;
        } else if (behavior == 0x7F) {
            this->m_animFlags |= 0x8;
        }
    } else if (behavior > 0xC9) {
        if (static_cast<uint32_t>(behavior) - 0x1CA < 3) {
            this->m_animFlags |= 0x2000000;
        }
    } else if (behavior == 0xC9) {
        this->m_animFlags |= 0x400000;
    } else if (behavior == 0xC8) {
        this->m_animFlags |= 0x40000;
    }

    // Dying in the air at the fly tier while actually airborne (flying, swimming or falling far).
    if (IsAnimationBehavior466To468Or472(static_cast<int32_t>(animID)) && this->m_animTier == 3
        && (this->m_localMove.GetMoveFlags() & 0x2201000)) {
        this->m_animFlags |= 0x4000000;
    } else {
        this->m_animFlags &= ~0x4000000;
    }

    uint32_t flags = this->m_animFlags;

    if (flags & 0x20000) {
        if (!IsCombatAnimation(static_cast<int32_t>(animID))
            && !IsSpellCastAnimation(static_cast<int32_t>(animID))) {
            return;
        }

        this->m_animFlags = flags | 0x2000;
    }
}

// ref: FUN_007225e0
const char* CGUnit_C::GetGenderedText(const char* key, int32_t count) const {
    uint32_t gender = (this->m_unit->bytes0 >> 16) & 0xFF;

    return FrameScript_GetText(key, count, gender == 1 ? GENDER_FEMALE : GENDER_MALE);
}

// ref: FUN_0071af90
bool CGUnit_C::IsAttacking() const {
    return this->m_attackTarget != 0;
}

// ref: FUN_0071afb0
bool CGUnit_C::IsAttackingOrPetInCombat() const {
    if (!(this->m_unit->flags & 0x800) && this->m_attackTarget == 0) {
        return false;
    }

    return true;
}

// ref: FUN_0071afe0
void CGUnit_C::ClearEffectFlag4000() {
    for (auto effect = this->m_effects; effect; effect = effect->m_linkNext) {
        effect->m_flags &= ~0x4000;
    }
}

// ref: FUN_0071f560
bool CGUnit_C::IsDeadOrFeigning() const {
    // The reference reaches the stand state through a virtual (vtable +0x138), which CGPlayer_C
    // overrides at FUN_006d64e0 to prefer the locally predicted state for the active player. frozen
    // has no such accessor yet, so this reads the descriptor byte the base version returns.
    int32_t standState = this->m_unit->bytes1 & 0xFF;

    if (this->m_unit->health > 0 && !(this->m_unit->flags2 & 0x1) && standState != 7) {
        // Alive, not feigning, not in the dead pose -- unless one of its effects is playing a death
        // animation, in which case treat it as dead so nothing overrides that.
        for (auto effect = this->m_effects; effect; effect = effect->m_linkNext) {
            if (effect->m_kit && IsDeathAnimation(effect->m_kit->m_animID)) {
                return true;
            }
        }

        return false;
    }

    return true;
}

// ref: FUN_0071e400
bool CGUnit_C::ApplyEffectAnimation(const M2BoneSequenceState* mount, const M2BoneSequenceState* body,
                                    const M2BoneSequenceState* upper, int32_t hasUpper,
                                    M2BoneSequenceState* mountOut, M2BoneSequenceState* bodyOut,
                                    M2BoneSequenceState* upperOut, int32_t* bodyKeepVariation,
                                    int32_t* upperKeepVariation) {
    // The first effect still running that wants to drive the animation wins.
    CEffect* effect = this->m_effects;

    while (true) {
        if (!effect) {
            return false;
        }

        CEffect* next = effect->m_linkNext;

        if (!(effect->m_flags & 0x800000) && effect->m_kit && effect->m_kit->m_animID >= 1
            && (effect->m_flags & 0x1000)) {
            break;
        }

        effect = next;
    }

    int32_t animID = effect->m_kit->m_animID;
    // An effect whose animation is already model-ready skips the fallback walk.
    bool resolved = (effect->m_flags & 0x1000000) != 0;

    if (mount && mountOut) {
        *mountOut = *mount;

        if (static_cast<int32_t>(mountOut->uint90) != animID) {
            mountOut->currentTime = 0;
        }

        mountOut->uint90 = resolved
            ? static_cast<uint32_t>(animID)
            : this->ResolveAnimation(static_cast<uint32_t>(animID), this->m_mountModel);
        mountOut->uint94 = 0xFFFFFFFF;
    }

    if (body && bodyOut) {
        *bodyOut = *body;

        if (static_cast<int32_t>(bodyOut->uint90) != animID) {
            bodyOut->currentTime = 0;
        }

        bodyOut->uint90 = resolved
            ? static_cast<uint32_t>(animID)
            : this->ResolveAnimation(static_cast<uint32_t>(animID), this->m_model);
        bodyOut->uint94 = 0xFFFFFFFF;

        if (bodyKeepVariation) {
            *bodyKeepVariation = 0;
        }
    }

    if (hasUpper && upper && upperOut) {
        *upperOut = *upper;

        if (static_cast<int32_t>(upperOut->uint90) != animID) {
            upperOut->currentTime = 0;
        }

        upperOut->uint90 = resolved
            ? static_cast<uint32_t>(animID)
            : this->ResolveAnimation(static_cast<uint32_t>(animID), this->m_model);
        upperOut->uint94 = 0xFFFFFFFF;

        if (upperKeepVariation) {
            *upperKeepVariation = 0;
        }
    }

    return true;
}

// ref: FUN_004f6210
const VehicleRec* CGUnit_C::GetVehicleRec() const {
    if (this->m_vehicle) {
        return this->m_vehicle->m_rec;
    }

    return nullptr;
}

// ref: FUN_004f6250
bool CGUnit_C::IsRidingVehicle() const {
    return this->m_vehiclePassenger && this->m_vehiclePassenger->m_state == 3;
}

// ref: FUN_005140c0
const VehicleSeatRec* CGUnit_C::GetVehicleSeatRec() const {
    if (this->m_vehiclePassenger) {
        return this->m_vehiclePassenger->m_seat;
    }

    return nullptr;
}

// ref: FUN_0074bb60
uint32_t CGUnit_C::SeatAllowsExitAnimation(const VehicleSeatRec* seat) const {
    uint32_t mask = (this->m_localMove.GetMoveFlags2() & 0x40) ? 0x8 : 0x8000;

    return static_cast<uint32_t>(seat->m_flags) & mask;
}

// ref: FUN_00715880
bool CGUnit_C::IsStandStateUprightOrSeated() const {
    // The reference reaches the stand state through the virtual at vtable +0x138; see
    // IsDeadOrFeigning for why frozen reads the descriptor byte instead.
    switch (this->m_unit->bytes1 & 0xFF) {
        case 0:
        case 1:
        case 4:
        case 5:
        case 6:
            return true;

        default:
            return false;
    }
}

// ref: FUN_00723e30
bool CGUnit_C::CanPlayActionAnimation(int32_t animID, int32_t currentAnimID) {
    if (currentAnimID == -1) {
        return false;
    }

    // A rider whose seat has an animation for the phase it is in plays that, whatever else is true.
    auto passenger = this->m_vehiclePassenger;

    if (passenger && passenger->m_state == 3
        && passenger->GetSeatAnimation(passenger->m_seat) != 0x1FA) {
        return true;
    }

    // A vehicle animating through the bone its rider sits on, or one whose owner is driving the
    // animation, keeps what it has; so does anything dying.
    bool boneDriven = this->m_upperBodyBoneId != 0xFFFFFFFF && this->m_vehicle
        && this->m_vehicle->m_rec && this->m_vehicle->TestFlag(this->m_upperBodyBoneId);

    if (boneDriven) {
        return false;
    }

    if (this->m_vehicle && this->m_vehicle->m_rec
        && this->m_vehicle->OwnerIsControllingAnimation()) {
        return false;
    }

    if (IsDeathAnimation(animID)) {
        return false;
    }

    uint32_t moveFlags = this->m_localMove.GetMoveFlags();

    // Turning in place, or mid-jump.
    bool turningOrJumping = (moveFlags & 0x30) != 0 || (this->m_animFlags & 0x1800) != 0;
    // Holding an animation without channelling anything.
    bool holding = this->m_unit->channelSpell == 0 && (this->m_animFlags & 0x400) != 0;
    // For a player the reference then reads a CGPlayer_C field (+0x1944) that frozen has not
    // recovered, so this term is always false here. It is the only part of this function left out.
    bool playerOverride = false;

    if (this->IsA(TYPE_PLAYER)) {
        playerOverride = false;
    }

    bool allow;

    if (IsActionAnimation(animID)
        && ((this->m_animFlags & 0x1800) != 0
            || (moveFlags & 0x2E000FF) != 0
            || this->m_localMove.IsInForcedMotion()
            || (this->m_unit->bytes1 & 0xFF) != 0
            || (IsReadyAnimation(animID) && this->m_unit->mountDisplayID > 0)
            || playerOverride)) {
        allow = true;
    } else if ((IsJumpAnimation(animID) && IsCombatOrReadyAnimation(animID))
        || (IsCombatAnimation(animID) && (this->m_move->GetMoveFlags() & 0x1000) != 0)
        || (holding && (moveFlags & 0x2E0000F) != 0)) {
        allow = true;
    } else {
        allow = false;
    }

    if (IsJumpLandAnimation(animID)) {
        allow = false;
    }

    if (IsAnimationBehavior133To134(animID) && !this->IsStandStateUprightOrSeated()) {
        allow = false;
    }

    // Strafing is the one movement that does not cancel an action animation.
    if ((holding || this->m_unit->channelSpell != 0 || this->m_attackTarget != 0)
        && turningOrJumping && (this->m_move->GetMoveFlags() & 0xC) == 0) {
        allow = false;
    }

    return allow;
}

// ref: FUN_0071e340
void CGUnit_C::GetPostureAnimation(uint32_t* out, int32_t ignore464) const {
    auto passenger = this->m_vehiclePassenger;

    if (passenger && passenger->m_state == 3
        && passenger->GetSeatAnimation(passenger->m_seat) != 0x1FA) {
        return;
    }

    // Mid-jump (0x4) or in the airborne-death state (0x800000): leave the caller's choice alone.
    if (this->m_animFlags & 0x800004) {
        return;
    }

    uint32_t current = this->GetCurrentAnimationId();

    if (ignore464 == 0 && GetAnimationBehavior(static_cast<int32_t>(current)) == 0x1D0) {
        *out = current;

        return;
    }

    // Swimming or flying.
    if (this->m_localMove.GetMoveFlags() & 0x2200000) {
        *out = 0x29;

        return;
    }

    // The stealth creep flag, byte 2 bit 1 of UNIT_FIELD_BYTES_1.
    if (this->m_unit->bytes1 & 0x20000) {
        *out = 0x78;

        return;
    }

    *out = this->m_move->IsUnsupportedOrHovering() ? 0xC1 : 0;
}

// ref: FUN_0071de90
bool CGUnit_C::CanPlayTurnAnimation() const {
    // 0x30 is turning left or right; 0x1800 of the animation flags is mid-jump.
    if (((this->m_localMove.GetMoveFlags() & 0x30) == 0 && (this->m_animFlags & 0x1800) == 0)
        || this->m_move->IsUnsupportedHoveringSwimmingOrSlowFalling()) {
        return false;
    }

    if (this->m_vehicle && this->m_vehicle->m_rec
        && this->m_vehicle->ControlsPassengerAnimation()) {
        return false;
    }

    int32_t current = static_cast<int32_t>(this->GetCurrentAnimationId());

    if (!IsEmoteAnimation(current) && !IsSpellCastAnimation(current) && !IsThrownAnimation(current)
        && !IsBowAnimation(current) && !IsRifleAnimation(current)
        && (this->m_animFlags & 0x40000C) == 0) {
        return true;
    }

    return false;
}

// ref: FUN_0071dfc0
bool CGUnit_C::HasAirborneDeathAnimation() {
    uint32_t animID = this->ResolveAnimation(0x1D2, nullptr);

    if (this->m_model) {
        return this->m_model->HasSequence(animID);
    }

    return false;
}

// ref: FUN_007385c0
// Transcribed against the disassembly rather than the decompilation: every predicate here takes its
// animation id in a register, which Ghidra drops, so which id each one is asked about was read out
// of 0x007385c0..0x007395c0 directly.
//
// Three calls the reference makes are NOT here, each because the thing it calls is not ported:
// the weapon-trail release (FUN_00715ba0), the sound and trail trigger (FUN_00738180) and the
// object-effect refresh (FUN_0071e5b0). Also absent, as in the appliers, is the walk that repeats a
// change on every passenger riding this unit. All four are marked at the spot.
void CGUnit_C::SetAnimation(uint32_t animID, uint32_t flags) {
    if (!this->m_postInited || this->m_inReenable || !this->GetObjectModel() || !this->m_model) {
        return;
    }

    if (!this->GetObjectModel()->IsLoaded(0, 0) || !this->m_model->IsLoaded(0, 0)) {
        // Remember it and replay it once the model is there.
        if (animID == 0xFFFFFFFF) {
            return;
        }

        this->m_pendingAnimID = static_cast<int32_t>(animID);

        return;
    }

    this->m_pendingAnimID = -1;

    // Nothing but a death animation reaches a dead unit.
    if (this->IsDeadOrFeigning() && !IsDeathAnimation(static_cast<int32_t>(animID))) {
        return;
    }

    if (this->GetObjectModel()->m_animationHeldTime) {
        return;
    }

    if (this->GetVehicleRec() && this->m_vehicle->HasFlag26()) {
        return;
    }

    uint32_t animFlags = this->m_animFlags;

    // Already lifting off or hovering: only a death or a behaviour-127/201/202 animation interrupts.
    if (animFlags & 0xC0000) {
        if (!IsDeathAnimation(static_cast<int32_t>(animID))
            && !IsAnimationBehavior127Or201To202(static_cast<int32_t>(animID))) {
            return;
        }
    }

    if (animFlags & 0x2000000) {
        return;
    }

    if ((animFlags & 0x2000) && !IsDeathAnimation(static_cast<int32_t>(animID))
        && !IsAnimationBehavior127Or201To202(static_cast<int32_t>(animID))) {
        return;
    }

    int32_t targetAnimID;

    if (!(flags & 0x2)) {
        // 57 Special1H / 58 Special2H asked for without flag 8: pick by what is in hand, and fall to
        // 118 when the unit holds nothing.
        if (!(flags & 0x8) && (animID == 0x39 || animID == 0x3A)) {
            const uint8_t* mainHand = nullptr;
            uint8_t mainHandPair[2] = {};
            bool offHand = false;

            auto mainRec = g_itemDB.GetRecord(this->m_unit->virtualItemSlotID[0]);

            if (mainRec) {
                mainHandPair[0] = static_cast<uint8_t>(mainRec->m_classID);
                mainHandPair[1] = static_cast<uint8_t>(mainRec->m_subclassID);
                mainHand = mainHandPair;
            }

            offHand = g_itemDB.GetRecord(this->m_unit->virtualItemSlotID[1]) != nullptr;

            if (!mainHand && !offHand) {
                animID = 0x76;
            } else {
                animID = IsTwoHandedWeapon(mainHand) ? 0x3A : 0x39;
            }
        }

        targetAnimID = static_cast<int32_t>(animID);

        if (!(flags & 0x4)) {
            targetAnimID = static_cast<int32_t>(this->ResolveAnimation(animID, nullptr));
        }
    } else {
        targetAnimID = static_cast<int32_t>(animID);
    }

    M2BoneSequenceState mountState;
    M2BoneSequenceState bodyState;
    M2BoneSequenceState upperState;
    this->GetBoneSequenceStates(&mountState, &bodyState, &upperState, 0);

    int32_t currentAnimID = static_cast<int32_t>(upperState.uint90);

    if (upperState.uint90 == 0xFFFFFFFF) {
        currentAnimID = static_cast<int32_t>(bodyState.uint90);
    }

    bool hasUpper = upperState.uint90 != 0xFFFFFFFF;

    // Mid-swing with another swing asked for: just speed the current one up.
    if ((this->m_animFlags & 0x20) && IsCombatAnimation(currentAnimID)
        && IsCombatAnimation(targetAnimID)) {
        uint32_t boneId = hasUpper ? this->m_upperBodyBoneId : 0xFFFFFFFF;
        this->SetBoneSequenceSpeed(this->m_model, boneId, 2.0f, 0);
        this->m_heldAnimID = targetAnimID;

        return;
    }

    this->m_heldAnimID = -1;

    // The reference releases the weapon trails here (FUN_00715ba0, which walks the two emitters the
    // unit keeps at +0xb50); neither the emitters nor that function are ported.

    float speed = 1.0f;
    uint32_t startTime = 0;
    uint32_t variation = 0xFFFFFFFF;

    // Dying while already dead keeps the variation the model is holding, so a corpse does not
    // restart into a different death.
    if (IsAnimationBehavior6Or132Or467To468Or472(static_cast<int32_t>(animID))
        && IsDeathAnimation(static_cast<int32_t>(this->GetCurrentAnimationId()))) {
        this->m_model->GetBoneSequenceId(0xFFFFFFFF, &variation);
    }

    // A movement animation is played at the ratio of the unit's speed to the one it was authored at,
    // and picked up at the phase the current animation had reached.
    M2SequenceInfo info = {};
    this->m_model->GetSequenceInfo(static_cast<uint32_t>(targetAnimID), 0, info);

    if (info.moveSpeed != 0.0f && IsMovementAnimation(static_cast<uint32_t>(targetAnimID))
        && (this->m_localMove.GetMoveFlags() & 0xC0000F)) {
        speed = this->m_localMove.GetCurrentSpeed(0) / std::fabs(info.moveSpeed);

        if (info.duration != 0) {
            M2BoneSequenceState playing = {};
            this->m_model->GetBoneSequenceState(0xFFFFFFFF, &playing);

            M2SequenceInfo playingInfo = {};
            this->m_model->GetSequenceInfo(playing.uint90, static_cast<int32_t>(playing.uint94),
                                           playingInfo);

            if (playingInfo.moveSpeed != 0.0f && playingInfo.duration != 0) {
                startTime = static_cast<uint32_t>(
                    (static_cast<uint64_t>(static_cast<uint32_t>(playing.currentTime))
                     * info.duration / playingInfo.duration) % info.duration);
            }
        }
    }

    uint32_t blend = ~flags & 0x1;
    uint32_t upperBlend = blend;
    uint32_t bodyBlend = blend;

    animFlags = this->m_animFlags;
    uint32_t turnFlag = animFlags & 0x10;
    uint32_t applyBody = animFlags & 0x40;
    uint32_t applyUpper = animFlags & 0x20;

    M2BoneSequenceState mountOut = mountState;
    M2BoneSequenceState bodyOut = bodyState;
    M2BoneSequenceState upperOut = upperState;

    // The sequence the selector is aiming at, which each slot may be given a copy of.
    M2BoneSequenceState target = {};
    target.uint90 = static_cast<uint32_t>(targetAnimID);
    target.uint94 = variation;
    target.currentTime = static_cast<int32_t>(startTime);
    target.speed = speed;

    int32_t bodySkip = 1;
    int32_t upperSkip = 1;

    bool holding = this->m_unit->channelSpell == 0 && (animFlags & 0x400) != 0;

    bool canAct = this->CanPlayActionAnimation(targetAnimID, currentAnimID);

    if (canAct && applyUpper == 0) {
        canAct = false;
    }

    bool boneDriven = this->m_upperBodyBoneId != 0xFFFFFFFF && this->GetVehicleRec()
        && this->m_vehicle->TestFlag(this->m_upperBodyBoneId);

    // A special attack, a death, or the airborne-death state takes the whole body.
    if (IsSpecialAttackAnimation(targetAnimID) || IsDeathAnimation(targetAnimID)
        || (this->m_animFlags & 0x20000)) {
        canAct = false;
        applyUpper = 0;
        applyBody = 1;
    }

    // The reference lays the mounted case out first, so the original source tested for a mount
    // rather than for its absence.
    if (this->m_mountModel) {
        // Mounted: the mount takes base-state animations and the rider takes actions.
        if (IsBaseStateAnimation(targetAnimID)) {
            mountOut = target;
            mountOut.uint90 = this->ResolveAnimation(static_cast<uint32_t>(targetAnimID),
                                                    this->m_mountModel);
        }

        if (!IsBaseStateAnimation(targetAnimID) && IsActionAnimation(targetAnimID)) {
            upperOut = target;
            upperSkip = 0;
        }

        // While mounted the rider's body holds the mounted pose, whatever the body state resolved to.
        if (this->m_mountedAnimID
            != GetAnimationBehavior(static_cast<int32_t>(bodyState.uint90))) {
            bodyOut.uint90 = this->ResolveAnimation(static_cast<uint32_t>(this->m_mountedAnimID),
                                                   this->m_model);
            bodyOut.speed = 1.0f;
            bodyOut.uint94 = 0xFFFFFFFF;
            bodyOut.currentTime = 0;
            bodyBlend = 0;
            bodySkip = 0;
        }
    } else {
        if (targetAnimID == 0x3D) {
            // 61 EmoteTalk: upper body only.
            upperOut = target;
            upperSkip = 0;
            applyUpper = 1;
        } else if (targetAnimID == 0x3E && (this->m_move->GetMoveFlags() & 0x2200000)) {
            // 62 EmoteTalkNoSheathe while swimming or flying: upper body only, body left alone.
            upperOut = target;
            upperSkip = 0;
            applyUpper = 1;
            applyBody = 0;
        } else if (canAct) {
            if ((IsEmoteAnimation(targetAnimID) && targetAnimID != 0x45)
                || (!holding && !IsCombatOrReadyAnimation(currentAnimID)
                    && !IsSpellCastOrReadySpellAnimation(currentAnimID)
                    && this->m_sheathState != 2)) {
                upperOut = target;
                upperSkip = 0;
                applyUpper = 1;
            } else if (!IsMovementAnimation(static_cast<uint32_t>(targetAnimID))
                && !IsReadyAnimation(targetAnimID) && targetAnimID != 0) {
                upperSkip = 0;
                upperOut = target;
                applyUpper = 1;

                uint32_t moveFlags = this->m_localMove.GetMoveFlags();

                // Swimming or flying but not moving under its own power: the body swims while the
                // upper body does the action.
                if ((moveFlags & 0x2200000) && !(moveFlags & 0xF)) {
                    bodyOut.uint90 = this->ResolveAnimation(0x29, this->m_model);
                    bodyOut.speed = 1.0f;
                    bodyOut.uint94 = 0xFFFFFFFF;
                    bodyOut.currentTime = 0;
                    bodySkip = 0;
                }
            } else {
                if (!hasUpper) {
                    upperOut = bodyState;
                    upperSkip = 1;
                    applyUpper = 1;
                }

                bodySkip = 0;
                bodyOut = target;
            }
        } else if (IsMovementAnimation(static_cast<uint32_t>(targetAnimID))
            && (IsCombatAnimation(static_cast<int32_t>(bodyState.uint90))
                || IsSpellCastAnimation(static_cast<int32_t>(bodyState.uint90)))) {
            upperOut = bodyState;
            upperSkip = 1;
            applyUpper = 1;
            bodySkip = 0;
            bodyOut = target;
        } else if (((IsCombatOrReadyAnimation(currentAnimID)
                     && (IsReadyAnimation(targetAnimID) || targetAnimID == 0))
                    || (IsSpellCastOrReadySpellAnimation(currentAnimID)
                        && (IsReadySpellAnimation(targetAnimID) || IsReadyAnimation(targetAnimID)
                            || targetAnimID == 0)))
                   && hasUpper && !boneDriven) {
            // Coming back to a ready pose while the upper body holds a combat one: let the body
            // take the new animation and hand the upper body back what it was showing.
            applyUpper = 1;
            bodySkip = 1;
            upperOut.uint90 = 0xFFFFFFFF;
            upperSkip = 0;
            bodyOut = upperState;
        } else {
            bodySkip = 0;
            bodyOut = target;
        }
    }

    // Hovering without swimming or flying: a slot that fell back to a grounded idle takes Hover.
    if (this->m_move->IsUnsupportedOrHovering()
        && !(this->m_move->GetMoveFlags() & 0x2200000)) {
        if (this->ReplaceIdleWithHover(&bodyOut)) {
            bodySkip = 0;
        }

        if (this->ReplaceIdleWithHover(&upperOut)) {
            upperSkip = 0;
        }
    }

    if (IsSpecialAttackAnimation(targetAnimID) || IsDeathAnimation(targetAnimID)
        || (this->m_animFlags & 0x20000)) {
        applyUpper = 0;
        applyBody = 1;
        bodyOut = target;

        bodySkip = 0;

        if (IsAnimationBehavior6Or132Or467To468Or472(targetAnimID)
            && IsDeathAnimation(static_cast<int32_t>(this->GetCurrentAnimationId()))) {
            bodySkip = 1;
        }
    }

    // A running spell visual overrides all three slots -- except over a death.
    if (!IsDeathAnimation(targetAnimID)
        && this->ApplyEffectAnimation(&mountState, &bodyState, &upperState, hasUpper, &mountOut,
                                      &bodyOut, &upperOut, &bodySkip, &upperSkip)) {
        applyBody = 1;

        if (hasUpper) {
            applyUpper = 1;
        }

        if (this->m_mountModel) {
            if (!IsBaseStateAnimation(static_cast<int32_t>(bodyOut.uint90))
                && IsActionAnimation(static_cast<int32_t>(bodyOut.uint90))) {
                upperOut = bodyOut;
                upperSkip = bodySkip;
                applyUpper = 1;
            }

            if (GetAnimationBehavior(static_cast<int32_t>(bodyOut.uint90))
                != this->m_mountedAnimID) {
                bodyOut.uint90 = this->ResolveAnimation(static_cast<uint32_t>(this->m_mountedAnimID),
                                                       this->m_model);
                bodyOut.speed = 1.0f;
                bodyOut.uint94 = 0xFFFFFFFF;
                bodyOut.currentTime = 0;
                bodySkip = 0;
                bodyBlend = 0;
            }
        }
    }

    // Aboard a vehicle and alive: the seat's animation wins over everything worked out above.
    if (this->IsRidingVehicle() && this->m_unit->health > 0) {
        auto seat = this->GetVehicleSeatRec();
        int32_t seatAnim = this->m_vehiclePassenger->GetSeatAnimation(seat);

        if (seatAnim != 0x1FA) {
            // When the upper body is already showing something of its own, or the seat has no upper
            // animation, the body slot simply takes the seat's and the upper slot is left as it is.
            bool considerBody = true;

            if (applyUpper) {
                if (GetAnimationBehavior(static_cast<int32_t>(upperOut.uint90)) != 0) {
                    considerBody = false;
                } else if (this->m_vehiclePassenger->GetSeatUpperAnimation(seat) == 0) {
                    considerBody = false;
                } else {
                    applyUpper = 0;
                }
            }

            if (considerBody && GetAnimationBehavior(static_cast<int32_t>(bodyOut.uint90)) != 0) {
                upperOut = bodyOut;
                upperSkip = bodySkip;
                applyUpper = 1;
            }

            bodyOut.speed = 1.0f;
            bodyOut.uint90 = static_cast<uint32_t>(seatAnim);
            bodyOut.uint94 = 0xFFFFFFFF;
            bodyOut.currentTime = 0;
            bodySkip = 0;
        }
    }

    // A vehicle animating through the bone its rider sits on drives nothing itself.
    if (boneDriven) {
        applyUpper = 0;
        currentAnimID = -1;
    }

    // Both slots resolved to the mounted pose: let the upper-body bone go so the mount drives it.
    if (this->m_mountModel && upperOut.uint90 == static_cast<uint32_t>(this->m_mountedAnimID)
        && bodyOut.uint90 == static_cast<uint32_t>(this->m_mountedAnimID)) {
        applyUpper = 0;
        currentAnimID = -1;
        this->UnsetBoneSequence(this->m_model, this->m_upperBodyBoneId, 1, 0, 0);
    }

    // The mount's own sequence, when it changed or its speed did.
    if (turnFlag && this->m_mountModel && mountOut.uint90 != 0xFFFFFFFF
        && (mountState.uint90 != mountOut.uint90
            || std::fabs(mountOut.speed - mountState.speed) > 0.01f)) {
        this->SetBoneSequence(this->m_mountModel, 0xFFFFFFFF, mountOut.uint90, mountOut.uint94,
                              static_cast<uint32_t>(mountOut.currentTime), mountOut.speed, blend, 1,
                              0);
    }

    // Dying: release every bone sequence so nothing holds the corpse in a pose.
    if (IsDeathAnimation(targetAnimID)) {
        applyUpper = 0;

        if (this->m_upperBodyBoneId != 0xFFFFFFFF) {
            this->UnsetBoneSequence(this->m_model, this->m_upperBodyBoneId, 1, 0, 0);
            this->UnsetBoneSequence(this->m_model, this->m_upperBodyBoneId, 1, 1, 0);
        }

        this->UnsetBoneSequence(this->m_model, 0xFFFFFFFF, 1, 0, 0);
    }

    if (!(applyBody && upperOut.uint90 == 0xFFFFFFFF && bodyOut.uint90 == upperState.uint90)
        && applyUpper) {
        uint32_t upperArg = upperBlend;

        if (upperState.uint90 == 0xFFFFFFFF && upperOut.uint90 == bodyState.uint90) {
            bodyBlend = 1;
            upperArg = 0;
        }

        this->ApplySequence(&upperOut, upperState.uint90, 1, currentAnimID,
                            static_cast<int32_t>(upperArg), upperSkip);
    }

    if (applyBody) {
        this->ApplySequence(&bodyOut, bodyState.uint90, 0, currentAnimID,
                            static_cast<int32_t>(bodyBlend), bodySkip);
    }

    // The reference finishes with the swing sound and trail trigger (FUN_00738180, called with
    // (flags >> 5) & 0xffffff01) and the object-effect refresh (FUN_0071e5b0). Neither is ported.
}

// The animation chooser. CGUnit_C::ChooseAnimation runs a fixed chain of candidates, each of which
// answers "I have decided" and may write the animation it decided on; the first one to decide wins.
// Every candidate takes the same `allow` mask, which says which classes of change the caller will
// accept: a candidate whose class is masked out still DECIDES (stopping the chain) but writes
// nothing, which is how the caller pins the unit to what it is already playing.
//
// The reference reaches the unit's virtual items through a virtual at vtable +0x12c that returns a
// pointer to a cached class/subclass byte pair. frozen has no cache of that shape, so this fills a
// local pair out of Item.dbc instead.
static const uint8_t* VirtualItemPair(const CGUnit_C* unit, int32_t slot, uint8_t out[2]) {
    auto rec = g_itemDB.GetRecord(unit->Unit()->virtualItemSlotID[slot]);

    if (!rec) {
        return nullptr;
    }

    out[0] = static_cast<uint8_t>(rec->m_classID);
    out[1] = static_cast<uint8_t>(rec->m_subclassID);

    return out;
}

// ref: FUN_007172b0
uint32_t CGUnit_C::GetModelAnimationId() const {
    if (this->m_model && this->m_model->IsLoaded(0, 0)) {
        return this->m_model->GetBoneUint90(0xFFFFFFFF);
    }

    return 0xFFFFFFFF;
}

// ref: FUN_0071b6b0
bool CGUnit_C::IsLooting() const {
    if (ClntObjMgrGetActivePlayer() != this->GetGUID()) {
        // UNIT_FIELD_FLAGS 0x400.
        return ((this->m_unit->flags >> 10) & 1) != 0;
    }

    return static_cast<const CGPlayer_C*>(this)->m_lootTarget != 0;
}

// ref: FUN_007222a0
bool CGUnit_C::CanShowLootAnimation() const {
    if (ClntObjMgrGetActivePlayer() != this->GetGUID()) {
        // UNIT_FIELD_FLAGS bit 28 clear.
        return !((this->m_unit->flags >> 28) & 1);
    }

    auto target = ClntObjMgrObjectPtr(static_cast<const CGPlayer_C*>(this)->m_lootTarget,
                                      TYPE_OBJECT, __FILE__, __LINE__);

    if (!target) {
        return false;
    }

    // A fishing bobber (GAMEOBJECT_BYTES_1 byte 1 == 17) and a living unit both refuse; so does an
    // item. Byte 1 of that field is the gameobject's type.
    if (target->IsA(TYPE_GAMEOBJECT)
        && static_cast<CGGameObject_C*>(target)->GameObject()->type == 0x11) {
        return false;
    }

    if (target->IsA(TYPE_UNIT) && static_cast<CGUnit_C*>(target)->Unit()->health > 0) {
        return false;
    }

    return !target->IsA(TYPE_ITEM);
}

// ref: FUN_00714dd0
int32_t CGUnit_C::GetReadyWeaponAnimation() const {
    uint8_t pair[2] = {};
    uint8_t offPair[2] = {};
    auto weapon = VirtualItemPair(this, 0, pair);

    if (!weapon || weapon[0] != 2) {
        weapon = VirtualItemPair(this, 1, pair);
    }

    if (weapon && weapon[0] == 2) {
        bool offHand = VirtualItemPair(this, 1, offPair) != nullptr;

        switch (weapon[1]) {
            case 0x00:
            case 0x04:
            case 0x07:
            case 0x0B:
            case 0x0D:
            case 0x0E:
            case 0x0F:
            case 0x14:
                return 0x1A;

            case 0x01:
            case 0x05:
            case 0x08:
            case 0x0C:
                return offHand ? 0x1A : 0x1B;

            case 0x06:
            case 0x0A:
            case 0x11:
                return 0x1C;

            default:
                break;
        }
    }

    return 0x19;
}

// ref: FUN_007fa290
const SpellVisualRec* GetSpellVisual(const SpellRec* spell) {
    if (!spell) {
        return nullptr;
    }

    return g_spellVisualDB.GetRecord(spell->m_spellVisualID[0]);
}

// ref: FUN_007224d0
bool CGUnit_C::GetSpellVisualAnimation(int32_t* animOut, uint32_t* kitFlagsOut) {
    if (this->m_intFA4 != -1) {
        return false;
    }

    bool casting = true;
    auto spell = g_spellDB.GetRecord(this->m_castSpellID);

    if (!spell) {
        casting = false;
        spell = g_spellDB.GetRecord(this->m_unit->channelSpell);

        if (!spell) {
            return false;
        }
    }

    auto visual = GetSpellVisual(spell);

    if (!visual) {
        return false;
    }

    auto kit = g_spellVisualKitDB.GetRecord(casting ? visual->m_precastKit : visual->m_channelKit);

    if (!kit) {
        return false;
    }

    // The kit's start animation only applies while casting, and only with animation flag 0x10000.
    int32_t anim = 0;

    if (casting && (this->m_animFlags & 0x10000) && kit->m_startAnimID > 0) {
        anim = kit->m_startAnimID;
    } else if (kit->m_animID > 0) {
        anim = kit->m_animID;
    } else {
        return false;
    }

    if (kitFlagsOut) {
        *kitFlagsOut = static_cast<uint32_t>(kit->m_flags);
    }

    *animOut = anim;

    return true;
}

// ref: FUN_00724060
bool CGUnit_C::GetDeathAnimation(uint32_t allow, int32_t* out, uint32_t* decidedFlags) {
    // A rider aboard a live vehicle that is not feigning is not treated as dead here.
    if (!this->IsDeadOrFeigning()
        || (this->m_vehiclePassenger && this->m_vehiclePassenger->m_state == 3
            && !((this->m_unit->flags2 >> 0x11) & 1))) {
        return (allow & 0xFFFFFFFE) == 0;
    }

    // An effect playing a death animation with kit flag 0x4 supplies the pose instead.
    bool kitWins = false;

    for (auto effect = this->m_effects; effect; effect = effect->m_linkNext) {
        if (effect->m_kit && IsDeathAnimation(effect->m_kit->m_animID)) {
            if (effect->m_kit->m_flags & 0x4) {
                kitWins = true;
            }

            if (kitWins) {
                *out = effect->m_kit->m_animID;

                if (decidedFlags) {
                    *decidedFlags |= 0x2;
                }

                return true;
            }
        }
    }

    uint32_t current = this->GetCurrentAnimationId();

    // Already holding an airborne death: leave it.
    if ((this->m_animFlags & 0x4000000) && IsDeathAnimation(static_cast<int32_t>(current))) {
        return true;
    }

    if (this->HasAirborneDeathAnimation()) {
        // Falling far, or carried by a spline: the plain airborne death; otherwise the 472 variant.
        if (!(this->m_move->GetMoveFlags() & 0x1000) && !this->m_move->IsSplineFlag200()
            && !this->m_move->IsSplineFlag800()) {
            *out = 0x1D8;
        } else {
            *out = 0x1D2;
        }

        return true;
    }

    uint32_t moveFlags = this->m_localMove.GetMoveFlags();

    if ((!(moveFlags & 0x1000) || !(moveFlags & 0x2000))
        && !IsAnimationBehavior1Or131Or466To467(static_cast<int32_t>(current)) && (allow & 0x1)) {
        // 6 Dead, or 132 when the unit is already down.
        *out = 0x6;
    }

    return true;
}

// ref: FUN_00716fd0
bool CGUnit_C::GetStandState10Animation(uint32_t allow, int32_t* out) {
    if (!(this->m_animFlags & 0x40) || this->m_mountModel || this->m_lastStandState != 10) {
        return (allow & 0xFFFFF800) == 0;
    }

    if (this->m_stateFlags & 0x40000000) {
        return false;
    }

    if (!this->m_model->HasSequence(0x7F)) {
        return (allow & 0xFFFFFFFC) == 0;
    }

    *out = 0x7F;

    return true;
}

// ref: FUN_0071dff0
bool CGUnit_C::GetStandState9Animation(uint32_t allow, int32_t* out) {
    if (this->m_animFlags & 0x400008) {
        return true;
    }

    if (!(this->m_animFlags & 0x40) || this->m_mountModel) {
        return (allow & 0xFFFFFFFC) == 0;
    }

    int32_t standState = this->m_unit->bytes1 & 0xFF;
    int32_t anim;

    if (standState == 9) {
        // 201 going into it, 202 while held.
        anim = (this->m_lastStandState == 9) + 0xC9;
    } else {
        if (this->m_lastStandState != 9) {
            return (allow & 0xFFFFFFFC) == 0;
        }

        anim = 0xE0;

        if (!this->m_model->HasSequence(0xE0)) {
            anim = 0x7F;
        }
    }

    if (!this->m_model->HasSequence(static_cast<uint32_t>(anim))) {
        return (allow & 0xFFFFFFFC) == 0;
    }

    if (static_cast<int32_t>(this->GetCurrentAnimationId()) != anim && (allow & 0x2)) {
        *out = anim;
    }

    return true;
}

// ref: FUN_00724200
bool CGUnit_C::GetForcedMotionAnimation(int32_t* out) {
    if (!this->m_localMove.IsInForcedMotion()) {
        return false;
    }

    uint32_t current = 0xFFFFFFFF;

    if (this->m_model && this->m_model->IsLoaded(0, 0)) {
        current = this->m_model->GetBoneUint90(0xFFFFFFFF);
    }

    // Already in a jump or a fall: decide, but leave the animation alone.
    if (IsAnimationBehavior37To40Or467(static_cast<int32_t>(current))) {
        *out = 0x1FA;
    } else {
        *out = 0x28;
    }

    return true;
}

// ref: FUN_00717050
bool CGUnit_C::GetMovementAnimation(uint32_t allow, int32_t* out) {
    uint32_t moveFlags = this->m_localMove.GetMoveFlags();

    if (!(moveFlags & 0xF)) {
        return (allow & 0xFFFFFFF8) == 0;
    }

    uint32_t animFlags = this->m_animFlags;

    if ((animFlags & 0x70) && (allow & 0x4) && !(animFlags & 0x800000)
        && !(animFlags & 0x2000000)) {
        if (moveFlags & 0x2200000) {
            // Swimming or flying: strafe if strafing, else swim forward or backward.
            if (moveFlags & 0xC) {
                *out = 0x2C - ((moveFlags & 0x4) != 0);
            } else {
                *out = ((moveFlags & 0x2) ? 3 : 0) + 0x2A;
            }

            return true;
        }

        if (this->m_move->IsSplineFlag2000()) {
            *out = 0x87;

            return true;
        }

        if (moveFlags & 0x2) {
            *out = 0xD;

            return true;
        }

        // The stealth creep flag.
        if (this->m_unit->bytes1 & 0x20000) {
            *out = 0x77;

            return true;
        }

        float speed = this->m_localMove.GetCurrentSpeed(0);

        if (speed > 11.0f) {
            *out = 0x8F;

            return true;
        }

        if (speed > this->m_localMove.GetWalkSpeed() + this->m_localMove.GetWalkSpeed()) {
            *out = 0x5;

            return true;
        }

        *out = 0x4;
    }

    return true;
}

// ref: FUN_00724280
bool CGUnit_C::GetLootAnimation(uint32_t allow, int32_t* out) {
    if (!this->m_mountModel && (this->m_animFlags & 0x40) && this->CanShowLootAnimation()) {
        int32_t current = static_cast<int32_t>(this->GetCurrentAnimationId());

        if (!this->IsLooting()) {
            // Finished looting: stand back up out of the loot hold.
            if (current != 0xBC) {
                return (allow & 0xFFFFFFF0) == 0;
            }

            if (allow & 0x8) {
                *out = 0xBD;
            }
        } else if ((allow & 0x8) && current != 0xBC && current != 0x32) {
            *out = 0x32;
        }

        return true;
    }

    return (allow & 0xFFFFFFF0) == 0;
}

// ref: FUN_0071e0d0
bool CGUnit_C::GetCombatAnimation(uint32_t allow, int32_t* out, const uint32_t* decidedFlags) {
    if (!(this->m_animFlags & 0x60) || this->m_attackTarget == 0) {
        return (allow & 0xFFFFFFC0) == 0;
    }

    if (!(allow & 0x20)) {
        return true;
    }

    // Mid-swing already: keep the swing.
    if (decidedFlags && (*decidedFlags & 0x40)) {
        int32_t current = static_cast<int32_t>(this->GetCurrentAnimationId());

        if (IsCombatAnimation(current)) {
            *out = current;

            return true;
        }
    }

    int32_t anim = this->m_heldAnimID;

    if (anim == -1) {
        if (this->m_move->GetMoveFlags() & 0x2200000) {
            *out = 0x29;

            return true;
        }

        anim = this->GetReadyWeaponAnimation();
    }

    *out = anim;

    return true;
}

// ref: FUN_0071e180
bool CGUnit_C::GetTurnAnimation(uint32_t allow, int32_t* out) {
    if (!this->CanPlayTurnAnimation()) {
        return (allow & 0xFFFFFF80) == 0;
    }

    if ((this->m_animFlags & 0x70) && (allow & 0x40)) {
        if (!(this->m_localMove.GetMoveFlags() & 0x10) && !(this->m_animFlags & 0x800)) {
            *out = 0xC;
        } else {
            *out = 0xB;
        }
    }

    return true;
}

// ref: FUN_00714f90
bool CGUnit_C::GetRangedReadyAnimation(uint32_t allow, int32_t* out) {
    if (this->m_sheathState != 2 || !(this->m_animFlags & 0x200)) {
        return (allow & 0xFFFFFF00) == 0;
    }

    if ((allow & 0x80) && !(this->m_animFlags & 0x4000)) {
        *out = 0x19;

        uint8_t pair[2] = {};
        auto ranged = VirtualItemPair(this, 2, pair);

        if (ranged && ranged[0] == 2) {
            switch (ranged[1]) {
                case 0x02:
                    *out = 0x69;
                    break;

                case 0x03:
                case 0x12:
                    *out = 0x6A;
                    break;

                case 0x10:
                    *out = 0x70;
                    break;

                case 0x13:
                    *out = 0x6F;
                    break;

                default:
                    break;
            }
        }
    }

    return true;
}

// ref: FUN_0071e1f0
bool CGUnit_C::GetStandStateAnimation(uint32_t allow, int32_t* out) {
    if (!(this->m_animFlags & 0x40) || this->m_mountModel) {
        return (allow & 0xFFFFFE00) == 0;
    }

    int32_t standState = this->m_unit->bytes1 & 0xFF;
    int32_t last = this->m_lastStandState;
    int32_t anim;

    switch (standState) {
        case 0:
            // Standing up again, from whichever pose the unit was in.
            if (last == 1) {
                anim = 0x62;
            } else if (last == 3) {
                anim = 0x65;
            } else if (last == 8) {
                anim = 0x74;
            } else {
                return (allow & 0xFFFFFE00) == 0;
            }

            break;

        case 1:
            anim = (last == 0 || this->GetCurrentAnimationId() == 0x60) ? 0x60 : 0x61;
            break;

        case 3:
            anim = (last != 0) + 99;

            if (anim == 0x1FA) {
                return true;
            }

            break;

        case 4:
            anim = 0x66;
            break;

        case 5:
            anim = 0x67;
            break;

        case 6:
            anim = 0x68;
            break;

        case 7:
            if (last == 7) {
                return true;
            }

            if (!(this->m_move->GetMoveFlags() & 0x200000)) {
                anim = (this->HasAirborneDeathAnimation() ? 0x1D2 : 0) + 6;
            } else {
                anim = 0x84;
            }

            break;

        case 8:
            anim = (last != 0) + 0x72;

            if (anim == 0x1FA) {
                return true;
            }

            break;

        default:
            return (allow & 0xFFFFFE00) == 0;
    }

    if (allow & 0x100) {
        *out = anim;
    }

    return true;
}

// ref: FUN_007171c0
bool CGUnit_C::GetEmoteStateAnimation(uint32_t allow, int32_t* out) {
    bool eligible = (this->m_animFlags & 0x40) && !this->m_mountModel;

    if (!eligible && !(this->m_animFlags & 0x20)) {
        return (~(allow >> 9) & 1) != 0;
    }

    int32_t emote = static_cast<int32_t>(this->m_unit->emoteState);

    if (emote == 0) {
        return (~(allow >> 9) & 1) != 0;
    }

    auto rec = g_emotesDB.GetRecord(emote);

    if (!rec) {
        return (~(allow >> 9) & 1) != 0;
    }

    // Emote flag 0x2000 on the active player's own unit is suppressed.
    if (ClntObjMgrGetActivePlayer() == this->GetGUID() && (rec->m_flags & 0x2000)) {
        return (~(allow >> 9) & 1) != 0;
    }

    if (allow & 0x200) {
        *out = rec->m_animID;
    }

    return true;
}

// ref: FUN_00724330
bool CGUnit_C::GetSpellCastAnimation(uint32_t allow, int32_t* out, const uint8_t* decidedFlags) {
    if ((this->m_unit->channelSpell == 0 && !(this->m_animFlags & 0x400))
        || !(this->m_animFlags & 0x60) || this->m_mountModel) {
        return (allow & 0xFFFFFFE0) == 0;
    }

    int32_t anim = 0;
    uint32_t kitFlags = 0;

    if (!this->GetSpellVisualAnimation(&anim, &kitFlags)) {
        return (allow & 0xFFFFFFE0) == 0;
    }

    ConvertAnimationFlags(kitFlags, &kitFlags);

    // Already playing it on the upper body but not on the whole model: take it anyway.
    if (anim == static_cast<int32_t>(this->GetCurrentAnimationId())
        && this->GetCurrentAnimationId() != this->GetModelAnimationId()) {
        *out = anim;

        return true;
    }

    if (anim == static_cast<int32_t>(this->GetCurrentAnimationId())) {
        if (!decidedFlags || !(*decidedFlags & 0x10) || !(kitFlags & 0x40)) {
            return true;
        }

        *out = anim;

        return true;
    }

    if (!(allow & 0x10)) {
        return true;
    }

    *out = anim;

    return true;
}

// ref: FUN_00724500
int32_t CGUnit_C::ChooseAnimation(uint32_t allow, uint32_t* decidedFlags, uint8_t* kitFlags) {
    int32_t anim = 0x1FA;

    if (this->GetDeathAnimation(allow, &anim, decidedFlags)) {
        return anim;
    }

    if (this->GetStandState10Animation(allow, &anim)) {
        return anim;
    }

    if (this->m_vehiclePassenger && this->m_vehiclePassenger->GetRideAnimation(allow, &anim)) {
        return anim;
    }

    // A vehicle whose owner drives the pose skips straight to the cast animation and stops.
    if (this->m_vehicle && this->m_vehicle->m_rec
        && this->m_vehicle->OwnerIsControllingAnimation()) {
        this->GetSpellCastAnimation(allow, &anim, kitFlags);

        return anim;
    }

    if (this->GetStandState9Animation(allow, &anim)) {
        return anim;
    }

    if (this->GetForcedMotionAnimation(&anim)) {
        return anim;
    }

    if (this->GetMovementAnimation(allow, &anim)) {
        return anim;
    }

    if (this->GetLootAnimation(allow, &anim)) {
        return anim;
    }

    if (this->GetSpellCastAnimation(allow, &anim, kitFlags)) {
        return anim;
    }

    if (this->GetCombatAnimation(allow, &anim, decidedFlags)) {
        return anim;
    }

    if (this->GetTurnAnimation(allow, &anim)) {
        return anim;
    }

    if (this->GetRangedReadyAnimation(allow, &anim)) {
        return anim;
    }

    if (this->GetStandStateAnimation(allow, &anim)) {
        return anim;
    }

    if (this->GetEmoteStateAnimation(allow, &anim)) {
        return anim;
    }

    if (this->m_vehiclePassenger
        && this->m_vehiclePassenger->GetRideUpperAnimation(allow, &anim)) {
        return anim;
    }

    // Nothing chose: an animation asked for while the model was loading takes precedence, and
    // failing that the unit falls back to its posture.
    if (this->m_pendingAnimID != -1) {
        return this->m_pendingAnimID;
    }

    if (decidedFlags) {
        *decidedFlags = 1;
    }

    if (this->m_deferredAnimID == -1) {
        uint32_t posture = static_cast<uint32_t>(anim);
        this->GetPostureAnimation(&posture, 0);
        anim = static_cast<int32_t>(posture);
    }

    return anim;
}

// ref: FUN_0073ac30
void CGUnit_C::UpdateAnimation(uint32_t setFlags, uint32_t allow) {
    if (this->m_model && !this->m_model->IsLoaded(0, 0)) {
        return;
    }

    uint8_t kitFlags = 0;
    int32_t anim = this->ChooseAnimation(allow, &setFlags, &kitFlags);
    bool play = true;

    if (anim == 0x1FA) {
        // Nothing chose, and the unit is not mid-ride: leave its pose alone.
        if (!this->m_vehiclePassenger || this->m_vehiclePassenger->m_state == 0) {
            play = false;
        }

        anim = 0;
    } else if (anim == this->m_pendingAnimID) {
        this->m_deferredAnimID = anim;
    }

    if (play) {
        this->SetAnimation(static_cast<uint32_t>(anim), setFlags);
    }

    // Stand state 135 on the active player closes whatever window it was using.
    if (this->m_animFlags & 0x40) {
        if (this->GetCurrentAnimationId() == 0x87) {
            auto camera = CGWorldFrame::GetActiveCamera();

            if (camera && camera->GetTarget() == this->GetGUID()) {
                // The reference then calls FUN_005186a0, a GameUI function that closes whatever
                // window this target had open. It is not ported, so only the call is missing; the
                // test that guards it is here.
            }
        }

        this->m_lastStandState = this->m_unit->bytes1 & 0xFF;
    }
}

// ref: FUN_0073bff0
void CGUnit_C::OnAnimationFinished(CM2Model* model, uint32_t boneId, int32_t animID,
                                   int32_t interrupted) {
    if (this->m_deferredAnimID == animID) {
        this->m_deferredAnimID = -1;
    }

    // Clear the "currently playing X" bits the finished animation set (see m_animFlags): 192 LiftOff
    // and 200 clear the airborne bit, 121 and 37 clear their own.
    if (animID == 0xC0 || animID == 200) {
        this->m_animFlags &= ~0x40000;
    }

    if (animID == 0x79) {
        this->m_animFlags &= ~0x80000;
    } else if (animID == 0x25) {
        this->m_animFlags &= ~0x800000;
    }

    if (interrupted == 0) {
        // The animation ran to its end. While deciding what follows it the unit is allowed only a
        // mount change (0x10) -- that is what stops the follow-up from being overridden mid-choice --
        // and once the choice is made all three of 0x10/0x20/0x40 come back.
        this->m_animFlags = (this->m_animFlags & ~0x60) | 0x10;

        this->PlayFollowUpAnimation(model, boneId, animID);

        this->m_animFlags |= 0x70;
    } else if (animID == 0x27 || animID == 0xBB) {
        // A jump that was cut short is no longer in progress.
        this->m_animFlags &= ~0x4;
    }
}

// ref: FUN_0073b510
// An animation ran to its end: what follows it, by the finished animation's behaviour
// (AnimationData +0x18). A jump start goes on into the jump loop, a fall keeps falling, a landing
// re-chooses, sitting down settles into the seated loop, dying lies dead; anything else
// re-chooses with only the mount-change permission (0x10).
//
// PARTIAL: a vehicle seat's ride animations (the top of the reference, CVehiclePassenger_C seat
// +0x3c .. +0x48) are the vehicle port's, and so is nothing here while nothing creates a
// passenger; the fishing bobber the channel cast spawns (FUN_007221d0, behaviour 0x85) is the
// game-object effect port's.
void CGUnit_C::PlayFollowUpAnimation(CM2Model* model, uint32_t boneId, int32_t animID) {
    auto rec = g_animationDataDB.GetRecord(animID);
    int32_t behavior = rec ? rec->m_behaviorID : 0x1FA;

    // The seated, sleeping and kneeling holds follow the stand state the unit is in now.
    auto standState = [this]() {
        return static_cast<int32_t>(this->GetStandStateByte());
    };

    // Lie dead in the pose `next`, from where the bone's sequence got to.
    auto lieDead = [&](uint32_t next) {
        M2BoneSequenceState state = {};
        model->GetBoneSequenceState(boneId, &state);
        this->SetBoneSequence(model, 0xFFFFFFFF, next, state.uint94, 0, 1.0f, 1, 1, 0);
        this->UpdateObjectEffects();
    };

    int32_t next = 0x1FA;

    if (rec && behavior <= 0x1CC) {
        if (0x1C9 < behavior) {
            this->SetAnimation(0x1D0, 0x10);
            return;
        }

        switch (behavior) {
            case 1:
                if (this->IsDeadOrFeigning()) {
                    lieDead(6);
                    return;
                }

                break;

            case 6:
                if (this->IsDeadOrFeigning()) {
                    this->UpdateObjectEffects();
                    return;
                }

                break;

            case 0x25:
            case 0x26:
                if (!(this->m_move->m_moveFlags & 0x2000000)) {
                    this->SetAnimation(0x26, 0x10);
                    return;
                }

                break;

            case 0x27:
                this->m_animFlags &= 0xfffffffb;
                this->UpdateAnimation(0x10, 0xffffffff);
                return;

            case 0x28:
                this->SetAnimation(0x28, 0x10);
                return;

            case 0x32:
                this->SetAnimation(0xBC, 0x10);
                return;

            case 0x45:
                if (!this->IsAttackingOrPetInCombat() && this->m_attackTarget == 0) {
                    this->SetAnimation(0x45, 0x10);
                    return;
                }

                break;

            case 0x60:
            case 0x61:
                next = (standState() != 1) + 0x61;
                break;

            case 99:
            case 100:
                next = (standState() != 3) + 100;
                break;

            case 0x66:
                if (standState() == 4) {
                    next = 0x66;
                }

                break;

            case 0x67:
                if (standState() == 5) {
                    this->SetAnimation(0x67, 0x10);
                    return;
                }

                break;

            case 0x68:
                if (standState() == 6) {
                    this->SetAnimation(0x68, 0x10);
                    return;
                }

                break;

            case 0x69:
                this->SetAnimation(0x6D, 0x10);
                return;

            case 0x6A:
                this->SetAnimation(0x6E, 0x10);
                return;

            case 0x6D:
                // A ranged shot: hold the aim while channelling or still shooting (0x600), else
                // the arrow leaves the hand.
                if (this->m_rangedModel) {
                    if (this->m_unit->channelSpell != 0 || (this->m_animFlags & 0x600)) {
                        this->SetAnimation(0x6D, 0x10);
                        return;
                    }

                    if (this->m_rangedAmmoModel && this->m_rangedAmmoModel->m_attachParent) {
                        this->m_rangedAmmoModel->DetachFromParent();
                        this->m_animFlags &= 0xffffbfff;
                        this->m_rangedModel->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);
                        this->UpdateAnimation(0x10, 0xffffffff);
                        return;
                    }
                }

                break;

            case 0x6E:
                if (this->m_rangedModel && (this->m_unit->channelSpell != 0 || (this->m_animFlags & 0x400))) {
                    this->SetAnimation(0x6E, 0x10);
                    return;
                }

                break;

            case 0x6F:
            case 0x70:
                if (this->m_rangedModel && (this->m_unit->channelSpell != 0 || (this->m_animFlags & 0x400))) {
                    this->SetAnimation(0x6F, 0x10);
                    return;
                }

                break;

            case 0x72:
            case 0x73:
                next = (standState() != 8) + 0x73;
                break;

            case 0x7F:
                this->m_animFlags &= 0xfffffff7;
                this->UpdateAnimation(0x10, 0xffffffff);
                return;

            case 0x83:
                if (this->IsDeadOrFeigning()) {
                    lieDead(0x84);
                    return;
                }

                break;

            case 0x84:
                if (this->IsDeadOrFeigning()) {
                    return;
                }

                break;

            case 0x85:
                // FUN_007221d0: the fishing bobber's splash, see above.
                this->SetAnimation(0x86, 0x10);
                return;

            case 0xC9:
                this->m_animFlags &= 0xffbfffff;
                this->SetAnimation(0xCA, 0x10);
                return;

            default:
                break;
        }

        if (next != 0x1FA) {
            this->SetAnimation(static_cast<uint32_t>(next), 0x10);
            return;
        }

        this->UpdateAnimation(0x10, 0xffffffff);
        return;
    }

    // No record, or a behaviour past the table: the death and posture holds.
    switch (behavior) {
        case 0x1D0: {
            uint32_t posture = 0x1FA;
            this->GetPostureAnimation(&posture, 1);

            if (posture != 0x1FA) {
                this->SetAnimation(posture, 0x10);
                return;
            }

            break;
        }

        case 0x1D2:
        case 0x1D3:
            if (this->IsDeadOrFeigning()) {
                this->SetAnimation(0x1D4 - ((this->m_animFlags & 0x4000000) != 0), 0x10);
                return;
            }

            break;

        case 0x1D4:
            if (this->IsDeadOrFeigning()) {
                lieDead(this->ResolveAnimation(0x1D8, nullptr));
                return;
            }

            break;

        case 0x1D8:
            if (this->IsDeadOrFeigning()) {
                this->UpdateObjectEffects();
                return;
            }

            break;

        default:
            break;
    }

    this->UpdateAnimation(0x10, 0xffffffff);
}

float NormalizeAngle(float angle);

// ref: FUN_004f5130
// The heading from one point to another in the ground plane, with the axis-aligned cases settled
// without atan2: straight up or down the y axis is a half or one and a half pi.
float FacingBetween(const C3Vector& from, const C3Vector& to) {
    float dx = to.x - from.x;
    float dy = to.y - from.y;

    if (std::fabs(dx) < 2.38419e-07f) {
        return dy < 0.0f ? 1.5f * CMath::PI : 0.5f * CMath::PI;
    }

    if (2.38419e-07f <= std::fabs(dy)) {
        return std::atan2(dy, dx);
    }

    return to.x < from.x ? CMath::PI : 0.0f;
}

// ref: FUN_007160b0
// A critically damped spring: (value, velocity) pulled toward the target at `rate` for `dt`
// seconds, with the exponential's Pade-style approximation the reference uses.
void SpringToward(float* state, const float* target, float rate, float dt) {
    float x = rate * dt;
    float decay = 1.0f / (x * x * 0.48f + x * x * x * 0.235f + x + 1.0f);
    float pull = ((state[0] - *target) * rate + state[1]) * dt;

    state[0] = ((state[0] - *target) + pull) * decay + *target;
    state[1] = decay * (state[1] - pull * rate);
}

// ref: FUN_00722640
// Whether the unit turns to face its target: not while casting a spell with attribute 0x80000.
bool CGUnit_C::FacesTarget() const {
    if (this->m_castSpellID == 0) {
        return true;
    }

    auto spell = g_spellDB.GetRecord(this->m_castSpellID);

    return !(spell && (spell->m_attributes & 0x80000));
}

// ref: FUN_0071b700
// The object the active player has open for looting; nothing for anyone else.
WOWGUID CGUnit_C::GetActiveLootTarget() const {
    if (ClntObjMgrGetActivePlayer() != this->GetGUID()) {
        return 0;
    }

    return static_cast<const CGPlayer_C*>(this)->m_lootTarget;
}

// ref: FUN_00735f60
// The facing the camera and the model follow. The active player's is its movement facing as it
// stands, with two bits on the animation flags saying whether it is turning or being steered by
// the mouse (0x1) and turning or being dragged (0x2), and the time it last stood still.
//
// Every other unit's chases a target facing: its movement facing, or -- standing, not emoting,
// not stunned or otherwise held -- the facing toward its target (its channel object while it
// channels for its summoner, or the player while they talk to it). The chase is the creature's
// own CreatureMovementInfo spring when it has one, a four-sample average of the turn for most
// units, and a 20 (30 in combat) spring for creatures with unit flag 0x8.
//
// PARTIAL, the vehicle port's: the vehicle's own aim target (CVehicle_C +0x160), the seat that
// keeps its rider's facing (seat flag 0x400 through FUN_005d3340), and pushing the facing on to
// the vehicle's passengers (FUN_00735ef0). Nothing creates a CVehicle_C yet, so none arises.
void CGUnit_C::UpdateSmoothFacing(const float* seatOffset) {
    float facing = this->m_localMove.m_facing;

    if (this->GetGUID() != ClntObjMgrGetActivePlayer()) {
        auto unitData = this->m_unit;
        bool emoting = false;

        if (unitData->emoteState != 0) {
            auto emote = g_emotesDB.GetRecord(static_cast<int32_t>(unitData->emoteState));
            emoting = emote && !(emote->m_flags & 0x2000);
        }

        if (!(this->m_move->m_moveFlags & 0xf) && this->GetStandStateByte() == 0 && !emoting
            && !(unitData->flags & 0x40000) && !(unitData->flags2 & 0x8000)) {
            if (this->m_stateFlags & 0x1) {
                facing = this->m_heldFacing;
            } else if (!(unitData->flags & 0x1000000) && !this->IsA(TYPE_PLAYER) && this->FacesTarget()) {
                WOWGUID target = 0;

                if (unitData->channelSpell == 0 || !(this->m_stateFlags & 0x8000) || this->IsPlayerControlled()) {
                    target = unitData->target;
                } else {
                    target = unitData->channelObject;
                }

                if (target == 0) {
                    target = this->GetActiveLootTarget();
                }

                if (target == 0 && CGGameUI::GetInteractTarget() == this->GetGUID()) {
                    target = ClntObjMgrGetActivePlayer();
                }

                auto object = target ? static_cast<CGObject_C*>(ClntObjMgrObjectPtr(target, TYPE_OBJECT, __FILE__, __LINE__))
                                     : nullptr;

                if (object) {
                    float toward = FacingBetween(this->GetPosition(), object->GetPosition());

                    if (seatOffset) {
                        facing = NormalizeAngle(toward - *seatOffset);
                    } else if (this->IsTransportUnit()) {
                        auto transport = static_cast<CGUnit_C*>(
                            ClntObjMgrObjectPtr(this->GetTransportGUID(), TYPE_UNIT, __FILE__, __LINE__));

                        facing = transport ? NormalizeAngle(toward - transport->GetWorldSmoothFacing())
                                           : NormalizeAngle(facing);
                    } else if (this->GetTransportGUID() != 0) {
                        facing = NormalizeAngle(toward - MovementGetTransportFacing(this->GetTransportGUID()));
                    } else {
                        facing = NormalizeAngle(toward);
                    }
                }
            }
        }

        int32_t movementID = this->m_creatureStats ? this->m_creatureStats->m_movementID : 0;
        auto movement = g_creatureMovementInfoDB.GetRecord(movementID);
        float dt = CGWorldFrame::s_currentWorldFrame ? CGWorldFrame::s_currentWorldFrame->m_elapsed : 0.0f;

        if (movement && 1e-05f < movement->m_smoothFacingChaseRate) {
            float target = facing;

            if (facing + CMath::PI < this->m_smoothFacing) {
                target = facing + CMath::TWO_PI;
            } else if (this->m_smoothFacing < facing - CMath::PI) {
                target = facing - CMath::TWO_PI;
            }

            SpringToward(&this->m_smoothFacing, &target, movement->m_smoothFacingChaseRate, dt);
            facing = NormalizeAngle(this->m_smoothFacing);
        } else if (!(unitData->flags & 0x8) || this->IsA(TYPE_PLAYER)) {
            float delta = facing - this->m_smoothFacing;

            if (CMath::PI < delta) {
                delta -= CMath::TWO_PI;
            } else if (delta < -CMath::PI) {
                delta += CMath::TWO_PI;
            }

            if (std::fabs(delta) < 0.01f) {
                this->m_smoothFacingHistory[0] = 0.0f;
                this->m_smoothFacing = facing;

                return;
            }

            auto history = this->m_smoothFacingHistory;
            float turn = delta;

            // A turn the other way starts the average over.
            if (0.0f <= delta) {
                if (history[0] < 0.0f) {
                    history[0] = 0.0f;
                }
            } else if (0.0f < history[0]) {
                history[0] = 0.0f;
            }

            if (history[0] == 0.0f) {
                history[0] = delta;
                history[1] = delta;
                history[2] = delta;
                history[3] = delta;
            } else {
                history[3] = history[2];
                history[2] = history[1];
                history[1] = history[0];
                history[0] = delta;
                turn = (history[0] + history[1] + history[2] + history[3]) * 0.25f;

                // Never more than the turn left.
                if (delta <= 0.0f) {
                    if (turn < delta) {
                        turn = delta;
                    }
                } else if (delta < turn) {
                    turn = delta;
                }
            }

            facing = NormalizeAngle(turn * 0.5f + this->m_smoothFacing);
        } else {
            float target = facing;

            if (facing + CMath::PI < this->m_smoothFacing) {
                target = facing + CMath::TWO_PI;
            } else if (this->m_smoothFacing < facing - CMath::PI) {
                target = facing - CMath::TWO_PI;
            }

            SpringToward(&this->m_smoothFacing, &target, this->IsAttackingOrPetInCombat() ? 30.0f : 20.0f, dt);
            facing = NormalizeAngle(this->m_smoothFacing);
        }

        this->m_smoothFacing = facing;

        return;
    }

    this->m_smoothFacing = facing;
    this->m_smoothFacingStep = 0.0f;

    auto input = InputControlGetActive();
    bool turning = (this->m_localMove.m_moveFlags & 0x30) != 0;

    if (turning || (input && input->CanSetFacing())) {
        this->m_animFlags |= 0x1;
    } else {
        this->m_animFlags &= 0xfffffffe;
        this->m_turnStillTime = static_cast<uint32_t>(OsGetAsyncTimeMs());
    }

    if (turning || (input && input->IsDrag(static_cast<uint32_t>(OsGetAsyncTimeMs())))) {
        this->m_animFlags |= 0x2;
    } else {
        this->m_animFlags &= 0xfffffffd;
    }
}

// ref: FUN_0073c090
// The unit's own model reports a sequence done. A unit gone from the object manager still has its
// non-root bones let go of a sequence that ran out.
void CGUnit_C::OnModelSequenceDone(CM2Model* model, uint32_t boneId, uint32_t animID, int32_t interrupted,
                                   int32_t overshoot, WOWGUID owner) {
    (void)overshoot;

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(owner, TYPE_UNIT, __FILE__, __LINE__));

    if (!unit) {
        if (boneId != 0xFFFFFFFF && boneId != 0x1A && interrupted == 0 && model) {
            model->UnsetBoneSequence(boneId, 1, 1);
        }

        return;
    }

    // The vehicle that animates through this bone handles it (FUN_00757280, the vehicle port's;
    // it cannot arise while nothing creates a CVehicle_C).
    if (!unit->m_mountModel && unit->m_vehicle && unit->m_vehicle->m_rec && unit->m_vehicle->TestFlag(boneId)) {
        return;
    }

    unit->OnModelAnimationFinished(model, boneId, static_cast<int32_t>(animID), interrupted);
}

// ref: FUN_0073bbd0
// One of the unit model's sequences is over. The bookkeeping first -- the deferred and held
// animations, the death time, the "playing X" bits -- then, for the root or the body bone and a
// sequence that ran out, the follow-up (FUN_0073b510) with the permission bits handed back.
//
// PARTIAL: the player's queued dance steps (FUN_006e2e10, Player_C +0x1944, the dance studio's
// CMSG_SYNC_DANCE chain, which frozen does not keep) are the Player_C port's.
void CGUnit_C::OnModelAnimationFinished(CM2Model* model, uint32_t boneId, int32_t animID, int32_t interrupted) {
    if (this->m_vehiclePassenger && this->m_vehiclePassenger->m_state != 0) {
        this->m_vehiclePassenger->OnRiderSequenceDone(boneId);
    }

    if (this->m_deferredAnimID == animID) {
        this->m_deferredAnimID = -1;
    }

    auto rec = g_animationDataDB.GetRecord(animID);
    int32_t behavior = rec ? rec->m_behaviorID : 0x1FA;

    if (behavior == 1 || behavior == 0x83 || behavior == 0x1D4) {
        if (this->m_deathTime == 0) {
            this->m_deathTime = CWorld::GetCurTimeMs();
        }

        this->ShowLootSparkle();
    }

    if (animID == this->GetCastVisualAnimation()) {
        this->m_animFlags &= 0xfffeffff;
    }

    if (this->m_intFA4 != -1 && this->m_model && this->m_model->IsLoaded(0, 0)) {
        int32_t held = this->m_intFA4;
        bool upper = this->m_upperBodyBoneId != 0xFFFFFFFF
            && static_cast<int32_t>(this->m_model->GetBoneUint90(this->m_upperBodyBoneId)) == held;
        int32_t body = static_cast<int32_t>(this->m_model->GetBoneUint90(0xFFFFFFFF));

        if ((animID == held && (!upper || body != held)) || (!upper && body != held)) {
            this->m_intFA4 = -1;
        }
    }

    if (IsCombatAnimation(animID) && !IsCombatAnimation(this->m_heldAnimID)) {
        this->m_heldAnimID = -1;
    }

    if (behavior == 0xC0 || behavior == 200) {
        this->m_animFlags &= 0xfffbffff;
    }

    if (behavior == 0x79) {
        this->m_animFlags &= 0xfff7ffff;
    }

    if ((this->m_animFlags & 0x20000) && (IsCombatAnimation(behavior) || IsSpellCastAnimation(behavior))) {
        this->m_animFlags &= 0xffffdfff;
    }

    if (behavior == 0x25) {
        this->m_animFlags &= 0xff7fffff;
    }

    if (rec && 0x1C9 < rec->m_behaviorID && rec->m_behaviorID < 0x1CD) {
        this->m_animFlags &= 0xfdffffff;
    }

    if (interrupted != 0) {
        switch (behavior) {
            case 0x27:
                this->m_animFlags &= 0xfffffffb;
                break;

            case 0x7F:
                this->m_animFlags &= 0xfffffff7;
                break;

            case 0xC9:
                this->m_animFlags &= 0xffbfffff;
                break;

            case 0x59:
            case 0x5A: {
                C3Vector position = this->GetPosition();

                if (boneId == 3) {
                    this->ReturnSwingWeapon(&position, 0, 0x100000);
                } else {
                    this->ReturnSwingWeapon(&position, 1, 0x200000);
                }

                break;
            }

            default:
                break;
        }

        return;
    }

    if (boneId != 0xFFFFFFFF && boneId != 0x1A) {
        this->PlayBoneFollowUpAnimation(model, boneId, behavior);
        return;
    }

    if (this->m_animFlags & 0x8000) {
        this->SetSheathState(this->m_previousSheathState, 1, 0);
        this->m_animFlags &= 0xffff7fff;
    }

    if (this->m_mountTransition && this->m_mountTransition->IsDone()) {
        return;
    }

    this->m_animFlags = (this->m_animFlags & 0xffffffcf) | 0x40;

    this->PlayFollowUpAnimation(model, boneId, animID);

    this->m_animFlags |= 0x70;
}

// ref: FUN_00737bd0
// A sequence ran out on a bone other than the root or the body (the upper body, a hand): a weapon
// swing's end sheathes that hand's weapon, and otherwise the bone takes what the chooser would
// play now -- unless that is the body's own action, or no action at all, in which case the bone is
// let go so it follows its parent again. A spell visual may replace what the bone takes, and a
// seat that animates its rider's upper body keeps it.
//
// PARTIAL, the vehicle port's: a vehicle letting go of the bone lets it go on each rider the seat
// animates too (the passenger list at CVehicle_C +0x170/+0x178, which frozen does not keep).
void CGUnit_C::PlayBoneFollowUpAnimation(CM2Model* model, uint32_t boneId, int32_t behavior) {
    if (behavior == 0xF) {
        return;
    }

    if (0x58 < behavior && behavior < 0x5B) {
        if (boneId == 2) {
            if (!(this->m_animFlags & 0x200000) && this->PlayOffHandSheath()) {
                return;
            }
        } else if (boneId == 3) {
            if (!(this->m_animFlags & 0x100000) && this->PlayMainHandSheath()) {
                return;
            }
        }
    }

    int32_t animID = -1;
    M2BoneSequenceState state = {};
    state.uint90 = 0xFFFFFFFF;
    state.uint94 = 0xFFFFFFFF;
    state.speed = 1.0f;
    bool release = false;

    bool seatHolds = false;

    if (this->m_model && this->m_model->IsLoaded(0, 0)) {
        uint32_t decided = 0;
        uint8_t kitFlags = 0;
        int32_t chosen = this->ChooseAnimation(0xFFFFFFFF, &decided, &kitFlags);

        animID = static_cast<int32_t>(this->ResolveAnimation(static_cast<uint32_t>(chosen), nullptr));
        state.uint90 = static_cast<uint32_t>(animID);

        auto passenger = this->m_vehiclePassenger;
        const VehicleSeatRec* seat = passenger ? passenger->m_seat : nullptr;
        bool seatAnimates = passenger && passenger->m_state == 3 && passenger->GetSeatAnimation(seat) != 0x1FA;

        if (boneId == this->m_upperBodyBoneId) {
            if (kitFlags == 0 && chosen == 0x1FA && seatAnimates) {
                this->UpdateObjectEffects();
                return;
            }

            if (seatAnimates && passenger->GetSeatUpperAnimation(seat) != 0x1FA) {
                seatHolds = true;
            }
        }

        if (!seatHolds) {
            int32_t body = static_cast<int32_t>(this->m_model->GetBoneUint90(0xFFFFFFFF));

            if (!IsActionAnimation(animID) || !this->CanPlayActionAnimation(animID, body) || animID == body) {
                release = true;
            }
        }
    }

    M2BoneSequenceState out = {};

    if (!this->ApplyEffectAnimation(nullptr, nullptr, &state, 1, nullptr, nullptr, &out, nullptr, nullptr)) {
        if (release) {
            if (model && (!this->m_vehiclePassenger || !this->m_vehiclePassenger->IsRidingLiveVehicle())) {
                if (model->IsLoaded(0, 0) && model->BoneHasParent(boneId)) {
                    model->UnsetBoneSequence(boneId, 1, 1);
                }

                // The vehicle's riders, see above.
            }

            this->UpdateObjectEffects();
            return;
        }

        out.uint90 = static_cast<uint32_t>(animID);
        out.uint94 = 0xFFFFFFFF;
        out.currentTime = 0;
        out.speed = 1.0f;
    }

    this->SetBoneSequence(model, boneId, out.uint90, out.uint94, static_cast<uint32_t>(out.currentTime), out.speed,
                          1, 1, 0);
    this->UpdateObjectEffects();
}

// ref: FUN_00717ba0
// A unit that died lootable (dynamic flag 0x1) shows the loot sparkle, once: nothing is added
// while one is already on its effect list.
//
// DIVERGED: the reference releases the effect after InitializeLootArt even when that freed it
// (its model failed to load); frozen checks that the effect was linked before releasing it.
void CGUnit_C::ShowLootSparkle() {
    if (!(this->m_unit->dynamicFlags & 0x1)) {
        return;
    }

    for (auto effect = this->m_effects; effect; effect = effect->m_linkNext) {
        if (effect->m_flags & CEffect::EFFECT_LOOT_ART) {
            return;
        }
    }

    void* mem = SMemAlloc(sizeof(CEffect), __FILE__, __LINE__, 0);

    if (!mem) {
        return;
    }

    auto effect = new (mem) CEffect();
    effect->InitializeLootArt(this, nullptr);

    if (this->m_effects == effect) {
        effect->Release();
    }
}

// ref: FUN_004d07b0
// The sound a weapon of this material makes going into its sheath or coming out of it, two yards
// above `position`; the active player's own is played as the listener's.
void PlaySheathSound(const UNIT_WEAPON_INFO* info, int32_t toSheath, const C3Vector* position, int32_t isActivePlayer) {
    if (!info) {
        return;
    }

    auto material = g_materialDB.GetRecord(info->m_material);

    if (!material) {
        return;
    }

    C3Vector at = { position->x, position->y, position->z + 2.0f };

    SoundKitProperties properties;
    properties.ResetToDefaults();

    if (isActivePlayer) {
        properties.int20 = 0x6E;
    }

    SI2::PlaySoundKit(toSheath == 0 ? material->m_unsheatheSoundID : material->m_sheatheSoundID, &at, nullptr,
                      &properties, 0, nullptr, 1, 0);
}

// ref: FUN_00732500
// A swing is over: the hand's weapon goes back where the sheath state wants it -- the state the
// swing temporarily drew (+0xb5c), or the one before it when the `drawnBit` animation flag is clear
// -- and makes its sound. A ranged weapon held the wrong way round for the hand stays put.
void CGUnit_C::ReturnSwingWeapon(const C3Vector* position, int32_t hand, uint32_t drawnBit) {
    auto handInfo = this->GetWeaponInfo(hand, 0);
    auto ranged = this->GetWeaponInfo(2, 0);
    int32_t state;
    int32_t toSheath;

    if ((this->m_animFlags & drawnBit) && this->m_sheathState != 0) {
        state = this->m_sheathState;
        toSheath = 0;
    } else {
        state = this->m_previousSheathState;
        toSheath = 1;
    }

    bool isActive = this->GetGUID() == ClntObjMgrGetActivePlayer();

    if (state == 2) {
        bool keep = false;

        if (ranged) {
            if (ranged->m_inventoryType == 0xF) {
                keep = hand == 0;
            } else if (static_cast<uint8_t>(ranged->m_inventoryType - 0x19) < 2) {
                keep = hand == 1;
            }
        }

        if (!keep) {
            this->AttachRangedWeapon(toSheath, nullptr);
            PlaySheathSound(ranged, toSheath, position, isActive ? 1 : 0);
        }
    } else {
        this->MoveHandItem(hand, toSheath);
        PlaySheathSound(handInfo, toSheath, position, isActive ? 1 : 0);
    }

    if (this->m_previousSheathState == 2) {
        this->AttachHandItem(0);
    }

    PortraitRefresh(this->GetGUID(), 1);
}

// ref: FUN_007228b0
// The animation the spell being cast shows, from its visual's kit (the kit's +4), or -1.
int32_t CGUnit_C::GetCastVisualAnimation() const {
    if (!this->m_castSpellID) {
        return -1;
    }

    auto spell = g_spellDB.GetRecord(this->m_castSpellID);

    if (!spell) {
        return -1;
    }

    auto visual = const_cast<CGUnit_C*>(this)->GetSpellVisualRec(spell);

    if (!visual) {
        return -1;
    }

    auto kit = g_spellVisualKitDB.GetRecord(visual->m_precastKit);

    return kit ? kit->m_startAnimID : -1;
}

// ref: FUN_0073c140
// The fourth argument is the "replaced" flag (1 from NotifySequenceDone, 0 for a sequence that
// ran out), the fifth the time past its end. This passed the fifth on as the flag, which made a
// run-out with any overshoot look replaced and a replaced one look finished.
void CGUnit_C::OnSequenceDone(CM2Model* model, uint32_t boneId, uint32_t animID, int32_t interrupted,
                              int32_t overshoot, WOWGUID owner) {
    (void)overshoot;

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(owner, TYPE_UNIT, __FILE__, __LINE__));

    if (!unit) {
        return;
    }

    // A mounted unit whose vehicle animates through this bone lets the vehicle handle it instead.
    // FUN_00757280, the vehicle-side handler, is not ported, so that case is simply not handled --
    // it cannot arise while nothing creates a CVehicle_C.
    if (unit->m_mountModel && unit->m_vehicle && unit->m_vehicle->m_rec
        && unit->m_vehicle->TestFlag(boneId)) {
        return;
    }

    unit->OnAnimationFinished(model, boneId, static_cast<int32_t>(animID), interrupted);
}

// ref: FUN_007370d0
// The unit's world object follows its model: the model's own placement (position, facing and the
// ground tilt SetWorldTransform applied, with the scale in it), the model's box and sphere --
// combined with what is attached to it once the attachments are in -- and a collision centre half
// the unit's height up.
//
// TWO THINGS ARE NOT THE REFERENCE'S YET. The height is the unit's +0x854, which nothing writes
// through a plain float store anywhere in the binary (it arrives in a block copy not yet traced),
// so it is taken from CreatureModelData's collision height here, scaled the way the model is. And
// the vehicle arms -- a seat's matrix from AnimateAndGetWorldMatrix, the seat's reach added to the
// sphere, and the passengers updated after -- wait on the vehicle module, which frozen does not
// create.
void CGUnit_C::UpdateWorldObject(int32_t noRelink) {
    if (!this->m_worldObject) {
        return;
    }

    CM2Model* model = this->GetObjectModel();

    C44Matrix matrix;

    if (model) {
        matrix = model->matrixB4;
    } else {
        matrix.Identity();
        matrix.Translate(this->GetPosition());
        matrix.RotateAroundZ(this->GetFacing());
        matrix.Scale(this->GetScale());
    }

    CAaBox box = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    CAaSphere sphere = { { 0.0f, 0.0f, 0.0f }, 0.0f };

    auto modelData = this->GetModelData();
    float height = modelData ? modelData->m_collisionHeight * this->GetModelScale() : 0.0f;
    C3Vector collisionCenter = { 0.0f, 0.0f, height * 0.5f };

    if (model && model->IsLoaded(0, 0)) {
        if (!model->IsLoaded(0, 1)) {
            model->GetBoundingBox(box);
            model->GetBoundingSphere(sphere);
        } else {
            model->GetCombinedBounds(box);
            model->GetCombinedSphere(sphere);
        }
    }

    CWorld::UpdateObject(this->m_worldObject, matrix, box, sphere, collisionCenter, noRelink, 0);
}

// ref: FUN_0072a000
// PHASE4: the reference's first branch -- `checkPossess` with bit 0x80 of the unit's +0xf42 byte
// set -- walks the unit's own aura table for a SPELL_AURA 0x117 effect and answers with the
// caster's name. The aura table is the Unit_C port's (it is AuraCache in frozen today), so that
// branch lands with it.
const char* CGUnit_C::GetUnitName(const char** realm, int32_t checkPossess) {
    auto data = this->Unit();
    WOWGUID guid = this->GetGUID();

    if (!this->IsA(TYPE_PLAYER)) {
        if (data && data->petNumber != 0) {
            DBCACHEKEY32 key(data->petNumber);

            auto pet = g_petNameCache.GetRecord(key, &guid, &CGUnit_C::OnNameArrived, nullptr, true);

            if (pet) {
                if (pet->m_timestamp == data->petNameTimestamp) {
                    return pet->m_name;
                }

                // A renamed pet: the stale record goes and the new name is asked for. PHASE4: the
                // reference first hands the old name to the combat log (FUN_0074f400) so lines
                // already printed keep it; that is UnitCombatLog_C's.
                g_petNameCache.Invalidate(key);
                g_petNameCache.GetRecord(key, &guid, &CGUnit_C::OnNameArrived, nullptr, true);
            }
        } else if (this->m_creatureStats) {
            return this->m_creatureStats->m_names[0];
        }
    } else {
        auto name = g_nameCache.GetRecord(DBCACHEKEY64(guid), &guid, &CGUnit_C::OnNameArrived, nullptr, true);

        if (name) {
            if (realm && name->m_realm[0]) {
                *realm = name->m_realm;
            }

            return name->m_name;
        }
    }

    auto unknown = FrameScript_GetText("UNKNOWNOBJECT", -1, GENDER_NOT_APPLICABLE);

    if (!unknown || !*unknown) {
        unknown = "Unknown Being";
    }

    return unknown;
}

// ref: FUN_00728ca0
// PHASE4: three steps of the reference are not here yet, each waiting on a module of its own --
// the name plate is hidden (FUN_00725840, NamePlate), the tooltip's level line is refreshed when
// it shows this unit (FUN_00512ab0), and the party member slot the unit fills takes the new name
// and signals PARTY_MEMBER event 0xa5 (FUN_005139b0). The UNIT_NAME_UPDATE signal every frame
// waits on is here.
void CGUnit_C::OnNameArrived(uint32_t id, const WOWGUID* guid, void* param, bool found) {
    if (!found) {
        return;
    }

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(*guid, TYPE_UNIT, __FILE__, __LINE__));

    if (!unit) {
        return;
    }

    // FUN_00512b00
    ScriptEventsSignalUnitEvent(*guid, SCRIPT_UNIT_NAME_UPDATE);
}

// ref: FUN_0072cde0
// The template arrived: the unit takes it, a creature of a family re-runs its scale, and the
// collision box is rebuilt against it. PARTIAL: the name plate's removal (FUN_00725840) and the
// party slot (FUN_005139b0) are the name plate and party ports'.
void CGUnit_C::OnCreatureStatsArrived(uint32_t id, const WOWGUID* guid, void* param, bool found) {
    auto stats = g_creatureCache.Peek(DBCACHEKEY32(id));

    if (!stats) {
        return;
    }

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(*guid, TYPE_UNIT, __FILE__, __LINE__));

    if (unit) {
        unit->m_creatureStats = stats;

        if (stats->m_family != 0) {
            unit->UpdateDisplayScale(0);
        }

        unit->UpdateCollisionBox(1, 0);
    }

    // FUN_00512b00, then FUN_0060bf10 with event 0x94.
    ScriptEventsSignalUnitEvent(*guid, SCRIPT_UNIT_NAME_UPDATE);
    ScriptEventsSignalUnitEvent(*guid, 0x94);
}

// ------------------------------------------------------------------------------------------------
// The effect- and mount-facing helpers
// ------------------------------------------------------------------------------------------------

float NormalizeAngle(float angle);

namespace {

// The model attachment each spell visual attachment index names (0x00adaa20). Index 13 reads
// entry 4 (0x00adaa30) instead of its own.
const int32_t s_spellAttachments[16] = { 20, 34, 19, 21, 22, 17, 23, 24, 25, 15, 16, 37, 38, 22, 47, 48 };

// How high above a unit an attachment sits when its model lacks it, by attachment (0x00adb630).
const float s_attachmentHeights[50] = {
    1.0f, 1.0f, 1.0f, 1.5f, 1.5f, 1.8f, 1.8f, 0.5f, 0.5f, 1.0f,
    1.0f, 2.0f, 1.5f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f, 2.5f, 0.0f,
    2.0f, 1.5f, 1.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 3.0f,
    1.5f, 1.5f, 1.0f, 1.0f, 1.5f, 1.0f, 1.0f, 2.5f, 1.5f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
};

// The dismount sound, the SoundEntries row named "SpiritWolf (DONOTRENAME)" (DAT_00ca12cc).
int32_t s_dismountSoundID;

// The camera follows a unit whose model changed under it.
void RetargetCamera(CGUnit_C* unit) {
    auto camera = CGWorldFrame::GetActiveCamera();

    if (camera && camera->GetTarget() == unit->GetGUID()) {
        camera->SetTarget(unit->GetGUID());
    }
}

} // namespace

// ref: FUN_00747860
// PHASE4(UnitSound): the footstep tables FUN_00747760 builds first are the unit sound port's.
void UnitSoundInitialize() {
    CVar::Register("FootstepSounds", nullptr, 0x0, "1", nullptr, SOUND);

    for (uint32_t i = 0; i < static_cast<uint32_t>(g_soundEntriesDB.GetNumRecords()); i++) {
        auto entry = g_soundEntriesDB.GetRecordByIndex(i);

        if (entry && entry->m_name && !SStrCmp(entry->m_name, "SpiritWolf (DONOTRENAME)", STORM_MAX_STR)) {
            s_dismountSoundID = entry->m_ID;

            break;
        }
    }
}

// ref: FUN_006e6f80
CM2Model* CGUnit_C::GetObjectModel() {
    return this->m_mountModel ? this->m_mountModel : this->m_model;
}

// ref: FUN_00715b50
float CGUnit_C::GetFadeInAlpha() {
    if (this->m_displayInfo) {
        return static_cast<float>(this->m_displayInfo->m_creatureModelAlpha) * 0.003921568859368563f;
    }

    return 1.0f;
}

// ref: FUN_0074b8b0
bool CGUnit_C::IsTransportUnit() const {
    WOWGUID transport = this->m_localMove.GetTransportGUID();
    uint32_t low = static_cast<uint32_t>(transport);
    uint32_t high = static_cast<uint32_t>(transport >> 32);

    if ((high & 0xF0F00000) == 0xF0500000) {
        return true;
    }

    if ((high & 0xF0000000) != 0) {
        return false;
    }

    return !(low == 0 && (high & 0xF07FFFFF) == 0);
}

// ref: FUN_00717e50
// The reference reads the movement of whatever the walk last reached, which is null when a unit
// in the chain is not there; the walk stops at the last unit found instead.
float CGUnit_C::GetWorldSmoothFacing() {
    float facing = 0.0f;
    CGUnit_C* unit = this;

    for (;;) {
        facing += unit->m_smoothFacing;

        if (!unit->IsTransportUnit()) {
            break;
        }

        auto next = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(unit->GetTransportGUID(), TYPE_UNIT, ".\\Unit_C.cpp", 0x206a));

        if (!next) {
            break;
        }

        unit = next;
    }

    return unit->m_localMove.GetFacing(facing);
}

// ref: FUN_00716190
bool CGUnit_C::IsSittingStandState() const {
    uint8_t standState = static_cast<uint8_t>(this->m_unit->bytes1 & 0xFF);

    return standState == 1 || (4 <= standState && standState <= 6);
}

// ref: FUN_00722180
bool CGUnit_C::IsAnimationLocked() {
    if (this->m_vehicle && this->m_vehicle->m_rec && this->m_vehicle->ControlsPassengerAnimation()) {
        return true;
    }

    if (this->m_creatureStats) {
        return (this->m_creatureStats->m_typeFlags >> 3) & 1;
    }

    return this->GetCurrentAnimationId() == 0x79;
}

// ref: FUN_00736640
void CGUnit_C::PlayWoundAnimation(int32_t critical) {
    if (this->IsDeadOrFeigning() || !this->m_model || !this->m_model->IsLoaded(0, 0) || this->IsAnimationLocked()) {
        return;
    }

    for (auto effect = this->m_effects; effect; effect = effect->m_linkNext) {
        if (effect->m_flags & 0x4) {
            return;
        }
    }

    uint32_t animID;
    uint32_t boneID = this->m_upperBodyBoneId;

    if (critical) {
        animID = 10;
    } else if (this->m_attackTarget) {
        animID = 9;
    } else {
        animID = 8;

        if (!this->m_mountModel && (this->m_localMove.GetMoveFlags() & 0x2E0100F) == 0
            && !this->m_localMove.IsUnsupportedOrHovering() && !this->IsSittingStandState()
            && GetAnimationBehavior(this->GetCurrentAnimationId()) != 0x62) {
            boneID = 0xFFFFFFFF;
        }
    }

    if (boneID == this->m_upperBodyBoneId) {
        uint32_t upper = this->m_model->GetBoneUint90(0xFFFFFFFF);

        if (IsReadyAnimation(upper) || upper == 0) {
            boneID = 0xFFFFFFFF;
        }
    }

    animID = this->ResolveAnimation(animID, nullptr);

    if (this->m_model->HasSequence(animID)) {
        this->SetBoneSequence(this->m_model, boneID, animID, 0xFFFFFFFF, 0, 1.0f, 0, 0, 0);
    }

    this->UpdateObjectEffects();
}

// The riders of this vehicle whose animation it drives, while `model` is the vehicle's own: the walk
// the passenger variants below repeat their change over.
template <class F>
static void ForEachAnimatedRider(CGUnit_C* unit, CM2Model* model, int32_t line, F f) {
    auto vehicle = unit->m_vehicle;

    if (!vehicle || !vehicle->m_rec || !vehicle->HasStateBits() || model != unit->GetObjectModel()) {
        return;
    }

    for (auto passenger = vehicle->m_passengers.Head(); passenger; passenger = vehicle->m_passengers.Next(passenger)) {
        auto rider = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(passenger->m_guid, TYPE_UNIT, ".\\Unit_C.cpp", line));

        if (rider && rider->m_vehiclePassenger && rider->m_vehiclePassenger->IsRidingLiveVehicle()) {
            f(rider);
        }
    }
}

// ref: FUN_00735bb0
void CGUnit_C::SetBoneSequenceTimeOnPassengers(CM2Model* model, uint32_t boneId, int32_t time, int32_t fromPassenger) {
    if (!model) {
        return;
    }

    if (!fromPassenger && this->m_vehiclePassenger && this->m_vehiclePassenger->IsRidingLiveVehicle()) {
        return;
    }

    model->SetBoneSequenceTime(boneId, time);

    ForEachAnimatedRider(this, model, 0x1a25, [&](CGUnit_C* rider) {
        rider->SetBoneSequenceTimeOnPassengers(rider->GetObjectModel(), boneId, time, 1);
    });
}

// ref: FUN_00735dd0
void CGUnit_C::SetAnimationHoldOnPassengers(CM2Model* model, int32_t hold, int32_t fromPassenger) {
    if (!model) {
        return;
    }

    if (!fromPassenger && this->m_vehiclePassenger && this->m_vehiclePassenger->IsRidingLiveVehicle()) {
        return;
    }

    if (hold) {
        uint32_t time = model->m_scene->m_time;
        model->m_animationHeldTime = time ? time : 1;
    } else {
        model->m_animationHeldTime = 0;
    }

    ForEachAnimatedRider(this, model, 0x1a6e, [&](CGUnit_C* rider) {
        rider->SetAnimationHoldOnPassengers(rider->GetObjectModel(), hold, 1);
    });
}

// ref: FUN_007202c0
bool CGUnit_C::DrawsThroughComponent() const {
    if (this->IsA(TYPE_PLAYER) && this->m_modelData && (this->m_modelData->m_flags & 0x4)
        && this->m_displayInfoExtra && (this->m_displayInfoExtra->m_flags & 0x1)) {
        return true;
    }

    if (this->m_formReverted) {
        return true;
    }

    if (this->m_localDisplayID) {
        return this->m_localDisplayID == this->m_unit->nativeDisplayID;
    }

    return this->m_unit->nativeDisplayID == this->m_unit->displayID;
}

// ref: FUN_00723730
void CGUnit_C::ReapplyItemSection(int32_t section) {
    if (this->DrawsThroughComponent() && this->IsA(TYPE_PLAYER)) {
        int32_t slot = ComponentItemSlotToInvSlot(section);

        if (slot != -1) {
            this->m_characterComponent->RemoveItem(static_cast<ITEM_SLOT>(section));

            auto player = static_cast<CGPlayer_C*>(this);
            player->ApplyVisibleItem(&player->Player()->visibleItems[slot], slot);
        }

        return;
    }

    if (!this->m_characterComponent || !this->m_displayInfoExtra) {
        return;
    }

    this->m_characterComponent->RemoveItem(static_cast<ITEM_SLOT>(section));

    int32_t displayID = this->m_displayInfoExtra->m_npcitemDisplay[section];

    if (displayID) {
        this->m_characterComponent->AddItem(static_cast<ITEM_SLOT>(section), displayID, 0);
    }
}

// ref: FUN_00715670
void CGUnit_C::SetMountTransition(MountTransitionObject* transition, CEffect* effect) {
    this->m_mountTransition = transition;
    this->m_mountTransitionEffect = effect;
}

// ref: FUN_00715690
void CGUnit_C::ClearMountTransition() {
    this->m_mountTransition = nullptr;
    this->m_mountTransitionEffect = nullptr;
}

// ref: FUN_007412b0
void CGUnit_C::ReleaseMountTransition() {
    if (this->m_mountTransitionEffect) {
        this->SetMountDisplay(this->m_mountTransitionEffect->m_displayID);
    }

    this->m_mountTransitionEffect = nullptr;
    this->m_mountTransition = nullptr;
}

// ref: FUN_00715d90
uint32_t CGUnit_C::GetCreatureTypeFlag22() const {
    return this->m_creatureStats ? (this->m_creatureStats->m_typeFlags >> 22) & 1 : 0;
}

// ref: FUN_00715db0
uint32_t CGUnit_C::GetCreatureTypeFlag25() const {
    return this->m_creatureStats ? (this->m_creatureStats->m_typeFlags >> 25) & 1 : 0;
}

// ref: FUN_00717ad0
float CGUnit_C::GetModelHeight() {
    auto modelData = this->m_modelData ? this->m_modelData : this->GetModelData();

    if (!modelData) {
        return 0.0f;
    }

    return this->GetScale() * (modelData->m_geoBoxMaxZ - modelData->m_geoBoxMinZ);
}

// ref: FUN_0071a7f0
int32_t CGUnit_C::HasSpellAttachment(int32_t attachment, int32_t worldAttach) {
    if (!this->m_model || !this->m_model->IsLoaded(0, 0)) {
        return 0;
    }

    if (worldAttach) {
        return this->m_model->HasAttachment(attachment);
    }

    if (attachment == 13) {
        return this->m_model->HasAttachment(s_spellAttachments[4]);
    }

    return this->m_model->HasAttachment(s_spellAttachments[attachment]);
}

// ref: FUN_0071a860
// Attachment 13 is the weapon in the right hand: the first of the weapon model's attachments 4..0
// it carries, else the default.
C3Vector& CGUnit_C::GetSpellAttachmentWorldPosition(C3Vector& out, int32_t attachment, const C3Vector& offset,
                                                    int32_t worldAttach) {
    if (worldAttach) {
        return this->GetAttachmentPosition(out, attachment, &offset);
    }

    if (attachment != 13) {
        return this->GetAttachmentPosition(out, s_spellAttachments[attachment], &offset);
    }

    for (auto child = this->m_model->m_attachList; child; child = child->m_attachNext) {
        if (child->m_attachId != 1) {
            continue;
        }

        for (int32_t id = 4; id >= 0; id--) {
            if (child->HasAttachment(id)) {
                C44Matrix transform = child->GetAttachmentWorldTransform(id);
                out = offset * transform;

                return out;
            }
        }

        break;
    }

    return this->GetAttachmentPosition(out, s_spellAttachments[4], &offset);
}

// ref: FUN_00746bd0
C3Vector& CGUnit_C::GetAttachmentPosition(C3Vector& out, uint32_t attachment, const C3Vector* offset) {
    bool hasOffset = offset && (offset->x != 0.0f || offset->y != 0.0f || offset->z != 0.0f);

    if (this->m_model && this->m_model->IsLoaded(0, 0) && this->m_model->HasAttachment(attachment)) {
        if (hasOffset) {
            C44Matrix transform = this->m_model->GetAttachmentWorldTransform(attachment);
            out = *offset * transform;

            return out;
        }

        out = this->m_model->GetAttachmentWorldPosition(attachment);

        return out;
    }

    C3Vector point = offset ? *offset : C3Vector(0.0f, 0.0f, 0.0f);

    if (attachment < 50) {
        point.z += s_attachmentHeights[attachment];
    }

    if (!hasOffset) {
        C3Vector base = this->m_vehiclePassenger ? this->GetModelWorldPosition() : this->GetPosition();
        out = { base.x + point.x, base.y + point.y, base.z + point.z };

        return out;
    }

    C44Matrix world;

    if (this->m_vehiclePassenger) {
        this->GetModelWorldMatrix(world);
    } else {
        this->GetWorldMatrix(world);
    }

    out = point * world;

    return out;
}

// ref: FUN_0071a3f0
const CreatureSoundDataRec* CGUnit_C::GetSoundData() const {
    if (this->m_mountSoundData) {
        return this->m_mountSoundData;
    }

    if (this->m_soundData && this->m_unit->petNumber && 0 < this->m_soundData->m_creatureSoundDataIdpet) {
        return g_creatureSoundDataDB.GetRecord(this->m_soundData->m_creatureSoundDataIdpet);
    }

    return this->m_soundData;
}

// ref: FUN_00740450
// A vehicle with riders keeps its mount (m_stateFlags 0x10000000) until they are off.
void CGUnit_C::SetMountDisplay(int32_t displayID) {
    if (displayID == this->m_mountDisplayID) {
        return;
    }

    if (displayID == 0 && this->m_vehicle && this->m_vehicle->m_passengers.Head()) {
        this->m_stateFlags |= 0x10000000;
        return;
    }

    this->m_stateFlags &= ~0x10000000u;

    if (this->m_mountDisplayID) {
        this->Dismount(displayID == 0);
    }

    int32_t previous = this->m_mountDisplayID;
    this->m_mountDisplayID = displayID;

    if (displayID) {
        auto transition = this->m_mountTransition;

        this->Mount(0, previous == 0);

        // No transition leads to this mount: it appears with the hard-coded mount effect.
        if (!transition) {
            auto effect = CEffect::Create();
            WOWGUID guid = this->GetGUID();
            effect->InitializeHardcoded(6, guid, &CGObject_C::KitEffectOneShot);
            effect->m_flags |= 0x20;
            effect->Release();
        }
    }

    this->UpdateMountSound();
    this->AttachQuestMarker();
    PlayerNameInvalidate(this->m_nameDesc);
    this->UpdateObjectEffectPackage();
}

// ref: FUN_0073d5d0
void CGUnit_C::Mount(int32_t displayID, int32_t checkCollision) {
    this->m_boneMask = 0;

    if (!this->m_mountModel) {
        auto display = g_creatureDisplayInfoDB.GetRecord(this->m_mountDisplayID);

        if (!display) {
            return;
        }

        auto modelData = g_creatureModelDataDB.GetRecord(display->m_modelID);

        if (!modelData) {
            return;
        }

        this->m_mountScale = display->m_creatureModelScale;

        auto mount = CWorld::GetM2Scene()->CreateModel(modelData->m_modelName, 0);

        if (!mount) {
            return;
        }

        mount->SetSequenceDoneCallback(&CGUnit_C::OnSequenceDone, this->GetGUID());
        mount->SetAnimEventCallback(&CGUnit_C::AnimEventCallback, this->GetGUID());

        C44Matrix placement = this->m_model->matrixB4;
        mount->m_flag8000 = 1;
        mount->matrixB4 = placement;

        CCharacterComponent::ApplyMonsterGeosets(mount, display);
        CCharacterComponent::ReplaceMonsterSkin(mount, display, modelData);

        this->SetMountModel(mount);
        mount->Release();

        this->m_mountFootprintTexture = modelData->m_footprintTextureID;
        this->m_mountFootprintLength = modelData->m_footprintTextureWidth * 0.02777777798473835f;
        this->m_mountFootprintWidth = 0.02777777798473835f * modelData->m_footprintTextureLength;
        this->m_mountSoundData = this->GetMountSoundData();

        this->m_mountModel->m_flag80000 = this->m_model->m_flag80000;
        this->m_model->m_flag80000 = 0;

        auto parent = this->m_model->m_attachParent;
        uint32_t parentAttach = this->m_model->m_attachId;

        if (parent) {
            this->m_model->DetachFromParent();
        }

        this->m_model->AttachToParent(this->m_mountModel, 0, nullptr, 0);

        if (parent) {
            this->m_mountModel->AttachToParent(parent, parentAttach, nullptr, 0);
        }

        this->SetBoneSequence(this->m_model, 0xFFFFFFFF, this->m_mountedAnimID, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);

        // The rider is drawn at its own scale, not the mount's.
        C44Matrix unscale;
        unscale.Scale(1.0f / this->m_mountScale);
        this->m_model->m_flag8000 = 1;
        this->m_model->matrixB4 = unscale;

        this->m_mountModel->m_baseDiffuse = this->m_model->m_baseDiffuse;
        this->m_model->m_baseAlpha = 1.0f;
        this->m_model->m_baseDiffuse = { 1.0f, 1.0f, 1.0f };
        this->m_model->m_baseEmissive = { 0.0f, 0.0f, 0.0f };

        // A player mounting where the mount does not fit gets off again. The reference hands the
        // first argument on as the fit test's skip flag.
        if (this->IsA(TYPE_PLAYER) && checkCollision
            && !this->UpdateMountedCollision(modelData->m_mountHeight, displayID)) {
            this->RequestDismount();
        }
    }

    if (this->m_mountTransitionEffect && (this->m_mountTransitionEffect->m_flags & 0x100) == 0) {
        this->m_mountTransitionEffect->Finish();
    }

    this->m_animFlags |= 0x10;
    this->UpdateAnimation(0, 0xFFFFFFFF);

    RetargetCamera(this);

    this->UpdateObjectEffects();
}

// ref: FUN_0073d940
void CGUnit_C::Dismount(int32_t restoreCollision) {
    if (!this->m_mountModel) {
        return;
    }

    auto mount = this->m_model->m_attachParent;

    if (mount) {
        auto parent = mount->m_attachParent;
        uint32_t parentAttach = mount->m_attachId;

        this->m_model->m_flag80000 = mount->m_flag80000;
        this->m_model->DetachFromParent();

        if (parent) {
            this->m_model->AttachToParent(parent, parentAttach, nullptr, 0);
        }
    }

    this->m_model->m_baseDiffuse = this->m_mountModel->m_baseDiffuse;

    this->SetMountModel(nullptr);
    this->SetBoneSequence(this->m_model, 0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 0, 1, 0);

    this->m_animFlags &= 0xFF77DFFB;
    this->UpdateAnimation(0, 0xFFFFFFFF);

    this->UpdateShadowRadius();
    this->UpdateEffectAttachments();

    this->m_mountSoundData = nullptr;

    RetargetCamera(this);

    this->PlayDismountSound();

    if (restoreCollision) {
        this->UpdateCollisionBox(1, 0);
    }

    if (this->GetObjectModel()->IsLoaded(0, 0)) {
        this->UpdateBoneMask();
    } else {
        this->m_boneMask = 0;
    }

    this->UpdateObjectEffects();

    this->m_stateFlags &= ~0x10000000u;
}

// ref: FUN_00717910
void CGUnit_C::SetMountModel(CM2Model* model) {
    if (model == this->m_mountModel) {
        return;
    }

    this->Virtual0E0();

    if (model) {
        model->m_refCount++;
    }

    if (this->m_mountModel) {
        if (this->m_mountModel->m_attachParent) {
            this->m_mountModel->DetachFromParent();
        }

        this->m_mountModel->SetLoadedCallback(nullptr, nullptr);
        this->m_mountModel->Release();
    }

    this->m_mountModel = model;

    if (!model) {
        if (this->GetObjectModel()->IsLoaded(0, 0)) {
            this->UpdateHeight();
        } else {
            this->SetFlag21();
        }
    } else {
        model->SetLoadedCallback(&CGObject_C::ModelLoadedCallback, this);
    }

    if (this->m_worldObject) {
        CWorld::SetObjectModel(this->m_worldObject, this->GetObjectModel());
    }
}

// ref: FUN_007195d0
const CreatureSoundDataRec* CGUnit_C::GetMountSoundData() const {
    auto display = g_creatureDisplayInfoDB.GetRecord(this->m_mountDisplayID);

    if (!display) {
        return nullptr;
    }

    if (auto sound = g_creatureSoundDataDB.GetRecord(display->m_soundID)) {
        return sound;
    }

    auto modelData = g_creatureModelDataDB.GetRecord(display->m_modelID);

    return modelData ? g_creatureSoundDataDB.GetRecord(modelData->m_soundID) : nullptr;
}

// ref: FUN_00720330
void CGUnit_C::UpdateShadowRadius() {
    CAaBox box;
    this->GetShadowBox(box);

    float dx = box.t.x - box.b.x;
    float dy = box.t.y - box.b.y;

    if (2.384185791015625e-07f <= fabsf(dx) || 2.384185791015625e-07f <= fabsf(dy)) {
        float radius = sqrtf(dy * dy + dx * dx) * 0.5f;
        float scaled = this->GetScale() * radius;

        this->m_shadowRadius = sqrtf(scaled);

        if (5.0f < scaled) {
            float over = scaled - 5.0f;
            this->m_shadowRadius = over * over * 0.05999999865889549f + sqrtf(scaled);
        }

        if (this->m_shadowRadius <= 10.0f) {
            return;
        }

        this->m_shadowRadius = 10.0f;

        return;
    }

    this->m_shadowRadius = 1.2000000476837158f;
}

// ref: FUN_00715fd0
void CGUnit_C::UpdateBoneMask() {
    this->m_boneMask = 0;

    auto model = this->GetObjectModel();

    for (uint32_t bone = 0x1B; bone < 0x23; bone++) {
        if (model->HasBone(bone)) {
            this->m_boneMask |= 1u << (bone - 0x1B);
            this->m_boneValues[bone - 0x1B] = 0;
        }
    }
}

// ref: FUN_007467f0
// PHASE4(Sound): the reference first asks the sound engine whether the same sound already plays
// within six yards (FUN_004cfe00 over SESound's channel list) and leaves it to that one.
void CGUnit_C::UpdateMountSound() {
    auto soundData = this->GetSoundData();

    if (0 < this->m_unit->health && (this->m_unit->flags2 & 0x1) == 0 && soundData
        && soundData->m_loopSoundID != 0 && !SI2::IsPlaying(this->m_mountSound)) {
        C3Vector position = this->GetPosition();

        SoundKitProperties properties;
        properties.ResetToDefaults();
        properties.uint1c = 1;
        properties.int30 = 0;
        properties.byte38 = 1;

        SI2::PlaySoundKit(soundData->m_loopSoundID, &position, this->m_mountSound, &properties, 0, nullptr, 1, 0);
        this->m_mountSound->SetObjectGUID(this->GetGUID());

        return;
    }

    SI2::StopOrFadeOut(this->m_mountSound, 0, 0.5f, 1);
}

// ref: FUN_007470d0
void CGUnit_C::PlayDismountSound() {
    static CVar* listenerAtCharacter = CVar::Lookup("Sound_ListenerAtCharacter");

    bool isMover = CGUnit_C::s_activeMover == this->GetGUID();
    bool atCharacter = isMover && listenerAtCharacter && listenerAtCharacter->GetInt() != 0;

    C3Vector position = this->GetPosition();

    SoundKitProperties properties;
    properties.ResetToDefaults();

    if (isMover) {
        properties.int20 = 0x6E;

        if (atCharacter) {
            properties.m_fadeOutTime = 0.6499999761581421f;
        }
    }

    SI2::PlaySoundKit(s_dismountSoundID, atCharacter ? nullptr : &position, nullptr, &properties, 0, nullptr, 1, 0);
}

// ref: FUN_007412e0
void CGUnit_C::RequestDismount() {
    if (this->GetGUID() != ClntObjMgrGetActivePlayer() || (this->m_unit->flags >> 20) & 1) {
        return;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_CANCEL_MOUNT_AURA));
    msg.Finalize();
    ClientServices::Send(&msg);

    if (this->m_mountDisplayID && this->m_vehicle && this->m_vehicle->m_passengers.Head()) {
        this->m_stateFlags |= 0x10000000;
    } else if (this->m_mountDisplayID) {
        this->m_stateFlags &= ~0x10000000u;
        this->Dismount(1);
        this->m_mountDisplayID = 0;
        this->UpdateMountSound();
        this->AttachQuestMarker();
        PlayerNameInvalidate(this->m_nameDesc);
        this->UpdateObjectEffectPackage();
    }
}

namespace {

// The states a death (0xf) or a feign (0x51) ends: standing and moving, flying and swimming, and
// every pace and direction of movement.
void ClearMovementObjectEffectStates(CObjectEffect* effects) {
    static const uint32_t s_states[] = {
        0x25, 0x26, 6, 7, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b,
        0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42,
    };

    for (auto state : s_states) {
        effects->ClearState(state, 1);
    }
}

} // namespace

// ref: FUN_0071e5b0
// The unit's ObjectEffect states follow its three bone sequences (the mount's, the body's and the
// upper body's): a sequence's new state goes on -- a death ends the movement states -- and one no
// sequence plays any longer goes off.
void CGUnit_C::UpdateObjectEffects() {
    auto effects = this->m_objectEffects;

    if (!effects) {
        return;
    }

    M2BoneSequenceState sequences[3];
    this->GetBoneSequenceStates(&sequences[0], &sequences[1], &sequences[2], 1);

    if (this->m_mountModel) {
        if (!this->m_mountModel->IsLoaded(0, 0)) {
            return;
        }

        if (sequences[0].uint90 < 0x1fa) {
            sequences[0].uint90 = this->m_mountModel->ResolveSequenceFallback(sequences[0].uint90);
        }
    }

    if (this->m_model) {
        if (!this->m_model->IsLoaded(0, 0)) {
            return;
        }

        if (sequences[1].uint90 < 0x1fa) {
            sequences[1].uint90 = this->m_model->ResolveSequenceFallback(sequences[1].uint90);
        }

        if (sequences[2].uint90 < 0x1fa) {
            sequences[2].uint90 = this->m_model->ResolveSequenceFallback(sequences[2].uint90);
        }
    }

    int32_t states[3];
    int32_t slots[3];
    int32_t count = 0;

    for (int32_t slot = 0; slot < 3; slot++) {
        if (sequences[slot].uint90 != 0xffffffff) {
            states[count] = ObjectEffectGetAnimState(sequences[slot].uint90, sequences[slot].uint94);
            slots[count] = slot;
            count++;
        }
    }

    int32_t previous[3] = {
        this->m_objectEffectAnimStates[0], this->m_objectEffectAnimStates[1], this->m_objectEffectAnimStates[2],
    };

    auto playing = [&](int32_t state) {
        for (int32_t i = 0; i < count; i++) {
            if (states[i] == state) {
                return true;
            }
        }

        return false;
    };

    for (auto& held : this->m_objectEffectAnimStates) {
        if (held && !playing(held)) {
            held = 0;
        }
    }

    for (int32_t i = 0; i < count; i++) {
        int32_t state = states[i];
        bool held = false;

        for (auto current : this->m_objectEffectAnimStates) {
            if (current == state) {
                held = true;
                break;
            }
        }

        if (held) {
            continue;
        }

        if (state) {
            effects->SetState(static_cast<uint32_t>(state), 1, 0);

            if (state == 0xf || state == 0x51) {
                ClearMovementObjectEffectStates(effects);
            }
        }

        this->m_objectEffectAnimStates[slots[i]] = state;
    }

    for (auto state : previous) {
        if (state && !playing(state)) {
            effects->ClearState(static_cast<uint32_t>(state), 1);
        }
    }
}

// ref: FUN_00725df0
// The unit's ObjectEffect package is its display's -- the mount's while it rides, or a local
// display that still matches the native one -- and the manager is remade when that changes,
// starting in the movement states it is in.
void CGUnit_C::UpdateObjectEffectPackage() {
    int32_t displayID = this->m_mountDisplayID;

    if (!displayID) {
        displayID = this->m_localDisplayID;

        if (!displayID || this->m_unit->nativeDisplayID != this->m_unit->displayID) {
            displayID = this->m_unit->displayID;
        }
    }

    auto display = g_creatureDisplayInfoDB.GetRecord(displayID);
    int32_t packageID = display ? display->m_objectEffectPackageID : 0;

    if (this->m_objectEffects && this->m_objectEffects->m_packageID != packageID) {
        this->m_objectEffectAnimStates[0] = 0;
        this->m_objectEffectAnimStates[1] = 0;
        this->m_objectEffectAnimStates[2] = 0;

        delete this->m_objectEffects;
        this->m_objectEffects = nullptr;
    }

    if (!packageID || this->m_objectEffects) {
        return;
    }

    this->m_objectEffects = new CObjectEffect();

    if (this->m_objectEffects->Init(display->m_objectEffectPackageID, this)) {
        this->m_objectEffectAnimStates[0] = 0;
        this->m_objectEffectAnimStates[1] = 0;
        this->m_objectEffectAnimStates[2] = 0;

        this->UpdateMovementEffects();

        return;
    }

    delete this->m_objectEffects;
    this->m_objectEffects = nullptr;
}


// ref: FUN_0071c0e0
float CGUnit_C::GetScale() const {
    float scale = this->m_displayScale * this->m_scaleMultiplier * this->m_scale;

    if (this->m_mountModel) {
        scale *= this->m_mountScale;
    }

    return scale;
}

// ref: FUN_0071fd80
// PHASE4(Unit_C): a swimming or flying unit pitches with its movement (FUN_00719b80 for the active
// mover, FUN_00719a90 for the rest), a unit leading a mount transition is placed along it
// (FUN_007193f0), and the transition's model follows the unit (FUN_0071fbf0). Those land with the
// movement smoothing they read; until then every unit takes the ground placement below.
int32_t CGUnit_C::PlaceModel(float elapsed) {
    // A rider is placed by its seat.
    if (this->m_vehiclePassenger) {
        this->m_vehiclePassenger->PlaceModel();
        return 1;
    }

    // FUN_007197d0: the lean eases toward the movement's up vector while that is not too steep.
    C3Vector up = this->m_localMove.GetWorldUp();

    if (0.3572123646736145f <= up.z) {
        float ease = static_cast<float>(pow(0.0017999890446662903, static_cast<double>(elapsed)));

        this->m_tiltAxis = {
            (this->m_tiltAxis.x - up.x) * ease + up.x,
            (this->m_tiltAxis.y - up.y) * ease + up.y,
            (this->m_tiltAxis.z - up.z) * ease + up.z
        };
    }

    auto model = this->GetObjectModel();

    if (model) {
        float scale = this->GetScale();
        float facing = this->GetRenderFacing();

        model->SetWorldTransform(this->GetPosition(), facing, scale, &this->m_tiltAxis);
    }

    return 1;
}

// ------------------------------------------------------------------------------------------------
// Auras
// ------------------------------------------------------------------------------------------------

namespace {

// The companion kinds COMPANION_UPDATE names (0x00acccc4).
const char* const s_companionTypes[2] = { "CRITTER", "MOUNT" };

// ref: FUN_0053b440
void SignalCompanionUpdate(int32_t type) {
    if (type == 2) {
        FrameScript_SignalEvent(0x260, nullptr);

        return;
    }

    FrameScript_SignalEvent(0x260, "%s", s_companionTypes[type]);
}

// ref: FUN_007fe3e0
// The aura vision a player needs to see a spell's aura: its RequiredAuraVision level as a bit,
// 0x20 for a stealth-detect effect and 0x40 for an invisibility-detect one of type 0 or 10.
uint32_t SpellAuraVisionMask(const SpellRec* spell) {
    uint32_t mask = spell->m_requiredAuraVision < 1 ? 0 : 1u << (spell->m_requiredAuraVision - 1);

    for (int32_t i = 0; i < 3; i++) {
        if (spell->m_effectAura[i] == 0x11) {
            mask |= 0x20;
        }

        if (spell->m_effectAura[i] == 0x13 && (spell->m_effectMiscValue[i] == 0 || spell->m_effectMiscValue[i] == 10)) {
            mask |= 0x40;
        }
    }

    return mask;
}

// The party slot the guid fills (FUN_006cf670), for whether its auras matter to the party frames.
bool IsPartyMember(WOWGUID guid) {
    for (uint32_t i = 0; i < 4; i++) {
        if (CGPartyInfo::GetMemberGuid(i) == guid) {
            return true;
        }
    }

    return false;
}

const SpellVisualKitRec* GetStateKit(const SpellVisualRec* visual, int32_t done) {
    if (!visual) {
        return nullptr;
    }

    return g_spellVisualKitDB.GetRecord(done ? visual->m_stateDoneKit : visual->m_stateKit);
}

} // namespace

const SpellVisualRec* GetSpellVisual(const SpellRec* spell);

// ref: FUN_00716510
void CAuraState::Read(const CGUnit_C* unit, uint32_t now, CDataStore* msg) {
    msg->Get(reinterpret_cast<uint32_t&>(this->m_spellID));

    if (this->m_spellID == 0) {
        return;
    }

    msg->Get(this->m_flags);
    msg->Get(this->m_level);
    msg->Get(this->m_stacks);

    if ((this->m_flags & 0x8) == 0) {
        SmartGUID caster;
        *msg >> caster;
        this->m_caster = caster;
    } else {
        this->m_caster = unit->GetGUID();
    }

    if ((this->m_flags & 0x20) == 0) {
        this->m_maxDuration = 0;
        this->m_expireTime = 0;

        return;
    }

    uint32_t remaining;
    msg->Get(reinterpret_cast<uint32_t&>(this->m_maxDuration));
    msg->Get(remaining);

    this->m_expireTime = remaining + now;

    if (this->m_expireTime == 0) {
        this->m_expireTime = 1;
    }
}

// ref: FUN_007300a0
int32_t ReceiveAuraUpdate(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    SmartGUID packed;
    *msg >> packed;
    WOWGUID guid = packed;

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x4a3));

    if (unit) {
        unit->UpdateAuras(msg, msgId == SMSG_AURA_UPDATE_ALL);

        return 1;
    }

    // The rest of the message is skipped.
    msg->Seek(msg->Size());

    return 0;
}

// ref: FUN_0072f360
void CGUnit_C::GrowAuras(uint32_t slot) {
    uint32_t count = this->m_auras.Count();

    if (slot < count) {
        return;
    }

    this->m_auras.SetCount(slot + 1);

    for (uint32_t i = count; i <= slot; i++) {
        this->m_auras[i].m_spellID = 0;
    }

    for (auto& states : this->m_auraSlotStates) {
        uint32_t from = states.Count();
        states.SetCount(slot + 1);

        for (uint32_t i = from; i <= slot; i++) {
            states[i] = CAuraSlotState();
        }
    }

    uint32_t from = this->m_auraVisualSpells.Count();
    this->m_auraVisualSpells.SetCount(slot + 1);

    for (uint32_t i = from; i <= slot; i++) {
        this->m_auraVisualSpells[i] = 0;
    }
}

// ref: FUN_00556e10
const CAuraState* CGUnit_C::GetAura(uint32_t slot) const {
    if (slot >= this->m_auras.Count()) {
        return nullptr;
    }

    return &this->m_auras[slot];
}

// ref: FUN_005a1120
bool CGUnit_C::HasAuraType(uint32_t auraType) const {
    return (this->m_auraTypeMask[auraType >> 3] >> (auraType & 7)) & 1;
}

// ref: FUN_00727e70
void CGUnit_C::RebuildAuraTypeMask() {
    memset(this->m_auraTypeMask, 0, sizeof(this->m_auraTypeMask));

    for (uint32_t i = 0; i < this->m_auras.Count(); i++) {
        const CAuraState& aura = this->m_auras[i];
        auto spell = g_spellDB.GetRecord(aura.m_spellID);

        if (!spell) {
            continue;
        }

        for (int32_t effect = 0; effect < 3; effect++) {
            if (aura.m_flags & (1 << effect)) {
                uint32_t type = static_cast<uint32_t>(spell->m_effectAura[effect]);

                if (type < sizeof(this->m_auraTypeMask) * 8) {
                    this->m_auraTypeMask[type >> 3] |= 1 << (type & 7);
                }
            }
        }
    }
}

// ref: FUN_0072f5d0
// TODO(SpellBookFrame): an aura change on the player, its pet or charm, its target or focus, or a
// party member refreshes the spell book (FUN_0053cf10); for the target and focus the reference
// also re-tests the cast in progress against the player's detection (FUN_007262e0) and, when that
// flips, refreshes the cast bar (FUN_00720e50) and signals UNIT_SPELLCAST_(NOT_)INTERRUPTIBLE.
// TODO(UnitCombatLog_C): a stack change (FUN_00752860) and a refresh (FUN_00752ba0) are logged.
void CGUnit_C::UpdateAuras(CDataStore* msg, int32_t all) {
    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());

    uint32_t oldCount = this->m_auras.Count();
    TSGrowableArray<CAuraState> old;
    old.SetCount(oldCount);

    for (uint32_t i = 0; i < oldCount; i++) {
        old[i] = this->m_auras[i];
    }

    uint8_t touched[256];

    if (!all) {
        memset(touched, 0, sizeof(touched));
    } else {
        for (uint32_t i = 0; i < oldCount; i++) {
            this->m_auras[i] = CAuraState();
        }

        memset(touched, 1, sizeof(touched));
    }

    while (!msg->IsRead()) {
        uint8_t slot;
        msg->Get(slot);

        touched[slot] = 1;

        this->GrowAuras(slot);
        this->m_auras[slot].Read(this, now, msg);
    }

    this->m_stateFlags &= ~0x40000u;
    this->RebuildAuraTypeMask();

    bool changed = false;

    // What went: a slot that had an applied aura and now has nothing, or something else.
    for (uint32_t i = 0; i < oldCount; i++) {
        if (!touched[i]) {
            continue;
        }

        const CAuraState& was = old[i];
        int32_t newSpell = this->m_auras[i].m_spellID;

        bool wasActive = was.m_spellID != 0 && (was.m_flags & 7) != 0;
        bool stillActive = was.m_spellID == newSpell && (this->m_auras[i].m_flags & 7) != 0;

        if ((wasActive && !stillActive) || newSpell == 0) {
            if (!all) {
                this->OnAuraRemoved(i, was.m_flags >> 7, &was, was.m_spellID);
                changed = true;
            } else if (auto spell = g_spellDB.GetRecord(was.m_spellID)) {
                this->RemoveAuraVisual(i, spell);
            }
        }
    }

    // What came.
    for (uint32_t i = 0; i < this->m_auras.Count(); i++) {
        if (!touched[i]) {
            continue;
        }

        bool inOld = i < oldCount;
        int32_t oldSpell = inOld ? old[i].m_spellID : 0;
        bool oldActive = inOld && (old[i].m_flags & 7) != 0;

        const CAuraState& aura = this->m_auras[i];
        bool same = oldSpell == aura.m_spellID && oldActive;

        if (aura.m_spellID == 0 || (aura.m_flags & 7) == 0) {
            continue;
        }

        if (all) {
            if (auto spell = g_spellDB.GetRecord(aura.m_spellID)) {
                this->AddAuraVisual(i, spell);
            }

            if (this->GetGUID() == ClntObjMgrGetActivePlayer()) {
                // TODO(ActionBarFrame): FUN_005aab10 refreshes the action buttons of the spell.
                if (CGMinimapFrame::HasTrackingSpell(aura.m_spellID)) {
                    CGMinimapFrame::SetTrackingSpell(aura.m_spellID);
                }
            }

            continue;
        }

        if (!same) {
            this->OnAuraApplied(i, aura.m_flags >> 7, &aura, aura.m_spellID);
            changed = true;
        }
    }

    (void)changed;
    (void)IsPartyMember;

    // UNIT_AURA, once for the update, through the unit event queue (0x0072fded).
    WOWGUID guid = this->GetGUID();
    ScriptEventsQueueUnitEvent(guid, SCRIPT_UNIT_AURA);
}

// ref: FUN_00727760
// TODO(UnitCombatLog_C): FUN_00752710 logs the gain.
// TODO(ActionBarFrame): FUN_005aab10 refreshes the action buttons of the spell.
void CGUnit_C::OnAuraApplied(uint32_t slot, int32_t negative, const CAuraState* aura, int32_t spellID) {
    auto spell = g_spellDB.GetRecord(spellID);

    if (spell) {
        this->AddAuraVisual(slot, spell);
    }

    if (this->GetGUID() == ClntObjMgrGetActivePlayer()) {
        if (static_cast<uint32_t>(spellID) != CGMinimapFrame::s_trackingSpell && CGMinimapFrame::HasTrackingSpell(spellID)) {
            CGMinimapFrame::SetTrackingSpell(spellID);
        }

        // TODO(ShapeshiftBar): a spell of the shapeshift bar (FUN_0053bd40) without a 0x24 effect
        // signals UPDATE_SHAPESHIFT_FORMS (0x179).
    }
}

// ref: FUN_00722090
// TODO(UnitCombatLog_C): FUN_00752710 logs the fade.
// TODO(ActionBarFrame) and TODO(ShapeshiftBar): as in OnAuraApplied.
void CGUnit_C::OnAuraRemoved(uint32_t slot, int32_t negative, const CAuraState* aura, int32_t spellID) {
    auto spell = g_spellDB.GetRecord(spellID);

    if (spell) {
        this->RemoveAuraVisual(slot, spell);
    }

    if (this->GetGUID() == ClntObjMgrGetActivePlayer()) {
        if (CGMinimapFrame::HasTrackingSpell(spellID)) {
            CGMinimapFrame::SetTrackingSpell(0);
        }
    }
}

// ref: FUN_00724820
// TODO(Player_C): effect 0xe9 (every humanoid's model, FUN_006d73b0) and 0x111 (FUN_006d7490) walk
// the visible players with a kit; TODO(SpellBookFrame): effects 0x106 and 0x113 refresh the spell
// book; TODO(Spell_C): a periodic client trigger (effect 0x30) is registered (FUN_0080df10).
// PHASE4(Missile_C): a spell with a missile shows its state kit when the missile lands; the
// reference skips it while one of the unit's missiles (+0x9ec) is still in flight.
void CGUnit_C::AddAuraVisual(uint32_t slot, const SpellRec* spell) {
    if (slot >= this->m_auraVisualSpells.Count()) {
        return;
    }

    int32_t auraSpell = this->m_auras[slot].m_spellID;

    if (this->m_auraVisualSpells[slot] != 0) {
        if (!spell) {
            this->m_auraVisualSpells[slot] = auraSpell;

            return;
        }

        if (this->m_auraVisualSpells[slot] == spell->m_ID) {
            return;
        }

        auto showing = g_spellDB.GetRecord(this->m_auraVisualSpells[slot]);

        if (showing && spell->m_spellPriority < showing->m_spellPriority) {
            return;
        }
    }

    if (spell) {
        uint32_t vision = SpellAuraVisionMask(spell);

        if (vision) {
            auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

            if (!player || (player->Player()->field_bytes_2_4 & vision) == 0) {
                return;
            }
        }
    }

    this->m_auraVisualSpells[slot] = auraSpell;

    if (!spell) {
        return;
    }

    if (this->GetGUID() == ClntObjMgrGetActivePlayer()) {
        for (int32_t i = 0; i < 3; i++) {
            switch (spell->m_effectAura[i]) {
                case 0x4C: {
                    auto camera = CGWorldFrame::GetActiveCamera();

                    if (camera) {
                        camera->SetFirstPersonLook(static_cast<float>(spell->m_effectMiscValue[i]));
                    }

                    break;
                }

                case 0x104:
                    CGWorldFrame::UpdateScreenEffect();
                    break;

                default:
                    break;
            }
        }
    }

    for (int32_t i = 0; i < 3; i++) {
        if (spell->m_effectAura[i] == 0x4E) {
            SignalCompanionUpdate(1);

            // The rider's pose on this mount: the state kit's animation.
            auto visual = spell->m_spellVisualID[0] ? g_spellVisualDB.GetRecord(spell->m_spellVisualID[0]) : nullptr;
            auto kit = GetStateKit(visual, 0);

            if (kit && 0 <= kit->m_animID) {
                this->m_mountedAnimID = kit->m_animID;
            }
        } else if (spell->m_effectAura[i] == 0x117) {
            // FUN_00512b00
            ScriptEventsSignalUnitEvent(this->GetGUID(), SCRIPT_UNIT_NAME_UPDATE);
        }
    }

    auto visual = GetSpellVisual(spell);

    if (!visual) {
        return;
    }

    auto kit = GetStateKit(visual, 0);

    if (kit) {
        for (int32_t i = 0; i < 4; i++) {
            if (kit->m_charProc[i] == 0xB && (!this->m_model || !this->m_model->IsLoaded(0, 0))) {
                this->m_pendingStateKitSpell = spell->m_ID;

                goto played;
            }
        }

        {
            SPELLVISUALKITPARAMS params;
            params.m_spell = spell;
            params.m_kit = kit;
            params.m_kitType = 2;
            params.m_stateParam = 1;
            params.m_param9 = -1;
            params.m_target = this->m_auras[slot].m_caster;

            this->PlayKit(params);
        }
    }

played:
    if (visual->m_flags & 0x8) {
        this->UpdateSheathedAuraVisuals(1, 1);
    }
}

// ref: FUN_0071e930
// TODO(Player_C) / TODO(SpellBookFrame) / TODO(Spell_C): as in AddAuraVisual, and effect 0x124
// clears the pending auction query (FUN_005a0f10).
void CGUnit_C::RemoveAuraVisual(uint32_t slot, const SpellRec* spell) {
    if (slot >= this->m_auraVisualSpells.Count()) {
        return;
    }

    if (this->GetGUID() == ClntObjMgrGetActivePlayer()) {
        for (int32_t i = 0; i < 3; i++) {
            switch (spell->m_effectAura[i]) {
                case 0x4C: {
                    auto camera = CGWorldFrame::GetActiveCamera();

                    if (camera) {
                        camera->SetFirstPersonLook(0.0f);
                    }

                    break;
                }

                case 0x4E: {
                    SignalCompanionUpdate(1);

                    auto visual = spell->m_spellVisualID[0] ? g_spellVisualDB.GetRecord(spell->m_spellVisualID[0]) : nullptr;
                    auto kit = GetStateKit(visual, 0);

                    if (kit && 0 <= kit->m_animID && this->m_mountedAnimID == kit->m_animID) {
                        this->m_mountedAnimID = 0x5B;
                    }

                    break;
                }

                case 0x104:
                    CGWorldFrame::UpdateScreenEffect();
                    break;

                default:
                    break;
            }
        }
    }

    for (int32_t i = 0; i < 3; i++) {
        if (spell->m_effectAura[i] == 0x117) {
            // FUN_00512b00
            ScriptEventsSignalUnitEvent(this->GetGUID(), SCRIPT_UNIT_NAME_UPDATE);
        }
    }

    int32_t showing = this->m_auraVisualSpells[slot];

    if (showing == 0) {
        return;
    }

    this->RemoveAlphaEffects(showing);
    this->ApplyAlphaEffects();

    for (auto effect = this->m_colorEffects; effect; ) {
        auto next = effect->m_linkNext;

        if (effect->m_spellID == showing) {
            effect->Release();
        }

        effect = next;
    }

    this->StopEffects(showing, 1);

    auto kit = GetStateKit(GetSpellVisual(spell), 1);

    if (kit) {
        SPELLVISUALKITPARAMS params;
        params.m_kit = kit;
        params.m_kitType = 8;
        params.m_stateParam = 1;
        params.m_param9 = -1;

        this->PlayKit(params);
    }

    this->m_auraVisualSpells[slot] = 0;
}

// ref: FUN_00720400
void CGUnit_C::UpdateSheathedAuraVisuals(int32_t show, int32_t force) {
    uint32_t state = this->m_stateFlags;
    uint32_t visible = show & (state >> 16) & 1 & (this->m_sheathState == 0 ? 1 : 0) & (this->m_castSpellID == 0 ? 1 : 0);

    if (!force && (static_cast<uint32_t>(visible) == ((state >> 14) & 1) || (state & 0x2000) == 0)) {
        return;
    }

    this->m_stateFlags = state & ~0x2000u;

    for (uint32_t slot = 0; slot < this->m_auraVisualSpells.Count(); slot++) {
        int32_t spellID = this->m_auraVisualSpells[slot];

        if (spellID == 0) {
            continue;
        }

        auto spell = g_spellDB.GetRecord(spellID);

        if (!spell) {
            continue;
        }

        auto visual = this->GetSpellVisualRec(spell);
        auto kit = visual ? g_spellVisualKitDB.GetRecord(visual->m_stateKit) : nullptr;

        if (!visual || !visual->m_stateKit || (visual->m_flags & 0x8) == 0 || !kit) {
            continue;
        }

        if (!visible) {
            this->m_stateFlags &= ~0x4000u;
            this->StopEffects(this->m_auraVisualSpells[slot], 1);
        } else if ((this->m_stateFlags & 0x4000) == 0) {
            SPELLVISUALKITPARAMS params;
            params.m_spell = spell;
            params.m_kit = kit;
            params.m_kitType = 2;

            this->PlayKit(params);
            this->m_stateFlags |= 0x4000;
        }

        this->m_stateFlags |= 0x2000;
    }
}

// ref: FUN_0071ab80
void CGUnit_C::RemoveAlphaEffects(int32_t spellID) {
    for (auto effect = this->m_alphaEffects; effect; ) {
        auto next = effect->m_linkNext;

        if (effect->m_spellID == spellID) {
            this->m_alphaHidesShadow = effect->m_kit && (effect->m_kit->m_flags & 0x400) ? 1 : 0;
            effect->Release();
        }

        effect = next;
    }
}

// ref: FUN_0071abe0
void CGUnit_C::ApplyAlphaEffects() {
    float alpha = this->GetFadeInAlpha();
    uint32_t duration = 1000;

    if (auto effect = this->m_alphaEffects) {
        alpha *= effect->m_alpha;

        if (effect->m_param) {
            duration = effect->m_param;
        }
    }

    this->SetAlpha(alpha, duration);
}

// ref: FUN_007178e0
void CGUnit_C::RemoveColorEffects(int32_t spellID) {
    for (auto effect = this->m_colorEffects; effect; ) {
        auto next = effect->m_linkNext;

        if (effect->m_spellID == spellID) {
            effect->Release();
        }

        effect = next;
    }
}

// ------------------------------------------------------------------------------------------------
// Field handlers
// ------------------------------------------------------------------------------------------------

int32_t OnVirtualItemChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param);
int32_t OnDisplayChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param);
int32_t OnSheathChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param);

namespace {

CGUnit_C* HandlerUnit(WOWGUID guid, int32_t line) {
    return static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", line));
}

// ref: FUN_007167c0
// UNIT_FIELD_BYTES_1 byte 3: the animation tier the unit plays its moves at.
int32_t OnAnimTierChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x27a);

    if (unit) {
        int32_t tier = static_cast<uint8_t>(unit->Unit()->bytes1 >> 24);

        if (tier != unit->m_animTier) {
            unit->m_animTier = tier;
        }
    }

    return 1;
}

// ref: FUN_00716900
// UNIT_FIELD_TARGET.
int32_t OnTargetChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x47c);

    if (unit) {
        unit->m_targetChangeTime = static_cast<uint32_t>(OsGetAsyncTimeMs());
    }

    return 1;
}

// ref: FUN_007419c0
// UNIT_FIELD_MOUNTDISPLAYID.
int32_t OnMountDisplayChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x28c);

    if (unit) {
        unit->SetMountDisplay(unit->Unit()->mountDisplayID);
    }

    return 1;
}

// ref: FUN_0073f4b0
// UNIT_DYNAMIC_FLAGS.
int32_t OnEmoteStateChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x3eb);

    if (unit) {
        unit->UpdateAnimation(0, 0xFFFFFFFF);
    }

    return 1;
}


// ref: FUN_00730050
int32_t OnLevelFieldChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    int32_t was = *static_cast<const int32_t*>(old);
    auto unit = HandlerUnit(guid, 0x2a0);

    if (unit && unit->Unit()->level != was) {
        unit->OnLevelChanged();
    }

    return 1;
}

// ref: FUN_0072ceb0
// OBJECT_FIELD_ENTRY moved: the creature template is looked up again, and once it is in the cache
// the unit takes it and its name event fires.
//
// PARTIAL: the name plate's removal (FUN_00725840), the party slot (FUN_005139b0) and the target
// frame (FUN_00512b00) are the name plate, party and UI ports'.
int32_t OnEntryChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x2b4);

    if (unit) {
        WOWGUID key = guid;
        auto stats = g_creatureCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(unit->GetEntryID())), &key,
                                               &CGUnit_C::OnCreatureStatsArrived, nullptr, false);

        if (stats) {
            unit->m_creatureStats = stats;
            ScriptEventsSignalUnitEvent(guid, 0x94);
        }
    }

    return 1;
}

// ref: FUN_0073f330
// UNIT_FIELD_HEALTH moved: the reported health follows it, a bleeding unit (one with blood) under
// a fifth of its health is marked (state 0x2), and crossing zero is a death or a revival -- the
// active player's movement re-runs first.
//
// PARTIAL: the name plate's health (FUN_0098e5b0) and the unit frames (FUN_0053cf10) are the name
// plate and UI ports'.
int32_t OnHealthChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x2d4);

    if (!unit) {
        return 1;
    }

    auto data = unit->Unit();
    unit->m_reportedHealth = static_cast<uint32_t>(data->health);

    if (0.2f <= static_cast<float>(data->health) / static_cast<float>(data->maxHealth) || unit->IsDead()) {
        unit->m_stateFlags &= 0xFFFFFFFD;
    } else if (unit->m_bloodRec) {
        unit->m_stateFlags |= 0x2;
    }

    int32_t was = *static_cast<const int32_t*>(old);
    bool isActive = guid == ClntObjMgrGetActivePlayer();

    if (data->health < 1) {
        if (0 < was) {
            if (isActive) {
                if (auto input = InputControlGetActive()) {
                    input->UpdatePlayerMovement(static_cast<uint32_t>(OsGetAsyncTimeMs()), 1);
                }
            }

            unit->OnDeath();
        }
    } else if (was <= 0) {
        if (isActive) {
            if (auto input = InputControlGetActive()) {
                input->UpdatePlayerMovement(static_cast<uint32_t>(OsGetAsyncTimeMs()), 1);
            }
        }

        unit->OnResurrect(0);
    }

    return 1;
}

// ref: FUN_007234d0
// UNIT_FIELD_POWERn (or health, index -2) moved. The player's and its pet's power, while the
// predicted-power option is on, only goes up through here (the prediction already took it down).
int32_t OnPowerChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x306);
    int32_t index = static_cast<int32_t>((offset - 0x4C) >> 2);

    if (!unit) {
        return 1;
    }

    auto data = unit->Unit();
    int32_t value = index == -2 ? data->health : data->power[index];

    if (CGGameUI::s_predictedPowerCvar && CGGameUI::s_predictedPowerCvar->GetInt() && (guid == ClntObjMgrGetActivePlayer() || guid == CGPetInfo::GetPet(0))) {
        int32_t reported = index == -2 ? static_cast<int32_t>(unit->m_reportedHealth) : unit->m_reportedPower[index];

        if (value <= reported) {
            return 1;
        }
    }

    unit->SetReportedPower(index, value);

    return 1;
}

// ref: FUN_007235c0
int32_t OnMaxPowerChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x326);
    int32_t index = static_cast<int32_t>((offset - 0x6C) >> 2);

    if (unit) {
        unit->SetReportedPower(index, index == -2 ? unit->Unit()->health : unit->Unit()->power[index]);
    }

    return 1;
}

// ref: FUN_00723620
// UNIT_FIELD_BYTES_0 byte 3 (the power type) moved.
int32_t OnPowerTypeChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x342);

    if (unit) {
        int32_t type = static_cast<uint8_t>(unit->Unit()->bytes0 >> 24);
        unit->SetReportedPower(type, unit->Unit()->power[type]);
    }

    return 1;
}

// ref: FUN_00716810
// UNIT_FIELD_AURASTATE moved for the player or its target: the action bars re-check usability.
// PARTIAL: the bars' refresh (FUN_0053cf10) is the UI port's; the event is sent.
int32_t OnAuraStateChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    if (guid == ClntObjMgrGetActivePlayer()) {
        FrameScript_SignalEvent(0x15E, nullptr);
    }

    return 1;
}

// ref: FUN_0073f270
int32_t OnFlagsFieldChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x239);

    if (unit) {
        unit->OnFlagsChanged(*static_cast<const uint32_t*>(old));
    }

    return 1;
}

// ref: FUN_0073f2b0
int32_t OnFlags2FieldChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x249);

    if (unit) {
        unit->OnFlags2Changed(*static_cast<const uint32_t*>(old));
    }

    return 1;
}

// ref: FUN_0073f2f0
int32_t OnVisibilityFieldChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x259);

    if (unit) {
        unit->OnVisibilityChanged(*static_cast<const uint8_t*>(old));
    }

    return 1;
}

// ref: FUN_00728d20
int32_t OnPvpFieldChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x26a);

    if (unit) {
        unit->OnPvpFlagsChanged(*static_cast<const uint8_t*>(old));
    }

    return 1;
}

// ref: FUN_00723680
int32_t OnFactionChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x36c);

    if (unit) {
        unit->UpdateReaction(0);
    }

    return 1;
}

// ref: FUN_00728d60
// UNIT_FIELD_CHARM / _SUMMON moved: the name is redrawn and the reaction re-checked, and the
// charm's "possessed" state bit (0x1000) is dropped.
//
// PARTIAL: the pet frame and the possess bar (FUN_0080dfe0 for the player's own, FUN_00728880 for
// its pet) and the name plate (FUN_0098e580) are the UI and name plate ports'.
int32_t OnCharmChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x37f);

    if (unit) {
        if (unit->m_nameDesc) {
            PlayerNameInvalidate(unit->m_nameDesc);
        }

        unit->UpdateReaction(0);
        unit->m_stateFlags &= 0xFFFFEFFF;
    }

    return 1;
}

// ref: FUN_0073f460
// UNIT_FIELD_BYTES_1 byte 0 (the stand state) moved for a unit other than the player (the player's
// own arrives as SMSG_STANDSTATE_UPDATE).
int32_t OnStandStateFieldChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    if (guid == ClntObjMgrGetActivePlayer()) {
        return 1;
    }

    auto unit = HandlerUnit(guid, 0x3c6);

    if (unit) {
        unit->OnStandStateUpdate(static_cast<uint8_t>(unit->Unit()->bytes1));
    }

    return 1;
}

// ref: FUN_0071c9d0
int32_t OnNpcFlagsFieldChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x3d9);

    if (unit) {
        unit->OnNpcFlagsChanged(*static_cast<const uint32_t*>(old));
    }

    return 1;
}

// ref: FUN_0072cf70
// UNIT_FIELD_PET_NAME_TIMESTAMP moved: the pet's name is asked for again, and the player's own
// pet's frame hears it (UNIT_PET 0x13d).
int32_t OnPetNameTimestampChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x3f6);

    if (unit) {
        unit->GetUnitName(nullptr, 1);

        auto data = unit->Unit();
        const WOWGUID& owner = data->charm != 0 ? data->charm : data->summon;

        if (owner == ClntObjMgrGetActivePlayer()) {
            FrameScript_SignalEvent(0x13D, nullptr);
        }
    }

    return 1;
}

// ref: FUN_00741a00
int32_t OnDynamicFlagsFieldChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x40e);

    if (unit) {
        unit->OnDynamicFlagsUpdate(*static_cast<const uint32_t*>(old));
    }

    return 1;
}

// ref: FUN_0073f4f0
// UNIT_FIELD_CHANNEL_OBJECT and _SPELL (twelve bytes, the old values given).
int32_t OnChannelChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x421);

    if (unit) {
        auto was = static_cast<const uint32_t*>(old);
        WOWGUID object = static_cast<WOWGUID>(was[0]) | (static_cast<WOWGUID>(was[1]) << 32);
        int32_t spell = static_cast<int32_t>(was[2]);

        unit->OnChannelObjectChanged(object, spell);
        unit->OnChannelSpellChanged(spell);
    }

    return 1;
}

// ref: FUN_0072cff0
// UNIT_FIELD_PETNUMBER moved: the name and the display scale are re-run.
// PARTIAL: the name plate's removal (FUN_00725840) is the name plate port's.
int32_t OnPetNumberChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x437);

    if (unit) {
        unit->GetUnitName(nullptr, 1);
        unit->UpdateDisplayScale(0);
    }

    ScriptEventsSignalUnitEvent(guid, 0x94);

    if (guid == CGPetInfo::GetPet(0)) {
        FrameScript_SignalEvent(0x1A5, nullptr);
    }

    return 1;
}

// ref: FUN_0072d070
int32_t OnScaleFieldChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x455);

    if (unit) {
        unit->OnScaleChanged(*static_cast<const float*>(old));
    }

    return 1;
}

// ref: FUN_0071ca10
// UNIT_FIELD_HOVERHEIGHT: the movement keeps the unit at the new height.
int32_t OnHoverHeightChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x48f);

    if (unit) {
        unit->m_localMove.m_hoverHeight = unit->Unit()->hoverHeight;
    }

    return 1;
}

// ref: FUN_00741d00
// The unit's descriptor handlers, in the reference's order.
void RegisterUnitFieldHandlers() {
    for (uint32_t offset = 0xC8; offset < 0xD4; offset += 4) {
        MirrorRegisterHandler(ID_UNIT, offset, 4, &OnVirtualItemChanged, nullptr, 0, 0);
    }

    MirrorRegisterHandler(ID_UNIT, 0xC0, 4, &OnLevelFieldChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_OBJECT, 0xC, 4, &OnEntryChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0x48, 4, &OnHealthChanged, nullptr, 0, 0);

    for (uint32_t offset = 0x6C; offset < 0x88; offset += 4) {
        MirrorRegisterHandler(ID_UNIT, offset - 0x20, 4, &OnPowerChanged, nullptr, 0, 0);
        MirrorRegisterHandler(ID_UNIT, offset, 4, &OnMaxPowerChanged, nullptr, 0, 0);
    }

    MirrorRegisterHandler(ID_UNIT, 0x47, 1, &OnPowerTypeChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0xDC, 4, &OnAuraStateChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0xD4, 4, &OnFlagsFieldChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0xD8, 4, &OnFlags2FieldChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0x112, 1, &OnVisibilityFieldChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0x1D1, 1, &OnPvpFieldChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0x113, 1, &OnAnimTierChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0xFC, 4, &OnMountDisplayChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0xC4, 4, &OnFactionChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0x18, 0x10, &OnCharmChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0xF4, 4, &OnDisplayChanged, nullptr, 1, 0);
    MirrorRegisterHandler(ID_UNIT, 0x110, 1, &OnStandStateFieldChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0x130, 4, &OnNpcFlagsFieldChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0x134, 4, &OnEmoteStateChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0x118, 4, &OnPetNameTimestampChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0x124, 4, &OnDynamicFlagsFieldChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0x38, 0xC, &OnChannelChanged, nullptr, 0, 1);
    MirrorRegisterHandler(ID_UNIT, 0x114, 4, &OnPetNumberChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_OBJECT, 0x10, 4, &OnScaleFieldChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0x1D0, 1, &OnSheathChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0x30, 8, &OnTargetChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0x230, 4, &OnHoverHeightChanged, nullptr, 0, 0);
}

} // namespace


// ------------------------------------------------------------------------------------------------
// Movement messages: other units' MSG_MOVE_* broadcasts, the server's forced changes to the local
// player, speeds and spline states (Unit_C.cpp 0x00740d30 .. 0x00741c90, 0x007307a0).
// ------------------------------------------------------------------------------------------------

// ref: FUN_007187f0
// A knockback broadcast: the status, then the direction and the two speeds.
int32_t CGUnit_C::RemoteKnockback(int32_t time, const CMovementStatus& status, CDataStore* msg) {
    C2Vector direction = { 0.0f, 0.0f };
    float horizontal;
    float vertical;

    msg->Get(direction.x);
    msg->Get(direction.y);
    msg->Get(horizontal);
    msg->Get(vertical);

    return this->m_localMove.RemoteKnockback(time, status, direction, horizontal, vertical);
}

// ref: FUN_00718890
// A stunned unit (UNIT_FLAG_STUNNED, 0x40000) does not turn.
int32_t CGUnit_C::RemoteStartTurn(int32_t time, const CMovementStatus& status, int32_t left) {
    if (this->m_unit->flags & 0x40000) {
        return 0;
    }

    return this->m_localMove.RemoteStartTurn(time, status, left);
}

// ref: FUN_007188c0
int32_t CGUnit_C::RemoteStartPitch(int32_t time, const CMovementStatus& status, int32_t up) {
    if (this->m_unit->flags & 0x40000) {
        return 0;
    }

    return this->m_localMove.RemoteStartPitch(time, status, up);
}

// ref: FUN_007188f0
// The unroot runs with state bit 0x20000000 up, so what it triggers knows it came from the server.
int32_t CGUnit_C::RemoteUnroot(int32_t time, const CMovementStatus& status) {
    this->m_stateFlags |= 0x20000000;
    int32_t result = this->m_localMove.RemoteUnroot(time, status);
    this->m_stateFlags &= 0xdfffffff;

    return result;
}

// ref: FUN_007189a0
// PARTIAL: a passenger teleported into a seat (move-flags-2 0x2000) boards it (FUN_0074be10, the
// vehicle port's).
int32_t CGUnit_C::RemoteTeleport(int32_t time, const CMovementStatus& status) {
    return this->m_localMove.RemoteTeleport(time, status);
}

// ref: FUN_00740d30
// Another unit's MSG_MOVE_*: its status, then the change the opcode names. A change that took
// re-chooses the unit's animation.
int32_t CGUnit_C::OnRemoteMoveMessage(int32_t opcode, CDataStore* msg) {
    CMovementStatus status;
    *msg >> status;

    int32_t time = static_cast<int32_t>(OsGetAsyncTimeMs());
    auto& move = this->m_localMove;
    int32_t changed;

    switch (opcode) {
        case 0x0b5: changed = move.RemoteStartMove(time, status, 1); break;
        case 0x0b6: changed = move.RemoteStartMove(time, status, 0); break;
        case 0x0b7: changed = move.RemoteStopMove(time, status); break;
        case 0x0b8: changed = move.RemoteStartStrafe(time, status, 1); break;
        case 0x0b9: changed = move.RemoteStartStrafe(time, status, 0); break;
        case 0x0ba: changed = move.RemoteStopStrafe(time, status); break;
        case 0x0bb: changed = move.RemoteJump(time, status); break;
        case 0x0bc: changed = this->RemoteStartTurn(time, status, 1); break;
        case 0x0bd: changed = this->RemoteStartTurn(time, status, 0); break;
        case 0x0be: changed = move.RemoteStopTurn(time, status); break;
        case 0x0bf: changed = this->RemoteStartPitch(time, status, 1); break;
        case 0x0c0: changed = this->RemoteStartPitch(time, status, 0); break;
        case 0x0c1: changed = move.RemoteStopPitch(time, status); break;
        case 0x0c2: changed = move.RemoteSetRun(time, status, 1); break;
        case 0x0c3: changed = move.RemoteSetRun(time, status, 0); break;
        case 0x0c5: changed = this->RemoteTeleport(time, status); break;
        case 0x0c9:
        case 0x0ee: changed = move.RemoteHeartbeat(time, status); break;
        case 0x0ca:
        case 0x341: changed = move.RemoteStartSwim(time, status); break;
        case 0x0cb:
        case 0x342: changed = move.RemoteStopSwim(time, status); break;
        case 0x0d9: return 1;
        case 0x0da: changed = move.RemoteSetFacing(time, status); break;
        case 0x0db: changed = move.RemoteSetPitch(time, status); break;
        case 0x0ec: changed = move.RemoteRoot(time, status); break;
        case 0x0ed: changed = this->RemoteUnroot(time, status); break;
        case 0x0f1: changed = this->RemoteKnockback(time, status, msg); break;
        case 0x0f7: changed = move.RemoteHover(time, status); break;
        case 0x2b0: changed = move.RemoteFeatherFall(time, status); break;
        case 0x2b1: changed = move.RemoteWaterWalk(time, status); break;
        case 0x34a: changed = move.RemoteSwimFlyTransition(time, status); break;
        case 0x359: changed = move.RemoteStartAscend(time, status, 1); break;
        case 0x35a: changed = move.RemoteStopAscend(time, status); break;
        case 0x3a7: changed = move.RemoteStartAscend(time, status, 0); break;
        case 0x3ad: changed = move.RemoteCanFly(time, status); break;
        case 0x4d2: changed = move.RemoteSetGravity(time, status); break;
        default: return 0;
    }

    if (changed) {
        this->OnMovementPacketSent(opcode);
        this->UpdateAnimation(0, 0xffffffff);
    }

    return 1;
}

// ref: FUN_007406a0
// A speed broadcast (MSG_MOVE_SET_*_SPEED, MSG_MOVE_SET_COLLISION_HGT). The local player already
// took it from the forced change and applies the echo without answering; another unit takes it
// with its status.
int32_t CGUnit_C::OnSpeedMessage(int32_t opcode, CDataStore* msg) {
    CMovementStatus status;
    *msg >> status;

    int32_t time = static_cast<int32_t>(OsGetAsyncTimeMs());
    float value;
    msg->Get(value);

    int32_t type;

    switch (opcode) {
        case 0x0cd: type = 0x17; break;
        case 0x0cf: type = 0x18; break;
        case 0x0d1: type = 0x19; break;
        case 0x0d3: type = 0x1a; break;
        case 0x0d5: type = 0x1b; break;
        case 0x37e: type = 0x1c; break;
        case 0x380: type = 0x1d; break;
        case 0x0d8: type = 0x1e; break;
        case 0x45b: type = 0x1f; break;
        case 0x518: type = 0x3a; break;
        default: return 0;
    }

    if (this->GetGUID() == CGUnit_C::s_activeMover) {
        this->m_localMove.QueueEchoedValue(time, type, value);
        return 1;
    }

    int32_t changed = type == 0x3a
        ? this->m_localMove.RemoteSetCollisionHeight(time, status, value)
        : this->m_localMove.RemoteSetSpeed(time, status, type, value);

    if (changed) {
        this->OnMovementPacketSent(opcode);
        this->UpdateAnimation(0, 0xffffffff);
    }

    return 1;
}

// ref: FUN_00740a60
// A spline speed (SMSG_SPLINE_SET_*_SPEED): set at once, nothing to answer.
int32_t CGUnit_C::OnSplineSpeedMessage(int32_t opcode, float speed) {
    auto& move = this->m_localMove;

    switch (opcode) {
        case 0x2fe: move.SetRunSpeed(speed); break;
        case 0x2ff: move.SetRunBackSpeed(speed); break;
        case 0x300: move.SetSwimSpeed(speed); break;
        case 0x301: move.SetWalkSpeed(speed); break;
        case 0x302: move.SetSwimBackSpeed(speed); break;
        case 0x303: move.SetTurnRate(speed); break;
        case 0x385: move.SetFlightSpeed(speed); break;
        case 0x386: move.SetFlightBackSpeed(speed); break;
        case 0x45e: move.SetPitchRate(speed); break;
        default: return 0;
    }

    this->OnMovementPacketSent(opcode);
    this->UpdateAnimation(0, 0xffffffff);

    return 1;
}

// ref: FUN_00740ba0
// A spline state (SMSG_SPLINE_MOVE_*): the queue is flushed first, then the state set at once.
int32_t CGUnit_C::OnSplineFlagMessage(int32_t opcode) {
    auto& move = this->m_localMove;
    move.FlushEvents(0, 0);

    switch (opcode) {
        case 0x304: move.SplineUnroot(); break;
        case 0x305: move.SetSafeFall(1); move.RestartFall(); break;
        case 0x306: move.SetSafeFall(0); move.RestartFall(); break;
        case 0x307: move.SplineSetHover(1); break;
        case 0x308: move.SplineSetHover(0); break;
        case 0x309: move.SetWaterWalking(1); break;
        case 0x30a: move.SetWaterWalking(0); break;
        case 0x30b: move.SplineStartSwim(); break;
        case 0x30c: move.SplineStopSwim(); break;
        case 0x30d: move.SetRun(1); break;
        case 0x30e: move.SetRun(0); break;
        case 0x31a: move.SplineRoot(); break;
        case 0x422: move.SplineSetFlying(1); break;
        case 0x423: move.SplineSetFlying(0); break;
        case 0x4d3: move.SplineSetGravity(0); break;
        case 0x4d4: move.SplineSetGravity(1); break;
        default: return 0;
    }

    this->OnMovementPacketSent(opcode);
    this->UpdateAnimation(0, 0xffffffff);

    return 1;
}

// ref: FUN_0072d1b0
// SMSG_MOVE_KNOCK_BACK: the local player is thrown. PARTIAL: an open loot window closes first
// (MovementStartPrologue(1, 1, 0), the loot port's); a unit that is not the active mover becomes
// it through FUN_00729010 (ChangeActiveMover).
void CGUnit_C::ReceiveKnockback(int32_t time, uint32_t counter, CDataStore* msg) {
    if (this->IsActiveMover()) {
        this->CancelClickToMove(0, 1);
    }

    C2Vector direction = { 0.0f, 0.0f };
    float horizontal;
    float vertical;

    msg->Get(direction.x);
    msg->Get(direction.y);
    msg->Get(horizontal);
    msg->Get(vertical);

    if (this->GetGUID() != CGUnit_C::s_activeMover) {
        CGUnit_C::ChangeActiveMover(this->GetGUID());
    }

    this->m_localMove.QueueForcedKnockback(time, counter, direction, horizontal, vertical);
}

// ref: FUN_0072d2d0
// MSG_MOVE_TELEPORT_ACK from the server: a normal player turns the camera to the new facing and
// takes the teleport (answered when the event runs); anything else answers at once. PARTIAL: as
// ReceiveKnockback, the loot window.
void CGUnit_C::ReceiveTeleportAck(int32_t time, uint32_t counter, CDataStore* msg) {
    CMovementStatus status;
    *msg >> status;

    if (ClntObjMgrGetPlayerType() == PLAYER_NORMAL) {
        if (auto camera = CGWorldFrame::GetActiveCamera()) {
            camera->FaceYaw(status.facing34);
        }

        if (this->GetGUID() != CGUnit_C::s_activeMover) {
            CGUnit_C::ChangeActiveMover(this->GetGUID());
        }

        if (this->IsActiveMover()) {
            this->CancelClickToMove(0, 1);
        }

        this->m_localMove.QueueTeleport(time, 1, counter, status);

        return;
    }

    CDataStore ack;
    ack.Put(static_cast<uint32_t>(MSG_MOVE_TELEPORT_ACK));
    ack.Put(this->GetGUID());
    ack.Put(counter);
    ack.Put(static_cast<uint32_t>(time));
    ack.Finalize();
    ClientServices::Send(&ack);
}

// ref: FUN_007307a0
// The server's changes to the local player: each read with its counter and queued, to be applied
// and acknowledged when its time comes.
int32_t CGUnit_C::OnForcedMoveMessage(int32_t opcode, CDataStore* msg) {
    int32_t time = static_cast<int32_t>(OsGetAsyncTimeMs());
    auto& move = this->m_localMove;

    uint32_t counter = 0;
    msg->Get(counter);

    float value;

    switch (opcode) {
        case 0x0c7:
            this->ReceiveTeleportAck(time, counter, msg);
            break;

        case 0x0de: move.QueueForcedState(time, 0x27, counter); break;
        case 0x0df: move.QueueForcedState(time, 0x28, counter); break;

        case 0x0e2: {
            // The extra byte: when set, the speed is also the one the client remembers as the
            // unit's base run speed (DAT_00ca11a4, read by nothing ported).
            uint8_t remember;
            msg->Get(remember);
            msg->Get(value);
            move.QueueForcedValue(time, 0x17, counter, value);
            break;
        }

        case 0x0e4: msg->Get(value); move.QueueForcedValue(time, 0x18, counter, value); break;
        case 0x2da: msg->Get(value); move.QueueForcedValue(time, 0x19, counter, value); break;
        case 0x0e6: msg->Get(value); move.QueueForcedValue(time, 0x1a, counter, value); break;
        case 0x2dc: msg->Get(value); move.QueueForcedValue(time, 0x1b, counter, value); break;
        case 0x381: msg->Get(value); move.QueueForcedValue(time, 0x1c, counter, value); break;
        case 0x383: msg->Get(value); move.QueueForcedValue(time, 0x1d, counter, value); break;
        case 0x2de: msg->Get(value); move.QueueForcedValue(time, 0x1e, counter, value); break;
        case 0x45c: msg->Get(value); move.QueueForcedValue(time, 0x1f, counter, value); break;
        case 0x516: msg->Get(value); move.QueueForcedValue(time, 0x3a, counter, value); break;

        case 0x0e8: move.QueueForcedState(time, 0x29, counter); break;
        case 0x0ea: move.QueueForcedState(time, 0x2a, counter); break;
        case 0x0ef: this->ReceiveKnockback(time, counter, msg); break;
        case 0x0f2: move.QueueForcedState(time, 0x23, counter); break;
        case 0x0f3: move.QueueForcedState(time, 0x24, counter); break;
        case 0x0f4: move.QueueForcedState(time, 0x25, counter); break;
        case 0x0f5: move.QueueForcedState(time, 0x26, counter); break;
        case 0x33e: move.QueueForcedState(time, 0x38, counter); break;
        case 0x33f: move.QueueForcedState(time, 0x39, counter); break;
        case 0x343: move.QueueForcedState(time, 0x2f, counter); break;
        case 0x344: move.QueueForcedState(time, 0x30, counter); break;
        case 0x4ce: move.QueueForcedState(time, 0x21, counter); break;
        case 0x4d0: move.QueueForcedState(time, 0x20, counter); break;

        default:
            break;
    }

    return 1;
}

namespace {

// The unit a movement message names, or nothing: an unknown one's message is skipped whole.
CGUnit_C* MovementMessageUnit(CDataStore* msg, int32_t line) {
    SmartGUID guid;
    *msg >> guid;

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", line));

    if (!unit) {
        msg->Seek(msg->Size());
    }

    return unit;
}

// ref: FUN_00741b60
int32_t UnitRemoteMoveHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    auto unit = MovementMessageUnit(msg, 0x515);

    return unit ? unit->OnRemoteMoveMessage(msgId, msg) : 0;
}

// ref: FUN_00732450
int32_t UnitForcedMoveHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    SmartGUID guid;
    *msg >> guid;

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x5fe));

    return unit ? unit->OnForcedMoveMessage(msgId, msg) : 1;
}

// ref: FUN_00741b00
int32_t UnitSpeedHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    auto unit = MovementMessageUnit(msg, 0x4e7);

    return unit ? unit->OnSpeedMessage(msgId, msg) : 0;
}

// ref: FUN_00741bc0
int32_t UnitSplineSpeedHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    SmartGUID guid;
    *msg >> guid;

    float speed;
    msg->Get(speed);

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x52f));

    if (!unit) {
        msg->Seek(msg->Size());
        return 0;
    }

    return unit->OnSplineSpeedMessage(msgId, speed);
}

// ref: FUN_00741c30
int32_t UnitSplineFlagHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    auto unit = MovementMessageUnit(msg, 0x559);

    return unit ? unit->OnSplineFlagMessage(msgId) : 0;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// SMSG_MONSTER_MOVE: a unit walked along a server spline (Unit_C.cpp 0x0073c8e0, 0x0073f590)
// ------------------------------------------------------------------------------------------------

namespace {

const float SPLINE_POINT_EPSILON = 0.0007716049929149449f;   // 0x00a349b0, (1/36)^2

float DistanceSq(const C3Vector& a, const C3Vector& b) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float dz = b.z - a.z;

    return dz * dz + dy * dy + dx * dx;
}


} // namespace

// ref: FUN_007180c0
// Fit the path to where the unit stands (`position`, in the transport's space when there is
// one): a path still ahead of it gains its position as the first point, one it is part way along
// starts where it is, one already behind it becomes a straight line to the end. The points sit at
// `points`, with room for one before them; the result is the new first point, or null with
// `*count` 0 when nothing is left to walk.
C3Vector* CGUnit_C::FitSplineToPosition(WOWGUID transport, const C3Vector& position, C3Vector* points,
                                        uint32_t* count) {
    C3Vector here = position;

    if (transport) {
        C44Matrix matrix;
        MovementGetTransportMatrixChecked(transport, matrix, 0, ".\\Unit_C.cpp", 0x2257);
        here = here * matrix.AffineInverse();
    }

    uint32_t n = *count;

    if (n == 1) {
        if (SPLINE_POINT_EPSILON <= DistanceSq(points[0], here)) {
            points[-1] = here;
            (*count)++;
            return points - 1;
        }

        *count = 0;
        return nullptr;
    }

    uint32_t segments = n - 1;
    uint32_t ahead = 0;
    uint32_t behind = 0;

    for (uint32_t i = 0; i < segments; i++) {
        const C3Vector& a = points[i];
        const C3Vector& b = points[i + 1];
        C3Vector d = { b.x - a.x, b.y - a.y, b.z - a.z };
        float offset = -(d.x * here.x + d.y * here.y + d.z * here.z);
        float s0 = d.x * a.x + d.y * a.y + d.z * a.z + offset;
        float s1 = d.x * b.x + d.y * b.y + d.z * b.z + offset;

        if (s0 < 0.0f || s1 < 0.0f) {
            if (0.0f < s0 || 0.0f < s1) {
                break;
            }

            behind++;
        } else {
            ahead++;
        }
    }

    if (ahead == segments) {
        float dx = points[0].x - here.x;
        float dy = points[0].y - here.y;

        if (1.0f <= dy * dy + dx * dx) {
            points[-1] = here;
            (*count)++;
            return points - 1;
        }

        return points;
    }

    if (behind == segments) {
        if (SPLINE_POINT_EPSILON <= DistanceSq(points[n - 1], here)) {
            points[0] = here;
            points[1] = points[n - 1];
            *count = 2;
            return points;
        }

        *count = 0;
        return nullptr;
    }

    for (uint32_t i = 0; i < *count - 1; i++) {
        const C3Vector& a = points[i];
        const C3Vector& b = points[i + 1];
        C3Vector d = { b.x - a.x, b.y - a.y, b.z - a.z };
        float inv = 1.0f / std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        C3Vector unit = { inv * d.x, inv * d.y, inv * d.z };
        float offset = -(unit.x * here.x + unit.y * here.y + here.z * unit.z);
        float s0 = unit.x * a.x + unit.y * a.y + unit.z * a.z + offset;
        float s1 = unit.z * b.z + b.y * unit.y + b.x * unit.x + offset;

        if (s0 <= 0.0f && 0.0f <= s1) {
            // It stands beside this segment: start from the foot of it.
            float f = s0 / (s0 - s1);
            C3Vector foot = { d.x * f + a.x, d.y * f + a.y, d.z * f + a.z };

            if (SPLINE_POINT_EPSILON <= DistanceSq(foot, b)) {
                points[i] = foot;
            } else if (SPLINE_POINT_EPSILON <= DistanceSq(here, b)) {
                points[i] = here;
                *count -= i;
                return points + i;
            } else {
                i++;

                if (i == segments) {
                    *count = 0;
                    return nullptr;
                }
            }

            *count -= i;
            return points + i;
        }

        if (0.0f <= s0 && 0.0f <= s1) {
            *count -= i;
            C3Vector* first = points + i;
            float dx = first->x - here.x;
            float dy = first->y - here.y;

            if (1.0f <= dy * dy + dx * dx) {
                first[-1] = here;
                (*count)++;
                return first - 1;
            }

            return first;
        }
    }

    return points;
}

// ref: FUN_00718930
// Face the unit a spline names as its target.
void CGUnit_C::FaceSplineTarget(WOWGUID target, int32_t flush) {
    auto object = ClntObjMgrObjectPtr(target, TYPE_OBJECT, ".\\Unit_C.cpp", 0x2609);

    if (!object) {
        return;
    }

    this->m_localMove.FaceForSpline(FacingBetween(this->GetPosition(), object->GetPosition()), flush);
}

// ref: FUN_0073af00
// The animation tier a spline gives the unit (ground, swim, hover, fly): leaving the ground for
// hover or flight plays the take-off (0x1ca), coming back down plays the landing (0x1cc).
void CGUnit_C::SetSplineAnimationTier(uint8_t tier) {
    int32_t current = this->m_animTier;

    if (current == 0 && (tier == 3 || tier == 2)) {
        this->SetAnimation(0x1ca, 0);
    } else if ((current == 3 && tier == 0) || (current == 2 && tier == 0)) {
        this->SetAnimation(0x1cc, 0);
    }

    this->m_animTier = tier;
}

// ref: FUN_0073c8e0
// A monster move: where the unit starts, the kind of move (0 a path, 1 a stop, 2 .. 4 a path
// that faces a spot, a target or an angle), then for a path its flags, its options and its points.
// The path is fitted to where the unit is, padded at both ends, and walked at the speed its
// length and duration give, capped by the unit's run speed; a path too short to walk puts the
// unit at its end at once. PARTIAL: a vehicle's own spline step (FUN_00758130), the player's
// stand-up (FUN_006dcb40) and the effect +0x980 releases are the vehicle, Player_C and
// ObjectEffect ports'.
void CGUnit_C::OnMonsterMove(CDataStore* msg, int32_t opcode, WOWGUID transport, uint8_t seat, int32_t flush) {
    auto& move = this->m_localMove;

    this->m_stateFlags |= 0x20000000;

    if (flush) {
        move.FlushEvents(0, 0);
    }

    move.SetSplineTransport(transport, seat, 1);
    this->m_stateFlags &= 0xdfffffff;

    if (move.m_transportGUID != transport) {
        return;
    }

    move.SetMoveFlags2Bit100(0);

    C3Vector start = { 0.0f, 0.0f, 0.0f };
    msg->Get(start.x);
    msg->Get(start.y);
    msg->Get(start.z);

    uint32_t id;
    msg->Get(id);

    uint8_t type;
    msg->Get(type);

    C3Vector spot = { 0.0f, 0.0f, 0.0f };
    WOWGUID target = 0;
    float angle = 0.0f;

    switch (type) {
        case 1: {
            C3Vector here = this->GetRawPosition();
            auto tolerance = CVar::Lookup("pathDistTol");
            float limit = tolerance ? tolerance->m_floatValue : 1.0f;

            if (DistanceSq(here, start) < limit * limit) {
                move.StopSplineAt(id, start, 0, 1);
                this->UpdateAnimation(0, 0xffffffff);
                return;
            }

            break;
        }

        case 2:
            *msg >> spot;
            break;

        case 3:
            msg->Get(target);
            break;

        case 4:
            msg->Get(angle);
            break;

        default:
            break;
    }

    uint32_t flags;
    uint32_t duration = 0;
    uint32_t pointCount;
    uint8_t animTier = 0;
    uint32_t animTime = 0;
    float acceleration = 0.0f;
    uint32_t arcStart = 0;

    if (type == 1) {
        flags = 0x1000;
        pointCount = 1;
    } else {
        msg->Get(flags);

        if (flags & 0x200000) {
            msg->Get(animTier);
            msg->Get(animTime);
        }

        msg->Get(duration);

        if (flags & 0x800) {
            msg->Get(acceleration);
            msg->Get(arcStart);
        }

        msg->Get(pointCount);
    }

    // Room for the points, two before them and a padding point after.
    TSGrowableArray<C3Vector> buffer;
    buffer.SetCount(pointCount + 4);
    C3Vector* points = buffer.m_data;

    float facing = this->GetRawFacing();
    C3Vector forward = { std::cos(facing), std::sin(facing), 0.0f };
    C3Vector destination = { 0.0f, 0.0f, 0.0f };
    C3Vector* list = nullptr;
    uint32_t count = 0;

    if (type == 1) {
        C3Vector here = this->GetRawPosition();
        points[0] = { here.x - forward.x, here.y - forward.y, here.z - forward.z };
        points[1] = here;
        points[2] = start;
        points[3] = start;
        destination = start;
        list = points;
        count = 4;
    } else if (!(flags & 0x42000)) {
        // A straight path: the start, the waypoints packed around the midpoint, the end.
        *msg >> destination;

        C3Vector* path = points + 2;
        path[0] = start;
        uint32_t n = 1;

        if (pointCount <= 1) {
            if (SPLINE_POINT_EPSILON < DistanceSq(destination, start)) {
                path[1] = destination;
                n = 2;
            }
        } else {
            C3Vector middle = {
                (start.x + destination.x) * 0.5f,
                (destination.y + start.y) * 0.5f,
                (destination.z + start.z) * 0.5f,
            };

            for (uint32_t i = 0; i < pointCount - 1; i++) {
                ReadPackedMovementOffset(msg, middle, path[n]);
                n++;
            }

            path[n] = destination;
            n++;
        }

        C3Vector* fitted = CGUnit_C::FitSplineToPosition(transport, this->GetPosition(), path, &n);

        if (fitted && n) {
            // Pad both ends: the last point repeated, the first mirrored across the second.
            list = fitted - 1;
            list[n + 1] = list[n];
            list[0] = {
                list[1].x - (list[2].x - list[1].x),
                list[1].y - (list[2].y - list[1].y),
                list[1].z - (list[2].z - list[1].z),
            };
            count = n + 2;
        }
    } else {
        // A curve or a flight: from just behind the unit through every point.
        C3Vector here = this->GetRawPosition();
        points[0] = { here.x - forward.x, here.y - forward.y, here.z - forward.z };
        points[1] = here;
        uint32_t n = 2;

        C3Vector point;
        msg->Get(point.x);
        msg->Get(point.y);
        msg->Get(point.z);

        if (SPLINE_POINT_EPSILON <= DistanceSq(point, here)) {
            points[2] = point;
            n = 3;
        }

        for (uint32_t i = 1; i < pointCount; i++) {
            msg->Get(point.x);
            msg->Get(point.y);
            msg->Get(point.z);
            points[n++] = point;
        }

        if (!(flags & 0x80000)) {
            points[n] = point;
            destination = point;
        } else {
            // A loop closes on its own first two points.
            points[n] = points[2];
            n++;
            points[n] = points[3];
            destination = points[2];
        }

        list = points;
        count = n + 1;
    }

    bool started = false;

    if (list && 3 < count) {
        float length = 0.0f;

        for (uint32_t i = 1; i + 1 < count - 1; i++) {
            length = std::sqrt(DistanceSq(list[i], list[i + 1])) + length;
        }

        if (0.1666666716337204f < length) {
            float speed = std::max(28.0f, this->m_move->m_runSpeed * 4.0f);

            if (flags & 0x42000) {
                speed = 50.0f;
            }

            if (duration != 0) {
                float limit = length / (static_cast<float>(duration) * 0.001f);

                if (limit < speed) {
                    speed = limit;
                }
            }

            if (9.5367431640625e-07f < speed) {
                int32_t ms = static_cast<int32_t>(std::nearbyint((length / speed) * 1000.0f));
                uint32_t splineDuration = 1 < ms ? static_cast<uint32_t>(ms) : 1;

                if (move.StartSpline(list, count, splineDuration, flags, id)) {
                    started = true;

                    if (type == 2) {
                        move.SetSplineFacingSpot(spot);
                    } else if (type == 3) {
                        move.SetSplineFacingTarget(target);
                    } else if (type == 4) {
                        move.SetSplineFacingAngle(angle);
                    }

                    if (!(flags & 0x800)) {
                        if (flags & 0x200000) {
                            // Coming down from hover or flight lands at the end: the landing
                            // starts its own length before the spline does.
                            if ((this->m_animTier == 3 && (animTier == 0 || animTier == 2))
                                || (this->m_animTier == 2 && animTier == 0)) {
                                if (auto model = this->GetObjectModel()) {
                                    M2SequenceInfo info = {};
                                    model->GetSequenceInfo(0x1cf, 0, info);

                                    if (info.duration + animTime < splineDuration) {
                                        animTime = splineDuration - info.duration;
                                    }
                                }
                            }

                            move.SetSplineAnimation(animTier, animTime);
                        }
                    } else {
                        move.SetSplineParabolic(acceleration, arcStart);
                    }

                    if (this->GetGUID() == ClntObjMgrGetActivePlayer()) {
                        if (auto input = InputControlGetActive()) {
                            input->UpdatePlayerMovement(static_cast<uint32_t>(OsGetAsyncTimeMs()), 1);
                        }
                    }
                }
            }
        }
    }

    if (!started && type != 1) {
        // Nothing to walk: face where the move faces and be at its end.
        if (type == 2) {
            C3Vector world = spot;

            if (transport) {
                C44Matrix matrix;
                MovementGetTransportMatrixChecked(transport, matrix, 0, ".\\Unit_C.cpp", 0x2257);
                world = spot * matrix;
            }

            move.FaceForSpline(FacingBetween(this->GetPosition(), world), flush);
        } else if (type == 3) {
            this->FaceSplineTarget(target, flush);
        } else if (type == 4) {
            move.FaceForSpline(move.GetFacing(angle), flush);
        }

        move.StopSplineAt(id, destination, flags, flush);
    } else if (!started) {
        move.StopSplineAt(id, destination, flags, flush);
    }

    this->ReleaseRangedWeapon();
    this->UpdateAnimation(0, 0xffffffff);
}

namespace {

// ref: FUN_0073f590
// SMSG_MONSTER_MOVE and SMSG_MONSTER_MOVE_TRANSPORT (with the transport and the seat). A move onto
// or off a seat that waits on the vehicle's animation is queued on the ride instead.
int32_t UnitMonsterMoveHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    SmartGUID guid;
    *msg >> guid;

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x5d4));

    if (!unit) {
        msg->Seek(msg->Size());
        return 0;
    }

    WOWGUID transport = 0;
    uint8_t seat = 0xff;

    if (msgId == SMSG_MONSTER_MOVE_TRANSPORT) {
        SmartGUID transportGuid;
        *msg >> transportGuid;
        transport = transportGuid;
        msg->Get(seat);
    }

    // FUN_0074b9b0: the byte before the move toggles move-flags-2 0x40 (the unit is carried).
    uint8_t carried = 0;
    msg->Get(carried);
    unit->m_localMove.SetMoveFlags2Bit40(carried);

    if (!UnitQueueVehicleMove(unit, msg, transport, seat)) {
        unit->OnMonsterMove(msg, msgId, transport, seat, 1);
    }

    return 1;
}

// ref: FUN_00716db0
// SMSG_SET_VEHICLE_REC_ID: the unit becomes the vehicle the row names, or stops being one (its
// passengers put off).
int32_t UnitSetVehicleRecHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    (void)param;
    (void)msgId;
    (void)time;

    SmartGUID guid;
    *msg >> guid;

    uint32_t recID = 0;
    msg->Get(recID);

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\Unit_C.cpp", 0x729));

    if (unit) {
        if (recID != 0) {
            UnitCreateVehicle(unit, nullptr, static_cast<int32_t>(recID));
            return 1;
        }

        UnitDestroyVehicle(unit, 1);
    }

    return 1;
}

} // namespace

namespace {

// The movement registrations of FUN_00742220, in its order.
void RegisterUnitMovementHandlers() {
    static const uint16_t s_remote[] = {
        0x0b5, 0x0b6, 0x0b7, 0x0b8, 0x0b9, 0x0ba, 0x0bb, 0x0bc, 0x0bd, 0x0be, 0x0bf, 0x0c0, 0x0c1,
        0x0c2, 0x0c3, 0x0c5, 0x0c9, 0x0ca, 0x0cb, 0x0d9, 0x0da, 0x0db, 0x0ec, 0x0ed, 0x0ee, 0x0f1,
        0x0f7, 0x2b0, 0x2b1, 0x341, 0x342, 0x34a, 0x359, 0x35a, 0x3a7, 0x3ad, 0x4d2,
    };
    static const uint16_t s_forced[] = {
        0x0c7, 0x0de, 0x0df, 0x0e2, 0x0e4, 0x0e6, 0x0e8, 0x0ea, 0x0ef, 0x0f2, 0x0f3, 0x0f4, 0x0f5,
        0x2da, 0x2dc, 0x2de, 0x33e, 0x33f, 0x343, 0x344, 0x381, 0x383, 0x45c, 0x4ce, 0x4d0, 0x516,
    };
    static const uint16_t s_speed[] = { 0x0cd, 0x0cf, 0x0d1, 0x0d3, 0x0d5, 0x0d8, 0x37e, 0x380, 0x45b, 0x518 };
    static const uint16_t s_splineSpeed[] = { 0x2fe, 0x2ff, 0x300, 0x301, 0x302, 0x303, 0x385, 0x386, 0x45e };
    static const uint16_t s_splineFlag[] = {
        0x304, 0x305, 0x306, 0x307, 0x308, 0x309, 0x30a, 0x30b, 0x30c, 0x30d, 0x30e, 0x31a, 0x422,
        0x423, 0x4d3, 0x4d4,
    };

    for (auto op : s_remote) {
        ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(op), &UnitRemoteMoveHandler, nullptr);
    }

    for (auto op : s_forced) {
        ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(op), &UnitForcedMoveHandler, nullptr);
    }

    for (auto op : s_speed) {
        ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(op), &UnitSpeedHandler, nullptr);
    }

    for (auto op : s_splineSpeed) {
        ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(op), &UnitSplineSpeedHandler, nullptr);
    }

    for (auto op : s_splineFlag) {
        ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(op), &UnitSplineFlagHandler, nullptr);
    }

    ClientServices::SetMessageHandler(SMSG_ON_MONSTER_MOVE, &UnitMonsterMoveHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_MONSTER_MOVE_TRANSPORT, &UnitMonsterMoveHandler, nullptr);
}

} // namespace


// ------------------------------------------------------------------------------------------------
// The rest of FUN_00742220's unit messages
// ------------------------------------------------------------------------------------------------

// ref: FUN_0073f060
// The active player's stand state from the server: kept for the input (FUN_006e2b30), the weapon
// sheathed when sitting down, and the posture's animation -- a jump back up from sleep (9), the
// fall from a stun (7), a re-choice otherwise. PARTIAL: Player_C's own half of FUN_006e2b30
// (looting closed, attacking stopped, the stand-state UI refresh) is the Player_C port's; the
// stun's fall pose FUN_0071ee70 / FUN_0073af80 is the combat port's.
void CGUnit_C::OnStandStateUpdate(uint8_t standState) {
    if (this->GetGUID() == ClntObjMgrGetActivePlayer()) {
        auto player = static_cast<CGPlayer_C*>(this);

        if (player->m_requestedStandState != standState) {
            player->m_requestedStandState = standState;

            if (standState == 0) {
                if (auto input = InputControlGetActive()) {
                    input->UpdatePlayerMovement(static_cast<uint32_t>(OsGetAsyncTimeMs()), 1);
                }
            }
        }
    }

    int32_t current = this->GetStandStateByte();

    if (this->m_sheathState != 0 && current != 0 && current != 2) {
        this->SetSheathState(0, 1, 0);
    }

    if (current != this->m_lastStandState) {
        if (current == 0) {
            if (this->m_lastStandState == 9) {
                if ((!this->m_vehicle || !this->m_vehicle->m_rec || !this->m_vehicle->ControlsPassengerAnimation())
                    && this->GetObjectModel() && this->GetObjectModel()->IsLoaded(0, 0)) {
                    this->SetAnimation(this->GetObjectModel()->HasSequence(0x7f) ? 0x7f : 0xe0, 0);
                }
            } else {
                this->m_animFlags |= 0x40;
                this->UpdateAnimation(0, 0xffffffff);
            }
        } else if (current == 9) {
            if (!this->m_vehicle || !this->m_vehicle->m_rec || !this->m_vehicle->ControlsPassengerAnimation()) {
                this->SetAnimation(0xc9, 0);
            }
        } else if (current != 7) {
            this->m_animFlags |= 0x40;
            this->UpdateAnimation(0, 0xffffffff);
        }

        this->m_lastStandState = this->GetStandStateByte();
    }
}

namespace {

// ref: FUN_00714ad0
// SMSG_MULTIPLE_PACKETS: whole messages one after another, each dispatched as if it had arrived
// alone.
int32_t UnitMultiplePacketsHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    while (!msg->IsRead() && msg->Tell() <= msg->Size()) {
        ClientServices::Connection()->ProcessMessage(time, msg, 1);
    }

    return 1;
}

// ref: FUN_007169a0
// Movement messages packed together, each behind its length byte; SMSG_COMPRESSED_MOVES deflates
// them first (the uncompressed size ahead of the data, FUN_00778180).
int32_t UnitPackedMoves(CDataStore* msg, uint32_t time, int32_t compressed) {
    uint32_t size = 0;
    msg->Get(size);

    TSGrowableArray<uint8_t> buffer;
    uint32_t remaining = msg->Size() - msg->Tell();

    if (!compressed) {
        size = remaining;
        buffer.SetCount(size);

        if (size) {
            msg->GetArray(buffer.m_data, size);
        }
    } else {
        TSGrowableArray<uint8_t> packed;
        packed.SetCount(remaining);

        if (remaining) {
            msg->GetArray(packed.m_data, remaining);
        }

        buffer.SetCount(size);

        uint32_t inflated = size;

        if (ZlibDecompress(buffer.m_data, &inflated, packed.m_data, remaining) != 0) {
            return 1;
        }

        size = inflated;
    }

    CDataStore moves(buffer.m_data, size);

    while (moves.Tell() != size) {
        uint8_t length = 0;
        moves.Get(length);

        if (!moves.IsValid() || size < moves.Tell() + length) {
            break;
        }

        TSGrowableArray<uint8_t> one;
        one.SetCount(length);

        if (length) {
            moves.GetArray(one.m_data, length);
        }

        CDataStore single(one.m_data, length);
        ClientServices::Connection()->ProcessMessage(time, &single, 1);
    }

    return 1;
}

int32_t UnitCompressedMovesHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    return UnitPackedMoves(msg, time, 1);
}

int32_t UnitMultipleMovesHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    return UnitPackedMoves(msg, time, 0);
}

// ref: FUN_00716940
// SMSG_FLIGHT_SPLINE_SYNC: how far along its flight path the unit should be.
int32_t UnitFlightSplineSyncHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    float progress;
    msg->Get(progress);

    SmartGUID guid;
    *msg >> guid;

    if (auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x543))) {
        unit->m_localMove.SyncSplineProgress(progress);
    }

    return 1;
}

// ref: FUN_0071cab0
// MSG_MOVE_TIME_SKIPPED: another unit's client skipped time; its clock moves on with it.
int32_t UnitTimeSkippedHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    SmartGUID guid;
    *msg >> guid;

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x4ff));

    if (!unit) {
        msg->Seek(msg->Size());
        return 0;
    }

    uint32_t skipped = 0;
    msg->Get(skipped);
    unit->m_localMove.m_remoteTimeBase += skipped;

    return 1;
}

// ref: FUN_00716cd0
// SMSG_FORCE_DISPLAY_UPDATE: rebuild the unit's model.
int32_t UnitForceDisplayUpdateHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    SmartGUID guid;
    *msg >> guid;

    if (auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x694))) {
        unit->UpdateModel(1);
    }

    return 1;
}

// ref: FUN_00716d20
// SMSG_HEALTH_UPDATE: the health the server reports ahead of the field update.
int32_t UnitHealthUpdateHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    SmartGUID guid;
    *msg >> guid;

    uint32_t health;
    msg->Get(health);

    if (auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x6c2))) {
        unit->m_reportedHealth = health;
    }

    return 1;
}

// ref: FUN_007236c0
// SMSG_POWER_UPDATE: likewise for a power.
int32_t UnitPowerUpdateHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    SmartGUID guid;
    *msg >> guid;

    uint8_t powerType;
    uint32_t value;
    msg->Get(powerType);
    msg->Get(value);

    if (auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x6d8))) {
        unit->SetReportedPower(static_cast<int8_t>(powerType), static_cast<int32_t>(value));
    }

    return 1;
}

// ref: FUN_0072d130
// SMSG_CANCEL_AUTO_REPEAT: the unit stops shooting. PARTIAL: the active player's auto-repeat
// spell stop (FUN_00807560) is Spell_C's.
int32_t UnitCancelAutoRepeatHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    SmartGUID guid;
    *msg >> guid;

    if (auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x6a4))) {
        unit->m_animFlags &= 0xfffffdff;
        unit->ReleaseRangedWeapon();
    }

    return 1;
}

// ref: FUN_00718a20
// The unit is more than half its height above the floor under it.
int32_t UnitIsHighAboveFloor(CGUnit_C* unit) {
    float floor = 0.0f;
    float above = 0.0f;

    if (unit->GetFloorHeight(&floor)) {
        above = floor - unit->GetPosition().z;
    }

    return unit->GetModelHeight() * 0.5f < above ? 1 : 0;
}

// ref: FUN_0071cb30
// SMSG_MOUNT_SPECIAL_ANIM: another unit's mount rears. PARTIAL: CGUnit_C's slot 0x98 is not
// ported, so the base's empty one runs.
int32_t UnitMountSpecialAnimHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WOWGUID guid;
    msg->Get(guid);

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x614));

    if (unit && !UnitIsHighAboveFloor(unit) && ClntObjMgrGetActivePlayer() != guid) {
        unit->Virtual098();
    }

    return 1;
}

// ref: FUN_0073f540
// SMSG_STAND_STATE_UPDATE.
int32_t UnitStandStateHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint8_t standState;
    msg->Get(standState);

    if (auto player = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__))) {
        player->OnStandStateUpdate(standState);
    }

    return 1;
}

// ref: FUN_00741a40
// SMSG_DISMOUNT. A unit carrying passengers waits (state 0x10000000) until they are off.
int32_t UnitDismountHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    SmartGUID guid;
    *msg >> guid;

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x4bc));

    if (!unit || !unit->m_mountDisplayID) {
        return 1;
    }

    // A vehicle with passengers aboard holds the dismount until they are off.
    if (unit->m_vehicle && unit->m_vehicle->m_passengers.Head()) {
        unit->m_stateFlags |= 0x10000000;
        return 1;
    }

    unit->m_stateFlags &= 0xefffffff;
    unit->Dismount(1);
    unit->m_mountDisplayID = 0;
    unit->UpdateMountSound();
    unit->AttachQuestMarker();
    PlayerNameInvalidate(unit->m_nameDesc);
    unit->UpdateObjectEffectPackage();

    return 1;
}

// ref: FUN_0071ca50
// SMSG_LOOT_LIST: the unit's master looter and the next looter in turn.
int32_t UnitLootListHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WOWGUID guid;
    msg->Get(guid);

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x4d1));

    if (!unit) {
        msg->Seek(msg->Size());
        return 1;
    }

    SmartGUID master;
    SmartGUID roundRobin;
    *msg >> master;
    *msg >> roundRobin;
    unit->m_masterLooter = master;
    unit->m_roundRobinLooter = roundRobin;

    return 1;
}

// ref: FUN_00716b10
// SMSG_AI_REACTION: a creature noticing the player plays its aggro sound, a pet its own.
int32_t UnitAIReactionHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WOWGUID guid;
    uint32_t reaction;
    msg->Get(guid);
    msg->Get(reaction);

    if (auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x625))) {
        if (reaction == 0) {
            unit->PlayUnitSound(8, 0);
        } else if (reaction == 2) {
            unit->PlayPetSound(0);
        }
    }

    return 1;
}

// ref: FUN_00716b90
// SMSG_PET_ACTION_SOUND: the pet acknowledges an order (0) or an attack (1).
int32_t UnitPetActionSoundHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WOWGUID guid;
    uint32_t action;
    msg->Get(guid);
    msg->Get(action);

    if (auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x636))) {
        if (action == 0) {
            unit->PlayPetSound(1);
        } else if (action == 1) {
            unit->PlayPetSound(2);
        }
    }

    return 1;
}

// ref: FUN_00716c00
// SMSG_PET_DISMISS_SOUND: a model's dismiss sound at a point, the pet itself already gone.
int32_t UnitPetDismissSoundHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint32_t modelID;
    msg->Get(modelID);

    C3Vector position = { 0.0f, 0.0f, 0.0f };
    msg->Get(position.x);
    msg->Get(position.y);
    msg->Get(position.z);

    auto model = g_creatureModelDataDB.GetRecord(static_cast<int32_t>(modelID));
    auto sounds = model ? g_creatureSoundDataDB.GetRecord(model->m_soundID) : nullptr;

    if (sounds && sounds->m_soundPetDismissID) {
        position.z += 1.0f;
        SI2::PlaySoundKit(sounds->m_soundPetDismissID, &position, nullptr, nullptr, 0, nullptr, 1, 0);
    }

    return 1;
}

// ref: FUN_0072d0b0
// SMSG_CONTROL_UPDATE: whether the player may move this unit. One not in view keeps it for its
// PostInit.
int32_t UnitControlUpdateHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    SmartGUID guid;
    *msg >> guid;

    uint8_t hasControl;
    msg->Get(hasControl);

    if (auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x5bc))) {
        unit->OnControlUpdate(guid, hasControl != 0);
        return 1;
    }

    CGUnit_C::StorePendingControl(guid, hasControl != 0);

    return 1;
}

// ref: FUN_007324b0
// SMSG_MIRROR_IMAGE_COMPONENTED_DATA: the appearance a mirror image asked for.
int32_t UnitMirrorImageDataHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WOWGUID guid;
    msg->Get(guid);

    if (auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x684))) {
        unit->ReceiveMirrorImageData(msg);
    }

    return 1;
}

// ref: FUN_00741c90
// SMSG_HIGHEST_THREAT_UPDATE (with the new top) and SMSG_THREAT_UPDATE.
int32_t UnitThreatUpdateHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    SmartGUID guid;
    *msg >> guid;

    if (auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x6e8))) {
        unit->ReceiveThreatUpdate(msg, msgId == SMSG_HIGHEST_THREAT_UPDATE);
        return 1;
    }

    msg->Seek(msg->Size());

    return 1;
}

// ref: FUN_00737b20
// SMSG_THREAT_REMOVE: one unit leaves a creature's threat table.
int32_t UnitThreatRemoveHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    SmartGUID guid;
    *msg >> guid;

    if (auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x6f9))) {
        SmartGUID victim;
        *msg >> victim;

        WOWGUID removed = victim;
        unit->RemoveThreatEntry(removed);

        return 1;
    }

    msg->Seek(msg->Size());

    return 1;
}

// ref: FUN_00734b00
// SMSG_THREAT_CLEAR: a creature forgets everyone.
int32_t UnitThreatClearHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    SmartGUID guid;
    *msg >> guid;

    if (auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x70b))) {
        unit->ClearThreatList();
    }

    return 1;
}

// ref: FUN_00714b20
// SMSG 0x4d8: a guid and a string the client reads and drops.
int32_t UnitIgnoredStringHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WOWGUID guid;
    msg->Get(guid);

    char text[512];
    msg->GetString(text, sizeof(text));

    return 1;
}

void RegisterUnitMiscHandlers() {
    ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(0x4cd), &UnitMultiplePacketsHandler, nullptr);
    ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(0x4d8), &UnitIgnoredStringHandler, nullptr);
    ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(0x388), &UnitFlightSplineSyncHandler, nullptr);
    ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(0x2fb), &UnitCompressedMovesHandler, nullptr);
    ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(0x51e), &UnitMultipleMovesHandler, nullptr);
    ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(0x13c), &UnitAIReactionHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_PET_ACTION_SOUND, &UnitPetActionSoundHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_PET_DISMISS_SOUND, &UnitPetDismissSoundHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_CONTROL_UPDATE, &UnitControlUpdateHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_MIRROR_IMAGE_COMPONENTED_DATA, &UnitMirrorImageDataHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_HIGHEST_THREAT_UPDATE, &UnitThreatUpdateHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_THREAT_UPDATE, &UnitThreatUpdateHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_THREAT_REMOVE, &UnitThreatRemoveHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_THREAT_CLEAR, &UnitThreatClearHandler, nullptr);
    ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(0x403), &UnitForceDisplayUpdateHandler, nullptr);
    ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(0x47f), &UnitHealthUpdateHandler, nullptr);
    ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(0x3f9), &UnitLootListHandler, nullptr);
    ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(0x319), &UnitTimeSkippedHandler, nullptr);
    ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(0x172), &UnitMountSpecialAnimHandler, nullptr);
    ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(0x480), &UnitPowerUpdateHandler, nullptr);
    ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(0x29c), &UnitCancelAutoRepeatHandler, nullptr);
    ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(0x29d), &UnitStandStateHandler, nullptr);
    ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(0x3ac), &UnitDismountHandler, nullptr);
    ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(0x4a7), &UnitSetVehicleRecHandler, nullptr);
}

} // namespace

// ref: FUN_00722c50
// A power the server reports, capped at its maximum; reaching the maximum signals the unit's
// power-full event. PARTIAL: the unit frames' refresh for the player and its pet (FUN_0053d1b0)
// is the UI port's.
void CGUnit_C::SetReportedPower(int32_t powerType, int32_t value) {
    int32_t maximum = powerType == -2 ? this->m_unit->maxHealth : this->m_unit->maxPower[powerType];

    if (maximum < value) {
        value = maximum;
    }

    if (powerType < 0 || 7 <= powerType || value == this->m_reportedPower[powerType]) {
        return;
    }

    this->m_reportedPower[powerType] = value;

    if (value == maximum) {
        ScriptEventsSignalUnitEvent(this->GetGUID(), powerType + 0x13);
    }
}

// ref: FUN_00742220
// The reference undoes this at the end of a game (FUN_00742bb0, from FUN_00406510); frozen has no
// end of game yet, so a second game in the same session registers nothing twice.
void UnitInitialize() {
    static bool registered = false;

    if (!registered) {
        RegisterUnitFieldHandlers();
        RegisterUnitMovementHandlers();
        RegisterUnitMiscHandlers();
        registered = true;
    }

    UnitSoundInitialize();
}

// ------------------------------------------------------------------------------------------------
// The unit's own slots
// ------------------------------------------------------------------------------------------------

namespace {

// How often each kind of creature sound plays, in percent (0x00adb5d8).
const uint32_t s_unitSoundChance[14] = { 70, 100, 60, 100, 100, 40, 100, 100, 100, 100, 100, 100, 100, 0 };

// The impact sound families (0x00a37170).
const int32_t s_impactSoundTypes[4] = { 0, 8, 7, 9 };

// When the unit's last "kind 5" sound played (DAT_00ca12d4): those wait ten seconds between them.
uint32_t s_lastStandSound;

// ref: FUN_007461e0
int32_t GetCreatureSound(const CreatureSoundDataRec* sound, int32_t kind) {
    if (!sound) {
        return 0;
    }

    switch (kind) {
        case 0: return sound->m_soundExertionID;
        case 1: return sound->m_soundExertionCriticalID;
        case 2:
        case 0xD: return sound->m_soundInjuryID;
        case 3: return sound->m_soundInjuryCriticalID;
        case 4: return sound->m_soundStunID;
        case 5: return sound->m_soundStandID;
        case 7: return sound->m_soundAggroID;
        case 8: return sound->m_soundAlertID;
        case 9: return sound->m_soundInjuryCrushingBlowID;
        case 10: return sound->m_soundWingGlideID;
        case 0xB: return sound->m_birthSoundID;
        case 0xC: return sound->m_spellCastDirectedSoundID;
        default: return 0;
    }
}

// ref: FUN_00746b30
// The sound kind's channel: 9/0xf for exertions, 0xb/0x10 for injuries, the second when the
// creature template asks for its sounds louder (type flags 0x20).
int32_t GetUnitSoundChannel(const CGUnit_C* unit, int32_t kind) {
    bool loud = unit->m_creatureStats && (unit->m_creatureStats->m_typeFlags & 0x20);

    switch (kind) {
        case 0:
        case 1:
            return loud ? 0xF : 9;

        case 2:
        case 3:
        case 9:
            return loud ? 0x10 : 0xB;

        default:
            return 0;
    }
}

} // namespace

// ref: FUN_00747310
void CGUnit_C::PlayUnitSound(int32_t kind, int32_t force) {
    if (!force) {
        uint32_t roll = static_cast<uint32_t>((static_cast<uint64_t>(CRandom::uint32(g_rndSeed)) * 101) >> 32);

        if (s_unitSoundChance[kind] < roll) {
            return;
        }
    }

    if (kind == 5) {
        uint32_t now = CWorld::GetCurTimeMs();

        if (static_cast<int32_t>(now - s_lastStandSound - 10000) < 0) {
            return;
        }

        s_lastStandSound = now;
    }

    int32_t soundID = GetCreatureSound(this->GetSoundData(), kind);

    if (!soundID) {
        return;
    }

    C3Vector position = this->GetPosition();

    SoundKitProperties properties;
    properties.ResetToDefaults();
    properties.m_type = GetUnitSoundChannel(this, kind);

    if (CGUnit_C::s_activeMover == this->GetGUID() || this->GetGUID() == ClntObjMgrGetActivePlayer()) {
        static CVar* listenerAtCharacter = CVar::Lookup("Sound_ListenerAtCharacter");
        bool atCharacter = listenerAtCharacter && listenerAtCharacter->GetInt() != 0;

        if (atCharacter) {
            properties.m_fadeOutTime = 0.6499999761581421f;
        }

        properties.int20 = 0x6E;

        SI2::PlaySoundKit(soundID, atCharacter ? nullptr : &position, nullptr, &properties, 0, nullptr, 1, 0);

        return;
    }

    if (kind == 7) {
        properties.int30 = 0;
    }

    SI2::PlaySoundKit(soundID, &position, nullptr, &properties, 0, nullptr, 1, 0);
}

// ref: FUN_007464d0
// TODO(SoundInterface2DSP): FUN_004cfc10 plays the foley of the model data's foley material.
void CGUnit_C::PlayArmorFoley() {
}

// ref: FUN_007463e0
int32_t CGUnit_C::GetImpactSoundType() {
    if (this->m_soundData && static_cast<uint32_t>(this->m_soundData->m_creatureImpactType) < 4) {
        return s_impactSoundTypes[this->m_soundData->m_creatureImpactType];
    }

    return 0;
}

// ref: FUN_00716490
bool CGUnit_C::IsDead() const {
    return this->m_unit->health <= 0;
}

// ref: FUN_0071f440
const UNIT_WEAPON_INFO* CGUnit_C::GetWeaponInfo(int32_t hand, int32_t ignoreHidden) {
    if (!this->GetWeaponDisplayID(hand)) {
        return nullptr;
    }

    if (!ignoreHidden) {
        if (hand == 0) {
            if (this->m_unit->flags & 0x200000) {
                auto main = this->GetWeaponInfo(0, 1);

                if (main) {
                    return main->m_class == 2 ? nullptr : &this->m_weaponInfo[0];
                }
            }
        } else if (hand == 1) {
            if (this->m_unit->flags & 0x200000) {
                this->GetWeaponInfo(0, 1);

                if (!this->IsHandHidden(0)) {
                    auto off = this->GetWeaponInfo(1, 1);

                    if (off && off->m_class == 2) {
                        return nullptr;
                    }
                }
            }

            if (this->m_unit->flags2 & 0x80) {
                return nullptr;
            }
        } else if (hand == 2) {
            if (this->m_unit->flags2 & 0x400) {
                return nullptr;
            }
        }
    }

    return &this->m_weaponInfo[hand];
}

// ref: FUN_0071f540
const ItemDisplayInfoRec* CGUnit_C::GetWeaponDisplay(int32_t hand) {
    return g_itemDisplayInfoDB.GetRecord(this->m_weaponDisplays[hand]);
}

// ref: FUN_00718b10
int32_t CGUnit_C::GetWeaponDisplayID(int32_t hand) {
    return this->m_weaponDisplays[hand];
}

// ref: FUN_0071a380
uint8_t CGUnit_C::GetStandStateByte() const {
    return static_cast<uint8_t>(this->m_unit->bytes1 & 0xFF);
}

// ref: FUN_0071aa70
int32_t CGUnit_C::GetSpellSkill(const SpellRec* spell) {
    int32_t skill = this->m_unit->level * 5;

    if (0 < spell->m_maxLevel && spell->m_maxLevel * 5 <= skill) {
        skill = spell->m_maxLevel * 5;
    }

    return skill < 0 ? 0 : skill;
}

// ref: FUN_00734f70
void CGUnit_C::GetDefenseSkill(int32_t* skill, int32_t* bonus) {
    *skill = this->m_unit->level * 5;
    *bonus = 0;
}

// ref: FUN_00734fa0
void CGUnit_C::GetWeaponSkill(int32_t attack, int32_t* skill, int32_t* bonus) {
    *skill = this->m_unit->level * 5;
    *bonus = 0;
}

// ref: FUN_0071ad20
int32_t CGUnit_C::GetSpellCastTime(const SpellRec* spell) {
    if (!spell) {
        return 0;
    }

    auto castTime = g_spellCastTimesDB.GetRecord(spell->m_castingTimeIndex);

    if (!castTime) {
        return 0;
    }

    int32_t ms = (this->GetSpellSkill(spell) / 5 - spell->m_baseLevel) * castTime->m_perLevel + castTime->m_base;

    if (ms < castTime->m_minimum) {
        ms = castTime->m_minimum;
    }

    if ((spell->m_attributes & 0x30) == 0 && (spell->m_attributesEx3 & 0x20000000) == 0 && 0 < ms) {
        float speed = this->m_unit->modCastingSpeed;

        if (speed != 1.0f) {
            ms = static_cast<int32_t>(lrintf(static_cast<float>(ms) * speed));
        }
    }

    if (spell->m_attributes & 0x2) {
        ms = 0x7FFFFFFF;
    }

    return ms < 1 ? 0 : ms;
}

// ref: FUN_006e6fc0
float CGUnit_C::GetMovementPitch() const {
    return this->m_localMove.GetPitch();
}

// ------------------------------------------------------------------------------------------------
// Weapons and the sheath
// ------------------------------------------------------------------------------------------------

namespace {

// The inventory slot each hand's virtual item goes through (0x00adac04). Only two entries: the
// ranged hand reads past the table, into a value both helpers below reject, so a creature's ranged
// virtual item never goes on here -- it goes through AttachRangedWeapon.
const uint32_t s_handInvSlots[2] = { 0xF, 0x10 };

uint32_t HandInvSlot(int32_t hand) {
    return hand < 2 ? s_handInvSlots[hand] : 0x3F000000;
}

} // namespace

// ref: FUN_00718fc0
int32_t CGUnit_C::IsHandHidden(int32_t hand) {
    if (hand == 2) {
        return 0;
    }

    if (this->m_unit->flags & 0x200000) {
        auto main = this->GetWeaponInfo(0, 1);

        if (hand == 0) {
            return main && main->m_class == 2 ? 1 : 0;
        }

        if (!this->IsHandHidden(0)) {
            auto off = this->GetWeaponInfo(1, 1);

            if (off && off->m_class == 2) {
                return 1;
            }
        }
    }

    if (hand == 1 && (this->m_unit->flags2 & 0x80)) {
        return 1;
    }

    return 0;
}

// ref: FUN_00725010
// TODO(WeaponTrail): FUN_00720170 gives the new weapon its trail.
void CGUnit_C::UpdateVirtualItem(int32_t hand, int32_t oldEntry) {
    auto old = g_itemDB.GetRecord(oldEntry);
    auto item = g_itemDB.GetRecord(this->m_unit->virtualItemSlotID[hand]);

    this->m_weaponDisplays[hand] = item ? item->m_displayInfoID : 0;

    UNIT_WEAPON_INFO info;

    if (item) {
        info.m_class = static_cast<uint8_t>(item->m_classID);
        info.m_subclass = static_cast<uint8_t>(item->m_subclassID);
        info.m_soundOverride = static_cast<uint8_t>(item->m_soundOverrideSubclassID);
        info.m_material = static_cast<uint8_t>(item->m_material);
        info.m_inventoryType = static_cast<uint8_t>(item->m_inventoryType);
        info.m_sheathType = static_cast<uint8_t>(item->m_sheatheType);
    }

    this->m_weaponInfo[hand] = info;

    int32_t oldSheath = old ? old->m_sheatheType : 0;
    CCharacterComponent::RemoveHandItemLinks(this->m_model, static_cast<INVENTORY_SLOTS>(HandInvSlot(hand)), oldSheath, false);

    if (hand == 1) {
        CCharacterComponent::RemoveHandItemLinks(this->m_model, static_cast<INVENTORY_SLOTS>(s_handInvSlots[1]), oldSheath, true);
    }

    auto now = this->GetWeaponInfo(hand, 0);

    if (now) {
        auto display = this->GetWeaponDisplay(hand);

        if (display) {
            CCharacterComponent::AddHandItem(this->m_model, display, static_cast<INVENTORY_SLOTS>(HandInvSlot(hand)),
                                             static_cast<SHEATHE_TYPE>(now->m_sheathType), this->m_sheathState == 0,
                                             now->m_inventoryType == 0xE, false, 0);
        }
    }
}

// ref: FUN_00721ed0
bool CGUnit_C::IsFightingUnarmed() {
    uint32_t animID = 0xFFFFFFFF;

    if (this->m_model && this->m_model->IsLoaded(0, 0)) {
        if (this->m_upperBodyBoneId == 0xFFFFFFFF || (animID = this->m_model->GetBoneUint90(this->m_upperBodyBoneId)) == 0xFFFFFFFF) {
            animID = this->m_model->GetBoneUint90(0xFFFFFFFF);
        }
    }

    if (IsUnarmedAnimation(animID)) {
        return true;
    }

    return IsCombatAnimation(animID) && !this->GetWeaponInfo(0, 0);
}

// ref: FUN_0072dbc0
// TODO(WeaponTrail): FUN_00720170, as in UpdateVirtualItem; TODO(Player_C): a player's hand item
// also takes its enchantment visual (FUN_006e1290).
void CGUnit_C::AttachHandItem(int32_t hand) {
    if (this->m_modelData && (this->m_modelData->m_flags & 0x10)) {
        return;
    }

    if (!this->m_model) {
        return;
    }

    auto info = this->GetWeaponInfo(hand, 0);

    if (!info) {
        return;
    }

    auto display = this->GetWeaponDisplay(hand);

    if (!display) {
        return;
    }

    int32_t sheathType = info->m_sheathType;
    bool sheathed;
    int32_t state;

    if (this->m_sheathState == 2) {
        sheathed = hand != 2;
        state = 2;
    } else {
        int32_t effective;

        if (hand == 1 && this->IsFightingUnarmed()) {
            state = this->m_sheathState;
            effective = AdjustSheathState(state, reinterpret_cast<const uint8_t*>(this->GetWeaponInfo(0, 0)),
                                          reinterpret_cast<const uint8_t*>(info));
        } else {
            state = this->m_sheathState;
            effective = state;
        }

        sheathed = effective == 0;
    }

    bool shield = info->m_inventoryType == 0xE;
    bool heldRight = info->m_inventoryType == 0x1A || info->m_inventoryType == 0x19;

    uint32_t invSlot;
    uint32_t hand_link;
    bool first;

    if (hand == 0) {
        if (this->m_unit->flags & 0x200000) {
            auto main = this->GetWeaponInfo(0, 1);

            if (main && main->m_class == 2) {
                return;
            }
        }

        hand_link = 1;
        invSlot = 0xF;
        first = true;
    } else if (hand == 1) {
        if (this->m_unit->flags & 0x200000) {
            this->GetWeaponInfo(0, 1);

            if (!this->IsHandHidden(0)) {
                auto off = this->GetWeaponInfo(1, 1);

                if (off && off->m_class == 2) {
                    return;
                }
            }
        }

        if (this->m_unit->flags2 & 0x80) {
            return;
        }

        if (!this->IsA(TYPE_PLAYER)) {
            auto main = this->GetWeaponInfo(0, 0);

            if (main && main->m_class == 2) {
                uint8_t sub = main->m_subclass;

                if (sub == 1 || sub == 5 || sub == 8 || sub == 10 || sub == 12 || sub == 6 || sub == 17 || sub == 20) {
                    return;
                }
            }
        }

        hand_link = 2;
        invSlot = 0x10;
        first = false;
    } else {
        if (state != 2) {
            CCharacterComponent::RemoveHandItemLinks(this->m_model, static_cast<INVENTORY_SLOTS>(0x11), 0, false);

            return;
        }

        if (!this->IsHandHidden(heldRight ? 0 : 1)) {
            this->AttachRangedWeapon(0, nullptr);
        }

        return;
    }

    // While the ranged weapon is out, the hand item is kept rather than rebuilt.
    CM2Model* kept = nullptr;

    if (this->m_sheathState == 2) {
        for (auto child = this->m_model->m_attachList; child; child = child->m_attachNext) {
            if (child->m_attachId == hand_link) {
                child->m_refCount++;
                child->DetachFromParent();
                kept = child;

                break;
            }
        }
    }

    int32_t link = CCharacterComponent::AddHandItem(this->m_model, display, static_cast<INVENTORY_SLOTS>(invSlot),
                                                    static_cast<SHEATHE_TYPE>(sheathType), sheathed, shield, heldRight, 0);

    (void)link;
    (void)first;

    if (kept) {
        kept->AttachToParent(this->m_model, hand_link, nullptr, 0);
        kept->Release();
    }
}

// ref: FUN_0072afe0
// TODO(Combat): an active player's auto-shot stops (FUN_007272c0) when nothing else is attacking.
void CGUnit_C::ReleaseRangedWeapon() {
    this->m_animFlags &= ~0x4000u;

    if (this->m_rangedAmmoModel && this->m_rangedAmmoModel->m_attachParent) {
        this->m_rangedAmmoModel->DetachFromParent();
    }

    if (this->m_rangedModel && this->m_rangedModel->IsLoaded(0, 0)
        && this->m_rangedModel->GetBoneUint90(0xFFFFFFFF) != 0xA1) {
        this->m_rangedModel->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);
    }
}

// ref: FUN_0072b7f0
// TODO(Combat): the active player re-arms its auto-shot at the target when the spell it is
// casting or channelling (+0xa60) is one (FUN_0072b4a0); TODO(Player_C): a player's quiver (item
// section 11) is added or removed with the bow.
int32_t CGUnit_C::AttachRangedWeapon(int32_t inHand, uint8_t* heldRight) {
    if (this->m_modelData && (this->m_modelData->m_flags & 0x10)) {
        return 0;
    }

    auto info = this->GetWeaponInfo(2, 0);

    if (!info) {
        return 0;
    }

    this->ReleaseRangedWeapon();

    bool right = info->m_inventoryType == 0x1A || info->m_inventoryType == 0x19;

    if (heldRight) {
        *heldRight = right ? 1 : 0;
    }

    if (this->m_rangedModel) {
        this->m_rangedModel->m_flag20 = 1;
        this->m_rangedModel->m_lightingCallback = nullptr;
        this->m_rangedModel->m_lightingArg = nullptr;
        this->m_rangedModel->Release();
        this->m_rangedModel = nullptr;
    }

    if (!inHand) {
        this->m_model->DetachAllChildrenById(1);
        this->m_model->DetachAllChildrenById(2);
        this->m_model->DetachAllChildrenById(0);
    }

    auto display = this->GetWeaponDisplay(2);
    int32_t link = CCharacterComponent::AddHandItem(this->m_model, display, static_cast<INVENTORY_SLOTS>(0x11),
                                                    static_cast<SHEATHE_TYPE>(info->m_sheathType), inHand != 0, false, right, 0);

    for (auto child = this->m_model->m_attachList; child; child = child->m_attachNext) {
        if (static_cast<int32_t>(child->m_attachId) == link) {
            this->m_rangedModel = child;
            child->m_refCount++;
            child->SetSequenceDoneCallback(nullptr, 0);

            break;
        }
    }

    if (inHand) {
        this->UpdateSheathedAuraVisuals(1, 0);
    }

    return 1;
}

// ref: FUN_007310a0
int32_t CGUnit_C::MoveHandItem(int32_t hand, int32_t toSheath) {
    auto model = this->m_model;

    if (!model) {
        return 0;
    }

    auto info = this->GetWeaponInfo(hand, 0);

    if (!info) {
        return 0;
    }

    bool rightHanded = hand == 0 || info->m_inventoryType == 0x1A || info->m_inventoryType == 0x19;

    uint32_t handLink;

    if (hand == 0) {
        handLink = 1;
    } else if (hand == 1) {
        handLink = info->m_inventoryType != 0xE ? 2 : 0;
    } else if (hand == 2) {
        handLink = rightHanded ? 1 : 2;
    } else {
        return 0;
    }

    uint32_t from;
    uint32_t to;

    if (!toSheath) {
        from = CCharacterComponent::GetSheatheLink(static_cast<SHEATHE_TYPE>(info->m_sheathType), rightHanded);
        to = handLink;
    } else {
        from = handLink;
        to = CCharacterComponent::GetSheatheLink(static_cast<SHEATHE_TYPE>(info->m_sheathType), rightHanded);
    }

    // TODO(Player_C): the active player holding a shield while its fishing pole flag (DAT_00bd19b8)
    // is up leaves the shield off.

    for (auto child = model->m_attachList; child; child = child->m_attachNext) {
        if (child->m_attachId != from) {
            continue;
        }

        child->m_refCount++;
        child->DetachFromParent();

        if (to != 0xFFFFFFFF) {
            child->AttachToParent(model, to, nullptr, 0);
        }

        child->Release();

        if (handLink != 0) {
            if (to == handLink) {
                CCharacterComponent::ComponentCloseFingers(model, static_cast<COMP_HAND_SLOT>(handLink != 1));
            } else {
                CCharacterComponent::ComponentOpenFingers(model, static_cast<COMP_HAND_SLOT>(handLink != 1));
            }
        }

        if (hand != 2) {
            if (toSheath && rightHanded) {
                this->UpdateSheathedAuraVisuals(1, 0);
            }

            return 1;
        }

        if (!toSheath) {
            return 1;
        }

        this->AttachHandItem(0);
        this->AttachHandItem(1);

        if (rightHanded) {
            this->UpdateSheathedAuraVisuals(1, 0);
        }

        return 1;
    }

    this->AttachHandItem(hand);

    if (toSheath && rightHanded) {
        this->UpdateSheathedAuraVisuals(1, 0);
    }

    return 1;
}

// ref: FUN_00736d30
void CGUnit_C::SetSheathState(int32_t state, int32_t animate, int32_t fromServer) {
    if (this->IsFightingUnarmed()) {
        state = AdjustSheathState(state, reinterpret_cast<const uint8_t*>(this->GetWeaponInfo(0, 0)),
                                  reinterpret_cast<const uint8_t*>(this->GetWeaponInfo(1, 0)));
    }

    if (state == 2 && this->IsA(TYPE_PLAYER)) {
        auto classRec = g_chrClassesDB.GetRecord(static_cast<uint8_t>(this->m_unit->bytes0 >> 8));

        if (!classRec || (classRec->m_flags & 0x8)) {
            return;
        }
    }

    if (state == this->m_sheathState) {
        return;
    }

    if (this->m_creatureStats && (this->m_creatureStats->m_typeFlags & 0x10000000) && !fromServer) {
        return;
    }

    if (!this->m_model) {
        return;
    }

    if (animate) {
        if (!this->m_vehiclePassenger || !this->m_vehiclePassenger->IsRidingLiveVehicle()) {
            for (uint32_t bone : { 3u, 2u }) {
                if (this->m_model->IsLoaded(0, 0) && this->m_model->BoneHasParent(bone)) {
                    this->m_model->UnsetBoneSequence(bone, 1, 1);
                }

                ForEachAnimatedRider(this, this->m_model, 0x19fd, [&](CGUnit_C* rider) {
                    rider->UnsetBoneSequence(rider->GetObjectModel(), bone, 1, 1, 1);
                });
            }
        }
    }

    this->m_previousSheathState = this->m_sheathState;
    this->m_sheathState = state;

    if (this->GetGUID() == ClntObjMgrGetActivePlayer() && !fromServer) {
        CDataStore msg;
        msg.Put(static_cast<uint32_t>(CMSG_SET_SHEATHED));
        msg.Put(static_cast<uint32_t>(this->m_sheathState));
        msg.Finalize();
        ClientServices::Send(&msg);
    }

    if (state != 0) {
        this->UpdateSheathedAuraVisuals(0, 0);
    }

    if (animate) {
        this->AttachWeaponsForSheath();
    } else {
        this->PlaySheathAnimation();
    }
}

// ref: FUN_00731f40
// TODO(Combat): leaving the ranged state stops the active player's auto-shot (FUN_007272c0).
void CGUnit_C::AttachWeaponsForSheath() {
    int32_t state = this->m_sheathState;

    if (state == 0) {
        if (this->m_previousSheathState == 1) {
            this->MoveHandItem(0, 1);
            this->MoveHandItem(1, 1);
        } else if (this->m_previousSheathState == 2) {
            this->m_model->DetachAllChildrenById(0x23);

            if (this->m_characterComponent && this->IsA(TYPE_PLAYER)) {
                this->m_characterComponent->RemoveItem(ITEMSLOT_11);
            }

            auto ranged = this->GetWeaponInfo(2, 0);

            if (ranged) {
                if (ranged->m_inventoryType != 0x1A && ranged->m_inventoryType != 0x19) {
                    this->m_model->DetachAllChildrenById(2);
                    CCharacterComponent::ComponentOpenFingers(this->m_model, static_cast<COMP_HAND_SLOT>(1));
                } else {
                    this->m_model->DetachAllChildrenById(1);
                    CCharacterComponent::ComponentOpenFingers(this->m_model, static_cast<COMP_HAND_SLOT>(0));
                }
            }
        }

        this->m_animFlags &= 0xFFCFFFFF;

        return;
    }

    if (state == 1) {
        if (this->m_previousSheathState == 2) {
            this->AttachRangedWeapon(1, nullptr);
        }

        if (this->MoveHandItem(0, 0)) {
            this->m_animFlags |= 0x100000;
        }

        if (this->MoveHandItem(1, 0)) {
            this->m_animFlags |= 0x200000;
        }

        return;
    }

    if (state == 2) {
        if (this->m_previousSheathState == 1) {
            this->MoveHandItem(0, 1);
            this->MoveHandItem(1, 1);
        }

        this->m_animFlags &= 0xFFCFFFFF;

        uint8_t right = 0;

        if (this->AttachRangedWeapon(0, &right)) {
            this->m_animFlags |= right ? 0x100000 : 0x200000;
        }
    }
}

// ref: FUN_007367b0
int32_t CGUnit_C::PlayMainHandSheath() {
    this->m_animFlags |= 0x100000;

    if (this->m_previousSheathState == 0) {
        return 0;
    }

    if (this->m_sheathState == 1) {
        auto main = this->GetWeaponInfo(0, 0);

        if (main) {
            uint32_t anim = ((1u << (main->m_sheathType & 0x1F)) & 0x88) ? 0x5A : 0x59;
            this->SetBoneSequence(this->m_model, 3, anim, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);

            return 1;
        }
    } else if (this->m_sheathState == 2) {
        auto ranged = this->GetWeaponInfo(2, 0);

        if (ranged && (ranged->m_inventoryType == 0x1A || ranged->m_inventoryType == 0x19)) {
            this->AttachRangedWeapon(1, nullptr);

            uint32_t anim = ((1u << (ranged->m_sheathType & 0x1F)) & 0x88) ? 0x5A : 0x59;
            this->SetBoneSequence(this->m_model, 3, anim, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);

            return 1;
        }
    }

    return 0;
}

// ref: FUN_007368b0
int32_t CGUnit_C::PlayOffHandSheath() {
    this->m_animFlags |= 0x200000;

    if (this->m_previousSheathState == 0) {
        return 0;
    }

    if (this->m_sheathState == 1) {
        auto off = this->GetWeaponInfo(1, 0);

        if (off) {
            uint32_t anim = ((1u << (off->m_sheathType & 0x1F)) & 0x88) ? 0x5A : 0x59;
            this->SetBoneSequence(this->m_model, 2, anim, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);

            return 1;
        }
    } else if (this->m_sheathState == 2) {
        auto ranged = this->GetWeaponInfo(2, 0);

        if (ranged && ranged->m_inventoryType != 0x1A && ranged->m_inventoryType != 0x19) {
            this->AttachRangedWeapon(1, nullptr);

            uint32_t anim = ((1u << (ranged->m_sheathType & 0x1F)) & 0x88) ? 0x5A : 0x59;
            this->SetBoneSequence(this->m_model, 2, anim, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);

            return 1;
        }
    }

    return 0;
}

// ref: FUN_007369b0
void CGUnit_C::PlayDrawAnimation() {
    if (this->m_sheathState == 1) {
        auto main = this->GetWeaponInfo(0, 0);
        auto off = this->GetWeaponInfo(1, 0);

        if (main) {
            uint32_t anim = ((1u << (main->m_sheathType & 0x1F)) & 0x88) ? 0x5A : 0x59;
            this->SetBoneSequence(this->m_model, 3, anim, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);
            this->m_animFlags |= 0x100000;
        }

        if (off) {
            uint32_t anim = ((1u << (off->m_sheathType & 0x1F)) & 0x88) ? 0x5A : 0x59;
            this->SetBoneSequence(this->m_model, 2, anim, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);
            this->m_animFlags |= 0x200000;
        }

        return;
    }

    if (this->m_sheathState != 2) {
        return;
    }

    auto ranged = this->GetWeaponInfo(2, 0);

    if (!ranged) {
        return;
    }

    this->AttachRangedWeapon(1, nullptr);

    uint32_t anim = ((1u << (ranged->m_sheathType & 0x1F)) & 0x88) ? 0x5A : 0x59;

    if (ranged->m_inventoryType != 0x1A && ranged->m_inventoryType != 0x19) {
        this->SetBoneSequence(this->m_model, 2, anim, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);
        this->m_animFlags |= 0x200000;

        return;
    }

    this->SetBoneSequence(this->m_model, 3, anim, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);
    this->m_animFlags |= 0x100000;
}

// ref: FUN_00736b60
// TODO(Combat): leaving the ranged state stops the active player's auto-shot (FUN_007272c0).
void CGUnit_C::PlaySheathAnimation() {
    int32_t previous = this->m_previousSheathState;

    if (previous == 0) {
        this->PlayDrawAnimation();

        return;
    }

    if (previous == 1) {
        auto main = this->GetWeaponInfo(0, 0);
        auto off = this->GetWeaponInfo(1, 0);

        if (!main) {
            this->PlayMainHandSheath();
        } else {
            uint32_t anim = ((1u << (main->m_sheathType & 0x1F)) & 0x88) ? 0x5A : 0x59;
            this->SetBoneSequence(this->m_model, 3, anim, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);
            this->m_animFlags &= ~0x100000u;
        }

        if (!off) {
            this->PlayOffHandSheath();

            return;
        }

        uint32_t anim = ((1u << (off->m_sheathType & 0x1F)) & 0x88) ? 0x5A : 0x59;
        this->SetBoneSequence(this->m_model, 2, anim, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);
        this->m_animFlags &= ~0x200000u;

        return;
    }

    if (previous != 2) {
        return;
    }

    this->m_model->DetachAllChildrenById(0x23);

    if (this->m_characterComponent && this->IsA(TYPE_PLAYER)) {
        this->m_characterComponent->RemoveItem(ITEMSLOT_11);
    }

    auto ranged = this->GetWeaponInfo(2, 0);

    if (!ranged) {
        this->PlayDrawAnimation();

        return;
    }

    if (ranged->m_inventoryType != 0x1A && ranged->m_inventoryType != 0x19) {
        this->SetBoneSequence(this->m_model, 2, 0x59, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);
        this->m_animFlags &= ~0x200000u;
        this->PlayMainHandSheath();

        return;
    }

    this->SetBoneSequence(this->m_model, 3, 0x59, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);
    this->m_animFlags &= ~0x100000u;
    this->PlayOffHandSheath();
}

// ------------------------------------------------------------------------------------------------
// The model
// ------------------------------------------------------------------------------------------------

namespace {

const CreatureDisplayInfoRec* WornDisplay(const CGUnit_C* unit, int32_t* idOut) {
    int32_t id = unit->m_localDisplayID;

    if (id == 0 || unit->Unit()->nativeDisplayID != unit->Unit()->displayID) {
        id = unit->Unit()->displayID;
    }

    if (idOut) {
        *idOut = id;
    }

    return g_creatureDisplayInfoDB.GetRecord(id);
}

} // namespace

// ref: FUN_0072a480
bool CGUnit_C::NeedsModelUpdate() {
    int32_t id;
    auto display = WornDisplay(this, &id);

    if (!display) {
        // "NOUNITDISPLAYID|%d|%s" (SysMsgPrintf) in the reference.
        return false;
    }

    if (display == this->m_displayInfo) {
        return false;
    }

    if (this->m_displayInfo && display->m_modelID == this->m_displayInfo->m_modelID) {
        if (g_creatureModelDataDB.GetRecord(display->m_modelID) == this->m_modelData) {
            return g_creatureSoundDataDB.GetRecord(display->m_soundID) != this->m_soundData;
        }
    }

    return true;
}

// ref: FUN_007179d0
// PHASE4(Missile_C): the unit's missiles in flight reattach to the new model (FUN_00703900).
void CGUnit_C::SetUnitModel(CM2Model* model) {
    if (this->m_model == model) {
        return;
    }

    if (!this->m_mountModel) {
        this->Virtual0E0();
    }

    // FUN_00743660
    if (model) {
        model->m_refCount++;
    }

    auto previous = this->m_model;
    this->m_model = model;

    this->ModelChanged(previous);
}

// ref: FUN_00722ae0
float CGUnit_C::GetDisplayScale(int32_t displayID) {
    auto display = g_creatureDisplayInfoDB.GetRecord(displayID);

    if (!display) {
        return 1.0f;
    }

    auto modelData = g_creatureModelDataDB.GetRecord(display->m_modelID);

    if (!modelData) {
        return 1.0f;
    }

    return this->GetModelScale(display, modelData);
}

// ref: FUN_0072cbb0
void CGUnit_C::UpdateDisplayScale(int32_t keepScale) {
    float previous = this->m_displayScale;
    this->m_displayScale = this->GetDisplayScale(this->m_unit->displayID);

    float scale = keepScale ? this->m_scale : previous / this->m_displayScale * this->m_scale;

    this->SetScaleEase(scale);
    this->UpdateShadowRadius();
    this->UpdateEffectAttachments();

    if (this->m_vehicle && this->m_vehicle->m_rec) {
        this->m_vehicle->UpdatePassengerRadius();
    }
}

// ref: FUN_00728e70
void CGUnit_C::ReplayAuraVisuals() {
    for (int32_t slot = static_cast<int32_t>(this->m_auras.Count()) - 1; slot >= 0; slot--) {
        int32_t spellID = this->m_auras[slot].m_spellID;

        if (!spellID) {
            continue;
        }

        if (auto spell = g_spellDB.GetRecord(spellID)) {
            this->AddAuraVisual(slot, spell);
        }
    }
}

// ref: FUN_0072bc70
// TODO(Combat): a channel that is an auto-attack (spell attributes 0x4000) re-arms the active
// player's swing at the channel target (FUN_0072b4a0); TODO(Spell_C): a fishing channel strings its
// line (FUN_007221d0).
void CGUnit_C::UpdateChannelVisual() {
    this->m_stateFlags &= ~0x8000u;

    auto spell = g_spellDB.GetRecord(this->m_unit->channelSpell);

    if (!spell) {
        return;
    }

    if (spell->m_attributesEx & 0x4000) {
        this->m_stateFlags |= 0x8000;
    }

    auto visual = GetSpellVisual(spell);
    auto kit = visual ? g_spellVisualKitDB.GetRecord(visual->m_channelKit) : nullptr;

    if (!kit) {
        return;
    }

    auto castKit = g_spellVisualKitDB.GetRecord(visual->m_castKit);
    bool playing = castKit && castKit->m_animID == static_cast<int32_t>(this->GetCurrentAnimationId());

    SPELLVISUALKITPARAMS params;
    params.m_spell = spell;
    params.m_kit = kit;
    params.m_kitType = 2;
    params.m_stateParam = 1;
    params.m_param9 = -1;
    params.m_param8 = playing ? 0 : 1;

    this->PlayKit(params);
}

// ref: FUN_00717310
float CGUnit_C::GetAnimationProgress() {
    if (!this->m_model || !this->m_model->IsLoaded(0, 0)) {
        return 0.0f;
    }

    M2BoneSequenceState state = {};

    if (this->m_upperBodyBoneId != 0xFFFFFFFF) {
        this->m_model->GetBoneSequenceState(this->m_upperBodyBoneId, &state);
    }

    if (state.uint90 == 0xFFFFFFFF) {
        this->m_model->GetBoneSequenceState(0xFFFFFFFF, &state);
    }

    float length = static_cast<float>(state.endTime - state.startTime);

    if (!(0.0f < length)) {
        return 0.0f;
    }

    float progress = static_cast<float>(static_cast<uint32_t>(state.currentTime)) / length;

    if (progress < 0.0f) {
        return 0.0f;
    }

    return progress < 1.0f ? progress : 1.0f;
}

// ref: FUN_0073e410
// TODO(PlayerName): the name plate is rebuilt for the new model (FUN_007e6320, FUN_00719050).
void CGUnit_C::UpdateModel(int32_t force) {
    if (!force && !this->NeedsModelUpdate()) {
        return;
    }

    this->m_boneMask = 0;

    // A unit dying or dead keeps its death pose through the rebuild.
    uint32_t pose = 0x1FA;

    if (this->IsDeadOrFeigning()) {
        int32_t behavior = GetAnimationBehavior(this->GetCurrentAnimationId());

        if (IsAnimationBehavior1Or131Or466To467(behavior) && this->GetAnimationProgress() < 0.5f) {
            pose = behavior;
        }
    }

    // FUN_00746340
    SI2::StopOrFadeOut(this->m_mountSound, 0, 0.5f, 1);

    auto previousModelData = this->m_modelData;
    this->RefreshDataPointers();

    this->m_animFlags &= ~0x180u;
    this->m_upperBodyBoneId = 0xFFFFFFFF;

    const char* fileName;

    if (!this->GetModelFileName(fileName)) {
        return;
    }

    auto model = CWorld::GetM2Scene()->CreateModel(fileName, 0);

    if (!model) {
        return;
    }

    this->m_animFlags |= 0x8000000;

    uint32_t current = this->GetCurrentAnimationId();

    model->SetSequenceDoneCallback(&CGUnit_C::OnModelSequenceDone, this->GetGUID());

    this->SetUnitModel(model);
    model->Release();

    model->m_flag4 = this->m_modelData && (this->m_modelData->m_flags & 0x200) ? 1 : 0;

    this->UpdateDisplayScale(previousModelData ? 1 : 0);
    this->PlaceModel(0.0f);
    model->ForceAnimate();

    CCharacterComponent::ApplyMonsterGeosets(this->m_model, this->m_displayInfo);
    CCharacterComponent::ReplaceMonsterSkin(this->m_model, this->m_displayInfo, this->m_modelData);

    if (this->m_modelData) {
        this->m_footprintTexture = this->m_modelData->m_footprintTextureID;
        this->m_footprintLength = this->m_modelData->m_footprintTextureWidth * 0.02777777798473835f;
        this->m_footprintWidth = 0.02777777798473835f * this->m_modelData->m_footprintTextureLength;
        this->m_footprintParticleScale = this->m_modelData->m_footprintParticleScale;
    }

    this->UpdateMountSound();
    RetargetCamera(this);

    this->OnReenable();

    if (this->m_characterComponent) {
        CCharacterComponent::FreeComponent(this->m_characterComponent);
        this->m_characterComponent = nullptr;
    }

    // The component is built once the new model is in (GetHidden -> BuildComponent).
    this->m_stateFlags = (this->m_stateFlags & ~0x20000u) | 0x400000;

    this->ApplyAlphaEffects();
    this->ReplayAuraVisuals();
    this->UpdateChannelVisual();

    if (this->m_modelData && (this->m_modelData->m_flags & 0x8)) {
        this->m_animFlags |= 0x20000;
    } else {
        this->m_animFlags &= ~0x20000u;
    }

    this->m_animFlags &= 0xF932DFF3;

    if (this->m_modelData && (this->m_modelData->m_flags & 0x40)) {
        this->m_stateFlags |= 0x2000000;
    } else {
        this->m_stateFlags &= ~0x2000000u;
    }

    uint8_t setFlags = 0;

    if (pose == 0x1FA) {
        pose = this->GetCurrentAnimationId();

        if (pose == 0 && current != 0x1FA && current != 0xFFFFFFFF && this->m_localMove.IsInForcedMotion()) {
            pose = current;
        } else {
            // A death kit playing on the unit keeps its blend flags.
            for (auto effect = this->m_effects; effect; effect = effect->m_linkNext) {
                if (!effect->m_kit || !IsDeathAnimation(effect->m_kit->m_animID)) {
                    continue;
                }

                uint32_t kitFlags = effect->m_kit->m_flags;

                if (kitFlags & 0x4) setFlags |= 2;
                if (kitFlags & 0x8) setFlags |= 4;
                if (kitFlags & 0x10) setFlags |= 8;
                if (kitFlags & 0x100) setFlags |= 0x20;

                if (setFlags & 6) {
                    break;
                }
            }

            if ((setFlags & 6) == 0) {
                setFlags = 0;
            }
        }
    }

    this->SetAnimation(pose, setFlags);

    if (this->m_mountModel) {
        this->m_model->AttachToParent(this->m_mountModel, 0, nullptr, 0);
    }

    this->UpdateObjectEffectPackage();
    this->m_animFlags &= ~0x8000000u;
}

// ref: FUN_00715270
void CGUnit_C::AddFacingOffset(float delta) {
    this->m_smoothFacing = NormalizeAngle(delta + this->m_smoothFacing);
    this->m_lowerBodyFacing = NormalizeAngle(delta + this->m_lowerBodyFacing);
}

// ref: FUN_0071c4d0
WOWGUID CGUnit_C::GetCameraTransportGUID() {
    if (this->m_vehicleCamera && this->m_vehicleCamera->m_state != 0) {
        return this->m_vehicleCamera->GetRelativeGUID();
    }

    return this->GetTransportGUID();
}

// ref: FUN_0073e840
void CGUnit_C::OnModelLoaded(CM2Model* model) {
    this->CGObject_C::OnModelLoaded(model);

    if (this->GetTransportGUID() && model->m_loaded) {
        C44Matrix transport;
        MovementGetTransportMatrixChecked(this->GetTransportGUID(), transport, this->GetGUID(), ".\\Unit_C.cpp", 0x40c8);

        model->m_particleRelativeMatrix = transport;
        model->SetParticleRelative(&model->m_particleRelativeMatrix);
    }

    if (model == this->m_model) {
        this->m_animFlags &= ~0x180u;

        if (model->HasBone(4)) {
            this->m_animFlags |= 0x80;
        }

        if (model->HasBone(6)) {
            this->m_animFlags |= 0x100;
        }

        if (this->m_animFlags & 0x80) {
            this->m_upperBodyBoneId = 4;
        } else {
            this->m_upperBodyBoneId = (this->m_animFlags & 0x100) ? 6 : 0xFFFFFFFF;
        }

        this->AttachQuestMarker();
    } else if (model == this->m_mountModel && !model->HasAttachment(0)) {
        // "MOUNTDISPLAYIDNOMOUNTATTACHMENT|%d" (SysMsgPrintf) in the reference.
    }

    if (this->m_postInited) {
        this->UpdateAnimation(1, 0xFFFFFFFF);

        if (GetAnimationBehavior(this->GetCurrentAnimationId()) == 0x7F) {
            this->m_fadeDuration = 0;
            this->m_alpha = this->m_alphaTo;
        }

        this->UpdateObjectEffects();
    }

    if (model == this->GetObjectModel()) {
        this->UpdateBoneMask();
    }

    // A rider's model in: its vehicle's reach grows, and the vehicle re-places it.
    if (this->m_vehiclePassenger && this->m_vehiclePassenger->m_state == 3) {
        auto vehicle = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->GetTransportGUID(), TYPE_UNIT, ".\\Unit_C.cpp", 0x40fa));

        if (vehicle) {
            if (vehicle->m_vehicle && vehicle->m_vehicle->m_rec) {
                vehicle->m_vehicle->UpdatePassengerRadius();
            }

            vehicle->UpdateWorldObject(0);
        }
    }

    if (this->m_pendingStateKitSpell) {
        bool still = false;

        for (uint32_t slot = 0; slot < this->m_auras.Count(); slot++) {
            if (this->m_auras[slot].m_spellID == this->m_pendingStateKitSpell) {
                still = true;
                break;
            }
        }

        if (still) {
            auto spell = g_spellDB.GetRecord(this->m_pendingStateKitSpell);
            auto visual = spell ? GetSpellVisual(spell) : nullptr;
            auto kit = visual ? g_spellVisualKitDB.GetRecord(visual->m_stateKit) : nullptr;

            if (kit) {
                SPELLVISUALKITPARAMS params;
                params.m_spell = spell;
                params.m_kit = kit;
                params.m_kitType = 2;
                params.m_stateParam = 1;
                params.m_param9 = -1;

                this->PlayKit(params);
            }
        }

        this->m_pendingStateKitSpell = 0;
    }

    if (model == this->GetObjectModel()) {
        if (this->m_vehicle && this->m_vehicle->m_rec && this->m_vehicle->HasStateBits()) {
            this->m_vehicle->ResyncPassengerAnimations();
        }

        if (this->m_vehiclePassenger && this->m_vehiclePassenger->IsRidingLiveVehicle()) {
            this->m_vehiclePassenger->SyncToVehicle();
        }
    }
}

// ref: FUN_0071a430
bool CGUnit_C::IsCharacterDisplayPlayer() const {
    return this->IsA(TYPE_PLAYER) && this->m_modelData && (this->m_modelData->m_flags & 0x4)
        && this->m_displayInfoExtra && (this->m_displayInfoExtra->m_flags & 0x1);
}

// ref: FUN_00716e20
void CGUnit_C::ApplyItemVisualEffects() {
    for (auto effect = this->m_effects; effect; ) {
        auto next = effect->m_linkNext;

        if (effect->m_flags & CEffect::EFFECT_ITEM_VISUAL) {
            effect->ApplyItemSection();
        }

        effect = next;
    }
}

// ref: FUN_00716f10
void CGUnit_C::RequestMirrorImageData() {
    this->m_stateFlags |= 0x20000;

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_GET_MIRROR_IMAGE_DATA));
    msg.Put(this->GetGUID());
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_0071d010
int32_t CGUnit_C::InitComponent(CGPlayer_C* player, int32_t raceSexFromExtra) {
    this->m_characterComponent = CCharacterComponent::AllocComponent();

    if (!this->m_characterComponent) {
        return 0;
    }

    ComponentData data;

    if (raceSexFromExtra) {
        data.raceID = this->m_displayInfoExtra->m_displayRaceID;
        data.sexID = this->m_displayInfoExtra->m_displaySexID;
    } else {
        data.raceID = static_cast<uint8_t>(this->m_unit->bytes0);
        data.sexID = static_cast<uint8_t>(this->m_unit->bytes0 >> 16);
    }

    data.classID = static_cast<uint8_t>(this->m_unit->bytes0 >> 8);

    if (!player) {
        auto extra = this->m_displayInfoExtra;

        data.skinColorID = extra->m_skinID;
        data.faceID = extra->m_faceID;
        data.hairStyleID = extra->m_hairStyleID;
        data.hairColorID = extra->m_hairColorID;
        data.facialHairStyleID = extra->m_facialHairID;

        if (!CGPlayer_C::ActivePlayerSeesNatural() || !this->IsOtherPlayerWithFlaggedModel()) {
            if (!extra->m_bakeName || !extra->m_bakeName[0]) {
                return 0;
            }

            data.flags |= 0x1;
            SStrPrintf(data.npcBakedTexturePath, sizeof(data.npcBakedTexturePath), "%s%s", "Textures\\BakedNpcTextures\\", extra->m_bakeName);
        }
    } else {
        auto appearance = player->Player();

        data.skinColorID = appearance->skinID;
        data.faceID = appearance->faceID;
        data.hairStyleID = appearance->hairStyleID;
        data.hairColorID = appearance->hairColorID;
        data.facialHairStyleID = appearance->facialHairStyleID;
    }

    data.model = this->m_model;

    if (this->GetGUID() == ClntObjMgrGetActivePlayer()) {
        data.flags |= 0x2;
    } else {
        data.flags &= ~0x2u;
    }

    data.model->m_refCount++;

    CCharacterComponent::ValidateComponentData(&data, player ? static_cast<COMPONENT_CONTEXT>(1) : static_cast<COMPONENT_CONTEXT>(2));
    this->m_characterComponent->Init(&data, nullptr);

    return 1;
}

// ref: FUN_00730100
int32_t CGUnit_C::BuildComponent() {
    if (!this->m_model || !this->m_model->IsLoaded(0, 0)) {
        return 0;
    }

    this->m_stateFlags &= ~0x400000u;

    if (this->m_characterComponent) {
        CCharacterComponent::FreeComponent(this->m_characterComponent);
        this->m_characterComponent = nullptr;
    }

    // A mirror image waits for the server to send whose appearance it wears.
    if (this->m_unit->flags2 & 0x10) {
        if ((this->m_stateFlags & 0x20000) == 0) {
            this->RequestMirrorImageData();
        }

        this->m_stateFlags |= 0x400000;

        return 0;
    }

    auto player = this->IsA(TYPE_PLAYER) ? static_cast<CGPlayer_C*>(this) : nullptr;

    if (this->IsCharacterDisplayPlayer()) {
        this->InitComponent(player, 1);
    } else if (this->m_displayInfoExtra) {
        if (!this->InitComponent(nullptr, 1)) {
            return 0;
        }
    } else if (player && this->m_modelData && (this->m_modelData->m_flags & 0x4)) {
        this->InitComponent(player, 0);
    }

    if (!player || !player->ApplyVisibleItems()) {
        if ((!CGPlayer_C::ActivePlayerSeesNatural() || !this->IsOtherPlayerWithFlaggedModel())
            && this->m_characterComponent && this->m_displayInfoExtra) {
            for (int32_t slot = 0; slot < 11; slot++) {
                int32_t displayID = this->m_displayInfoExtra->m_npcitemDisplay[slot];

                if (displayID) {
                    this->m_characterComponent->AddItem(static_cast<ITEM_SLOT>(slot), displayID, 0);
                }
            }
        }

        this->AttachHandItem(0);
        this->AttachHandItem(1);
        this->AttachHandItem(2);
    }

    this->ApplyItemVisualEffects();

    // FUN_00512b50
    PortraitRefresh(this->GetGUID(), 3);

    return 1;
}

// ref: FUN_00730f30
void CGUnit_C::GetHidden(uint32_t flags, int32_t* hidden, int32_t* hiddenOther) {
    this->CGObject_C::GetHidden(flags, hidden, hiddenOther);

    if ((this->m_stateFlags & 0x400000) && !this->BuildComponent()) {
        *hiddenOther = 1;
    } else if (*hidden == 0 && *hiddenOther == 0) {
        if (this->m_characterComponent && !this->m_characterComponent->RenderPrep(0)) {
            *hiddenOther = 1;
        } else if (this->m_model && this->m_model->m_flag4000) {
            // While its model is still settling, the unit stays hidden until one of its attached
            // items can draw.
            bool drawable = false;

            for (auto child = this->m_model->m_attachList; child; child = child->m_attachNext) {
                if (child->m_attachParent ? child->m_flag80 : child->m_flag8) {
                    drawable = true;
                    break;
                }
            }

            if (!drawable) {
                *hiddenOther = 1;
            }
        }

        // A rider whose seat keeps the camera (0x400) is not drawn.
        auto ride = this->m_vehiclePassenger;

        if (*hiddenOther == 0 && ride && ride->m_state != 0 && (ride->m_flags & 0x400)) {
            *hiddenOther = 1;
        }
    } else if (this->m_characterComponent) {
        // FUN_004efed0: the geosets keep up while the unit is hidden.
        // TODO(CharacterComponent): an unbaked component with work pending queues it
        // (FUN_006ded60) instead.
        if ((this->m_characterComponent->m_data.flags & 0x1) || (this->m_characterComponent->m_flags & 0x1) == 0) {
            if (this->m_characterComponent->m_flags & 0x4) {
                this->m_characterComponent->GeosRenderPrep();
            }
        }
    }

    // The rider is drawn with its mount.
    if (this->m_mountModel && this->m_model) {
        uint32_t draw = (*hidden == 0 && *hiddenOther == 0) ? 1 : 0;

        if (this->m_model->m_attachParent) {
            this->m_model->m_flag80 = draw;
            this->m_model->m_flag20000 = draw;
        } else {
            this->m_model->m_flag8 = draw;
            this->m_model->m_flag10000 = draw;
        }

        *hiddenOther = 0;
    }
}

// ------------------------------------------------------------------------------------------------
// Field handlers: the model
// ------------------------------------------------------------------------------------------------

// ref: FUN_00728e20
int32_t OnVirtualItemChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x672));

    if (unit) {
        unit->UpdateVirtualItem(static_cast<int32_t>((offset - 0xC8) >> 2), *static_cast<const int32_t*>(old));
    }

    return 1;
}

// ref: FUN_00716860
// TODO(PlayerName): the active player's name plate refreshes when the option is up (FUN_0052e9f0).
int32_t OnDisplayChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x398));

    if (!unit) {
        return 1;
    }

    if (unit->IsA(TYPE_PLAYER) && unit->m_formReverted && unit->m_localDisplayID == unit->Unit()->nativeDisplayID) {
        unit->m_localDisplayID = 0;
    }

    unit->UpdateModel(0);

    return 1;
}

// ref: FUN_00737aa0
int32_t OnSheathChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x466));

    if (!unit) {
        return 1;
    }

    int32_t was = *static_cast<const int32_t*>(old);

    if (unit->GetGUID() != ClntObjMgrGetActivePlayer() || unit->m_sheathState == was) {
        unit->SetSheathState(static_cast<uint8_t>(unit->Unit()->bytes2), 1, 1);
    }

    return 1;
}

// ------------------------------------------------------------------------------------------------
// The player's hands and equipment
// ------------------------------------------------------------------------------------------------

namespace {

const ItemStats_C* ItemStats(int32_t entry) {
    if (entry < 0) {
        entry = -entry;
    }

    return g_itemCache.GetRecord(DBCACHEKEY32(entry), nullptr, nullptr, nullptr, true);
}

// ref: FUN_00758e50
int32_t VisibleItemDisplay(const CVisibleItemData* item) {
    int32_t entry = item->entryID < 0 ? -item->entryID : item->entryID;

    if (auto stats = ItemStats(entry)) {
        return stats->displayInfoID;
    }

    auto rec = g_itemDB.GetRecord(entry);

    return rec ? rec->m_displayInfoID : 0;
}

} // namespace

// ref: FUN_006de840
bool CGPlayer_C::ActivePlayerSeesNatural() {
    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

    // PHASE4(Player_C): the byte at +0xf42 is not named on CGPlayer_C yet; nothing sets it.
    return player && false;
}

// ref: FUN_006e04d0
const UNIT_WEAPON_INFO* CGPlayer_C::GetWeaponInfo(int32_t hand, int32_t ignoreHidden) {
    const CVisibleItemData* item = nullptr;

    if (hand == 0) {
        if (!ignoreHidden && this->IsHandHidden(0)) {
            return nullptr;
        }

        item = &this->Player()->visibleItems[INVSLOT_MAINHAND];
    } else if (hand == 1) {
        if (!ignoreHidden && this->IsHandHidden(1)) {
            return nullptr;
        }

        item = &this->Player()->visibleItems[INVSLOT_OFFHAND];
    } else if (hand == 2) {
        if (!ignoreHidden && (this->Unit()->flags2 & 0x400)) {
            return nullptr;
        }

        item = &this->Player()->visibleItems[INVSLOT_RANGED];
    }

    if (!item || (!ignoreHidden && item->entryID <= 0) || item->entryID == 0) {
        return nullptr;
    }

    int32_t entry = item->entryID < 0 ? -item->entryID : item->entryID;
    auto stats = ItemStats(entry);
    auto rec = g_itemDB.GetRecord(entry);

    UNIT_WEAPON_INFO& info = this->m_handInfo[hand];
    info.m_class = static_cast<uint8_t>(stats ? stats->itemClass : 0);
    info.m_subclass = static_cast<uint8_t>(stats ? stats->subClass : 0);
    info.m_material = static_cast<uint8_t>(stats ? stats->material : (rec ? rec->m_material : 0));
    info.m_inventoryType = static_cast<uint8_t>(stats ? stats->inventoryType : (rec ? rec->m_inventoryType : 0));
    info.m_sheathType = static_cast<uint8_t>(stats ? stats->sheath : (rec ? rec->m_sheatheType : 0));
    info.m_soundOverride = static_cast<uint8_t>(stats ? stats->soundOverrideSubclass : (rec ? rec->m_soundOverrideSubclassID : 0));

    return &info;
}

// ref: FUN_006dc7e0
const ItemDisplayInfoRec* CGPlayer_C::GetWeaponDisplay(int32_t hand) {
    return g_itemDisplayInfoDB.GetRecord(this->GetWeaponDisplayID(hand));
}

// ref: FUN_006e05d0
int32_t CGPlayer_C::GetWeaponDisplayID(int32_t hand) {
    int32_t slot;

    if (hand == 0) {
        if (this->IsHandHidden(0)) {
            return 0;
        }

        slot = INVSLOT_MAINHAND;
    } else if (hand == 1) {
        if (this->IsHandHidden(1)) {
            return 0;
        }

        slot = INVSLOT_OFFHAND;
    } else if (hand == 2) {
        slot = INVSLOT_RANGED;
    } else {
        return 0;
    }

    return VisibleItemDisplay(&this->Player()->visibleItems[slot]);
}

// ref: FUN_006e08c0
// TODO(WeaponTrail): FUN_00720170 after a hand item; TODO(Player_C): the tabard's guild emblem
// (FUN_006db510 -> FUN_007eada0) is part of the tabard path, which here adds the plain item.
void CGPlayer_C::ApplyVisibleItem(const CVisibleItemData* item, int32_t slot) {
    if (!item || item->entryID == 0 || VisibleItemDisplay(item) <= 0) {
        return;
    }

    int32_t hand;

    if (slot == INVSLOT_MAINHAND) {
        hand = 0;
    } else if (slot == INVSLOT_OFFHAND) {
        hand = 1;
    } else if (slot == INVSLOT_RANGED) {
        hand = 2;
    } else {
        if (!this->m_characterComponent || !this->DrawsThroughComponent()) {
            return;
        }

        if (CGPlayer_C::ActivePlayerSeesNatural() && this->IsOtherPlayerWithFlaggedModel()) {
            return;
        }

        if (slot == INVSLOT_HEAD && (this->Player()->flags & 0x400)) {
            return;
        }

        if (slot == INVSLOT_BACK && (this->Player()->flags & 0x800)) {
            return;
        }

        this->m_characterComponent->AddItemBySlot(static_cast<INVENTORY_SLOTS>(slot), VisibleItemDisplay(item), 0);

        return;
    }

    this->AttachHandItem(hand);
}

// ref: FUN_006e09e0
int32_t CGPlayer_C::ApplyVisibleItems() {
    if (!this->DrawsThroughComponent()) {
        return 0;
    }

    for (int32_t slot = 0; slot < 19; slot++) {
        const CVisibleItemData* item = &this->Player()->visibleItems[slot];

        // An item not yet known (a negative entry) comes off until it is.
        if (item->entryID != 0 && item->entryID < 1) {
            if (slot == INVSLOT_MAINHAND) {
                if (this->m_sheathState == 1 || this->m_sheathState == 0) {
                    CCharacterComponent::RemoveHandItemLinks(this->m_model, INVSLOT_MAINHAND, 0, false);
                }
            } else if (slot == INVSLOT_OFFHAND) {
                if (this->m_sheathState == 1 || this->m_sheathState == 0) {
                    CCharacterComponent::RemoveHandItemLinks(this->m_model, INVSLOT_OFFHAND, 0, false);
                    CCharacterComponent::RemoveHandItemLinks(this->m_model, INVSLOT_OFFHAND, 0, true);
                }
            } else if (slot == INVSLOT_RANGED) {
                if (this->m_sheathState == 2 || this->m_sheathState == 0) {
                    CCharacterComponent::RemoveHandItemLinks(this->m_model, INVSLOT_RANGED, 0, false);
                }

                if (this->m_sheathState == 2) {
                    CCharacterComponent::RemoveHandItemLinks(this->m_model, INVSLOT_OFFHAND, 0, false);
                    CCharacterComponent::RemoveHandItemLinks(this->m_model, INVSLOT_MAINHAND, 0, false);
                }
            } else if (this->m_characterComponent) {
                this->m_characterComponent->RemoveItemBySlot(static_cast<INVENTORY_SLOTS>(slot));
            }
        }

        this->ApplyVisibleItem(item, slot);

        if (item->entryID == 0 && slot == INVSLOT_MAINHAND) {
            for (int32_t sheath = 0; sheath < 4; sheath++) {
                CCharacterComponent::RemoveHandItemLinks(this->m_model, INVSLOT_MAINHAND, sheath, false);
            }
        }
    }

    return 1;
}

// ---- movement (Unit_C.cpp) ------------------------------------------------------------------

namespace {

// The click-to-move state (DAT_00ca11f4), 13 when nothing is driving the player. Click-to-move
// itself is not ported, so it never leaves 13 -- CGCamera's copy of the same query reads the same.
int32_t s_clickToMoveState = 13;

// The movement prologue the start functions share: a click-to-move cancel, and the loot window's
// close (FUN_00523640, the loot UI's, not ported) while the player loots.
void MovementStartPrologue(CGUnit_C* unit) {
    if (unit->GetGUID() == ClntObjMgrGetActivePlayer() && s_clickToMoveState != 13) {
        unit->CancelClickToMove(0, 1);
    }
}

} // namespace

// ref: FUN_007272c0
// PARTIAL: with click-to-move unported its state is always 13, so this returns at once; the
// interaction the move was walking toward, and the CTM stop signals, are click-to-move's.
void CGUnit_C::CancelClickToMove(int32_t face, int32_t stop) {
    (void)face;
    (void)stop;

    if (this->GetGUID() != ClntObjMgrGetActivePlayer() || s_clickToMoveState == 13) {
        return;
    }

    s_clickToMoveState = 13;
}

// ref: FUN_00722010
// PARTIAL: click-to-move is not ported, so its state is always 13 and there is nothing to carry;
// the move-to target (DAT_00ca1264, state 4) and the facing target (DAT_00ca11ec, states 2 and 8)
// join it.
void CGUnit_C::CarryClickToMove(const C44Matrix& matrix, float facing) {
    (void)matrix;
    (void)facing;

    if (this->GetGUID() != ClntObjMgrGetActivePlayer() || s_clickToMoveState == 13) {
        return;
    }
}

// ref: FUN_0072ea50
// The mouse turns the unit: it stops a click-to-move first. PARTIAL: the reference then, for a
// player whose virtual 0x138 answers 3, calls FUN_006dcb40(0); that state belongs to an unported
// subsystem and is never 3 here.
void CGUnit_C::SetFacingTo(int32_t time, float facing) {
    if (this->IsActiveMover()) {
        this->CancelClickToMove(0, 1);
    }

    this->m_localMove.QueueSetFacing(time, facing);
}

// ref: FUN_0072ead0
// PARTIAL: the active player looting closes the loot window here (FUN_00523640); the loot frame
// is the loot port's.
void CGUnit_C::SetPitchTo(int32_t time, float pitch) {
    if (this->IsActiveMover()) {
        this->CancelClickToMove(0, 1);
    }

    this->m_localMove.QueueSetPitch(time, pitch);
}

// ref: FUN_0072d3f0
// A unit that turns at full speed (move-flags-2 0x8) turns to the facing over time instead.
// PARTIAL: as SetFacingTo, the virtual 0x138 state.
void CGUnit_C::TurnTo(int32_t time, float facing) {
    if (this->IsActiveMover()) {
        this->CancelClickToMove(0, 1);
    }

    this->m_localMove.QueueTurnTo(time, facing);
}

// ref: FUN_0072d470
// PARTIAL: as SetFacingTo, the virtual 0x138 state.
void CGUnit_C::PitchTo(int32_t time, float pitch) {
    if (this->IsActiveMover()) {
        this->CancelClickToMove(0, 1);
    }

    this->m_localMove.QueuePitchTo(time, pitch);
}

// ref: FUN_0072e5d0
void CGUnit_C::StartMove(int32_t time, int32_t forward) {
    MovementStartPrologue(this);
    this->m_localMove.QueueStartMove(time, forward);
}

// ref: FUN_0072e680
void CGUnit_C::StartStrafe(int32_t time, int32_t left) {
    MovementStartPrologue(this);
    this->m_localMove.QueueStartStrafe(time, left);
}

// ref: FUN_0072e730
void CGUnit_C::StartAscend(int32_t time, int32_t up) {
    MovementStartPrologue(this);
    this->m_localMove.QueueStartAscend(time, up);
}

// ref: FUN_0072e7e0
// A channelled spell that breaks on turning (attributes 0x10, interrupt flag 0x4000) is
// cancelled when click-to-move drives the turn.
void CGUnit_C::StartTurn(int32_t time, int32_t left) {
    MovementStartPrologue(this);
    this->m_localMove.QueueStartTurn(time, left);
}

// ref: FUN_0072e900
void CGUnit_C::StartPitch(int32_t time, int32_t up) {
    MovementStartPrologue(this);
    this->m_localMove.QueueStartPitch(time, up);
}

// ref: FUN_0072e9b0
void CGUnit_C::StopPitch(int32_t time) {
    MovementStartPrologue(this);
    this->m_localMove.QueueStopPitch(time);
}

// ref: FUN_0071ae10
void CGUnit_C::StopMove(int32_t time) {
    this->m_localMove.QueueStopMove(time);
}

// ref: FUN_0071ae20
void CGUnit_C::StopStrafe(int32_t time) {
    this->m_localMove.QueueStopStrafe(time);
}

// ref: FUN_0071ae30
void CGUnit_C::StopAscend(int32_t time) {
    this->m_localMove.QueueStopAscend(time);
}

// ref: FUN_0071ae40
void CGUnit_C::StopTurn(int32_t time) {
    this->m_localMove.QueueStopTurn(time);
}

// ref: FUN_00718860
// DAT_00ca1240, the time flying was last asked for, gates the automatic landing.
void CGUnit_C::SetFlying(int32_t time, int32_t fly) {
    this->m_localMove.QueueSetFlying(time, fly);
}

// ref: FUN_0072eb80
// A mounted unit standing still rears its mount (CMSG_MOUNT_SPECIAL_ANIM) instead of jumping,
// unless the mount's model jumps (creature model flag 0x400), it can fly, or it is in water deep
// enough to swim (FUN_00718a20).
void CGUnit_C::Jump(int32_t time) {
    MovementStartPrologue(this);

    auto& move = this->m_localMove;
    bool mountJumps = false;

    if (auto display = g_creatureDisplayInfoDB.GetRecord(this->m_mountDisplayID)) {
        if (auto model = g_creatureModelDataDB.GetRecord(display->m_modelID)) {
            mountJumps = (model->m_flags & 0x400) != 0;
        }
    }

    if (mountJumps || this->m_mountDisplayID < 1 || (this->m_stateFlags & 0x10000000)
        || (move.m_moveFlags & 0x1000000) || ((move.m_moveFlags & 0xf) && !move.IsHeldOffGround())) {
        move.QueueJump(time);
        return;
    }

    if (move.m_moveFlags & 0x30) {
        return;
    }

    float floor;

    if (this->GetFloorHeight(&floor) && move.m_collisionHeight * 0.5f < floor - this->GetPosition().z) {
        return;
    }

    if (this->GetGUID() == ClntObjMgrGetActivePlayer()) {
        this->Virtual098();
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_MOUNT_SPECIAL_ANIM));
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_0071ae80
int32_t CGUnit_C::IsOrientationUnchanged() {
    if (0.1f <= std::fabs(this->GetRawFacing() - this->m_sentFacing)) {
        return 0;
    }

    auto& move = this->m_localMove;

    if (((move.m_moveFlags & 0x2200000) || (move.m_moveFlags2 & 0x20))
        && 0.1f <= std::fabs(this->GetMovementPitch() - this->m_sentPitch)) {
        return 0;
    }

    return 1;
}

// ref: FUN_007219f0
int32_t CGUnit_C::DeferTurn(uint32_t time, int32_t opcode) {
    if (opcode != MSG_MOVE_STOP_TURN) {
        this->m_deferredTurnOpcode = opcode;
        this->m_deferredTurnTime = time;
        return 0;
    }

    if (this->m_deferredTurnOpcode && !(this->m_stateFlags & 0x4000000)) {
        // The turn never reached the server: one facing report replaces start and stop.
        if (this->IsOrientationUnchanged()) {
            this->m_deferredTurnOpcode = 0;
            return 0;
        }

        this->SendMovementStatus(time, MSG_MOVE_SET_FACING, 0.0f, 0, 0, 0xff);
        this->m_deferredTurnOpcode = 0;
        return 1;
    }

    this->SendMovementStatus(time, MSG_MOVE_STOP_TURN, 0.0f, 0, 0, 0xff);
    this->m_stateFlags &= 0xfbffffff;
    this->m_deferredTurnOpcode = 0;

    return 1;
}

// ref: FUN_00721ac0
int32_t CGUnit_C::DeferPitch(uint32_t time, int32_t opcode) {
    if (opcode != MSG_MOVE_STOP_PITCH) {
        this->m_deferredPitchOpcode = opcode;
        this->m_deferredPitchTime = time;
        return 0;
    }

    if (this->m_deferredPitchOpcode && !(this->m_stateFlags & 0x8000000)) {
        if (this->IsOrientationUnchanged()) {
            this->m_deferredPitchOpcode = 0;
            return 0;
        }

        this->SendMovementStatus(time, MSG_MOVE_SET_PITCH, 0.0f, 0, 0, 0xff);
        this->m_deferredPitchOpcode = 0;
        return 1;
    }

    this->SendMovementStatus(time, MSG_MOVE_STOP_PITCH, 0.0f, 0, 0, 0xff);
    this->m_stateFlags &= 0xf7ffffff;
    this->m_deferredPitchOpcode = 0;

    return 1;
}

// ref: FUN_00721c20
int32_t CGUnit_C::FlushDeferredMovement(uint32_t time) {
    int32_t sent = 0;

    if (this->m_deferredTurnOpcode) {
        sent = this->SendMovementStatus(time, this->m_deferredTurnOpcode, 0.0f, 0, 0, 0xff) != 0;
        this->m_deferredTurnOpcode = 0;
    }

    if (this->m_deferredPitchOpcode) {
        if (this->SendMovementStatus(time, this->m_deferredPitchOpcode, 0.0f, 0, 0, 0xff)) {
            sent = 1;
        }

        this->m_deferredPitchOpcode = 0;
    }

    return sent;
}

// ref: FUN_00721b90
void CGUnit_C::SendDeferredMovement(uint32_t time) {
    if (this->m_deferredTurnOpcode && 100 < time - this->m_deferredTurnTime) {
        this->SendMovementStatus(time, this->m_deferredTurnOpcode, 0.0f, 0, 0, 0xff);
        this->m_deferredTurnOpcode = 0;
    }

    if (this->m_deferredPitchOpcode && 100 < time - this->m_deferredPitchTime) {
        this->SendMovementStatus(time, this->m_deferredPitchOpcode, 0.0f, 0, 0, 0xff);
        this->m_deferredPitchOpcode = 0;
    }
}

// ref: FUN_0071ef80
// The guid, then for an acknowledgement its counter, then the status, then for an
// acknowledgement with a value the value. A remote unit's status is not the client's to report:
// only acknowledgements and the active mover's own state go out.
int32_t CGUnit_C::WriteMovementHeader(uint32_t time, int32_t opcode, CDataStore& msg, float value, uint32_t counter) {
    msg.Put(static_cast<uint32_t>(opcode));

    SmartGUID guid;
    guid = this->GetGUID();
    msg << guid;

    if (IsMovementAckOpcode(opcode)) {
        msg.Put(counter);
    }

    if (!IsMovementAckOrNotActiveMoverOpcode(opcode)) {
        // FUN_0071ef20: the active player's own report, which on a running spline only the
        // swim and fly state opcodes may make.
        if (this->GetGUID() != ClntObjMgrGetActivePlayer()) {
            return 0;
        }

        if (this->m_localMove.IsSplineActive() && !IsMovementStateOpcode(opcode)) {
            return 0;
        }
    }

    // FUN_007164b0: a teleport acknowledgement carries only the time.
    if (opcode == MSG_MOVE_TELEPORT_ACK) {
        msg.Put(time);
    } else {
        CMovementStatus status;
        status.moveFlags = this->m_localMove.m_moveFlags & 0x77fffdff;
        this->m_localMove.BuildStatus(opcode, time, status);
        msg << status;
    }

    if (IsMovementAckWithValueOpcode(opcode)) {
        msg.Put(value);
    }

    if (!(this->m_localMove.m_moveFlags & 0x200)) {
        if (this->m_localMove.m_moveFlags & 0x1000) {
            this->m_stateFlags |= 0x80;
        } else {
            this->m_stateFlags &= 0xffffff7f;
        }
    }

    return 1;
}

// ref: FUN_0071f0c0
int32_t CGUnit_C::SendMovementStatus(uint32_t time, int32_t opcode, float value, uint32_t counter,
                                     WOWGUID guid, uint8_t seat) {
    this->m_sentFacing = this->GetRawFacing();

    auto& move = this->m_localMove;

    if ((move.m_moveFlags & 0x2200000) || (move.m_moveFlags2 & 0x20)) {
        this->m_sentPitch = this->GetMovementPitch();
    }

    CDataStore msg;

    if (!this->WriteMovementHeader(time, opcode, msg, value, counter)) {
        return 0;
    }

    if (opcode == CMSG_CHANGE_SEATS_ON_CONTROLLED_VEHICLE) {
        SmartGUID seatGuid;
        seatGuid = guid;
        msg << seatGuid;
        msg.Put(seat);
    }

    if (move.m_moveFlags & 0x30) {
        this->m_stateFlags |= 0x4000000;
    }

    if (move.m_moveFlags & 0xc0) {
        this->m_stateFlags |= 0x8000000;
    }

    msg.Finalize();
    ClientServices::Send(&msg);

    this->m_localMove.ScheduleHeartbeat(static_cast<int32_t>(time));

    return 1;
}

// ref: FUN_00717d90
void CGUnit_C::SendTimeSkipped(uint32_t ms) {
    if (this->m_localMove.IsSplineActive() || (this->m_localMove.m_moveFlags & 0x200)) {
        return;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_MOVE_TIME_SKIPPED));

    SmartGUID guid;
    guid = this->GetGUID();
    msg << guid;
    msg.Put(ms);

    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_0071f210
void CGUnit_C::SendSplineDone(uint32_t time, uint32_t id) {
    CDataStore msg;

    this->WriteMovementHeader(time, CMSG_MOVE_SPLINE_DONE, msg, 0.0f, 0);
    msg.Put(id);

    msg.Finalize();
    ClientServices::Send(&msg);

    this->m_localMove.ScheduleHeartbeat(static_cast<int32_t>(time));

    if (auto input = InputControlGetActive()) {
        input->ClearFlagBits16To19();
        input->UpdatePlayerMovement(time, 1);
    }
}

// ref: FUN_007413f0
int32_t CGUnit_C::SendMovement(uint32_t time, int32_t opcode, uint8_t send, float value, uint32_t counter,
                               WOWGUID guid, uint8_t seat) {
    this->UpdateMovementEffects();

    bool standCheck;

    switch (opcode) {
        case MSG_MOVE_STOP:
        case MSG_MOVE_STOP_STRAFE:
        case MSG_MOVE_START_TURN_LEFT:
        case MSG_MOVE_START_TURN_RIGHT:
        case MSG_MOVE_STOP_TURN:
        case MSG_MOVE_SET_RUN_MODE:
        case MSG_MOVE_SET_WALK_MODE:
        case 0xd7:
        case 0xd8:
        case 0xd9:
        case MSG_MOVE_SET_FACING:
        case 0x45a:
        case 0x45b:
            standCheck = false;
            break;

        case MSG_MOVE_START_PITCH_UP:
        case MSG_MOVE_START_PITCH_DOWN:
        case MSG_MOVE_STOP_PITCH:
        case MSG_MOVE_SET_PITCH:
            // A pitch report is only meaningful swimming or flying.
            if (!(this->m_localMove.m_moveFlags & 0x2200000) && !(this->m_localMove.m_moveFlags2 & 0x20)) {
                return 0;
            }

            standCheck = false;
            break;

        default:
            standCheck = true;
            break;
    }

    if (standCheck && !IsMovementAckOrNotActiveMoverOpcode(opcode) && this->GetStandStateByte()
        && this->IsA(TYPE_PLAYER)) {
        // FUN_006dcb40, Player_C's stand-up: moving stands a sitting player up. The player's
        // stand-state request is the Player_C port's.
    }

    int32_t sent = 0;

    if (send) {
        switch (opcode) {
            case MSG_MOVE_START_TURN_LEFT:
            case MSG_MOVE_START_TURN_RIGHT:
            case MSG_MOVE_STOP_TURN:
                if (this->m_localMove.m_moveFlags2 & 0x8) {
                    sent = this->DeferTurn(time, opcode);
                    goto posted;
                }
                break;

            case MSG_MOVE_START_PITCH_UP:
            case MSG_MOVE_START_PITCH_DOWN:
            case MSG_MOVE_STOP_PITCH:
                if (this->m_localMove.m_moveFlags2 & 0x10) {
                    sent = this->DeferPitch(time, opcode);
                    goto posted;
                }
                break;

            case MSG_MOVE_SET_FACING:
            case MSG_MOVE_SET_PITCH:
                if (this->IsOrientationUnchanged()) {
                    goto posted;
                }
                break;

            default:
                break;
        }

        if (this->SendMovementStatus(time, opcode, value, counter, guid, seat)) {
            sent = 1;
        }

        if (this->FlushDeferredMovement(time)) {
            sent = 1;
        }
    }

posted:
    if (this->GetGUID() == ClntObjMgrGetActivePlayer()) {
        auto input = InputControlGetActive();

        switch (opcode) {
            case MSG_MOVE_TELEPORT_ACK:
                if (input) {
                    input->ClearFlagBits16To19();
                    input->UpdatePlayerMovement(time, 1);
                }

                this->m_teleportAckTime = static_cast<uint32_t>(OsGetAsyncTimeMs());

                // The camera's reset on the player after a teleport (FUN_005186a0) is the camera
                // port's.
                this->OnMovementPacketSent(opcode);

                return sent;

            case CMSG_MOVE_SET_FLY:
                if (input) {
                    input->UpdatePlayerMovement(time, 1);
                }

                this->OnMovementPacketSent(opcode);

                return sent;

            case CMSG_FORCE_MOVE_ROOT_ACK:
            case CMSG_FORCE_MOVE_UNROOT_ACK:
                if (input) {
                    input->UpdatePlayerMovement(time, 1);
                }

                [[fallthrough]];

            case MSG_MOVE_STOP:
            case MSG_MOVE_STOP_STRAFE:
                // FUN_0071b1e0: the interaction the player walked toward runs when it stops; it
                // is click-to-move's and the interaction port's.
                break;

            default:
                break;
        }
    }

    this->OnMovementPacketSent(opcode);

    return sent;
}

// ref: FUN_00721300
// The ObjectEffect states of how the unit moves: moving (0x25) or still (0x26), swimming (6) or
// flying (7), turning (0x2f) or not (0x30), and the pace -- fast (0x31..0x36), walking
// (0x37..0x3c) or backwards (0x3d..0x42) -- with its forward, backward and strafe variants. Each
// goes on as the flags call for it and off when they no longer do.
void CGUnit_C::UpdateMovementEffects() {
    auto effects = this->m_objectEffects;

    if (!effects) {
        return;
    }

    auto flags = [this]() { return this->m_move->m_moveFlags; };
    auto set = [effects](uint32_t state) { effects->SetState(state, 1, 0); };
    auto clear = [effects](uint32_t state) { effects->ClearState(state, 1); };

    set((flags() & 0xf) ? 0x25 : 0x26);

    if (flags() & 0x400000) {
        set(6);
    } else if (flags() & 0x800000) {
        set(7);
    }

    set((flags() & 0x30) ? 0x2f : 0x30);

    // The pace: backwards (0x2000000 is the reference's walk-backward test), fast, or walking.
    auto setPace = [&](uint32_t base) {
        set(base);

        if (flags() & 0x1) {
            set(base + 1);
        }

        if (flags() & 0x2) {
            set(base + 2);
        }

        if (flags() & 0xc) {
            set(base + 5);

            if (flags() & 0x4) {
                set(base + 3);
            }

            if (flags() & 0x8) {
                set(base + 4);
            }
        }
    };

    if (flags() & 0x2000000) {
        setPace(0x3d);
    } else if (this->IsMovingFasterThanWalkPace()) {
        setPace(0x31);
    } else if (this->IsMovingAtWalkPace()) {
        setPace(0x37);
    }

    // Forward and backward: off for every pace but the one moving.
    if ((flags() & 0x3) == 0) {
        for (uint32_t state : { 0x31u, 0x32u, 0x33u, 0x37u, 0x38u, 0x39u, 0x3du, 0x3eu, 0x3fu }) {
            clear(state);
        }
    } else if (flags() & 0x2000000) {
        for (uint32_t state : { 0x37u, 0x38u, 0x39u, 0x31u, 0x32u, 0x33u }) {
            clear(state);
        }

        clear((flags() & 0x1) ? 0x3f : 0x3e);
    } else if (this->IsMovingFasterThanWalkPace()) {
        for (uint32_t state : { 0x37u, 0x38u, 0x39u, 0x3du, 0x3eu, 0x3fu }) {
            clear(state);
        }

        clear((flags() & 0x1) ? 0x33 : 0x32);
    } else if (this->IsMovingAtWalkPace()) {
        for (uint32_t state : { 0x3du, 0x3eu, 0x3fu, 0x31u, 0x32u, 0x33u }) {
            clear(state);
        }

        clear((flags() & 0x1) ? 0x39 : 0x38);
    }

    // Strafing, the same way.
    if ((flags() & 0xc) == 0) {
        for (uint32_t state : { 0x34u, 0x35u, 0x36u, 0x3au, 0x3bu, 0x3cu, 0x40u, 0x41u }) {
            clear(state);
        }

        clear(0x42);
    } else if (flags() & 0x2000000) {
        for (uint32_t state : { 0x34u, 0x35u, 0x36u, 0x3au, 0x3bu, 0x3cu }) {
            clear(state);
        }

        clear((flags() & 0x4) ? 0x41 : 0x40);
    } else if (this->IsMovingFasterThanWalkPace()) {
        for (uint32_t state : { 0x3au, 0x3bu, 0x3cu, 0x40u, 0x41u, 0x42u }) {
            clear(state);
        }

        clear((flags() & 0x4) ? 0x35 : 0x34);
    } else if (this->IsMovingAtWalkPace()) {
        for (uint32_t state : { 0x34u, 0x35u, 0x36u, 0x40u, 0x41u, 0x42u }) {
            clear(state);
        }

        clear((flags() & 0x4) ? 0x3b : 0x3a);
    }

    clear((flags() & 0xf) ? 0x26 : 0x25);
    clear((flags() & 0x30) ? 0x30 : 0x2f);

    if (!(flags() & 0x400000)) {
        clear(6);
    }

    if (!(flags() & 0x800000)) {
        clear(7);
    }
}

// ref: FUN_0073ed10
// PARTIAL: the fly-mount takeoff and landing poses (0x345, 0x346) wait on the mount port; every
// opcode that only re-chooses the animation is handled.
void CGUnit_C::OnMovementPacketSent(int32_t opcode) {
    switch (opcode) {
        case MSG_MOVE_START_FORWARD:
        case MSG_MOVE_START_BACKWARD:
        case MSG_MOVE_START_STRAFE_LEFT:
        case MSG_MOVE_START_STRAFE_RIGHT:
        case MSG_MOVE_START_SWIM:
        case MSG_MOVE_START_ASCEND:
        case MSG_MOVE_START_DESCEND:
        case 0x341:
            // FUN_0072afe0: a move ends click-to-move's pose and an emote's attachment.
            this->m_animFlags &= 0xffffbfff;
            this->UpdateAnimation(0, 0xffffffff);
            return;

        case MSG_MOVE_STOP:
        case MSG_MOVE_STOP_STRAFE:
        case MSG_MOVE_START_TURN_LEFT:
        case MSG_MOVE_START_TURN_RIGHT:
        case MSG_MOVE_STOP_TURN:
        case MSG_MOVE_TELEPORT:
        case MSG_MOVE_TELEPORT_ACK:
        case MSG_MOVE_STOP_SWIM:
        case 0xd9:
        case 0xe9:
        case 0xec:
        case 0x31a:
        case 0x342:
        case MSG_MOVE_STOP_ASCEND:
        case 0x3ad:
        case CMSG_MOVE_GRAVITY_DISABLE_ACK:
        case CMSG_MOVE_GRAVITY_ENABLE_ACK:
        case 0x4d3:
        case 0x4d4:
            this->UpdateAnimation(0, 0xffffffff);
            return;

        case MSG_MOVE_JUMP:
            this->m_animFlags &= 0xffffbfff;
            this->PlayUnitSound(0xb, 1);

            if (this->m_vehicle && this->m_vehicle->ControlsPassengerAnimation()) {
                return;
            }

            this->SetAnimation(0x25, 0);
            return;

        case MSG_MOVE_SET_RUN_MODE:
        case MSG_MOVE_SET_WALK_MODE:
        case 0xe3:
        case 0xe5:
        case 0xe7:
        case CMSG_FORCE_WALK_SPEED_CHANGE_ACK:
        case CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK:
        case CMSG_FORCE_FLIGHT_SPEED_CHANGE_ACK:
        case CMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK:
            if (this->m_localMove.m_moveFlags & 0xc0100f) {
                this->UpdateAnimation(0, 0xffffffff);
            }
            return;

        case 0xf0:
            if (this->m_vehicle && this->m_vehicle->ControlsPassengerAnimation()) {
                return;
            }

            this->SetAnimation(0x28, 0);
            return;

        default:
            return;
    }
}

// ref: FUN_0073ad00
// The fall pose (40, Fall) for a living unit falling far, unless the vehicle it rides or the
// seat it sits in poses it.
void CGUnit_C::UpdateFallAnimation() {
    if (!(this->m_localMove.m_moveFlags & 0x2000) || this->m_unit->health < 1) {
        return;
    }

    if (this->m_vehicle && this->m_vehicle->ControlsPassengerAnimation()) {
        return;
    }

    this->SetAnimation(0x28, 0);
}

// ref: FUN_0073d2b0
// The landing pose: JumpEnd (0x27) standing still, JumpLandRun (0xbb) landing at a run, and a
// fresh choice otherwise (backward, walking, swimming, flying). A fall that was neither a jump nor
// a long fall re-chooses only when the move flags changed while it lasted. PARTIAL: a vehicle that
// poses its passenger, and the passenger states 4 and 5, are the vehicle port's.
void CGUnit_C::PlayLandingAnimation(uint32_t oldFlags, uint32_t jumping) {
    uint32_t animFlags = this->m_animFlags;
    this->m_animFlags = animFlags & 0xfeffffff;

    if (this->m_animFlags & 0x4000000) {
        this->SetAnimation(0x1d4, 0);
        return;
    }

    if (!jumping && !(oldFlags & 0x2000) && !(animFlags & 0x1000000)) {
        if (!((this->m_move->m_moveFlags ^ oldFlags) & 0x40f)) {
            return;
        }
    } else if (!(this->m_move->m_moveFlags & 0x2200000)) {
        uint32_t moveFlags = this->m_localMove.m_moveFlags;

        if (!(moveFlags & 0xf)) {
            this->PlayUnitSound(0xc, 1);
            this->SetAnimation(0x27, 0);
            return;
        }

        if (!(moveFlags & 0x2) && !(moveFlags & 0x100) && !this->IsMovingAtWalkPace()) {
            this->PlayUnitSound(0xc, 1);
            this->SetAnimation(0xbb, 0);
            return;
        }
    }

    this->UpdateAnimation(0, 0xffffffff);
}

// ref: FUN_0073d3d0
// The pose, then for a player-controlled unit that landed from far enough (70 yards always, 13
// unless it died of it) and was not swimming or slow-falling, the hard-landing sound (0xd) -- not
// for a player in state 0x4000 or one already dead. PARTIAL: the camera shake that goes with it
// (FUN_00755270 kind 2) is the camera-shake port's.
void CGUnit_C::OnLanded(uint32_t oldFlags, uint32_t jumping) {
    this->PlayLandingAnimation(oldFlags, jumping);

    if (!this->IsPlayerControlled() || this->m_localMove.IsUnsupportedSwimmingOrSlowFalling()) {
        return;
    }

    float height = this->m_localMove.GetLandingFallHeight();

    if (!(70.0f < height || (13.0f < height && !this->IsDead()))) {
        return;
    }

    if (!this->IsA(TYPE_PLAYER)
        || (!(static_cast<CGPlayer_C*>(this)->Player()->flags & 0x4000) && 0 < this->m_unit->health)) {
        this->PlayUnitSound(0xd, 1);
    }
}

// ref: FUN_0073d4a0
// MSG_MOVE_FALL_LAND, when the fall was the client's or long enough to matter.
int32_t CGUnit_C::AcknowledgeLanding(uint32_t time, uint32_t oldFlags, uint16_t oldFlags2, uint32_t jumping) {
    (void)oldFlags2;

    this->OnLanded(oldFlags, jumping);

    int32_t sent = 0;

    if ((this->m_stateFlags & 0x80) || 1.8493989706039429f < this->m_localMove.GetLandingFallHeight()) {
        if (this->SendMovementStatus(time, MSG_MOVE_FALL_LAND, 0.0f, 0, 0, 0xff)) {
            sent = 1;
        }
    }

    if (this->m_stateFlags & 0x100000) {
        this->m_stateFlags &= 0xffefffff;
        // FUN_006d2950(1): the player's deferred stand-state change.
    }

    return sent;
}

// ref: FUN_0073ab20
// After each movement step: the world object follows the unit, the terrain type under it is
// noted, and it may take off, land, or start or stop swimming.
// PARTIAL: a vehicle's own step (FUN_00758130) is the vehicle port's.
int32_t WorldObjectTerrainType(HWORLDOBJECT object, int32_t* terrainType);

void CGUnit_C::OnMovementStep(uint32_t time, int32_t fromSpline, int32_t a3) {
    (void)fromSpline;

    this->UpdateWorldObject(0);

    if (!this->m_worldObject || !WorldObjectTerrainType(this->m_worldObject, &this->m_terrainType)) {
        this->m_terrainType = -1;
    }

    this->UpdateFlying(static_cast<int32_t>(time));
    this->UpdateSwimming(static_cast<int32_t>(time), a3);
}

int32_t CGUnit_C::GetFloorHeight(float* height) {
    uint32_t fieldBC;
    uint32_t a4;

    return CWorld::GetObjectFloor(this->m_worldObject, &fieldBC, height, &a4);
}

// ref: FUN_00717c50
void CGUnit_C::SetActiveMover(WOWGUID guid) {
    CGUnit_C::s_activeMover = guid;

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_SET_ACTIVE_MOVER));
    msg.Put(guid);
    msg.Finalize();
    ClientServices::Send(&msg);

    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());

    if (auto input = InputControlGetActive()) {
        input->UpdatePlayerMovement(now, 1);
    }

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x1f68));

    if (unit && (unit->m_localMove.m_moveFlags & 0xc0100f)) {
        unit->m_localMove.ScheduleHeartbeat(static_cast<int32_t>(now));
    }

    if (unit) {
        // A passenger's mover takes the transport's clock.
        if (WOWGUID transport = unit->GetTransportGUID()) {
            auto object = static_cast<CGGameObject_C*>(ClntObjMgrObjectPtr(transport, TYPE_GAMEOBJECT, ".\\Unit_C.cpp", 0x1f6e));

            if (object && object->m_type) {
                MovementSetTransportTime(static_cast<uint32_t>(object->m_type->Virtual0A8()));
            }
        }
    }

    VehicleOnActiveMoverChanged(unit);
}

// ------------------------------------------------------------------------------------------------
// The field-change bodies the descriptor handlers of FUN_00741d00 call (Unit_C.cpp).
// ------------------------------------------------------------------------------------------------

// ref: FUN_0071c110
// The scale a display draws at: the race's native scale times the display's and the model's, and
// for a creature of a family (a pet) the family's scale grown with its level between the family's
// two levels -- taken when it is the larger, or always for a pet.
float CGUnit_C::GetModelScale(const CreatureDisplayInfoRec* display, const CreatureModelDataRec* modelData) {
    float scale = GetNativeRaceModelScale(display) * display->m_creatureModelScale * modelData->m_modelScale;

    if (!(0.0f < scale)) {
        scale = 1.0f;
    }

    auto family = this->m_creatureStats ? g_creatureFamilyDB.GetRecord(this->m_creatureStats->m_family) : nullptr;

    if (!family) {
        return scale;
    }

    int32_t span = family->m_maxScaleLevel - family->m_minScaleLevel;
    int32_t into = this->m_unit->level < family->m_minScaleLevel ? 0 : this->m_unit->level - family->m_minScaleLevel;

    if (span < into) {
        into = span;
    }

    float fraction = span != 0 ? static_cast<float>(into) / static_cast<float>(span) : 0.0f;
    float grown = (family->m_maxScale - family->m_minScale) * fraction + family->m_minScale;

    if (scale < grown || this->m_unit->petNumber != 0) {
        return grown;
    }

    return scale;
}

// ref: FUN_006cf350
// The scale the collision box is built at: the model's scale (also returned in `modelScale`)
// times the object's own, never less than one.
float CGUnit_C::GetCollisionScale(const CreatureDisplayInfoRec* display, const CreatureModelDataRec* modelData,
                                  float* modelScale) {
    float base = this->GetBaseScale();

    if (base < 1.0f) {
        base = 1.0f;
    }

    float scale = this->GetModelScale(display, modelData);

    if (modelScale) {
        *modelScale = scale;
    }

    return scale * base;
}

// ref: FUN_006d78c0
// A player's collision box from its native display's model data. A model without a box, or a unit
// scaled to nothing, keeps the one it has.
//
// PARTIAL: the active player growing into a space it does not fit (FUN_006e8ff0, a hull query
// through FUN_0075e500) is refused there; that query is the collision port's, so the box is
// always taken.
int32_t CGUnit_C::UpdatePlayerCollision(int32_t skipFit, int32_t force) {
    (void)skipFit;

    auto display = g_creatureDisplayInfoDB.GetRecord(this->m_unit->nativeDisplayID);
    auto modelData = display ? g_creatureModelDataDB.GetRecord(display->m_modelID) : nullptr;

    if (!modelData) {
        return 1;
    }

    float modelScale = 1.0f;
    float scale = this->GetCollisionScale(display, modelData, &modelScale);

    if (std::fabs(modelData->m_collisionWidth) < 9.53674e-07f || std::fabs(modelData->m_collisionHeight) < 9.53674e-07f) {
        return 1;
    }

    if (std::fabs(scale) < 9.53674e-07f) {
        return 1;
    }

    this->m_localMove.SetCollisionBox(modelData->m_collisionWidth, modelData->m_collisionHeight, scale, modelScale, force);

    return 1;
}

// ref: FUN_006d7720
// A mounted player's box: as tall as half the rider plus the mount's height at the mount's scale,
// or the rider's own height when that is taller, and as wide as it already is.
//
// PARTIAL: as UpdatePlayerCollision, the active player's fit test (FUN_0075e480) is the collision
// port's; the box is always taken.
int32_t CGUnit_C::UpdateMountedCollision(float mountHeight, int32_t skipFit) {
    auto display = g_creatureDisplayInfoDB.GetRecord(this->m_unit->nativeDisplayID);
    auto modelData = display ? g_creatureModelDataDB.GetRecord(display->m_modelID) : nullptr;

    if (!modelData) {
        return 1;
    }

    float modelScale = 1.0f;
    float scale = this->GetCollisionScale(display, modelData, &modelScale);
    float height = modelData->m_collisionHeight * 0.5f + this->m_mountScale * mountHeight;

    if (height < modelData->m_collisionHeight) {
        height = modelData->m_collisionHeight;
    }

    float radius = this->m_localMove.m_collisionRadius;

    this->m_localMove.SetCollisionBox(radius + radius, height, scale, modelScale, skipFit);

    return 1;
}

// ref: FUN_00725f50
// The unit's collision box: a player's from its model (UpdatePlayerCollision), a creature whose
// template asks for one (type flag 0x20000) from its native display's model data, and everything
// else the default two-thirds-by-two-yards box.
int32_t CGUnit_C::UpdateCollisionBox(int32_t skipFit, int32_t force) {
    if (this->IsA(TYPE_PLAYER)) {
        return this->UpdatePlayerCollision(skipFit, force);
    }

    if (this->m_creatureStats && (this->m_creatureStats->m_typeFlags & 0x20000)) {
        auto display = g_creatureDisplayInfoDB.GetRecord(this->m_unit->nativeDisplayID);

        if (display) {
            auto modelData = g_creatureModelDataDB.GetRecord(display->m_modelID);
            float width = 0.6666666865348816f;
            float height = 2.027777671813965f;
            float scale = 1.0f;
            float displayScale = 1.0f;

            if (modelData) {
                float base = this->GetBaseScale();

                if (base < 1.0f) {
                    base = 1.0f;
                }

                displayScale = this->GetDisplayScale(this->m_unit->nativeDisplayID);
                scale = displayScale * base;
                width = modelData->m_collisionWidth;
                height = modelData->m_collisionHeight;
            }

            this->m_localMove.SetCollisionBox(width, height, scale, displayScale, 0);

            return 1;
        }
    }

    this->m_localMove.SetCollisionBox(0.6666666865348816f, 2.027777671813965f, 1.0f, 1.0f, 0);

    return 1;
}

// ref: FUN_0072a560
// OBJECT_FIELD_SCALE_X moved: the collision box is rebuilt (on the mount's model data for a
// mounted player), and a box that will not fit growing cancels the growth aura that grew it.
// Then the quest marker, the shadow and the effect attachments follow the new scale.
void CGUnit_C::OnScaleChanged(float oldScale) {
    bool growing = oldScale <= this->GetBaseScale();
    int32_t fits = 1;
    auto mountDisplay = this->m_unit->mountDisplayID;

    if (mountDisplay < 1 || !this->IsA(TYPE_PLAYER)) {
        fits = this->UpdateCollisionBox(growing ? 1 : 0, 0);
    } else {
        auto display = g_creatureDisplayInfoDB.GetRecord(mountDisplay);
        auto modelData = display ? g_creatureModelDataDB.GetRecord(display->m_modelID) : nullptr;

        fits = modelData ? this->UpdateMountedCollision(modelData->m_mountHeight, growing ? 1 : 0) : 0;
    }

    if (!fits) {
        CDataStore msg;
        msg.Put(static_cast<uint32_t>(CMSG_CANCEL_GROWTH_AURA));
        msg.Finalize();
        ClientServices::Send(&msg);
    }

    this->ScaleQuestMarker();
    this->UpdateShadowRadius();
    this->UpdateEffectAttachments();
}

// ref: FUN_0072e3a0
// UNIT_FIELD_LEVEL moved: the level-up flash (hard-coded effect 5, kept running), and the display
// scale again, which a pet's grows with its level.
//
// PARTIAL: a player's own refresh (FUN_006d66e0) and the name plate's level (FUN_0098ef10) are the
// Player_C and name plate ports'.
void CGUnit_C::OnLevelChanged() {
    void* mem = SMemAlloc(sizeof(CEffect), __FILE__, __LINE__, 0);
    auto effect = mem ? new (mem) CEffect() : nullptr;

    if (effect) {
        effect->InitializeHardcoded(5, this->GetGUID(), &CGObject_C::KitEffectOneShot);
        effect->m_flags |= CEffect::EFFECT_NO_EXPIRE;
        effect->Release();
    }

    if (!this->IsA(TYPE_PLAYER)) {
        this->UpdateDisplayScale(0);
    }
}

// ref: FUN_0071f8f0
// The unit's reaction to the player may have changed (faction, flags, charm, PvP): its name plate
// is redrawn, the unit frames hear UNIT_FACTION, and it re-evaluates what it shows the player.
//
// PARTIAL: the name plate colour (FUN_0098ee30), the active player's own re-check of every visible
// unit (FUN_006dc5a0 and the FUN_00718e50 / FUN_00718f00 walks) are the name plate and Player_C
// ports'.
void CGUnit_C::UpdateReaction(int32_t refreshOthers) {
    (void)refreshOthers;

    if (this->m_nameDesc) {
        // FUN_007e50f0: the name plate is marked for a redraw.
        PlayerNameInvalidateReaction(this->m_nameDesc);
    }

    ScriptEventsSignalUnitEvent(this->GetGUID(), 0x31);

    if (this->GetGUID() != ClntObjMgrGetActivePlayer()) {
        this->OnReenable();
    }
}

// ref: FUN_00728f70
// The active player's own UNIT_FIELD_FLAGS changes: a stun (0x40000) re-runs its movement, and
// losing or gaining 0x100000 (fleeing / confused) stops click-to-move.
//
// PARTIAL: the action bar refreshes (FUN_0053cf10), the pacified state's spell bar (FUN_00524600)
// and the "you are no longer under control" prompt (FUN_00508090) are the UI ports'.
void CGUnit_C::OnActivePlayerFlagsChanged(uint32_t changed) {
    if (changed & 0x40000) {
        if (auto input = InputControlGetActive()) {
            input->UpdatePlayerMovement(static_cast<uint32_t>(OsGetAsyncTimeMs()), 1);
        }
    }

    if ((changed & 0x100000) && (this->m_unit->flags & 0x100000)) {
        this->CancelClickToMove(0, 1);
    }
}

// ref: FUN_0073c330
// UNIT_FIELD_FLAGS changed from `old`. Another unit's change to 0x400 (in combat) re-chooses its
// animation; a change to 0x200000 (disarmed) puts the melee weapons back in or out of the hands;
// 0x180 (PvP attackable) re-checks the reaction; 0x800 (the player's pet attacking) tells the pet
// bar.
//
// PARTIAL: the tooltip's level line (FUN_00512ab0), the UNIT_FLAGS script event and the
// party-member refresh (FUN_00524350) for 0x2000000 are the UI ports'; the missiles a disarm
// drops (FUN_00703730) are the missile port's.
void CGUnit_C::OnFlagsChanged(uint32_t old) {
    uint32_t now = this->m_unit->flags;
    uint32_t changed = now ^ old;

    if (this->GetGUID() == ClntObjMgrGetActivePlayer()) {
        this->OnActivePlayerFlagsChanged(changed);
    } else if (0 < this->m_unit->health && (changed & 0x400)) {
        this->UpdateAnimation(0, 0xFFFFFFFF);
    }

    if (changed & 0x200000) {
        auto main = this->GetWeaponInfo(0, 1);

        if (main && main->m_class == 2) {
            auto again = this->GetWeaponInfo(0, 1);

            if ((this->m_unit->flags & 0x200000) && again && again->m_class == 2
                && (this->m_sheathState == 1 || this->m_sheathState == 0)) {
                CCharacterComponent::RemoveHandItemLinks(this->m_model, static_cast<INVENTORY_SLOTS>(0xF),
                                                         this->m_sheathState == 0 ? main->m_sheathType : 0, false);
            } else {
                this->AttachHandItem(0);
            }
        }

        auto off = this->GetWeaponInfo(1, 1);

        if (off && off->m_class == 2) {
            bool unlink = false;

            if (this->m_unit->flags & 0x200000) {
                this->GetWeaponInfo(0, 1);
                auto again = this->GetWeaponInfo(1, 1);

                unlink = !this->IsHandHidden(0) && again && again->m_class == 2;
            }

            if (!unlink && (this->m_unit->flags2 & 0x80)) {
                unlink = true;
            }

            if (unlink && this->m_sheathState == 1) {
                CCharacterComponent::RemoveHandItemLinks(this->m_model, static_cast<INVENTORY_SLOTS>(0x10), 0, false);
            } else {
                this->AttachHandItem(1);
            }
        }
    }

    if (changed & 0x180) {
        this->UpdateReaction(0);
    }

    if (changed & 0x800) {
        const WOWGUID& owner = this->m_unit->charmedBy != 0 ? this->m_unit->charmedBy : this->m_unit->summonedBy;

        if (owner == ClntObjMgrGetActivePlayer()) {
            CGPetInfo::SetAttacking(static_cast<int32_t>((old >> 11) & 1));
        }
    }

    if (changed & 0x8000000) {
        ScriptEventsQueueUnitEvent(this->GetGUID(), 0x8E);
    }
}

// ref: FUN_0073c5d0
// UNIT_FIELD_FLAGS_2 changed from `old`: feign death (0x1) entered or left, the regeneration
// lock (0x10) re-runs the model, disarming the off hand (0x80) and the ranged weapon (0x400)
// move those weapons, and the active player's 0x40 re-runs its movement.
//
// PARTIAL: the action bar refreshes (FUN_0053cf10, FUN_004fb530's script event 0xf0 is sent) are
// the UI's.
void CGUnit_C::OnFlags2Changed(uint32_t old) {
    uint32_t now = this->m_unit->flags2;
    uint32_t changed = now ^ old;
    bool isActive = this->GetGUID() == ClntObjMgrGetActivePlayer();

    if (changed & 0x1) {
        if (!(now & 0x1)) {
            this->UpdateMountSound();
            this->UpdateAnimation(0, 0xFFFFFFFF);
        } else {
            if (isActive) {
                if (auto input = InputControlGetActive()) {
                    input->UpdatePlayerMovement(static_cast<uint32_t>(OsGetAsyncTimeMs()), 0);
                }
            }

            this->UpdateMountSound();
            this->PlayDeathPose(1);
        }
    }

    if ((changed & 0x8) && isActive) {
        FrameScript_SignalEvent(0xF0, nullptr);
    }

    if ((changed & 0x10) && !(now & 0x10)) {
        this->UpdateModel(1);
    }

    if (changed & 0x80) {
        auto off = this->GetWeaponInfo(1, 1);

        if (off) {
            bool unlink = false;

            if (this->m_unit->flags & 0x200000) {
                this->GetWeaponInfo(0, 1);
                auto again = this->GetWeaponInfo(1, 1);

                unlink = !this->IsHandHidden(0) && again && again->m_class == 2;
            }

            if (!unlink && (this->m_unit->flags2 & 0x80)) {
                unlink = true;
            }

            if (unlink && (this->m_sheathState == 1 || this->m_sheathState == 0)) {
                CCharacterComponent::RemoveHandItemLinks(this->m_model, static_cast<INVENTORY_SLOTS>(0x10),
                                                         this->m_sheathState == 0 ? off->m_sheathType : 0,
                                                         off->m_inventoryType == 0xE);
            } else {
                this->AttachHandItem(1);
            }
        }
    }

    if (changed & 0x400) {
        if (!(this->m_unit->flags2 & 0x400) || this->m_sheathState != 2) {
            this->AttachHandItem(2);
        } else if (auto ranged = this->GetWeaponInfo(2, 1)) {
            if (this->m_model) {
                bool thrown = ranged->m_inventoryType == 0x1A || ranged->m_inventoryType == 0x19;

                this->m_model->DetachAllChildrenById(thrown ? 1 : 2);
            }
        }
    }

    if ((changed & 0x40) && isActive) {
        if (auto input = InputControlGetActive()) {
            input->UpdatePlayerMovement(static_cast<uint32_t>(OsGetAsyncTimeMs()), 1);
        }
    }
}

// ref: FUN_0073c830
// UNIT_FIELD_BYTES_1 byte 2 changed from `old`: stealth (0x2) re-chooses the animation, and it or
// the 0x1 bit redraws the name.
//
// PARTIAL: the name plate's text (FUN_0098e580) and the action bar refresh (FUN_0053cf10) are the
// name plate and UI ports'.
void CGUnit_C::OnVisibilityChanged(uint8_t old) {
    uint8_t changed = static_cast<uint8_t>(this->m_unit->bytes1 >> 16) ^ old;

    if (changed & 0x2) {
        this->UpdateAnimation(0, 0xFFFFFFFF);

        if (this->m_nameDesc) {
            PlayerNameInvalidate(this->m_nameDesc);
        }

        if (this->GetGUID() == ClntObjMgrGetActivePlayer()) {
            FrameScript_SignalEvent(0x20A, nullptr);
        }
    }

    if ((changed & 0x1) && this->m_nameDesc) {
        PlayerNameInvalidate(this->m_nameDesc);
    }
}

// ref: FUN_00724d90
// UNIT_FIELD_BYTES_2 byte 1 (the PvP flags) changed: the reaction is re-checked.
//
// PARTIAL: the active player's first PvP flag shows tutorial 0x1f (FUN_00530840), the tutorial
// frame's port.
void CGUnit_C::OnPvpFlagsChanged(uint8_t old) {
    if (old != static_cast<uint8_t>(this->m_unit->bytes2 >> 8)) {
        this->UpdateReaction(0);
    }
}

// ref: FUN_0071a0b0
// UNIT_NPC_FLAGS changed from `old`: losing gossip (0x1), quest giver (0x2), trainer (0x10),
// vendor-type (0x2000), banker, auctioneer or stable bits closes the window the player has open
// with this NPC, and a change to the quest-giver bits re-evaluates what the NPC shows.
//
// PARTIAL: the windows themselves (gossip FUN_0058a550, quests FUN_0058ca70, merchants
// FUN_00590ba0, trainers FUN_005940e0, bank FUN_0057b8d0, stable FUN_005a4270, auction
// FUN_0056da60) are the UI ports'; none of them opens, so none is open to close.
void CGUnit_C::OnNpcFlagsChanged(uint32_t old) {
    uint32_t changed = this->m_unit->npcFlags ^ old;

    if (changed & 0x2) {
        this->OnReenable();
    }

    if (changed & 0x2000) {
        this->OnReenable();
    }
}

// ref: FUN_00717c20
// The loot sparkle ends.
void CGUnit_C::HideLootSparkle() {
    for (auto effect = this->m_effects; effect; effect = effect->m_linkNext) {
        if (effect->m_flags & CEffect::EFFECT_LOOT_ART) {
            effect->Finish();
            return;
        }
    }
}

// ref: FUN_0073af80
// The pose a dead or feigning unit lies in: 0x1d2, or 0x83 (floating dead) in water or hovering
// over liquid within half its height and a tenth. A unit already in a death animation keeps it
// unless `force`.
void CGUnit_C::PlayDeathPose(int32_t force) {
    if (!force && IsDeathAnimation(static_cast<int32_t>(this->GetCurrentAnimationId()))) {
        return;
    }

    uint32_t animID = 0x1D2;

    if (this->m_move->m_moveFlags & 0x200000) {
        animID = 0x83;
    } else if (this->m_move->m_spline && this->m_move->IsUnsupportedOrHovering()) {
        // FUN_00716110: where the spline ends, or the unit's position without one.
        C3Vector at = this->m_move->m_spline ? this->m_move->m_spline->vector1F8 : this->m_move->m_position;
        float height = this->GetModelHeight();
        float liquid = 0.0f;
        uint32_t type = 0;

        // FUN_0077f360
        if (CMap::GetLiquidAt(at, &type, &liquid, nullptr, 0)
            && !(liquid - height * 0.5f + 0.1f < at.z)) {
            animID = 0x83;
        }
    }

    this->SetAnimation(animID, 0);
}

// ref: FUN_0073d530
// The unit lives again: the alive bit (state 0x1) and the death time clear, its sequence runs at
// normal speed again, the mount sound and the animation come back, and the name redraws.
//
// PARTIAL: the active player's own revival -- re-checking every visible unit (FUN_006dc5a0), the
// spirit-healer UI (FUN_00520e40) and PLAYER_ALIVE (0x101) -- is the Player_C port's, as is the
// name plate's text (FUN_0098ee30).
void CGUnit_C::OnResurrect(int32_t silent) {
    this->m_stateFlags &= 0xFFFFFFFE;
    this->UpdateMountSound();
    this->m_deathTime = 0;
    this->SetBoneSequenceSpeed(this->m_model, 0xFFFFFFFF, 1.0f, 0);
    this->UpdateAnimation(0, 0xFFFFFFFF);

    if (this->m_nameDesc) {
        PlayerNameInvalidateReaction(this->m_nameDesc);
    }

    if (this->GetGUID() == ClntObjMgrGetActivePlayer() && !silent) {
        FrameScript_SignalEvent(0x101, nullptr);
    }
}

// ref: FUN_00729220
// The unit died: it stops attacking, its model stops lifting, it lies down (unless a seat holds
// its rider's pose), its quest marker goes, its effects and sounds stop, and click-to-move ends.
//
// PARTIAL, each the subsystem's own: the player's death UI (FUN_006dc0f0), the corpse spell
// visuals (FUN_008063e0), the target and party refreshes (FUN_0071ee70, InPartyOrRaid), the
// missiles it fired (FUN_00703730), the name plate (FUN_0098ee30), the interaction window it had
// open (FUN_00518d50).
void CGUnit_C::OnDeath() {
    this->m_attackTarget = 0;

    if (this->m_model && this->m_model->IsLoaded(0, 0)) {
        this->m_model->m_animationHeldTime = 0;
    }

    if (!this->m_vehiclePassenger || this->m_vehiclePassenger->m_state != 3 || (this->m_unit->flags2 & 0x20000)) {
        UnitOnLeftVehicle(this);
    }

    if (!this->m_creatureStats || !(this->m_creatureStats->m_typeFlags & 0x80)) {
        this->ReleaseQuestMarker();
    }

    if (this->m_nameDesc) {
        PlayerNameInvalidateReaction(this->m_nameDesc);
    }

    this->StopAllEffects(0);
    // The two sounds the reference stops here (+0x8f4 / +0x934) are ones frozen does not keep.
    this->CancelClickToMove(0, 1);
}

// ref: FUN_00740570
// UNIT_DYNAMIC_FLAGS changed from `old`: becoming unlootable ends the loot sparkle; a change to
// 0x20 (dead, as a corpse shows it) is a death or a resurrection; a dead unit that is lootable
// shows the sparkle.
//
// PARTIAL: the first lootable corpse's tutorial (6, FUN_00530840) is the tutorial frame's.
void CGUnit_C::OnDynamicFlagsUpdate(uint32_t old) {
    uint32_t now = this->m_unit->dynamicFlags;

    if ((old & 0x1) && !(now & 0x1)) {
        this->HideLootSparkle();
    }

    if ((now ^ old) & 0x20) {
        ScriptEventsSignalUnitEvent(this->GetGUID(), 0x12);
        ScriptEventsSignalUnitEvent(this->GetGUID(), static_cast<int32_t>(this->m_unit->bytes0 >> 24) + 0x13);

        if (now & 0x20) {
            this->OnDeath();
        } else {
            this->OnResurrect(1);
        }
    }

    if (this->m_unit->health < 1 && (this->m_unit->dynamicFlags & 0x1)) {
        if (this->m_deathTime != 0) {
            this->StopAllEffects(0);
        }

        this->ShowLootSparkle();
    }

    if ((now ^ old) & 0x4) {
        ScriptEventsSignalUnitEvent(this->GetGUID(), 0x31);
    }
}

// ref: FUN_007221d0
// A unit channelling at a fishing bobber (a game object of type 17) draws the line to it from
// its rod (the model's attachment chain with an attachment of id 1).
void CGUnit_C::UpdateFishingLine() {
    auto bobber = static_cast<CGGameObject_C*>(
        ClntObjMgrObjectPtr(this->m_unit->channelObject, TYPE_GAMEOBJECT, __FILE__, __LINE__));

    if (!bobber || this->m_unit->channelSpell == 0 || bobber->GameObject()->type != 17 || !this->m_model) {
        return;
    }

    auto rod = this->m_model->m_attachList;

    while (rod && rod->m_attachId != 1) {
        rod = rod->m_attachNext;
    }

    if (!rod || this->m_fishingLine) {
        return;
    }

    void* mem = SMemAlloc(sizeof(CEffect), __FILE__, __LINE__, 0);
    this->m_fishingLine = mem ? new (mem) CEffect() : nullptr;

    if (this->m_fishingLine) {
        this->m_fishingLine->AttachFishingLine(this, this->m_unit->channelObject);
    }
}

// ref: FUN_0073a520
// UNIT_FIELD_CHANNEL_OBJECT / _SPELL changed (the old object and spell given): the old channel's
// kits stop on what it was aimed at, the fishing line goes, and a new channel at a bobber draws
// the rod out and the line.
//
// PARTIAL: a channel with attribute 0x4000 aimed at a unit by the active player re-targets the
// cast bar (FUN_0072b4a0), the spell-cast port's.
void CGUnit_C::OnChannelObjectChanged(WOWGUID oldObject, int32_t oldSpell) {
    auto target = static_cast<CGObject_C*>(ClntObjMgrObjectPtr(oldObject, TYPE_OBJECT, __FILE__, __LINE__));

    if (target && oldSpell != 0) {
        target->StopKitEffects(oldSpell, 2, 0, 0, 0);
        target->StopKitEffects(oldSpell, 1, 0, 0, 0);
        target->StopKitEffects(oldSpell, 5, 0, 0, 0);
        target->StopKitEffects(oldSpell, 6, 0, 0, 0);
    }

    if (this->m_fishingLine) {
        this->m_fishingLine->Release();
        this->m_fishingLine = nullptr;
    }

    auto bobber = static_cast<CGGameObject_C*>(
        ClntObjMgrObjectPtr(this->m_unit->channelObject, TYPE_GAMEOBJECT, __FILE__, __LINE__));

    if (bobber && this->m_unit->channelSpell != 0 && bobber->GameObject()->type == 17) {
        if (this->m_sheathState == 1) {
            this->UpdateFishingLine();
        } else {
            this->SetSheathState(1, 1, 0);
        }
    }
}

// ref: FUN_0073eb50
// UNIT_FIELD_CHANNEL_SPELL changed from `oldSpell`. The old spell's transparency, colour and
// effects come off (and its entries in the aura-visual list); then a channel that ended stops its
// effects and the fishing line and re-chooses the animation, and one that began shows its visual.
void CGUnit_C::OnChannelSpellChanged(int32_t oldSpell) {
    if (oldSpell != 0) {
        this->RemoveAlphaEffects(oldSpell);

        float alpha = this->GetFadeInAlpha();
        uint32_t duration = 1000;

        if (this->m_alphaEffects) {
            alpha *= this->m_alphaEffects->m_alpha;

            if (this->m_alphaEffects->m_param != 0) {
                duration = this->m_alphaEffects->m_param;
            }
        }

        this->SetAlpha(alpha, duration);
        this->RemoveColorEffects(oldSpell);
        this->StopEffects(oldSpell, 1);

        if (this->m_fishingLine) {
            this->m_fishingLine->Release();
            this->m_fishingLine = nullptr;
        }

        for (uint32_t i = 0; i < this->m_auraVisualSpells.Count(); i++) {
            if (this->m_auraVisualSpells[i] == oldSpell) {
                this->m_auraVisualSpells[i] = 0;
            }
        }
    }

    if (this->m_unit->channelSpell == 0) {
        auto spell = g_spellDB.GetRecord(oldSpell);

        if (spell && (spell->m_attributes & 0x4000)) {
            // FUN_00721fc0(2): the active player's click-to-move is in state 1.
            if (this->GetGUID() == ClntObjMgrGetActivePlayer() && s_clickToMoveState != 13
                && (2u & (1u << (s_clickToMoveState & 0x1F)))) {
                this->CancelClickToMove(0, 1);
            }
        }

        this->StopEffects(oldSpell, 1);

        if (this->m_fishingLine) {
            this->m_fishingLine->Release();
            this->m_fishingLine = nullptr;
        }

        this->UpdateAnimation(0, 0xFFFFFFFF);

        return;
    }

    this->UpdateChannelVisual();
}

// ------------------------------------------------------------------------------------------------
// The unit's per-frame update (Unit_C.cpp FUN_0073dab0 and FUN_00734390, with their helpers).
// ------------------------------------------------------------------------------------------------

// ref: FUN_007156c0
// An angle brought into [-pi, pi].
float WrapAngleSigned(float angle) {
    if (CMath::PI < angle) {
        return std::fmod(angle + CMath::PI, CMath::TWO_PI) - CMath::PI;
    }

    if (angle < -CMath::PI) {
        return std::fmod(angle - CMath::PI, CMath::TWO_PI) + CMath::PI;
    }

    return angle;
}

// ref: FUN_00719660
// The lower body's facing chases `target`: first kept within a quarter turn of the model's
// facing, then sprung toward the target at rate 20, then blended with the target by the
// blend weight (1 springs fully, 0 snaps).
void CGUnit_C::TurnLowerBodyToward(float target) {
    float facing = this->m_smoothFacing;

    if (facing + CMath::PI < this->m_lowerBodyFacing) {
        facing += CMath::TWO_PI;
    } else if (this->m_lowerBodyFacing < facing - CMath::PI) {
        facing -= CMath::TWO_PI;
    }

    const float limit = 1.5704764127731323f;

    if (this->m_lowerBodyFacing + limit < facing) {
        this->m_lowerBodyFacing = facing - limit;
    } else if (facing < this->m_lowerBodyFacing - limit) {
        this->m_lowerBodyFacing = facing + limit;
    }

    this->m_lowerBodyFacing = WrapAngleSigned(this->m_lowerBodyFacing);

    float to = target;

    if (to + CMath::PI < this->m_lowerBodyFacing) {
        to += CMath::TWO_PI;
    } else if (this->m_lowerBodyFacing < to - CMath::PI) {
        to -= CMath::TWO_PI;
    }

    float dt = CGWorldFrame::s_currentWorldFrame ? CGWorldFrame::s_currentWorldFrame->m_elapsed : 0.0f;
    float x = dt * 20.0f;
    float decay = 1.0f / (x * x * 0.48f + x * x * x * 0.235f + x + 1.0f);
    float away = this->m_lowerBodyFacing - to;
    float pull = (away * 20.0f + this->m_lowerBodyFacingStep) * dt;

    this->m_lowerBodyFacing = (away + pull) * decay + to;
    this->m_lowerBodyFacingStep = (this->m_lowerBodyFacingStep - pull * 20.0f) * decay;
    this->m_lowerBodyFacing = this->m_lowerBodyFacing * this->m_lowerBodyBlend + (1.0f - this->m_lowerBodyBlend) * to;
}

// ref: FUN_00720db0
// The model's diffuse colour: a running colour effect's (CEffect +0xec), white otherwise.
//
// PARTIAL: the timed colour flash (+0xb10..+0xb1c, FUN_0071a9a0, set by FUN_0071a940's caller) is
// not ported; nothing that sets it is, so it never runs.
void CGUnit_C::UpdateModelColor() {
    uint32_t color = 0xFFFFFFFF;

    if (this->m_colorEffects) {
        color = this->m_colorEffects->m_color;
    }

    auto model = this->GetObjectModel();

    if (!model) {
        return;
    }

    const float scale = 0.003921568859368563f;

    model->m_baseDiffuse = {
        static_cast<float>((color >> 16) & 0xFF) * scale,
        static_cast<float>((color >> 8) & 0xFF) * scale,
        static_cast<float>(color & 0xFF) * scale,
    };
}

// ref: FUN_0071bd20
// A vehicle's wheels (bones 0x1b..0x22 the model carries, m_boneMask) turn with the ground speed
// and the turn: each spins about its axle by the distance its pivot travels over its radius.
// A unit on a spline that does not drive its wheels, or rooted, holds them where they are.
void CGUnit_C::UpdateWheels() {
    if (!this->m_boneMask) {
        return;
    }

    auto model = this->GetObjectModel();

    if (!model) {
        return;
    }

    uint32_t now = CWorld::GetCurTimeMs();
    int32_t elapsed = static_cast<int32_t>(now - this->m_wheelTime);

    if (elapsed < 2) {
        elapsed = 1;
    } else if (499 < elapsed) {
        elapsed = 500;
    }

    this->m_wheelTime = now;

    float seconds = static_cast<float>(elapsed) * 0.001f;
    auto move = this->m_move;
    uint32_t moveFlags = move->m_moveFlags;
    float roll;
    float turn;

    if (moveFlags & 0xD) {
        roll = move->m_currentSpeed * seconds;
    } else if (moveFlags & 0x2) {
        roll = -(move->m_currentSpeed * seconds);
    } else {
        roll = 0.0f;
    }

    if (moveFlags & 0x10) {
        turn = (-move->m_turnRate - this->m_smoothFacingStep) * seconds;
    } else if (moveFlags & 0x20) {
        turn = (move->m_turnRate - this->m_smoothFacingStep) * seconds;
    } else {
        turn = -(seconds * this->m_smoothFacingStep);
    }

    auto spline = move->m_spline;
    bool hold = !(!spline || (spline->flags & 0x400) || !(spline->flags & 0x2000)) || (moveFlags & 0x2200000);

    for (int32_t i = 0; i < 8; i++) {
        if (!(this->m_boneMask & (1u << i))) {
            continue;
        }

        uint32_t bone = static_cast<uint32_t>(i) + 0x1B;

        if (!hold) {
            C3Vector pivot;
            model->GetBonePivot(pivot, bone);

            if (0.001f < std::fabs(pivot.z)) {
                auto& m = model->matrixB4;
                float scale = std::sqrt(m.a2 * m.a2 + m.a0 * m.a0 + m.a1 * m.a1);
                float y = pivot.y * scale;
                float z = scale * pivot.z;
                float angle = (y * turn + roll) / z + this->m_boneValues[i];

                while (angle < -CMath::PI) {
                    angle += CMath::TWO_PI;
                }

                while (CMath::PI < angle) {
                    angle -= CMath::TWO_PI;
                }

                this->m_boneValues[i] = angle;
            }
        }

        model->SetBoneFlags(bone, 0x80, 0x80);
        model->SetBoneMatrix(bone, RotationAroundAxis4(this->m_boneValues[i], { 0.0f, 1.0f, 0.0f }, true));
    }
}

// ref: FUN_0073dab0
// The unit's own per-frame step, after the object's: the model's colour, the lower body twisting
// toward where the unit moves (bones 4 and 6 carry the difference, the upper body a quarter turn
// at most and the head the rest), the turn-in-place shuffle, the wheels, and the regeneration
// lock's model flag.
//
// PARTIAL, each the subsystem's own port: the rider's seat timer (FUN_007490c0), the target's name plate blink (FUN_00729740) and the name
// plate step (FUN_007e6390), the scripted alpha timer (+0xb28), the missiles in flight
// (+0x9f0, FUN_00703730), the queued emotes (FUN_0073adc0, written by FUN_0071a260), a vehicle
// seat's aim (seat flag 0x200, vtable 0x14c) and the delayed
// kits (FUN_00728140, +0xf4c).
void CGUnit_C::UpdateForFrame(CGWorldFrame* frame) {
    uint32_t now = CWorld::GetCurTimeMs();

    if (static_cast<int32_t>(now - this->m_breathCheckTime) >= 0) {
        this->UpdateBreathState(now);
    }

    this->UpdateModelColor();

    this->CGObject_C::UpdateForFrame(frame);

    this->UpdateRipples(0);

    float elapsed = CGWorldFrame::s_currentWorldFrame ? CGWorldFrame::s_currentWorldFrame->m_elapsed : 0.0f;
    uint32_t moveFlags = this->m_move->m_moveFlags;
    // A vehicle whose row has flag 0x200 or 0x1000 (VehicleRec +4) holds the legs straight; the
    // rows are the vehicle port's, so none does.
    bool seatHolds = false;

    if (this->m_unit->health < 1
        || (this->m_vehiclePassenger && (this->m_vehiclePassenger->m_state == 2 || this->m_vehiclePassenger->m_state == 5))
        || (moveFlags & 0x2200000) || seatHolds || !(this->m_animFlags & 0x180)) {
        this->m_lowerBodyFacing = this->m_smoothFacing;
        this->m_lowerBodyFacingStep = 0.0f;
        this->m_lowerBodyBlend = 0.0f;
    } else if (!(moveFlags & 0xC)) {
        if (!(moveFlags & 0x1003)) {
            if (this->m_lowerBodyBlend < 1.0f) {
                this->m_lowerBodyBlend = elapsed * 2.5f + this->m_lowerBodyBlend;
            }
        } else if (this->m_lowerBodyBlend <= 0.0f) {
            this->m_lowerBodyFacing = this->m_smoothFacing;
            this->m_lowerBodyFacingStep = 0.0f;
        } else {
            this->m_lowerBodyBlend -= elapsed * 2.5f;

            if (1.0f < this->m_lowerBodyBlend) {
                this->m_lowerBodyBlend = 1.0f;
            }

            this->TurnLowerBodyToward(this->m_smoothFacing);
        }
    } else {
        // Strafing: the legs turn a quarter turn toward the side (an eighth when also moving
        // forward or back), the other way when strafing left and right together or neither.
        float offset = (moveFlags & 0x3) ? 0.7853981852531433f : 1.5707963705062866f;
        uint32_t sides = moveFlags & 0x6;

        if (sides == 6 || sides == 0) {
            offset = -offset;
        }

        this->m_lowerBodyBlend = 1.0f;

        float target = WrapAngleSigned(this->m_smoothFacing - this->m_lowerBodyFacing + offset);
        this->TurnLowerBodyToward(WrapAngleSigned(target + this->m_lowerBodyFacing));
    }

    float delta = WrapAngleSigned(this->m_smoothFacing - this->m_lowerBodyFacing);
    float magnitude = std::fabs(delta);
    float turnLeft = 0.0f;
    auto model = this->m_model;

    if (0.001f <= magnitude) {
        if (1.5707963705062866f < magnitude) {
            turnLeft = std::copysign(magnitude - 1.5707963705062866f, delta);
        }

        // Standing and not steered: the legs catch up at eight times the turn rate since the unit
        // last stood still.
        if (!(this->m_move->m_moveFlags & 0xC) && !(this->m_animFlags & 0x1)) {
            uint32_t still = static_cast<uint32_t>(OsGetAsyncTimeMs()) - this->m_turnStillTime;
            float step = static_cast<float>(still) * 0.001f * this->m_localMove.m_turnRate * 8.0f;

            if (magnitude < step) {
                step = magnitude;
            }

            turnLeft += std::copysign(step, delta);
        }

        this->m_lowerBodyFacing = WrapAngleSigned(turnLeft + this->m_lowerBodyFacing);

        float remaining = std::fabs(WrapAngleSigned(this->m_smoothFacing - this->m_lowerBodyFacing));

        if (remaining < 1e-05f) {
            if (model) {
                model->SetBoneFlags(4, 0, 0x80);
                model->SetBoneFlags(6, 0, 0x80);
            }
        } else if (model) {
            if (!this->m_mountModel && (this->m_animFlags & 0x80)) {
                float spine = remaining;

                if (this->GetGUID() != ClntObjMgrGetActivePlayer() || s_clickToMoveState == 13) {
                    spine *= 0.5f;
                }

                if (0.7853981852531433f < spine) {
                    spine = 0.7853981852531433f;
                }

                model->SetBoneFlags(4, 0x80, 0x80);
                model->SetBoneMatrix(4, RotationAroundAxis4(std::copysign(spine, delta), { 0.0f, 0.0f, 1.0f }, true));

                remaining -= spine;
            }

            if (this->m_animFlags & 0x100) {
                float head = remaining < 0.7853981852531433f ? remaining : 0.7853981852531433f;

                model->SetBoneFlags(6, 0x80, 0x80);
                model->SetBoneMatrix(6, RotationAroundAxis4(std::copysign(head, delta), { 0.0f, 0.0f, 1.0f }, true));
            }
        }
    } else if (model) {
        model->SetBoneFlags(4, 0, 0x80);
        model->SetBoneFlags(6, 0, 0x80);
    }

    this->UpdateWheels();

    this->m_animFlags &= 0xFFFFE7FF;

    bool looting = this->GetGUID() == ClntObjMgrGetActivePlayer()
        && static_cast<CGPlayer_C*>(this)->m_lootTarget != 0;

    if (!(this->m_move->m_moveFlags & 0x2E0100F) && !looting && this->GetStandStateByte() == 0) {
        if (1e-05f < turnLeft) {
            this->m_animFlags |= 0x800;
        } else if (turnLeft < -1e-05f) {
            this->m_animFlags |= 0x1000;
        }

        auto objectModel = this->GetObjectModel();
        int32_t current = objectModel ? static_cast<int32_t>(objectModel->GetBoneUint90(0xFFFFFFFF)) : -1;
        int32_t want = -1;

        if ((this->m_move->m_moveFlags & 0x10) || (this->m_animFlags & 0x800)) {
            want = 0xB;
        } else if ((this->m_move->m_moveFlags & 0x20) || (this->m_animFlags & 0x1000)) {
            want = 0xC;
        } else if (current == 0xB || current == 0xC) {
            want = 0;
        }

        if (want != -1 && current != want && this->CanPlayTurnAnimation()) {
            this->UpdateAnimation(0, 0xFFFFFFFF);
        }
    }

    if (this->m_model) {
        this->m_model->m_flag4000 = (this->m_unit->flags2 >> 1) & 1;
    }
}

// ref: FUN_00734390
// The world frame's step for a visible unit: the height a model flagged for it re-measures, and
// with its model loaded, the world object's light and the unit's effects.
//
// PARTIAL, each the subsystem's own port: the holiday costume display swap (FUN_0072e270, driven
// by Player_C's DAT_00c9eab0), the missile the unit holds and releases (+0x9ec, FUN_0072df00 /
// FUN_0072e240, and state 0x800), the name plate removal off screen (FUN_00725840), the blood
// spurts of a badly hurt unit (FUN_00720220, UnitBlood.dbc), click-to-move steering
// (FUN_007317a0) and the seat's per-frame hook (passenger flag 0x10, vtable 0x14).
void CGUnit_C::UpdateVisible(uint32_t time) {
    (void)time;

    if (this->m_flag21) {
        auto model = this->GetObjectModel();

        if (model && model->IsLoaded(0, 0)) {
            this->UpdateHeight();
        }
    }

    this->m_stateFlags &= 0x7FFFFFFF;

    if (!this->m_worldObject) {
        return;
    }

    auto model = this->GetObjectModel();

    if (!model || !model->IsLoaded(0, 0)) {
        return;
    }

    CWorld::UpdateObjectLighting(this->m_worldObject);

    for (auto effect = this->m_effects; effect;) {
        auto next = effect->m_linkNext;
        effect->Update();
        effect = next;
    }
}

// Which ripple kinds are a moving unit's wake (0x00adac00): standing 0, turning 1, moving 2, a
// splash 3. A wake trails the unit's heading at a rate set by its speed; the rest ring outward at
// a random angle every 400..450 ms.
static const uint8_t s_rippleIsWake[4] = { 0, 0, 1, 0 };

// ref: FUN_0071cba0
// A unit standing in shallow water rings it: while the water at its feet is deeper than nothing
// and shallower than its own collision height (twice it, or a yard at least), a ripple spreads
// from where it stands, sized by its scale and faded toward the depth limit. `splash` is an
// animation event's ripple (0xc9 is the splash kind) and restarts the timer.
void CGUnit_C::UpdateRipples(int32_t splash) {
    uint32_t fieldBC = 0;
    float surface = 0.0f;
    uint32_t unused = 0;

    if (!CWorld::GetObjectFloor(this->m_worldObject, &fieldBC, &surface, &unused)) {
        return;
    }

    uint32_t inLiquid = 0;
    uint32_t liquidBit9 = 0;

    if (!CWorld::GetObjectLiquidFlags(this->m_worldObject, &inLiquid, &liquidBit9) || !inLiquid) {
        return;
    }

    int32_t kind = 0;
    uint32_t moveFlags = this->m_move->m_moveFlags;

    if (splash) {
        this->m_nextRippleTime = 0;

        if (splash == 0xC9) {
            kind = 3;
        }
    } else if (moveFlags & 0xF) {
        kind = 2;
    } else if (moveFlags & 0x30) {
        kind = 1;
    }

    float depthLimit = this->m_localMove.m_collisionHeight + this->m_localMove.m_collisionHeight;

    if (!(1.0f < depthLimit)) {
        depthLimit = 1.0f;
    }

    float halfLimit = depthLimit * 0.5f;
    float depth = surface - this->GetPosition().z;

    if (!(depth < depthLimit)) {
        return;
    }

    uint32_t now = CWorld::GetCurTimeMs();

    if (this->m_nextRippleTime != 0 && static_cast<int32_t>(now - this->m_nextRippleTime) < 0) {
        return;
    }

    C3Vector at = this->GetPosition();
    at.z = surface;

    float spacing = 1.0f;
    float speed = this->m_move->m_currentSpeed;

    if (s_rippleIsWake[kind] && 0.0001f < speed) {
        spacing = 2.5f / (20.0f <= speed ? 20.0f : speed);
    }

    float rate = 1.0f / spacing;
    float facing = this->GetFacing();
    auto& seed = ObjectRandomSeed();

    float size = this->GetBaseScale() * 0.3333333432674408f;
    size = ((M2ParticleRand01(seed) * 0.2f - 0.1f) + 1.0f) * size;

    if (size < 0.3333333432674408f) {
        size = 0.3333333432674408f;
    } else if (1.6666666269302368f <= size) {
        size = 1.6666666269302368f;
    }

    float alpha = (M2ParticleRand01(seed) * 0.1f - 0.05f) + 0.65f;
    float grow = (M2ParticleRand01(seed) * 1.5f - 0.75f + 3.75f) * 12.0f * 0.02777777798473835f * rate;
    float life = 0.1666666716337204f;
    float fade = 1.0f;

    // Deeper than half the limit, the ripple shrinks and fades toward the limit.
    if (halfLimit < depth) {
        fade = 0.5f + ((depthLimit - depth) / (depthLimit - halfLimit)) * 0.5f;
        life = fade * 0.1666666716337204f;
        alpha = fade * alpha;
        size = fade * size;
    }

    if (kind == 0) {
        life *= 0.8f;
        grow *= 0.25f;
        size *= 0.6f;
    }

    float angle;

    if (!s_rippleIsWake[kind]) {
        angle = M2ParticleRand01(seed) * 6.2831854820251465f;
    } else {
        angle = facing;

        if (moveFlags & 0x4) {
            angle += (moveFlags & 0x1) ? 0.7853981852531433f : (moveFlags & 0x2) ? 2.356194496154785f : 1.5707963705062866f;
        } else if (moveFlags & 0x8) {
            angle -= (moveFlags & 0x1) ? 0.7853981852531433f : (moveFlags & 0x2) ? 2.356194496154785f : 1.5707963705062866f;
        } else if (moveFlags & 0x2) {
            angle += 3.1415927410125732f;
        }
    }

    CWorld::AddRipple(at, angle, size, alpha, life, grow, s_rippleIsWake[kind],
                      this->GetGUID() == ClntObjMgrGetActivePlayer() ? 1 : 0);

    if (s_rippleIsWake[kind]) {
        this->m_nextRippleTime = now - static_cast<uint32_t>(static_cast<int64_t>(fade * spacing * 0.25f * -1000.0f));
        return;
    }

    uint32_t jitter = static_cast<uint32_t>((static_cast<uint64_t>(CRandom::uint32(seed)) * 50) >> 32);
    this->m_nextRippleTime = jitter + 400 + now;
}

namespace {

// When the active player last took off (0x00ca1240), so it does not land at once.
int32_t s_flyStartTime = 0;

} // namespace

// ------------------------------------------------------------------------------------------------
// The movement step's world side: the ground a unit stands on, swimming, flying (Unit_C.cpp).
// ------------------------------------------------------------------------------------------------

// ref: FUN_004f53d0
// Whether a unit with these UNIT_FIELD_FLAGS swims in deep water: not one flagged 0x4000, always
// one flagged 0x8, 0x10 or 0x800, otherwise as flag 0x8000 says.
bool UnitFlagsAllowSwimming(uint32_t flags) {
    if (flags & 0x4000) {
        return false;
    }

    if ((flags & 0x8) || (flags & 0x800) || (flags & 0x10)) {
        return true;
    }

    return (flags >> 15) & 1;
}

// ref: FUN_00714b60
// How much clear air is under the unit: a segment from a yard above to a yard below its feet,
// and the part of it above whatever it hits (two yards when nothing is hit).
float CGUnit_C::GetClearanceBelow() {
    C3Vector at = this->GetPosition();
    C3Vector top = { at.x, at.y, at.z + 1.0f };
    C3Vector bottom = { at.x, at.y, at.z - 1.0f };
    C3Vector hit = bottom;
    float t = 1.0f;

    WorldQuerySegment(top, bottom, &hit, &t, 0x100111, nullptr);

    return top.z - hit.z;
}

// ref: FUN_00746720
// The splash a unit makes entering or leaving water: its race's splash sound (+0x8f0) where it
// is, or at the listener for the active player with the listener at the character.
void CGUnit_C::PlaySplashSound(const C3Vector& position) {
    static CVar* listenerAtCharacter = CVar::Lookup("Sound_ListenerAtCharacter");

    bool isActive = ClntObjMgrGetActivePlayer() == this->GetGUID();
    bool atListener = isActive && listenerAtCharacter && listenerAtCharacter->GetInt() != 0;

    SoundKitProperties properties;
    properties.ResetToDefaults();

    if (isActive) {
        properties.int20 = 0x6E;

        if (atListener) {
            properties.m_fadeOutTime = 0.6499999761581421f;
        }
    }

    properties.m_type = 8;

    SI2::PlaySoundKit(this->m_splashSoundID, atListener ? nullptr : &position, nullptr, &properties, 0, nullptr, 1, 0);
}

// ref: FUN_00721210
// Start swimming: the active player through its move queue (and the swimming tutorial, the
// tutorial frame's), another unit by the server's word unless a spline is driving it.
void CGUnit_C::StartSwimming(int32_t time, int32_t fromSpline) {
    if (this->GetGUID() == ClntObjMgrGetActivePlayer()) {
        this->m_localMove.QueueStartSwim(time);
        return;
    }

    auto spline = this->m_localMove.m_spline;

    if (!fromSpline && (!spline || (spline->flags & 0x400))) {
        this->m_localMove.QueueRemoteStartSwim(time);
        return;
    }

    this->m_localMove.SplineStartSwim();
}

// ref: FUN_00721290
void CGUnit_C::StopSwimming(int32_t time, int32_t fromSpline) {
    if (this->GetGUID() == ClntObjMgrGetActivePlayer()) {
        this->m_localMove.QueueStopSwim(time);
        return;
    }

    auto spline = this->m_localMove.m_spline;

    if (!fromSpline && (!spline || (spline->flags & 0x400))) {
        this->m_localMove.QueueRemoteStopSwim(time);
        return;
    }

    this->m_localMove.SplineStopSwim();
}

// ref: FUN_00730d10
// After each step: in water deeper than three quarters of its height a unit that may swim starts
// swimming, and stops when the water drops a few centimetres below that (or it leaves the
// liquid); crossing two fifths of its height splashes. A swimmer that may leave the water near
// the surface (0x400000) jumps out when the active mover.
//
// PARTIAL: the action bar refresh when the active player's in-water state changes (FUN_0053cf10)
// is the UI port's.
void CGUnit_C::UpdateSwimming(int32_t time, int32_t fromSpline) {
    auto& move = this->m_localMove;

    if (move.m_moveFlags2 & 0x4) {
        return;
    }

    uint32_t fieldBC = 0;
    uint32_t unused = 0;
    float surface = 0.0f;
    float depth = 0.0f;
    int32_t inLiquid = CWorld::GetObjectFloor(this->m_worldObject, &fieldBC, &surface, &unused);

    if (inLiquid) {
        depth = surface - this->GetPosition().z;
    }

    bool canSwim = UnitFlagsAllowSwimming(this->m_unit->flags) && this->GetTransportGUID() == 0;
    float swimDepth = move.m_collisionHeight * 0.75f;
    float leaveDepth = swimDepth - 0.02777777798473835f;

    if (!(move.m_moveFlags & 0x200000)) {
        if (swimDepth < depth && canSwim && !move.IsRising()) {
            this->StartSwimming(time, fromSpline);
        }

        float splashDepth = move.m_collisionHeight * 0.4f;

        if ((splashDepth < depth) != (splashDepth < this->m_lastLiquidDepth)) {
            this->PlaySplashSound(this->GetPosition());
            this->UpdateRipples(0xC9);
        }

        this->m_lastLiquidDepth = depth;
    } else {
        if ((move.m_moveFlags & 0x400000) && depth - swimDepth <= 0.3333333432674408f && this->IsActiveMover()) {
            this->Jump(time);
        }

        if (!(move.m_moveFlags & 0x2000000) && (!inLiquid || depth < leaveDepth || !canSwim)) {
            this->StopSwimming(time, fromSpline);
        }
    }

    if ((move.m_moveFlags & 0x200000) || (swimDepth < depth && canSwim)) {
        this->m_stateFlags |= 0x200000;
    } else {
        this->m_stateFlags &= 0xFFDFFFFF;
    }
}

// ref: FUN_0073a890
// A unit that may fly (0x1000000): falling a yard and a half clear of the ground while the active
// mover takes off; flying, the active player settles back on its feet when it comes within a yard
// and a half of the ground two seconds after taking off.
//
// PARTIAL: the flying unit's landing pose and sound near the ground (every two seconds, through
// the low-detail terrain height FUN_0077f8b0 / FUN_007ad700, the map port's) is not ported.
void CGUnit_C::UpdateFlying(int32_t time) {
    auto& move = this->m_localMove;

    if (!(move.m_moveFlags & 0x1000000)) {
        return;
    }

    if (!(move.m_moveFlags & 0x2000000)) {
        if (!move.IsRising() && (move.m_moveFlags & 0x1000) && this->IsActiveMover()
            && 1.5f <= this->GetClearanceBelow()) {
            move.QueueSetFlying(time, 1);
            s_flyStartTime = time;
        }

        return;
    }

    if (2000u < static_cast<uint32_t>(time - s_flyStartTime) && this->GetGUID() == ClntObjMgrGetActivePlayer()
        && this->GetClearanceBelow() < 1.5f) {
        move.QueueSetFlying(time, 0);
    }
}

// ref: FUN_0077f260
// The terrain type under an entity (its +0xb8), -1 when there is none.
int32_t WorldObjectTerrainType(HWORLDOBJECT object, int32_t* terrainType) {
    auto entity = reinterpret_cast<CMapEntity*>(object);

    if (entity && entity->m_groundType != -1) {
        *terrainType = entity->m_groundType;
        return 1;
    }

    return 0;
}

// ------------------------------------------------------------------------------------------------
// The unit's animation events: footsteps, breath, sounds and the weapon swaps the model's tracks
// call for (Unit_C.cpp FUN_00734a40 / FUN_00732650, UnitSound_C.cpp).
// ------------------------------------------------------------------------------------------------

namespace {

// The event ids are four characters, '$' first, read as a little-endian dword ("$FL0" is
// 0x304c4624). The footstep family is "$" + one of B F R S W + L or R + a digit 0..3.
bool IsFootstepEvent(uint32_t eventId, int32_t* left) {
    if ((eventId & 0xFF) != '$') {
        return false;
    }

    char kind = static_cast<char>((eventId >> 8) & 0xFF);
    char side = static_cast<char>((eventId >> 16) & 0xFF);
    char digit = static_cast<char>((eventId >> 24) & 0xFF);

    if (kind != 'B' && kind != 'F' && kind != 'R' && kind != 'S' && kind != 'W') {
        return false;
    }

    if ((side != 'L' && side != 'R') || digit < '0' || '3' < digit) {
        return false;
    }

    *left = side == 'L' ? 1 : 0;

    return true;
}

uint32_t EventId(const char* tag) {
    return static_cast<uint32_t>(static_cast<uint8_t>(tag[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(tag[1])) << 8)
        | (static_cast<uint32_t>(static_cast<uint8_t>(tag[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(tag[3])) << 24);
}

} // namespace

// ref: FUN_004cf100
// The footstep sound a creature footstep set makes on a terrain type, dry or in water.
//
// DIVERGED: the reference indexes a hash of FootstepTerrainLookup rows built once by FUN_004cf990
// (keyed by the creature footstep id, an array per terrain sound); frozen reads the rows
// directly, which answers the same question.
int32_t FootstepSoundFor(int32_t footstepID, int32_t terrainType, int32_t wet) {
    auto terrain = g_terrainTypeDB.GetRecord(terrainType);

    if (!terrain) {
        return 0;
    }

    for (int32_t i = 0; i < g_footstepTerrainLookupDB.GetNumRecords(); i++) {
        auto row = g_footstepTerrainLookupDB.GetRecordByIndex(i);

        if (row && row->m_creatureFootstepID == footstepID && row->m_terrainSoundID == terrain->m_soundID) {
            return wet ? row->m_soundIDSplash : row->m_soundID;
        }
    }

    return 0;
}

// ref: FUN_004cf170
// A footstep's sound: the terrain's (or the default terrain's) for the creature's footstep set.
// The active player's own play as the listener's when the listener is at the character.
void PlayFootstepSound(int32_t footstepID, const C3Vector* position, int32_t terrainType, int32_t wet, int32_t large,
                       CGUnit_C* unit) {
    if (!unit) {
        return;
    }

    static CVar* listenerAtCharacter = CVar::Lookup("Sound_ListenerAtCharacter");

    if (!listenerAtCharacter) {
        return;
    }

    int32_t soundID = FootstepSoundFor(footstepID, terrainType, wet);

    if (!soundID) {
        soundID = FootstepSoundFor(footstepID, 0, wet);
    }

    if (!soundID) {
        return;
    }

    SoundKitProperties properties;
    properties.ResetToDefaults();

    bool activePlayer = unit->IsA(TYPE_PLAYER) && unit->GetGUID() == ClntObjMgrGetActivePlayer();

    if (activePlayer && listenerAtCharacter->GetInt() != 0) {
        properties.float2c = large ? 50.0f : 20.0f;
        properties.m_fadeOutTime = 0.6499999761581421f;
        properties.m_type = 0x11;
        properties.int20 = 0x73;
        SI2::PlaySoundKit(soundID, nullptr, nullptr, &properties, 0, nullptr, 1, 0);
        return;
    }

    if (activePlayer) {
        properties.m_type = 0x11;
        properties.int20 = 0x73;
        SI2::PlaySoundKit(soundID, position, nullptr, &properties, 0, nullptr, 1, 0);
        return;
    }

    properties.m_type = 0xD;
    properties.int30 = 0;
    SI2::PlaySoundKit(soundID, position, nullptr, &properties, 0, nullptr, 1, 0);
}

// ref: FUN_0071a030
// Whether a footstep at `position` falls in shallow liquid the unit wades in (not swims).
bool CGUnit_C::IsWading(const C3Vector* position) {
    uint32_t fieldBC = 0;
    uint32_t unused = 0;
    float surface = 0.0f;

    if (!CWorld::GetObjectFloor(this->m_worldObject, &fieldBC, &surface, &unused)) {
        return false;
    }

    uint32_t bit8 = 0;
    uint32_t bit9 = 0;
    CWorld::GetObjectLiquidFlags(this->m_worldObject, &bit8, &bit9);

    return bit9 && position->z < surface + 0.01f && !(this->m_move->m_moveFlags & 0x200000);
}

// ref: FUN_00754f40
// The unit's footstep size class: the display's, or its model's when the display does not say.
int32_t CGUnit_C::GetFootstepSize() const {
    int32_t size = this->m_displayInfo ? this->m_displayInfo->m_sizeClass : -1;

    if (size == -1 && this->m_modelData) {
        size = this->m_modelData->m_sizeClass;
    }

    return size;
}

// ref: FUN_00747240
// "$FSD": a footstep's sound, and the armour's foley, for a visible unit on its feet.
void CGUnit_C::PlayFootstepEventSound(const C3Vector* position) {
    if ((static_cast<uint8_t>(this->m_unit->bytes1 >> 16) & 0x2) || (this->m_move->m_moveFlags & 0x40000000)) {
        return;
    }

    if (this->m_vehiclePassenger && this->m_vehiclePassenger->m_state != 0) {
        return;
    }

    if (this->IsA(TYPE_PLAYER) && (static_cast<CGPlayer_C*>(this)->Player()->flags & 0x10)) {
        return;
    }

    this->PlayArmorFoley();

    static CVar* footstepSounds = CVar::Lookup("FootstepSounds");
    auto soundData = this->GetSoundData();

    if (!soundData || !soundData->m_soundFootstepID || !footstepSounds || footstepSounds->GetInt() == 0) {
        return;
    }

    int32_t large = 2 < this->GetFootstepSize() ? 1 : 0;

    PlayFootstepSound(soundData->m_soundFootstepID, position, this->m_terrainType, this->IsWading(position) ? 1 : 0,
                      large, this);
}

// ref: FUN_0071fb60
// The footprint a step leaves: the mount's while mounted, the unit's own otherwise, scaled.
void CGUnit_C::GetFootprint(int32_t* texture, C2Vector* size) {
    float scale = this->GetScale();

    if (this->m_unit->mountDisplayID < 1 || (this->m_stateFlags & 0x10000000) || !this->m_mountModel) {
        *texture = this->m_footprintTexture;
        size->x = this->m_footprintLength * scale;
        size->y = scale * this->m_footprintWidth;
        return;
    }

    *texture = this->m_mountFootprintTexture;
    size->x = this->m_mountFootprintLength * scale;
    size->y = scale * this->m_mountFootprintWidth;
}

// ref: FUN_00723a50
// One footfall, within fifty yards of the camera, of a visible unit on its own feet (no pet
// number, not stealthed, not on a transport, not a ghost): the footprint, the model's footstep
// shake, and within twenty-five yards, walking or running forward, the terrain's spray -- or a
// splash in water shallower than half the unit's height.
//
void CGUnit_C::OnFootstep(const C3Vector* position, int32_t left) {
    if (this->m_unit->petNumber != 0 || (this->m_move->m_moveFlags & 0x40000000)
        || (static_cast<uint8_t>(this->m_unit->bytes1 >> 16) & 0x2) || this->GetTransportGUID() != 0) {
        return;
    }

    if (this->IsA(TYPE_PLAYER) && (static_cast<CGPlayer_C*>(this)->Player()->flags & 0x10)) {
        return;
    }

    auto camera = CGWorldFrame::GetActiveCamera();

    if (!camera) {
        return;
    }

    const C3Vector& eye = camera->Position();
    float dx = position->x - eye.x;
    float dy = position->y - eye.y;
    float dz = position->z - eye.z;
    float distance = dz * dz + dy * dy + dx * dx;

    if (2500.0f < distance) {
        return;
    }

    // The footprint (FUN_0077f040 -> FUN_0079fa70), unless the model leaves none (flag 0x20).
    if ((CWorld::s_enables & CWorld::Enable_Footprints) && !(this->m_modelData && (this->m_modelData->m_flags & 0x20))) {
        int32_t texture = 0;
        C2Vector size = { 0.0f, 0.0f };
        this->GetFootprint(&texture, &size);

        FootprintAdd(static_cast<uint32_t>(texture), size, *position, this->GetFacing(), left == 0, this->m_terrainType,
                     this->GetGUID() == ClntObjMgrGetActivePlayer());
    }

    auto modelData = this->m_modelData;

    if (modelData && modelData->m_footstepShakeSize != 0) {
        camera->AddShakeByID(modelData->m_footstepShakeSize, *position);
    }

    static CVar* footprintParticles = CVar::Lookup("showfootprintparticles");

    if (625.0f < distance || (this->m_move->m_moveFlags & 0x2) || (modelData && (modelData->m_flags & 0x1))
        || !footprintParticles || footprintParticles->GetInt() == 0) {
        return;
    }

    uint32_t fieldBC = 0;
    uint32_t unused = 0;
    float surface = 0.0f;

    if (!CWorld::GetObjectFloor(this->m_worldObject, &fieldBC, &surface, &unused)) {
        auto terrain = g_terrainTypeDB.GetRecord(this->m_terrainType);

        if (!terrain) {
            return;
        }

        auto spray = g_spellVisualEffectNameDB.GetRecord(this->IsMovingAtWalkPace() ? terrain->m_footstepSprayWalk
                                                                                    : terrain->m_footstepSprayRun);

        if (!spray) {
            return;
        }

        C3Vector at = *position;

        if (this->GetTransportGUID() != 0) {
            C44Matrix transport;
            MovementGetTransportMatrixChecked(this->GetTransportGUID(), transport, this->GetGUID(), ".\\Unit_C.cpp", 0xcce);
            at = at * transport.AffineInverse();
        }

        void* mem = SMemAlloc(sizeof(CEffect), __FILE__, __LINE__, 0);
        auto effect = mem ? new (mem) CEffect() : nullptr;

        if (effect) {
            effect->InitializeAtPoint(at, static_cast<uint32_t>(OsGetAsyncTimeMs()), 0, nullptr, spray, this->GetGUID(),
                                      0x220, &CGObject_C::KitEffectOneShot, 0, nullptr, this->GetTransportGUID());
            effect->Release();
        }

        return;
    }

    uint32_t bit8 = 0;
    uint32_t bit9 = 0;
    CWorld::GetObjectLiquidFlags(this->m_worldObject, &bit8, &bit9);

    if (!bit9) {
        return;
    }

    float depth = surface - this->GetPosition().z;

    if (!(depth < this->m_localMove.m_collisionHeight * 0.5f)) {
        return;
    }

    C3Vector at = { position->x, position->y, depth + position->z };
    void* mem = SMemAlloc(sizeof(CEffect), __FILE__, __LINE__, 0);
    auto effect = mem ? new (mem) CEffect() : nullptr;

    if (effect) {
        effect->InitializeHardcodedAt(this->IsMovingAtWalkPace() ? 0 : 1, at, this->GetGUID(), 0x220,
                                      &CGObject_C::KitEffectOneShot);
        effect->Release();
    }
}

// ref: FUN_0071fa90
// Every ten seconds: a unit deep under water (more than five yards past its own height) breathes
// bubbles (state 0x20).
//
// PARTIAL: the cold-air breath (state 0x40, FUN_0078f1f0 over the area's WorldParam) is the
// world parameters port's.
void CGUnit_C::UpdateBreathState(uint32_t time) {
    this->m_stateFlags &= 0xFFFFFF9F;

    uint32_t fieldBC = 0;
    uint32_t unused = 0;
    float surface = 0.0f;

    if (CWorld::GetObjectFloor(this->m_worldObject, &fieldBC, &surface, &unused)) {
        float height = this->m_height * this->m_scale;

        if (height + 5.0f < surface - this->GetPosition().z) {
            this->m_stateFlags |= 0x20;
        }
    }

    this->m_breathCheckTime = time + 10000;
}

// ref: FUN_007462e0
// One of the creature's fidget sounds, two yards above it.
void CGUnit_C::PlayFidgetSound(uint32_t index) {
    auto soundData = this->GetSoundData();

    if (!soundData || 5 <= index || !soundData->m_soundFidget[index]) {
        return;
    }

    C3Vector at = this->GetPosition();
    at.z += 2.0f;

    SI2::PlaySoundKit(soundData->m_soundFidget[index], &at, nullptr, nullptr, 0, nullptr, 1, 0);
}

// ref: FUN_007471a0
// "$FD1".."$FD5" the fidget sounds; "$FDX" another unit's fidget voice (unit sound 5).
void CGUnit_C::OnFidgetEvent(uint32_t eventId) {
    for (uint32_t i = 0; i < 5; i++) {
        if (eventId == (EventId("$FD1") + (i << 24))) {
            this->PlayFidgetSound(i);
            return;
        }
    }

    if (eventId == EventId("$FDX") && this->GetGUID() != ClntObjMgrGetActivePlayer()) {
        this->PlayUnitSound(5, 0);
    }
}

// ref: FUN_00746ad0
// "$ESD": the sound of the emote state the unit holds, when that emote is a sound emote (2).
void CGUnit_C::PlayEmoteStateSound(const C3Vector* position) {
    auto emote = g_emotesDB.GetRecord(static_cast<int32_t>(this->m_unit->emoteState));

    if (emote && emote->m_specProc == 2) {
        SI2::PlaySoundKit(emote->m_soundID, position, nullptr, nullptr, 0, nullptr, 1, 0);
    }
}

// ref: FUN_00715a50
// Where the unit's next missile flies from (state 0x80000000 until the frame takes it).
void CGUnit_C::SetMissileLaunchPoint(const C3Vector& point) {
    this->m_stateFlags |= 0x80000000;
    this->m_missileLaunchPoint = point;
}

// ref: FUN_00732650
// The animation event dispatch.
//
// PARTIAL, each the subsystem's own port: the missile a bow or gun releases ("$BWR", "$CSL",
// "$CSR", "$CST" with a held missile at +0x9ec), the combat events ("$AH#", "$CAH", "$DTH",
// "$BWP", "$CPP", "$CSS", FUN_00756240, UnitCombat_C), the spell cast sound ("$CSD",
// FUN_00746d60), the trade-skill event ("$TRD", FUN_00763570), and the vehicle's ("$VG#",
// FUN_00757060; "$VT#", FUN_007570f0).
void CGUnit_C::OnAnimEvent(CM2Model* model, uint32_t eventId, uint32_t eventData, const C3Vector* position) {
    (void)model;
    (void)eventData;

    int32_t left = 0;

    if (IsFootstepEvent(eventId, &left)) {
        this->OnFootstep(position, left);
        return;
    }

    if ((eventId & 0x00FFFFFF) == (EventId("$FD0") & 0x00FFFFFF) || eventId == EventId("$FDX")) {
        this->OnFidgetEvent(eventId);
        return;
    }

    if (eventId == EventId("$SHL")) {
        this->ReturnSwingWeapon(position, 1, 0x200000);
    } else if (eventId == EventId("$SHR")) {
        this->ReturnSwingWeapon(position, 0, 0x100000);
    } else if (eventId == EventId("$WGG")) {
        this->PlayUnitSound(10, 0);
    } else if (eventId == EventId("$WNG")) {
        this->PlayUnitSound(7, 0);
    } else if (eventId == EventId("$ESD")) {
        this->PlayEmoteStateSound(position);
    } else if (eventId == EventId("$FSD")) {
        this->PlayFootstepEventSound(position);
    } else if (eventId == EventId("$SMD")) {
        if (this->m_soundData) {
            SI2::PlaySoundKit(this->m_soundData->m_submergedSoundID, position, nullptr, nullptr, 0, nullptr, 1, 0);
        }
    } else if (eventId == EventId("$SMG")) {
        if (this->m_soundData) {
            SI2::PlaySoundKit(this->m_soundData->m_submergeSoundID, position, nullptr, nullptr, 0, nullptr, 1, 0);
        }
    } else if (eventId == EventId("$BRT")) {
        if (this->m_soundData) {
            SI2::PlaySoundKit(this->m_soundData->m_birthSoundID, position, nullptr, nullptr, 0, nullptr, 1, 0);
        }
    } else if (eventId == EventId("$SCD")) {
        if (this->m_soundData) {
            SI2::PlaySoundKit(this->m_soundData->m_spellCastDirectedSoundID, position, nullptr, nullptr, 0, nullptr, 1, 0);
        }
    } else if (eventId == EventId("$BTH")) {
        // The breath: bubbles deep under water (hard-coded effect 2), fog in cold air (3), and a
        // drunk player's (7, Player_C's drunkenness, not ported).
        if (ClntObjMgrGetPlayerType() == PLAYER_BOT || !this->m_modelData || (this->m_modelData->m_flags & 0x2)) {
            return;
        }

        int32_t index;

        if (this->m_stateFlags & 0x20) {
            index = 2;
        } else if (this->m_stateFlags & 0x40) {
            index = 3;
        } else {
            return;
        }

        void* mem = SMemAlloc(sizeof(CEffect), __FILE__, __LINE__, 0);
        auto effect = mem ? new (mem) CEffect() : nullptr;

        if (effect) {
            effect->InitializeHardcoded(index, this->GetGUID(), &CGObject_C::KitEffectOneShot);
            effect->m_flags |= CEffect::EFFECT_NO_EXPIRE;
            effect->Release();
        }
    } else if (eventId == EventId("$BWR")) {
        if (this->GetGUID() == ClntObjMgrGetActivePlayer() && s_clickToMoveState == 0) {
            this->CancelClickToMove(0, 1);
        }

        this->m_animFlags &= 0xFFFFBFFF;

        if (this->m_rangedAmmoModel && this->m_rangedAmmoModel->m_attachParent) {
            this->m_rangedAmmoModel->DetachFromParent();
        }
    }
}

// ref: FUN_00734a40
// The unit model's anim-event hook: the owner by guid, then its dispatch. A position with a NaN
// is reported (to the error log the reference keeps) and dispatched anyway.
void CGUnit_C::AnimEventCallback(CM2Model* model, uint32_t boneId, uint32_t eventId, uint32_t eventData,
                                 const C3Vector* position, uint32_t a6, WOWGUID owner) {
    (void)boneId;
    (void)a6;

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(owner, TYPE_UNIT, ".\\Unit_C.cpp", 0x21b));

    if (!unit || !position) {
        return;
    }

    unit->OnAnimEvent(model, eventId, eventData, position);
}

// ref: FUN_00730290
// A mirror image's appearance: for the display it still wears, either the creature's own baked
// look (race 0) or a whole character -- race, sex, class, the five appearance choices, the guild
// and the eleven visible items -- built as a player's would be.
//
// PARTIAL: a guild tabard's emblem (FUN_007eada0 through the guild cache, then FUN_004ec1c0) is the
// guild cache's, which frozen does not have yet; the tabard itself is dressed.
void CGUnit_C::ReceiveMirrorImageData(CDataStore* msg) {
    ComponentData data;

    this->m_stateFlags &= ~0x20000u;

    if (!this->m_model || !this->m_model->IsLoaded(0, 0)) {
        return;
    }

    uint32_t displayID;
    msg->Get(displayID);

    if (static_cast<int32_t>(displayID) != this->m_unit->displayID) {
        msg->Seek(msg->Size());
        return;
    }

    uint8_t value;
    msg->Get(value);
    data.raceID = value;

    if (data.raceID == 0) {
        if (auto extra = this->m_displayInfoExtra) {
            this->m_characterComponent = CCharacterComponent::AllocComponent();

            // FUN_00715930
            const char* bake = extra->m_bakeName;

            if (!bake || !bake[0]) {
                return;
            }

            data.raceID = extra->m_displayRaceID;
            data.sexID = extra->m_displaySexID;
            data.classID = 0;
            data.skinColorID = extra->m_skinID;
            data.faceID = extra->m_faceID;
            data.hairStyleID = extra->m_hairStyleID;
            data.hairColorID = extra->m_hairColorID;
            data.facialHairStyleID = extra->m_facialHairID;
            data.flags |= 0x1;
            SStrPrintf(data.npcBakedTexturePath, sizeof(data.npcBakedTexturePath), "%s%s", "Textures\\BakedNpcTextures\\", bake);
            data.model = this->m_model;
            data.model->m_refCount++;

            this->m_characterComponent->Init(&data, nullptr);

            if (this->m_characterComponent && this->m_displayInfoExtra) {
                for (int32_t slot = 0; slot < 11; slot++) {
                    int32_t item = this->m_displayInfoExtra->m_npcitemDisplay[slot];

                    if (item) {
                        this->m_characterComponent->AddItem(static_cast<ITEM_SLOT>(slot), item, 0);
                    }
                }
            }
        }
    } else {
        msg->Get(value);
        data.sexID = value;
        msg->Get(value);
        data.classID = value;
        msg->Get(value);
        data.skinColorID = value;
        msg->Get(value);
        data.faceID = value;
        msg->Get(value);
        data.hairStyleID = value;
        msg->Get(value);
        data.hairColorID = value;
        msg->Get(value);
        data.facialHairStyleID = value;

        uint32_t guildID;
        msg->Get(guildID);

        this->m_characterComponent = CCharacterComponent::AllocComponent();
        data.model = this->m_model;

        if (this->GetGUID() == ClntObjMgrGetActivePlayer()) {
            data.flags |= 0x2;
        } else {
            data.flags &= ~0x2u;
        }

        data.model->m_refCount++;

        CCharacterComponent::ValidateComponentData(&data, static_cast<COMPONENT_CONTEXT>(1));
        this->m_characterComponent->Init(&data, nullptr);

        // The visible slots, in the order the server writes them (FUN_004f2880 maps each to its
        // section).
        static const INVENTORY_SLOTS s_slots[] = {
            static_cast<INVENTORY_SLOTS>(0), static_cast<INVENTORY_SLOTS>(2), static_cast<INVENTORY_SLOTS>(3),
            static_cast<INVENTORY_SLOTS>(4), static_cast<INVENTORY_SLOTS>(5), static_cast<INVENTORY_SLOTS>(6),
            static_cast<INVENTORY_SLOTS>(7), static_cast<INVENTORY_SLOTS>(8), static_cast<INVENTORY_SLOTS>(9),
            static_cast<INVENTORY_SLOTS>(0xe), static_cast<INVENTORY_SLOTS>(0x12),
        };

        for (auto slot : s_slots) {
            uint32_t item;
            msg->Get(item);

            if (item) {
                this->m_characterComponent->AddItemBySlot(slot, static_cast<int32_t>(item), 0);
            }
        }

        // TODO(GuildCache): the tabard's emblem for `guildID` (FUN_007eada0 / FUN_004ec1c0).
        (void)guildID;
    }

    this->AttachHandItem(0);
    this->AttachHandItem(1);
    this->AttachHandItem(2);
    this->ApplyItemVisualEffects();

    // FUN_00512b50
    PortraitRefresh(this->GetGUID(), 3);

    this->m_stateFlags &= ~0x400000u;
}

// ---- threat (Unit_C.cpp) --------------------------------------------------------------------

// ref: FUN_007416f0
// The entry for `guid`, made when there is none: a new one starts below the tank with no percent
// yet, and the game UI learns that this unit holds threat against it.
CThreatEntry* CGUnit_C::AddThreatEntry(const WOWGUID& guid) {
    CHashKeyGUID key(guid);
    auto entry = this->m_threatList.Ptr(static_cast<uint32_t>(guid), key);

    if (entry) {
        return entry;
    }

    // FUN_0072cd90 / FUN_0073f200
    entry = this->m_threatList.New(static_cast<uint32_t>(guid), key, 0, 0);
    entry->m_guid = guid;
    entry->m_status = 1;
    entry->m_percent = 0xff;
    entry->m_threat = 0;

    if (guid == ClntObjMgrGetActivePlayer()) {
        this->m_stateFlags |= 0x8;
    }

    GameUIAddThreatUnit(this->GetGUID(), guid);

    return entry;
}

// ref: FUN_0071c3b0
// An entry's percentage of `topThreat` (capped at 250), and the low/high status that follows from
// it. When `notify` -- the entry is the player -- crossing over plays the warning and floats the
// world text. True when the status changed.
//
// PARTIAL: the tooltip's refresh for this unit (FUN_00512ab0) is the tooltip's.
bool CGUnit_C::UpdateThreatPercent(CThreatEntry* entry, int32_t topThreat, bool notify) {
    uint8_t percent;

    if (topThreat == 0) {
        percent = 100;
    } else if (entry->m_threat < 1) {
        percent = 0;
    } else {
        int64_t scaled = static_cast<int64_t>(entry->m_threat) * 100 / topThreat;
        percent = 249 < scaled ? 250 : static_cast<uint8_t>(scaled);
    }

    if (percent == entry->m_percent) {
        return false;
    }

    entry->m_percent = percent;

    uint8_t status = percent < 100 ? 1 : 2;
    bool changed = false;

    if (status != entry->m_status) {
        if (notify) {
            auto sounds = CGGameUI::s_threatPlaySoundsCvar;

            if (sounds && sounds->GetInt() != 0 && status == 2 && entry->m_status == 1) {
                SI2::PlayUISound(0x3b9e);
            }

            if (!(this->m_stateFlags & 0x8)) {
                this->ShowThreatWorldText(entry->m_status, status);
            }
        }

        entry->m_status = status;
        ScriptEventsQueueUnitEvent(entry->m_guid, 0x262);
        changed = true;
    }

    if (notify) {
        this->m_stateFlags &= ~0x8u;
    }

    return changed;
}

// ref: FUN_00737620
// The tank's own status: secure (4), or insecure (3) while anyone else is above it.
//
// PARTIAL: the tooltip's refresh for this unit (FUN_00512ab0) is the tooltip's.
void CGUnit_C::UpdateThreatTargetStatus() {
    CHashKeyGUID key(this->m_threatTarget);
    auto top = this->m_threatList.Ptr(static_cast<uint32_t>(this->m_threatTarget), key);

    if (!top) {
        return;
    }

    uint8_t status = 4;

    for (auto entry = this->m_threatList.Head(); entry; entry = this->m_threatList.Next(entry)) {
        if (entry->m_guid != this->m_threatTarget && entry->m_status == 2) {
            status = 3;
        }
    }

    WOWGUID player = ClntObjMgrGetActivePlayer();

    if (status != top->m_status) {
        if (this->m_threatTarget == player && !(this->m_stateFlags & 0x8)) {
            this->ShowThreatWorldText(top->m_status, status);

            auto sounds = CGGameUI::s_threatPlaySoundsCvar;

            if (sounds && sounds->GetInt() != 0) {
                SI2::PlayUISound(0x3b9f);
            }
        }

        top->m_status = status;
        ScriptEventsQueueUnitEvent(this->m_threatTarget, 0x262);
    }

    if (this->m_threatTarget != player) {
        return;
    }

    this->m_stateFlags &= ~0x8u;
}

// ref: FUN_007417a0
// SMSG_THREAT_UPDATE's body: each (guid, threat) pair, with SMSG_HIGHEST_THREAT_UPDATE's new top
// first. Every percentage is retaken against the top's threat -- the whole table when the top
// moved or was named, otherwise just the entries the message carried.
void CGUnit_C::ReceiveThreatUpdate(CDataStore* msg, bool highest) {
    WOWGUID player = ClntObjMgrGetActivePlayer();
    bool everyone = highest;

    if (highest) {
        SmartGUID top;
        *msg >> top;

        this->m_threatTarget = top;

        if (this->m_threatTarget) {
            this->AddThreatEntry(this->m_threatTarget);
        }
    }

    uint32_t count;
    msg->Get(count);

    TSGrowableArray<CThreatEntry*> updated;
    updated.SetCount(count);

    for (uint32_t i = 0; i < count; i++) {
        SmartGUID guid;
        *msg >> guid;

        uint32_t threat;
        msg->Get(threat);

        WOWGUID id = guid;
        auto entry = this->AddThreatEntry(id);
        entry->m_threat = static_cast<int32_t>(threat);

        if (id == this->m_threatTarget) {
            everyone = true;
        }

        updated[i] = entry;
    }

    CHashKeyGUID key(this->m_threatTarget);
    auto top = this->m_threatList.Ptr(static_cast<uint32_t>(this->m_threatTarget), key);
    bool changed = false;

    if (this->m_threatTarget && top) {
        int32_t topThreat = top->m_threat;

        if (everyone) {
            for (auto entry = this->m_threatList.Head(); entry; entry = this->m_threatList.Next(entry)) {
                if (entry->m_guid != this->m_threatTarget && this->UpdateThreatPercent(entry, topThreat, player == entry->m_guid)) {
                    changed = true;
                }
            }
        } else {
            for (uint32_t i = 0; i < count; i++) {
                auto entry = updated[i];

                if (entry->m_guid != this->m_threatTarget && this->UpdateThreatPercent(entry, topThreat, player == entry->m_guid)) {
                    changed = true;
                }
            }
        }
    }

    if (highest || changed) {
        this->UpdateThreatTargetStatus();
    }

    ScriptEventsQueueUnitEvent(this->GetGUID(), 0x261);
}

// ref: FUN_00737750
// SMSG_THREAT_REMOVE's body: `guid` leaves the table. Losing anyone but the tank above it may
// leave the tank secure again.
//
// PARTIAL: the tooltip's refresh when the player left (FUN_00512ab0) is the tooltip's.
void CGUnit_C::RemoveThreatEntry(const WOWGUID& guid) {
    CHashKeyGUID key(guid);
    auto entry = this->m_threatList.Ptr(static_cast<uint32_t>(guid), key);

    if (!entry) {
        return;
    }

    bool retake = false;

    if (guid == this->m_threatTarget) {
        this->m_threatTarget = 0;
    } else if (1 < entry->m_status) {
        retake = true;
    }

    WOWGUID removed = guid;

    GameUIRemoveThreatUnit(this->GetGUID(), removed);

    // FUN_00723270 / ObjectFree
    this->m_threatList.Delete(entry);

    ScriptEventsQueueUnitEvent(removed, 0x262);

    if (retake) {
        this->UpdateThreatTargetStatus();
    }

    ScriptEventsQueueUnitEvent(this->GetGUID(), 0x261);
}

// ref: FUN_007345c0
// SMSG_THREAT_CLEAR's body: the table empties and there is no top.
//
// PARTIAL: the tooltip's refresh for this unit (FUN_00512ab0) is the tooltip's.
void CGUnit_C::ClearThreatList() {
    if (!this->m_threatList.Head()) {
        return;
    }

    while (auto entry = this->m_threatList.Head()) {
        WOWGUID guid = entry->m_guid;

        ScriptEventsQueueUnitEvent(guid, 0x262);
        GameUIRemoveThreatUnit(this->GetGUID(), guid);
        this->m_threatList.Delete(entry);
    }

    this->m_threatTarget = 0;

    ScriptEventsQueueUnitEvent(this->GetGUID(), 0x261);
}

// ref: FUN_007374c0
// What this unit's table says about `guid`: its status, its percentage of the tank's, the raw
// percentage of the pull threshold (110% in melee range, 130% beyond it; 100 for the tank) and its
// threat. Without a tank or an entry everything is zero but the status, which is 1 when the entry
// is there. Zero when it is not on the table.
int32_t CGUnit_C::GetThreatSituation(const WOWGUID& guid, uint8_t* status, uint8_t* percent, float* rawPercent,
                                     int32_t* threat) {
    CHashKeyGUID key(guid);
    auto entry = this->m_threatList.Ptr(static_cast<uint32_t>(guid), key);

    if (!this->m_threatTarget || !entry) {
        if (status) {
            *status = entry ? 1 : 0;
        }

        if (percent) {
            *percent = 0;
        }

        if (rawPercent) {
            *rawPercent = 0.0f;
        }

        if (threat) {
            *threat = 0;
        }

        return 0;
    }

    if (status) {
        *status = entry->m_status;
    }

    if (percent) {
        *percent = entry->m_percent;
    }

    if (rawPercent) {
        if (guid == this->m_threatTarget) {
            *rawPercent = 100.0f;
        } else {
            auto other = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\Unit_C.cpp", 0x5b35));
            bool inMelee = false;

            if (other) {
                // FUN_004f5f40: the melee range between the two, never under 5 yards.
                float range = other->m_unit->combatReach + this->m_unit->combatReach + 1.3333334f;

                if (range < 5.0f) {
                    range = 5.0f;
                }

                // FUN_004f61d0
                C3Vector at = other->GetPosition();
                C3Vector here = this->GetPosition();
                float dx = here.x - at.x;
                float dy = here.y - at.y;
                float dz = here.z - at.z;

                inMelee = dy * dy + dz * dz + dx * dx <= range * range;
            }

            *rawPercent = static_cast<float>(entry->m_percent) * (inMelee ? 0.90909094f : 0.7692308f);
        }
    }

    if (threat) {
        *threat = entry->m_threat;
    }

    return 1;
}

// ref: FUN_00719220
// The floater a status change raises over this unit ("COMBAT_THREAT_INCREASE_2" and so on) while
// threat warnings are on -- except falling from high to low, or from insecure to secure.
//
// PHASE4(PlayerName): the text goes to the unit's name plate (+0xb0) as world text 10 through
// FUN_007e5100 / FUN_007e6030; frozen's name plates do not carry world text yet.
void CGUnit_C::ShowThreatWorldText(int32_t oldStatus, int32_t newStatus) {
    if (!GameUIThreatWarningActive()) {
        return;
    }

    auto worldText = CGGameUI::s_threatWorldTextCvar;

    if (!worldText || worldText->GetInt() == 0) {
        return;
    }

    bool quiet;

    if (oldStatus == 2) {
        quiet = newStatus == 1;
    } else if (oldStatus == 3) {
        quiet = newStatus == 4;
    } else {
        quiet = false;
    }

    if (quiet) {
        return;
    }

    char token[32];
    SStrPrintf(token, sizeof(token), "COMBAT_THREAT_%s_%d", newStatus <= oldStatus ? "DECREASE" : "INCREASE", newStatus);

    const char* text = FrameScript_GetText(token, -1, GENDER_NOT_APPLICABLE);

    if (!text || !text[0]) {
        return;
    }
}

// ---- control (Unit_C.cpp) -------------------------------------------------------------------

// ref: FUN_0071c930
// Whether the player controls this unit. Losing control stops a fall in progress and remembers a
// charm (0x1000); gaining it sets 0xc000400. For the player itself the game UI hears of it, and a
// rider's camera blends to its seat.
void CGUnit_C::SetHasControl(bool hasControl) {
    if (!hasControl) {
        this->m_stateFlags &= ~0x400u;

        if (this->m_unit->charm) {
            this->m_stateFlags |= 0x1000;
        }

        // FUN_006e9a60
        if (this->m_localMove.m_moveFlags & 0x100000) {
            this->m_localMove.StopFallAndMoving();
        }
    } else {
        this->m_stateFlags |= 0xc000400;
    }

    if (this->GetGUID() == ClntObjMgrGetActivePlayer()) {
        GameUISetPlayerControl((this->m_stateFlags >> 10) & 1);

        if (this->m_stateFlags & 0x400) {
            VehicleRefreshCameraBlend(this);
        }
    }
}

// ref: FUN_0072cca0
// SMSG_CONTROL_UPDATE for this unit: who the input moves follows -- the camera's unit when it may
// be moved, else nobody.
void CGUnit_C::OnControlUpdate(WOWGUID guid, bool hasControl) {
    this->SetHasControl(hasControl);

    auto camera = CGWorldFrame::GetActiveCamera();
    WOWGUID target = camera ? camera->m_target : 0;

    if (target == guid) {
        if (this->m_stateFlags & 0x400) {
            CGUnit_C::ChangeActiveMover(guid);
            return;
        }

        if (this->GetGUID() != CGUnit_C::s_activeMover) {
            return;
        }
    } else {
        if (this->GetGUID() != CGUnit_C::s_activeMover || (this->m_stateFlags & 0x400)) {
            return;
        }

        auto object = ClntObjMgrObjectPtr(target, TYPE_OBJECT, ".\\Unit_C.cpp", 0x5c8f);

        if (object && object->IsA(TYPE_UNIT) && (static_cast<CGUnit_C*>(object)->m_stateFlags & 0x400)) {
            CGUnit_C::ChangeActiveMover(target);
            return;
        }
    }

    CGUnit_C::ChangeActiveMover(0);
}

// ref: FUN_00729010
// The input moves `guid` (0 for nothing). The old mover lets go -- its click-to-move ends, and a
// unit without a spline holding it reports CMSG_MOVE_NOT_ACTIVE_MOVER -- and the new one takes over
// from where it is, with the player's flight bit, and the vehicle controls follow.
void CGUnit_C::ChangeActiveMover(WOWGUID guid) {
    if (CGUnit_C::s_activeMover == guid) {
        return;
    }

    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());
    WOWGUID player = ClntObjMgrGetActivePlayer();

    if (auto old = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(CGUnit_C::s_activeMover, TYPE_UNIT, ".\\Unit_C.cpp", 0x1f7d))) {
        if (old->GetGUID() == CGUnit_C::s_activeMover && s_clickToMoveState != 13) {
            old->CancelClickToMove(0, 1);
        }

        old->m_localMove.m_moveFlags &= 0x7fffffff;

        if (player != CGUnit_C::s_activeMover) {
            old->m_localMove.m_moveFlags &= ~0x200u;
        }

        auto spline = old->m_localMove.m_spline;

        if (!spline || (spline->flags & 0x400)) {
            old->m_localMove.OnLostActiveMover();

            if (old->GetGUID() == player) {
                // FUN_00724e70
                old->SendMovementStatus(MovementGetGlobals()->m_stepTime, CMSG_MOVE_NOT_ACTIVE_MOVER, 0.0f, 0, 0, 0xff);
            }
        }
    }

    CGUnit_C::s_activeMover = guid;

    if (guid) {
        CDataStore msg;
        msg.Put(static_cast<uint32_t>(CMSG_SET_ACTIVE_MOVER));
        msg.Put(guid);
        msg.Finalize();
        ClientServices::Send(&msg);
    }

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(CGUnit_C::s_activeMover, TYPE_UNIT, ".\\Unit_C.cpp", 0x1f99));

    if (unit) {
        if (player != CGUnit_C::s_activeMover) {
            auto self = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(player, TYPE_PLAYER, __FILE__, __LINE__));

            if (self && (self->m_localMove.m_moveFlags & 0x200)) {
                unit->m_localMove.m_moveFlags |= 0x200;
            }
        }

        unit->m_localMove.OnBecameActiveMover();

        if (auto input = InputControlGetActive()) {
            input->UpdatePlayerMovement(now, 1);
        }
    }

    VehicleOnActiveMoverChanged(unit);
}

// ref: FUN_00716060
// Keep an SMSG_CONTROL_UPDATE for a unit not yet in view: a new one replaces it when nothing is
// kept, when it is for the same unit, when the kept one was a loss, or when the new one is a gain.
void CGUnit_C::StorePendingControl(WOWGUID guid, bool hasControl) {
    if (!s_pendingControl || s_pendingControl == guid || !s_pendingControlHas || hasControl) {
        s_pendingControl = guid;
        s_pendingControlHas = hasControl;
    }
}

// ---- pet sounds (UnitSound_C.cpp) -----------------------------------------------------------

namespace {

// ref: FUN_004cda20
// The voice a Death Knight's sounds pick: "Death Knight <race> <sex>".
void SoundGetDeathKnightVoice(CGPlayer_C* player, char* buffer, uint32_t size) {
    static const char* const s_raceNames[] = {
        nullptr, "Human", "Orc", "Dwarf", "NightElf", "Scourge", "Tauren", "Gnome", "Troll", "Goblin",
        "BloodElf", "Draenei",
    };

    uint32_t bytes0 = player->Unit()->bytes0;
    uint32_t race = bytes0 & 0xff;
    const char* raceName = race < sizeof(s_raceNames) / sizeof(s_raceNames[0]) ? s_raceNames[race] : nullptr;

    SStrPrintf(buffer, size, "Death Knight %s %s", raceName ? raceName : "", ((bytes0 >> 16) & 0xff) ? "Female" : "Male");
}

} // namespace

// ref: FUN_007474b0
// The pet's voice: 0 aggro, 1 an order taken, 2 an attack, 4 its death (not for a creature whose
// template mutes it). A spoken line is never cut, nor a louder pet sound by a quieter one. A pet's
// voice waits on Sound_EnablePetSounds; anything else speaks regardless.
void CGUnit_C::PlayPetSound(int32_t type) {
    if (type == 4 && this->m_creatureStats && (this->m_creatureStats->m_typeFlags & 0x1000000)) {
        return;
    }

    if (!this->m_soundData) {
        return;
    }

    if (SI2::IsPlaying(this->m_speechSound)) {
        return;
    }

    if (SI2::IsPlaying(this->m_petSound) && type <= this->m_petSoundType) {
        return;
    }

    this->m_petSoundType = type;

    static CVar* s_enablePetSounds = CVar::Lookup("Sound_EnablePetSounds");
    bool voiced = !s_enablePetSounds || s_enablePetSounds->GetInt() != 0 || this->m_unit->petNumber == 0;
    int32_t soundID;

    switch (this->m_petSoundType) {
        case 0:
            if (!voiced) {
                return;
            }

            soundID = this->m_soundData->m_soundAggroID;
            break;

        case 1:
            if (!voiced) {
                return;
            }

            soundID = this->m_soundData->m_soundPetOrderID;
            break;

        case 2:
            if (!voiced) {
                return;
            }

            soundID = this->m_soundData->m_soundPetAttackID;
            break;

        case 4:
            soundID = this->m_soundData->m_soundDeathID;
            break;

        default:
            return;
    }

    C3Vector position;
    this->GetAttachmentPosition(position, 0x11, nullptr);

    static CVar* s_listenerAtCharacter = CVar::Lookup("Sound_ListenerAtCharacter");
    bool isMover = CGUnit_C::s_activeMover == this->GetGUID();
    bool atCharacter = isMover && s_listenerAtCharacter && s_listenerAtCharacter->GetInt() != 0;

    SoundKitProperties properties;
    properties.ResetToDefaults();

    if (isMover) {
        properties.int20 = 0x6e;

        auto player = CGPlayer_C::GetActivePtr();

        if (player && ((player->Unit()->bytes0 >> 8) & 0xff) == 6) {
            properties.uint60 = 1;
            SoundGetDeathKnightVoice(player, properties.m_voiceName, sizeof(properties.m_voiceName));
        }

        if (atCharacter) {
            properties.m_fadeOutTime = 0.65f;
        }
    }

    SI2::PlaySoundKit(soundID, atCharacter ? nullptr : &position, this->m_petSound, &properties, 0, nullptr, 1, 0);
}
