#include "object/client/CGDynamicObject_C.hpp"
#include "db/Db.hpp"
#include "model/CM2Model.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/SpellVisuals.hpp"
#include "sound/SI2.hpp"
#include "sound/SOUNDKITOBJECT.hpp"
#include "sound/SoundKitProperties.hpp"
#include "ui/game/CGCamera.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include <storm/Memory.hpp>
#include <tempest/Sphere.hpp>
#include <cmath>
#include <cstring>
#include <new>

const SpellVisualRec* GetSpellVisual(const SpellRec* spell);

namespace {

// FUN_00804cc0: whether the caster's cast of the spell at that time is still waiting to go off
// (the spell's pending-cast list, DAT_00af5278).
// TODO(Spell_C): frozen keeps no pending-cast list, so nothing is ever waiting -- what the
// reference answers with that list empty.
int32_t SpellIsCastPending(WOWGUID caster, int32_t spellID, uint32_t castTime) {
    (void)caster;
    (void)spellID;
    (void)castTime;

    return 0;
}

// ref: FUN_00704c30
// The model's events: "$SND" plays a sound kit at the event, "$SHK" its three camera shakes.
void PlayAreaEvent(uint32_t eventId, uint32_t eventData, const C3Vector* position) {
    if (eventId == 0x444e5324) {
        SI2::PlaySoundKit(static_cast<int32_t>(eventData), position, nullptr, nullptr, 0, nullptr, 1, 0);
        return;
    }

    if (eventId != 0x4b485324) {
        return;
    }

    auto shakes = g_spellEffectCameraShakesDB.GetRecord(static_cast<int32_t>(eventData));

    if (!shakes) {
        return;
    }

    for (auto shake : shakes->m_cameraShake) {
        if (auto camera = CGWorldFrame::GetActiveCamera()) {
            camera->AddShakeByID(shake, *position);
        }
    }
}

} // namespace

// ref: FUN_00704b30
const SpellVisualEffectNameRec* DynamicObjectGetEffectName(const SpellRec* spell) {
    if (!spell) {
        // "NOSPELLIDFOUND|%d" (SysMsgPrintf) in the reference.
        return nullptr;
    }

    auto visual = GetSpellVisual(spell);

    if (!visual) {
        // "SPELLVISUALIDNOTFOUND|%d|%d"
        return nullptr;
    }

    auto kit = g_spellVisualKitDB.GetRecord(visual->m_persistentAreaKit);

    if (!kit) {
        // "SPELLVISUALKITIDNOTFOUND|%d"
        return nullptr;
    }

    if (auto effect = g_spellVisualEffectNameDB.GetRecord(kit->m_worldEffect)) {
        return effect;
    }

    // "SPELLEFFECTIDNOTFOUND|%d" when this fails too.
    return g_spellVisualEffectNameDB.GetRecord(kit->m_baseEffect);
}

// ref: FUN_007053a0
CGDynamicObject_C::CGDynamicObject_C(uint32_t time, CClientObjCreate& objCreate)
    : CGObject_C(time, objCreate)
    , m_passenger(objCreate.move.status.transport, objCreate.move.status.position18, this->m_obj->m_guid) {
    this->m_passenger.m_facing = objCreate.move.status.facing24;
    this->m_dynamicFlags &= ~0x3u;
    this->m_sound = new (SMemAlloc(sizeof(SOUNDKITOBJECT), __FILE__, __LINE__, 0x8)) SOUNDKITOBJECT();
}

// ref: FUN_007054e0
CGDynamicObject_C::~CGDynamicObject_C() {
    if (this->m_blizzard) {
        // FUN_007f9ee0: the shards stop and the blizzard finishes on its own.
        this->m_blizzard->m_rate = 0.0f;
        this->m_blizzard = nullptr;
    }

    if (this->m_sound) {
        SI2::StopOrFadeOut(this->m_sound, 0, 3.0f, 1);
        this->m_sound->~SOUNDKITOBJECT();
        SMemFree(this->m_sound, __FILE__, __LINE__, 0);
        this->m_sound = nullptr;
    }
}

