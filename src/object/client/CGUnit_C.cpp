#include "object/client/CGUnit_C.hpp"
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
#include "component/CCharacterComponent.hpp"
#include "db/Db.hpp"
#include "model/Model2.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Data.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "object/client/CEffect.hpp"
#include "object/client/CGGameObject_C.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/CVehicle_C.hpp"
#include "object/client/CVehiclePassenger_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/Game.hpp"
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
    , m_localMove(objCreate.move.status.position28, objCreate.move.status.facing34, this->GetGUID(), this)
{
    this->m_mountSound = STORM_NEW(SOUNDKITOBJECT);

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

    this->m_targetChangeTime = CWorld::GetTickTimeMs() - 1000;

    // TODO

    this->RefreshDataPointers();

    // TODO
}

CGUnit_C::~CGUnit_C() {
    if (this->m_mountSound) {
        SI2::StopOrFadeOut(this->m_mountSound, 1, 0.0f, 1);
        this->m_mountSound->~SOUNDKITOBJECT();
        STORM_FREE(this->m_mountSound);
        this->m_mountSound = nullptr;
    }

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

// ref: FUN_0073fcc0
// PARTIAL: the movement start (FUN_006ea520, FUN_0074d070, FUN_0074b380), the collision box
// (FUN_00725f50), the stand state's posture (FUN_006e9920), the name plate (FUN_007e5f60), the
// party and raid slots (FUN_005139b0, FUN_0054d1c0), the vehicle and passenger starts and the
// ObjectEffect package (FUN_00725df0) are the Movement, PlayerName, party and ObjectEffect ports'.
void CGUnit_C::PostInit(uint32_t time, const CClientObjCreate& init, bool a4) {
    this->m_displayScale = this->GetDisplayScale(this->m_unit->displayID);

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

    // TODO
}

void CGUnit_C::PostMovementUpdate(const CClientMoveUpdate& move, int32_t activeMover) {
    // TODO
}

// The half-extent of the CURRENT animation's authored box, which is what the blob shadow uses as
// the caster footprint -- that is why a footprint grows as something rears up and shrinks as it
// crouches, instead of being a fixed circle.
//
// This used to walk m_data->sequences itself, matching m_animSeq against sequences[i].id, and
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

    if (this->m_animSeq < 0) {
        return 0.0f;
    }

    // GetSequenceInfo blocks in WaitForLoad when the model itself is not loaded yet. This runs
    // inside the per-frame shadow pass, so take the miss and let the caller use the model box
    // rather than stall a frame on a disc read.
    if (!model->m_loaded) {
        return 0.0f;
    }

    M2SequenceInfo info = {};
    model->GetSequenceInfo(static_cast<uint32_t>(this->m_animSeq), 0, info);

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

uint32_t CGUnit_C::GetSequenceDuration(int32_t animID) {
    if (!this->m_model || !this->m_model->m_shared || !this->m_model->m_shared->m_m2DataLoaded || !this->m_model->m_shared->m_data) {
        return 0;
    }

    auto& sequences = this->m_model->m_shared->m_data->sequences;

    for (uint32_t i = 0; i < sequences.Count(); i++) {
        if (sequences[i].id == animID) {
            return sequences[i].duration;
        }
    }

    return 0;
}

void CGUnit_C::UpdateIdleAnimation() {
    if (!this->m_model) {
        return;
    }

    // Same priority order the reference uses for a unit's looping pose: a dead unit holds Dead;
    // otherwise a scripted stand state (UNIT_FIELD_BYTES_1 byte 0) picks the matching sit/sleep/
    // kneel/submerged loop; otherwise an emote state resolves through Emotes.dbc -> AnimationData id;
    // otherwise plain Stand. Animation ids are AnimationData.dbc entries SetBoneSequence resolves.
    auto unitData = this->Unit();
    uint32_t now = this->m_model && this->m_model->m_scene ? this->m_model->m_scene->m_time : 0;
    bool dead = unitData && unitData->maxHealth > 0 && unitData->health <= 0;
    int32_t standState = unitData ? (unitData->bytes1 & 0xFF) : 0;
    int32_t seq;

    if (dead) {
        // Match the reference's death handling. A unit already dead when first seen (a corpse placed
        // in the world) settles straight into the Dead pose. A unit that dies while we are watching
        // plays the Death fall (AnimationData 1) once, then settles into Dead (6) when it ends -- we
        // time the fall off the model's own Death duration rather than assuming a non-looping hold.
        if (!this->m_wasDead && this->m_animSeq != -1) {
            uint32_t deathDuration = this->GetSequenceDuration(1);

            if (deathDuration > 0) {
                seq = 1; // Death fall
                this->m_deathStartTime = now;
                this->m_deathDuration = deathDuration;
            } else {
                seq = 6; // model has no Death animation -> straight to Dead
            }
        } else if (this->m_animSeq == 1 && (now - this->m_deathStartTime) < this->m_deathDuration) {
            seq = 1; // still falling
        } else {
            seq = 6; // Dead (settled, or spawned as a corpse)
        }

        this->m_wasDead = true;
    } else {
        this->m_wasDead = false;

        if (standState != 0) {
            switch (standState) {
                case 1: seq = 97; break;   // SIT              -> SitGround
                case 2: seq = 103; break;  // SIT_CHAIR        -> SitChairMed
                case 3: seq = 100; break;  // SLEEP            -> Sleep
                case 4: seq = 102; break;  // SIT_LOW_CHAIR    -> SitChairLow
                case 5: seq = 103; break;  // SIT_MEDIUM_CHAIR -> SitChairMed
                case 6: seq = 104; break;  // SIT_HIGH_CHAIR   -> SitChairHigh
                case 7: seq = 6; break;    // DEAD             -> Dead
                case 8: seq = 115; break;  // KNEEL            -> KneelLoop
                case 9: seq = 202; break;  // SUBMERGED        -> Submerged
                default: seq = 0; break;
            }
        } else if (this->m_emoteSeq && now < this->m_emoteEndMs) {
            // A one-shot emote outranks the resting pose while it is still running, but NOT death
            // or a scripted stand state -- a unit that is sitting or dead should not be interrupted
            // by a gesture.
            seq = this->m_emoteSeq;
        } else if (unitData && unitData->emoteState) {
            auto emote = g_emotesDB.GetRecord(unitData->emoteState);
            seq = (emote && emote->m_animID > 0) ? emote->m_animID : 0;
        } else {
            seq = 0; // Stand
        }
    }

    // A model that has no animation for the requested pose falls back to Stand, the way the
    // reference does: most creatures carry no SitChair/Sleep/Submerged or emote animation, and
    // asking for one leaves them in whatever pose they held.
    if (seq != 0 && !this->GetSequenceDuration(seq)) {
        seq = 0;
    }

    // Only restart the sequence when it actually changes; re-issuing it every frame would keep
    // resetting the animation to frame 0 and freeze it.
    if (this->m_emoteSeq && now >= this->m_emoteEndMs) {
        this->m_emoteSeq = 0;
    }

    if (seq != this->m_animSeq) {
        this->m_model->SetBoneSequence(-1, seq, -1, 0, 1.0f, 0, 1);
        this->m_animSeq = seq;
    }
}

// SMSG_EMOTE: play an emote once.
//
// Emotes.dbc maps the emote id to an AnimationData id, the same table UNIT_NPC_EMOTESTATE resolves
// through. A model that has no animation for it is left alone rather than snapped to Stand.
void CGUnit_C::PlayEmote(uint32_t emoteID) {
    auto emote = g_emotesDB.GetRecord(static_cast<int32_t>(emoteID));

    if (!emote || emote->m_animID <= 0) {
        return;
    }

    uint32_t duration = this->GetSequenceDuration(emote->m_animID);

    if (!duration) {
        return;
    }

    uint32_t now = this->m_model && this->m_model->m_scene ? this->m_model->m_scene->m_time : 0;

    this->m_emoteSeq = emote->m_animID;
    this->m_emoteEndMs = now + duration;
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

    auto object = ClntObjMgrObjectPtr(guid, TYPE_UNIT, __FILE__, __LINE__);

    if (!object) {
        return 1;
    }

    static_cast<CGUnit_C*>(object)->PlayEmote(emoteID);

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
        && ((static_cast<CGGameObject_C*>(target)->GameObject()->bytes1 >> 8) & 0xFF) == 0x11) {
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

        // The reference then calls FUN_0073b510 here (1392 bytes), which picks the animation that
        // follows the one that just ended. It is not ported, so the follow-up is whatever the next
        // UpdateAnimation chooses; the flag handling around it, which is what unblocks the chooser,
        // is here.

        this->m_animFlags |= 0x70;
    } else if (animID == 0x27 || animID == 0xBB) {
        // A jump that was cut short is no longer in progress.
        this->m_animFlags &= ~0x4;
    }
}

