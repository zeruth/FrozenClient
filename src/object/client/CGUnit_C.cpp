#include "object/client/CGUnit_C.hpp"
#include "component/CCharacterComponent.hpp"
#include "db/Db.hpp"
#include "model/Model2.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Data.hpp"
#include "object/client/NameCache.hpp"
#include "object/client/CEffect.hpp"
#include "object/client/CVehicle_C.hpp"
#include "object/client/CVehiclePassenger_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/Game.hpp"
#include "ui/game/CGPartyInfo.hpp"
#include "ui/game/CGRaidInfo.hpp"
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
    // TODO

    this->RefreshDataPointers();

    // TODO
}

CGUnit_C::~CGUnit_C() {
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

bool CGUnit_C::BuildNpcCharacterComponent() {
    if (!this->m_model) {
        return false;
    }

    auto disp = g_creatureDisplayInfoDB.GetRecord(this->GetDisplayID());

    if (!disp || disp->m_extendedDisplayInfoID <= 0) {
        return false; // an ordinary creature model, not a character
    }

    auto extra = g_creatureDisplayInfoExtraDB.GetRecord(disp->m_extendedDisplayInfoID);

    if (!extra) {
        return false;
    }

    // Dress it exactly like a player character, but from the creature's extended (character) data.
    ComponentData data;
    data.raceID = extra->m_displayRaceID;
    data.sexID = extra->m_displaySexID;
    data.classID = 1; // class does not affect appearance; a valid value keeps the component happy
    data.skinColorID = extra->m_skinID;
    data.faceID = extra->m_faceID;
    data.hairStyleID = extra->m_hairStyleID;
    data.hairColorID = extra->m_hairColorID;
    data.facialHairStyleID = extra->m_facialHairID;

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
        return false;
    }

    this->m_characterComponent->Init(&data, nullptr);

    // NPCItemDisplay[11] holds ItemDisplayInfo ids for the visible armour slots, in this order.
    static const int32_t s_slotInvType[11] = {
        1,  // head
        3,  // shoulders
        4,  // shirt (body)
        5,  // chest
        6,  // waist
        7,  // legs
        8,  // feet
        9,  // wrists
        10, // hands
        16, // back (cloak)
        19  // tabard
    };

    for (int32_t i = 0; i < 11; i++) {
        int32_t displayID = extra->m_npcitemDisplay[i];

        if (displayID > 0) {
            this->m_characterComponent->AddItemByInventoryType(s_slotInvType[i], displayID);
        }
    }

    this->m_characterComponent->RenderPrep(1);
    this->m_model->IsDrawable(1, 1);

    return true;
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

void CGUnit_C::PostInit(uint32_t time, const CClientObjCreate& init, bool a4) {
    // TODO

    this->CGObject_C::PostInit(time, init, a4);

    // TODO

    if (this->m_displayInfo) {
        CCharacterComponent::ApplyMonsterGeosets(this->m_model, this->m_displayInfo);
        CCharacterComponent::ReplaceMonsterSkin(this->m_model, this->m_displayInfo, this->m_modelData);

        if (this->m_modelData) {
            this->m_model->m_flag4 = (this->m_modelData->m_flags & 0x200) ? true : false;
        }
    }

    // TODO

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

    auto info = NameCacheGetCreatureInfo(this->GetEntryID());

    if (!info || info->subName.empty()) {
        return nullptr;
    }

    return info->subName.c_str();
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

    auto info = NameCacheGetCreatureInfo(this->GetEntryID());

    if (info) {
        return info->type;
    }

    // No creature template means a player, whose type comes from its race instead.
    auto data = this->Unit();
    auto race = data ? g_chrRacesDB.GetRecord(static_cast<int32_t>(data->bytes0 & 0xFF)) : nullptr;

    return (race && race->m_creatureType >= 1) ? race->m_creatureType : 0;
}

// ref: FUN_007153e0
int32_t CGUnit_C::GetCreatureFamily() const {
    auto info = NameCacheGetCreatureInfo(this->GetEntryID());

    return info ? info->family : 0;
}

// ref: FUN_00718a00
int32_t CGUnit_C::GetClassification() const {
    auto data = this->Unit();

    // A pet reports normal whatever it was tamed from, so taming an elite does not hand the player
    // an elite pet frame. The reference gates on the descriptor's pet number for exactly this.
    if (!data || data->petNumber != 0) {
        return 0;
    }

    auto info = NameCacheGetCreatureInfo(this->GetEntryID());

    return info ? info->classification : 0;
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

    // Creature stats

    if (this->GetType() == HIER_TYPE_UNIT) {
        // TODO load creature stats
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
    auto info = NameCacheGetCreatureInfo(this->GetEntryID());

    if (info) {
        return (info->typeFlags >> 11) & 1;
    }

    return 0;
}

// ref: FUN_00715f90
uint32_t CGUnit_C::GetCreatureTypeFlag12() const {
    auto info = NameCacheGetCreatureInfo(this->GetEntryID());

    if (info) {
        return (info->typeFlags >> 12) & 1;
    }

    return 0;
}

// ref: FUN_00715df0
uint32_t CGUnit_C::GetCreatureTypeFlag26() const {
    auto info = NameCacheGetCreatureInfo(this->GetEntryID());

    if (info) {
        return (info->typeFlags >> 26) & 1;
    }

    return 0;
}

// ref: FUN_00715e50
int32_t CGUnit_C::GetCreatureSkinningType() const {
    auto info = NameCacheGetCreatureInfo(this->GetEntryID());

    if (info) {
        if (info->typeFlags & 0x100) {
            return 1;
        }

        if (info->typeFlags & 0x200) {
            return 2;
        }

        if (info->typeFlags & 0x8000) {
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

    auto info = NameCacheGetCreatureInfo(this->GetEntryID());

    if (!info) {
        return 1;
    }

    return (info->typeFlags >> 10) & 1;
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

    // Passenger propagation goes here (reference 0x73591d): the variation each passenger gets is
    // this model's current one, or -1 when the sequence has fewer variations than that.
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

    // Passenger propagation goes here (reference 0x735d3e).
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