// ref: FUN_00705230
// The area appears: hidden while its spell is still to go off, its model's events and sequences
// answered, its visuals started; a farsight area the player cast takes the camera.
//
// PHASE4(Player_C): the farsight hand-off (FUN_006e4940 -> FUN_006e2880).
void CGDynamicObject_C::PostInit(uint32_t time, const CClientObjCreate& init, bool a4) {
    this->m_passenger.JoinTransport(init.move.status.position28);

    if (!this->GetTransportGUID()) {
        this->m_passenger.m_facing = init.move.status.facing34;
    }

    this->CGObject_C::PostInit(time, init, a4);

    if (this->IsSpellPending()) {
        this->m_dynamicFlags |= 0x2;
    } else {
        this->m_dynamicFlags &= ~0x2u;
    }

    if (auto model = this->GetObjectModel()) {
        model->SetAnimEventCallback(&CGDynamicObject_C::AnimEventCallback, this->GetGUID());
        model->SetSequenceDoneCallback(&CGDynamicObject_C::SequenceDoneCallback, this->GetGUID());

        if (this->m_dynamicFlags & 0x2) {
            model->SetParticleEmission(0);
            model->SetRibbonFlag8(0);
        }
    }

    this->StartAreaVisuals();
}

void CGDynamicObject_C::SetStorage(uint32_t* storage, uint32_t* saved) {
    this->CGObject_C::SetStorage(storage, saved);

    this->m_dynamicObj = reinterpret_cast<CGDynamicObjectData*>(&storage[CGDynamicObject::GetBaseOffset()]);
    this->m_dynamicObjSaved = &saved[CGDynamicObject::GetBaseOffsetSaved()];
}

int32_t CGDynamicObject_C::IsSpellPending() const {
    auto data = this->DynamicObject();

    return SpellIsCastPending(data->caster, data->spellID, data->castTime);
}

// ref: FUN_007051b0
// Each frame: an area waiting on its spell shows once the spell has gone -- its particles and
// ribbons come on, it plays its loop and its visuals start.
void CGDynamicObject_C::UpdateForFrame(uint32_t time) {
    if (!(this->m_dynamicFlags & 0x2) || this->IsSpellPending()) {
        return;
    }

    this->m_dynamicFlags &= ~0x2u;

    if (auto model = this->GetObjectModel()) {
        model->SetParticleEmission(1);
        model->SetRibbonFlag8(1);
        model->SetBoneSequence(0xffffffff, 0x7f, 0xffffffff, 0, 1.0f, 1, 1);

        this->StartAreaVisuals();
    }
}

// ref: FUN_00704a00
// The loop after any sequence: the spell's own (0x9e) when the model has it, else Stand.
void CGDynamicObject_C::PlayIdle() {
    if (auto model = this->GetObjectModel()) {
        model->SetBoneSequence(0xffffffff, (this->m_dynamicFlags & 0x1) ? 0x9e : 0, 0xffffffff, 0, 1.0f, 1, 1);
    }
}

// ref: FUN_00704f60
// The persistent-area kit's extras: a char proc 9 makes the blizzard of shards (its params the
// area model and the rate), and the kit's sound plays here, fading out over three seconds when it
// is replaced.
void CGDynamicObject_C::StartAreaVisuals() {
    if (this->m_dynamicFlags & 0x2) {
        return;
    }

    auto data = this->DynamicObject();
    auto spell = g_spellDB.GetRecord(data->spellID);
    auto visual = spell ? GetSpellVisual(spell) : nullptr;

    if (!visual) {
        return;
    }

    auto kit = g_spellVisualKitDB.GetRecord(visual->m_persistentAreaKit);

    if (!kit) {
        return;
    }

    for (uint32_t i = 0; i < 4; i++) {
        if (kit->m_charProc[i] == 9) {
            int32_t areaModel = static_cast<int32_t>(std::lround(kit->m_charParamZero[i])) & 0xff;
            this->m_blizzard = BlizzardObject::Create(this->GetRawPosition(), data->radius, areaModel, kit->m_charParamOne[i]);
            break;
        }
    }

    if (kit->m_soundID) {
        SI2::StopOrFadeOut(this->m_sound, 0, 3.0f, 1);

        C3Vector position = this->GetPosition();

        SoundKitProperties properties;
        properties.ResetToDefaults();

        float two = 2.0f;
        std::memcpy(&properties.uint10, &two, sizeof(properties.uint10));
        properties.uint1c = 1;

        SI2::PlaySoundKit(kit->m_soundID, &position, this->m_sound, &properties, 0, nullptr, 1, 0);
    }
}

