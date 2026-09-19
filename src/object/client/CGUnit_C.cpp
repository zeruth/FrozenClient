#include "object/client/CGUnit_C.hpp"
#include "component/CCharacterComponent.hpp"
#include "db/Db.hpp"
#include "model/Model2.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Data.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/Game.hpp"
#include <storm/Error.hpp>
#include <tempest/Math.hpp>

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

int32_t CGUnit_C::GetDisplayID() const {
    // Prefer local display ID if set and unit's display ID hasn't been overridden from unit's
    // native display ID.
    if (this->GetLocalDisplayID() && this->GetDisplayID() == this->GetNativeDisplayID()) {
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

float CGUnit_C::GetAnimFootprint() const {
    if (!this->m_model || !this->m_model->m_shared || !this->m_model->m_shared->m_m2DataLoaded || !this->m_model->m_shared->m_data) {
        return 0.0f;
    }

    auto& sequences = this->m_model->m_shared->m_data->sequences;

    if (this->m_animSeq < 0 || !sequences.Count()) {
        return 0.0f;
    }

    // m_animSeq is an AnimationData id, not an index, so find the sequence that carries it.
    for (uint32_t i = 0; i < sequences.Count(); i++) {
        if (sequences[i].id != this->m_animSeq) {
            continue;
        }

        const CAaBox& e = sequences[i].bounds.extent;
        float ex = (e.t.x - e.b.x) * 0.5f;
        float ey = (e.t.y - e.b.y) * 0.5f;
        float extent = ex > ey ? ex : ey;

        // Degenerate per-sequence bounds are common; let the caller fall back to the model box.
        return extent > 0.01f ? extent : 0.0f;
    }

    return 0.0f;
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
    int32_t standState = unitData ? (unitData->pad2 & 0xFF) : 0;
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