// ref: FUN_0073c140
void CGUnit_C::OnSequenceDone(CM2Model* model, uint32_t boneId, uint32_t animID, int32_t a4,
                              int32_t interrupted, WOWGUID owner) {
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
// PHASE4: the reference also re-runs the unit's scale (FUN_0072cbb0, when the template has a
// family) and its combat reach (FUN_00725f50) against the template, and does the name plate and
// party slot steps OnNameArrived lists. Those are the Unit_C port's.
void CGUnit_C::OnCreatureStatsArrived(uint32_t id, const WOWGUID* guid, void* param, bool found) {
    auto stats = g_creatureCache.Peek(DBCACHEKEY32(id));

    if (!stats) {
        return;
    }

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(*guid, TYPE_UNIT, __FILE__, __LINE__));

    if (unit) {
        unit->m_creatureStats = stats;
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

// ref: FUN_00735bb0
// PHASE4(Vehicle_C): the reference repeats the change on every passenger in the vehicle's seat
// list (+0x170/+0x178), which CVehicle_C does not carry yet; nothing creates a vehicle, so the walk
// has nothing to reach.
void CGUnit_C::SetBoneSequenceTimeOnPassengers(CM2Model* model, uint32_t boneId, int32_t time, int32_t fromPassenger) {
    if (!model) {
        return;
    }

    if (!fromPassenger && this->m_vehiclePassenger && this->m_vehiclePassenger->IsRidingLiveVehicle()) {
        return;
    }

    model->SetBoneSequenceTime(boneId, time);
}

// ref: FUN_00735dd0
// PHASE4(Vehicle_C): the passenger walk, as in SetBoneSequenceTimeOnPassengers.
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
// PHASE4(Vehicle_C): a vehicle with passengers keeps its mount (m_stateFlags 0x10000000) while
// its seat list is not empty; CVehicle_C carries no seat list yet, so that test reads empty.
// PHASE4(ObjectEffect): the reference ends with FUN_00725df0, the unit's ObjectEffect package.
void CGUnit_C::SetMountDisplay(int32_t displayID) {
    if (displayID == this->m_mountDisplayID) {
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
}

// ref: FUN_0073d5d0
// PHASE4(Unit_C): the reference gives the mount the unit's anim-event handler (FUN_00734a40,
// which forwards to FUN_00732650); frozen has no unit anim-event handler yet.
// PHASE4(Player_C): a player mounting where the mount does not fit (FUN_006d7720 against the
// model data's mount height) dismounts at once.
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

        (void)displayID;
        (void)checkCollision;
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
// PHASE4(Movement): `restoreCollision` re-runs the unit's collision box (FUN_00725f50).
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

    (void)restoreCollision;

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
// PHASE4(Vehicle_C): the seat-list test, as in SetMountDisplay.
// PHASE4(ObjectEffect): FUN_00725df0 at the end, as in SetMountDisplay.
void CGUnit_C::RequestDismount() {
    if (this->GetGUID() != ClntObjMgrGetActivePlayer() || (this->m_unit->flags >> 20) & 1) {
        return;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_CANCEL_MOUNT_AURA));
    msg.Finalize();
    ClientServices::Send(&msg);

    if (this->m_mountDisplayID) {
        this->m_stateFlags &= ~0x10000000u;
        this->Dismount(1);
        this->m_mountDisplayID = 0;
        this->UpdateMountSound();
        this->AttachQuestMarker();
        PlayerNameInvalidate(this->m_nameDesc);
    }
}

// ref: FUN_0071e5b0
// PHASE4(ObjectEffect): everything past the first test drives the unit's ObjectEffect manager
// (+0xcc), which nothing creates until ObjectEffect.cpp is ported; with none, the reference
// returns here too.
void CGUnit_C::UpdateObjectEffects() {
    if (!this->m_objectEffects) {
        return;
    }
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
// PHASE4(Vehicle_C): a passenger is placed by its seat (FUN_0074a7f0).
// PHASE4(Unit_C): a swimming or flying unit pitches with its movement (FUN_00719b80 for the active
// mover, FUN_00719a90 for the rest), a unit leading a mount transition is placed along it
// (FUN_007193f0), and the transition's model follows the unit (FUN_0071fbf0). Those land with the
// movement smoothing they read; until then every unit takes the ground placement below.
int32_t CGUnit_C::PlaceModel(float elapsed) {
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
int32_t OnDynamicFlagsChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto unit = HandlerUnit(guid, 0x3eb);

    if (unit) {
        unit->UpdateAnimation(0, 0xFFFFFFFF);
    }

    return 1;
}

// ref: FUN_00741d00
// PARTIAL: the reference registers 30 handlers here; each joins as its body is ported. Still to
// come: level (0xc0, FUN_00730050), entry (object 0xc,
// FUN_0072ceb0), health (0x48, FUN_0073f330), power and max power (0x4c/0x6c, FUN_007234d0 /
// FUN_007235c0), power type (0x47, FUN_00723620), aura state (0xdc, FUN_00716810), flags and flags 2
// (0xd4/0xd8, FUN_0073f270 / FUN_0073f2b0), visibility (0x112, FUN_0073f2f0), PvP (0x1d1,
// FUN_00728d20), faction (0xc4, FUN_00723680), charm and summon (0x18, FUN_00728d60), stand state
// (0x110, FUN_0073f460), NPC flags (0x130, FUN_0071c9d0), pet name
// timestamp (0x118, FUN_0072cf70), 0x124 (FUN_00741a00), channel (0x38, FUN_0073f4f0), pet number
// (0x114, FUN_0072cff0), scale (object 0x10, FUN_0072d070) and hover height (0x230, FUN_0071ca10).
void RegisterUnitFieldHandlers() {
    for (uint32_t offset = 0xC8; offset < 0xD4; offset += 4) {
        MirrorRegisterHandler(ID_UNIT, offset, 4, &OnVirtualItemChanged, nullptr, 0, 0);
    }

    MirrorRegisterHandler(ID_UNIT, 0xF4, 4, &OnDisplayChanged, nullptr, 1, 0);
    MirrorRegisterHandler(ID_UNIT, 0x1D0, 1, &OnSheathChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0x113, 1, &OnAnimTierChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0xFC, 4, &OnMountDisplayChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0x134, 4, &OnDynamicFlagsChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_UNIT, 0x30, 8, &OnTargetChanged, nullptr, 0, 0);
}

} // namespace

// ref: FUN_00742220
// The reference undoes this at the end of a game (FUN_00742bb0, from FUN_00406510); frozen has no
// end of game yet, so a second game in the same session registers nothing twice.
void UnitInitialize() {
    static bool registered = false;

    if (!registered) {
        RegisterUnitFieldHandlers();
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
        uint32_t now = CWorld::GetTickTimeMs();

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
// PHASE4(Vehicle_C): an animated draw repeats its bone release on every live passenger.
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
            if (this->m_model->IsLoaded(0, 0) && this->m_model->BoneHasParent(3)) {
                this->m_model->UnsetBoneSequence(3, 1, 1);
            }

            if (this->m_model->IsLoaded(0, 0) && this->m_model->BoneHasParent(2)) {
                this->m_model->UnsetBoneSequence(2, 1, 1);
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

    // FUN_0071c110: a creature of a family (a pet) grows with its level between the family's
    // two scales, and takes that scale when it is the larger, or always when it is a pet.
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

// ref: FUN_0072cbb0
// PHASE4(Vehicle_C): a vehicle re-seats its passengers at the new scale (FUN_00757d10).
void CGUnit_C::UpdateDisplayScale(int32_t keepScale) {
    float previous = this->m_displayScale;
    this->m_displayScale = this->GetDisplayScale(this->m_unit->displayID);

    float scale = keepScale ? this->m_scale : previous / this->m_displayScale * this->m_scale;

    this->SetScaleEase(scale);
    this->UpdateShadowRadius();
    this->UpdateEffectAttachments();
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
// PHASE4(ObjectEffect): the ObjectEffect package follows (FUN_00725df0).
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

    model->SetSequenceDoneCallback(&CGUnit_C::OnSequenceDone, this->GetGUID());

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

    this->m_animFlags &= ~0x8000000u;
}

// ref: FUN_0073e840
// PHASE4(Vehicle_C): a vehicle's or a passenger's seat follows the loaded model (FUN_00757d10,
// FUN_00757e70, FUN_00748620).
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
// PHASE4(VehiclePassenger_C): a passenger whose seat hides it stays hidden (+0xf60).
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