// ref: FUN_00704940
void CGDynamicObject_C::Disable() {
    this->CGObject_C::Disable();

    this->m_passenger.m_transportLink.Unlink();

    if (this->m_blizzard) {
        this->m_blizzard->m_rate = 0.0f;
        this->m_blizzard = nullptr;
    }

    SI2::StopOrFadeOut(this->m_sound, 0, 3.0f, 1);
}

// ref: FUN_00704990
void CGDynamicObject_C::Reenable() {
    this->CGObject_C::Reenable();

    if (this->IsSpellPending()) {
        this->m_dynamicFlags |= 0x2;
    } else {
        this->m_dynamicFlags &= ~0x2u;
    }
}

// ref: FUN_007050e0
void CGDynamicObject_C::PostReenable() {
    this->StartAreaVisuals();
    this->m_passenger.LeaveTransport();
    this->CGObject_C::PostReenable();
}

// ref: FUN_00704a70
// Going out of range, an area whose model has a despawn (0x9f) is held until it has played it.
void CGDynamicObject_C::HandleOutOfRange(OUT_OF_RANGE_TYPE type) {
    if (type != 1) {
        return;
    }

    auto model = this->GetObjectModel();

    if (!model || !model->IsLoaded(0, 0) || !model->HasSequence(0x9f)) {
        return;
    }

    ClntObjMgrLockObject(this->GetGUID());
    model->SetSequenceDoneCallback(&CGDynamicObject_C::DespawnDoneCallback, this->GetGUID());
    model->SetBoneSequence(0xffffffff, 0x9f, 0xffffffff, 0, 1.0f, 1, 1);
}

// ref: FUN_007064e0 (shared with the corpse's slot)
C3Vector CGDynamicObject_C::GetPosition() const {
    return this->m_passenger.GetPosition(this->m_passenger.m_position);
}

C3Vector CGDynamicObject_C::GetRawPosition() const {
    return this->m_passenger.m_position;
}

float CGDynamicObject_C::GetFacing() const {
    return this->m_passenger.GetFacing(this->m_passenger.m_facing);
}

float CGDynamicObject_C::GetRawFacing() const {
    return this->m_passenger.m_facing;
}

// ref: FUN_007054d0
float CGDynamicObject_C::GetBaseScale() const {
    return this->m_areaScale;
}

WOWGUID CGDynamicObject_C::GetTransportGUID() const {
    return this->m_passenger.m_transportGUID;
}

// ref: FUN_00704910
void CGDynamicObject_C::SetParticleRelative(const C44Matrix* relative) {
    this->CGObject_C::SetParticleRelative(relative);

    if (this->m_blizzard) {
        // FUN_007fa680
        this->m_blizzard->SetTransport(relative);
    }
}

// ref: FUN_00705100
int32_t CGDynamicObject_C::GetModelFileName(const char*& name) const {
    auto spell = g_spellDB.GetRecord(this->DynamicObject()->spellID);

    if (!spell) {
        // "NOSPELLIDFOUND|%d"
        return 0;
    }

    auto effect = DynamicObjectGetEffectName(spell);

    if (!effect) {
        // "NOOBJECTFILENAME|%d|Dynamic"
        return 0;
    }

    name = effect->m_fileName;

    return name != nullptr;
}

// ref: FUN_00704d90
// The area's model is scaled to the area: by its own bounding sphere, or by the effect's
// authored area size when the sphere is degenerate; the effect's scale on top. A portal (1) or a
// farsight area (2) keeps its size.
void CGDynamicObject_C::OnModelLoaded(CM2Model* model) {
    this->CGObject_C::OnModelLoaded(model);

    if (model->HasSequence(0x9e)) {
        this->m_dynamicFlags |= 0x1;
    } else {
        this->m_dynamicFlags &= ~0x1u;
    }

    auto data = this->DynamicObject();
    auto spell = g_spellDB.GetRecord(data->spellID);
    auto effect = DynamicObjectGetEffectName(spell);

    if (spell && (spell->m_attributesEx5 & 0x40000000)) {
        model->m_flag4 = 1;
    }

    uint8_t type = data->bytes & 0xff;
    bool scaled = true;

    if (type != 2 && type != 1) {
        CAaSphere sphere;
        model->GetBoundingSphere(sphere);

        if (sphere.r <= 0.001f) {
            if (!effect) {
                scaled = false;
            } else if (0.0f < effect->m_areaEffectSize) {
                this->m_areaScale = data->radius / effect->m_areaEffectSize;
            }
        } else {
            this->m_areaScale = data->radius / sphere.r;
        }
    }

    if (scaled) {
        if (effect && 0.0f < effect->m_scale) {
            this->m_areaScale = effect->m_scale * this->m_areaScale;
        }
    }

    this->m_scaleMultiplier = this->m_areaScale * this->m_scaleMultiplier;

    if (effect) {
        this->SetAlpha(1.0f, 0);
    }

    if (model->HasSequence(0x7f)) {
        model->SetBoneSequence(0xffffffff, 0x7f, 0xffffffff, 0, 1.0f, 1, 1);
    }
}

// ref: FUN_007049d0
void CGDynamicObject_C::GetHidden(uint32_t flags, int32_t* hidden, int32_t* hiddenOther) {
    this->CGObject_C::GetHidden(flags, hidden, hiddenOther);

    if (this->m_dynamicFlags & 0x2) {
        *hiddenOther = 1;
    }
}

// ref: FUN_00704cc0
// At its position turned by its facing, carried by its transport when it has one.
void CGDynamicObject_C::GetWorldMatrix(C44Matrix& matrix) const {
    matrix = C44Matrix();

    C3Vector position = this->GetRawPosition();
    matrix.d0 = position.x;
    matrix.d1 = position.y;
    matrix.d2 = position.z;

    matrix.RotateAroundZ(this->GetRawFacing());

    if (WOWGUID transport = this->GetTransportGUID()) {
        if (auto object = ClntObjMgrObjectPtr(transport, TYPE_OBJECT, ".\\DynamicObject_C.cpp", 0x199)) {
            C44Matrix carrier;
            object->GetWorldMatrix(carrier);
            matrix *= carrier;
        }
    }
}

// ref: FUN_00704f20
void CGDynamicObject_C::AnimEventCallback(CM2Model* model, uint32_t boneId, uint32_t eventId, uint32_t eventData,
                                          const C3Vector* position, uint32_t a6, WOWGUID owner) {
    if (ClntObjMgrObjectPtr(owner, TYPE_DYNAMICOBJECT, ".\\DynamicObject_C.cpp", 0x90)) {
        PlayAreaEvent(eventId, eventData, position);
    }
}

// ref: FUN_00704af0
void CGDynamicObject_C::SequenceDoneCallback(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4,
                                             int32_t a5, WOWGUID owner) {
    if (a4 != 0) {
        return;
    }

    if (auto object = static_cast<CGDynamicObject_C*>(ClntObjMgrObjectPtr(owner, TYPE_DYNAMICOBJECT, ".\\DynamicObject_C.cpp", 0x9d))) {
        object->PlayIdle();
    }
}

// ref: FUN_00704a40
// The despawn has played: the area is let go.
void CGDynamicObject_C::DespawnDoneCallback(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4,
                                            int32_t a5, WOWGUID owner) {
    if (owner) {
        ClntObjMgrUnlockObject(owner, ".\\DynamicObject_C.cpp", 0x28);
    }
}
