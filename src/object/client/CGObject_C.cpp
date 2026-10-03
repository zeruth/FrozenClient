#include "object/client/CGObject_C.hpp"
#include "component/CCharacterComponent.hpp"
#include "console/CVar.hpp"
#include "db/Db.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/RenderState.hpp"
#include "gx/Texture.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2Scene.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Data.hpp"
#include "model/Model2.hpp"
#include "object/Types.hpp"
#include "object/client/CEffect.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "ui/game/CGMinimapFrame.hpp"
#include "object/client/CGCorpse_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/CMovement_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/SpellVisuals.hpp"
#include "sound/SI2.hpp"
#include "ui/game/CGCamera.hpp"
#include "ui/game/CGGameUI.hpp"
#include "ui/game/CGPetInfo.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "ui/game/PlayerName.hpp"
#include "world/DayNightLight.hpp"
#include "world/Shadow.hpp"
#include "world/World.hpp"
#include <common/Handle.hpp>
#include <common/Time.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <cmath>

const SpellVisualRec* GetSpellVisual(const SpellRec* spell);

WOWGUID CGObject_C::s_highlightGUID;

namespace {

// The quest-giver marker kinds (0x00adac98): the model, its scale, and the file it is made from.
struct QuestMarker {
    CM2Model* model;
    float scale;
    const char* fileName;
};

QuestMarker s_questMarkers[12] = {
    { nullptr, 1.0f, nullptr },
    { nullptr, 1.0f, "Interface\\Buttons\\TalkToMe.mdx" },
    { nullptr, 1.0f, "Interface\\Buttons\\TalkToMeExclamation.mdx" },
    { nullptr, 1.0f, "Interface\\Buttons\\TalkToMeQuestion.mdx" },
    { nullptr, 1.0f, "Interface\\Buttons\\TalkToMeQuestion_Grey.mdx" },
    { nullptr, 1.0f, "Interface\\Buttons\\TalkToMeExclamation_Grey.mdx" },
    { nullptr, 1.0f, "Interface\\Buttons\\TalkToMeQuestion_White.mdx" },
    { nullptr, 1.0f, "Interface\\Buttons\\TalkToMeExclamation_Blue.mdx" },
    { nullptr, 1.0f, "Interface\\Buttons\\TalkToMeQuestion_Blue.mdx" },
    { nullptr, 1.0f, "Interface\\Buttons\\TalkToMeExclamation_Yellow.mdx" },
    { nullptr, 1.0f, "Interface\\Buttons\\TalkToMeQuestion_Yellow.mdx" },
    { nullptr, 1.0f, "Interface\\Buttons\\TalkToMeExclamation_Orange.mdx" },
};

// Which marker each quest-giver status shows (0x00a34f5c).
const int32_t s_questStatusMarker[11] = { 0, 6, 1, 2, 3, 5, 10, 11, 4, 9, 9 };

// The marker the frame shows quest markers with (0x00ac80a8).
int32_t s_showQuestMarkers = 1;

// The selection circle (0x00ca12c4) and its CVar (0x00ca12c8).
HTEXTURE s_selectionTexture;
CVar* s_selectionCircleCVar;

CEffect* EffectFromOwner(WOWGUID owner) {
    return reinterpret_cast<CEffect*>(static_cast<uintptr_t>(owner));
}

WOWGUID OwnerFromEffect(CEffect* effect) {
    return static_cast<WOWGUID>(reinterpret_cast<uintptr_t>(effect));
}

// ref: FUN_007440b0
int32_t UpdateVisibleWorldObject(WOWGUID guid, void* param) {
    auto object = ClntObjMgrObjectPtr(guid, TYPE_OBJECT, ".\\Object_C.cpp", 0x43e);

    if (object) {
        object->UpdateWorldObject(0);
    }

    return 1;
}

// ref: FUN_00744e10
int32_t ScaleEaseFinished(WOWGUID guid, void* param) {
    auto object = ClntObjMgrObjectPtr(guid, TYPE_OBJECT, ".\\Object_C.cpp", 0x315);

    if (object) {
        object->SetScaleEase(*static_cast<float*>(param));
    }

    return 1;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Construction
// ------------------------------------------------------------------------------------------------

// ref: FUN_00745e60
CGObject_C::CGObject_C(uint32_t time, CClientObjCreate& objCreate)
    : m_lockCount(0), m_disabled(0), m_inReenable(0), m_postInited(0), m_flag19(0),
      m_disablePending(0), m_flag21(0), m_flag22(0), m_unreached(0), m_highlight(0), m_flag27(0) {
    ClntObjMgrLinkInNewObject(this);

    this->m_scale = this->CGObject::GetScale();
}

// ref: FUN_00745f90
CGObject_C::~CGObject_C() {
    if (this->m_worldObject) {
        CWorld::RemoveObject(this->m_worldObject);
        this->m_worldObject = 0;
    }

    this->ReleaseModel();
    this->StopAllEffects(1);

    if (this->m_questMarker) {
        if (this->m_questMarker->m_attachParent) {
            this->m_questMarker->DetachFromParent();
        }

        this->m_questMarker->Release();
        this->m_questMarker = nullptr;
    }

    // PHASE4(ObjectEffect): the ObjectEffect manager at +0xcc is deleted here (FUN_006f7370);
    // nothing creates one until ObjectEffect.cpp is ported.
    this->m_objectEffects = nullptr;
}

// ------------------------------------------------------------------------------------------------
// The vtable, in slot order
// ------------------------------------------------------------------------------------------------

// ref: FUN_00744d20
// The object leaves the world: a camera riding it lets go, the world entry goes (fading when it
// may), the marker goes, and the object is stamped disabled now.
void CGObject_C::Disable() {
    auto camera = CGWorldFrame::GetActiveCamera();

    if (camera && camera->m_relativeTo == this->GetGUID()) {
        camera->SetRelativeTo(0);
    }

    this->RemoveWorldObject();

    if (this->m_questMarker) {
        if (this->m_questMarker->m_attachParent) {
            this->m_questMarker->DetachFromParent();
        }

        this->m_questMarker->Release();
        this->m_questMarker = nullptr;
    }

    this->m_fadeDuration = 0;
    this->m_alphaTo = 0;
    this->m_alpha = 0;

    this->m_highlight = 0;
    this->m_disabled = 1;

    this->m_disableTimeMs = CWorld::GetCurTimeMs();
}

// ref: FUN_00744db0
void CGObject_C::Reenable() {
    this->m_disabled = 0;
    this->m_inReenable = 1;

    this->m_scale = this->GetBaseScale();
    this->m_scaleEaseStart = 0;

    uint32_t duration = this->Virtual0E4() ? 1000 : 0;
    this->SetAlpha(this->GetFadeInAlpha(), duration);
}

// ref: FUN_00743ff0
void CGObject_C::PostReenable() {
    this->OnReenable();

    this->m_inReenable = 0;

    if (!this->m_worldObject && this->GetObjectModel()) {
        this->AddWorldObject();
    }
}

// ref: FUN_007438e0
// Hand the world where the object is: placed and turned and scaled, with its model's bounds and
// the centre of its collision box once the model is in.
void CGObject_C::UpdateWorldObject(int32_t noRelink) {
    if (!this->m_worldObject) {
        return;
    }

    C44Matrix matrix;
    matrix.Identity();
    matrix.Translate(this->GetPosition());
    matrix.RotateAroundZ(this->GetFacing());
    matrix.Scale(this->GetScale());

    CAaBox box = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    CAaSphere sphere = { { 0.0f, 0.0f, 0.0f }, 0.0f };
    C3Vector collisionCenter = { 0.0f, 0.0f, 0.0f };

    CM2Model* model = this->m_model;

    if (model && model->IsLoaded(0, 0)) {
        if (!model->m_shared->m_m2DataLoaded) {
            model->WaitForLoad(nullptr);
        }

        const CAaBox& collision = model->m_shared->m_data->collisionBounds.extent;
        collisionCenter = {
            (collision.t.x + collision.b.x) * 0.5f,
            (collision.t.y + collision.b.y) * 0.5f,
            (collision.t.z + collision.b.z) * 0.5f
        };

        model->GetBoundingBox(box);
        model->GetBoundingSphere(sphere);
    }

    CWorld::UpdateObject(this->m_worldObject, matrix, box, sphere, collisionCenter, noRelink, 0xFFFFFFFF);
}

// ref: FUN_007451b0
// Where a name or marker sits: the model's attachment 0x12 when it has one, else the object's
// height above its position.
C3Vector CGObject_C::GetHeadPosition() const {
    if (this->m_model && this->m_model->HasAttachment(0x12)) {
        return this->m_model->GetAttachmentWorldPosition(0x12);
    }

    C3Vector pos = this->GetPosition();
    pos.z = this->m_height * this->m_scale * 1.25f + pos.z;

    return pos;
}

C3Vector CGObject_C::GetPosition() const {
    return { 0.0f, 0.0f, 0.0f };
}

float CGObject_C::GetFacing() const {
    return 0.0f;
}

float CGObject_C::GetRawFacing() const {
    return this->GetFacing();
}

float CGObject_C::GetBaseScale() const {
    return this->CGObject::GetScale();
}

WOWGUID CGObject_C::GetTransportGUID() const {
    return 0;
}

// ref: FUN_004d5f20
C4Quaternion CGObject_C::GetRotation() const {
    C3Vector up = { 0.0f, 0.0f, 1.0f };

    return C4Quaternion(this->GetFacing(), up);
}

// ref: FUN_00744380
// A model whose creation flags carry bit 0 draws its particles in the given space.
void CGObject_C::SetParticleRelative(const C44Matrix* relative) {
    auto model = this->m_model;

    if (!model || !model->m_loaded) {
        return;
    }

    if (relative) {
        model->m_particleRelativeMatrix = *relative;
        model->SetParticleRelative(&model->m_particleRelativeMatrix);
    } else {
        model->SetParticleRelative(nullptr);
    }
}

// ref: FUN_00745da0
// The marker the status calls for: statuses 2..4 show only while the player is not in a quest
// flow (FUN_0057bf30); an override marker shows when there is no status.
void CGObject_C::UpdateQuestMarker() {
    if (this->m_questMarker) {
        if (this->m_questMarker->m_attachParent) {
            this->m_questMarker->DetachFromParent();
        }

        this->m_questMarker->Release();
        this->m_questMarker = nullptr;
    }

    // The low-level quest statuses (2..4) only show while the minimap tracks them; a status that
    // does not show gives way to the override.
    int32_t marker;

    if (this->m_questStatus != 0
        && (this->m_questStatus - 2 > 2 || CGMinimapFrame::IsTrackingTrivialQuests())) {
        marker = s_questStatusMarker[this->m_questStatus];
    } else {
        marker = static_cast<int32_t>(this->m_questMarkerOverride);
    }

    if (marker == 0) {
        return;
    }

    auto source = s_questMarkers[marker].model;

    this->m_questMarker = source ? CWorld::GetM2Scene()->CreateModelFrom(source, 0) : nullptr;

    if (this->m_questMarker) {
        this->m_questMarker->m_baseAlpha = s_questMarkers[marker].scale;
    }

    this->AttachQuestMarker();
    this->ShowQuestMarker(s_showQuestMarkers);
}

// ref: FUN_00744460
void CGObject_C::AttachQuestMarker() {
    if (!this->m_questMarker || !this->m_model || !this->m_model->IsLoaded(0, 0)) {
        return;
    }

    if (this->m_questMarker->m_attachParent) {
        this->m_questMarker->DetachFromParent();
    }

    if (!this->m_model->HasAttachment(0x12)) {
        return;
    }

    this->m_questMarker->AttachToParent(this->m_model, 0x12, nullptr, 0);

    this->ScaleQuestMarker();
    this->UpdateQuestMarkerSequence();
}

// ref: FUN_007444e0
// The marker keeps the size the name plate wants whatever the model it hangs from is scaled to.
void CGObject_C::ScaleQuestMarker() {
    if (!this->m_questMarker || !this->m_questMarker->m_attachParent || !this->m_model
        || !this->m_model->IsLoaded(0, 0)) {
        return;
    }

    C44Matrix attach = this->m_model->GetAttachmentWorldTransform(this->m_questMarker->m_attachId);

    float lengthSq = attach.a0 * attach.a0 + attach.a2 * attach.a2 + attach.a1 * attach.a1;

    if (!(0.0f < lengthSq)) {
        return;
    }

    float wanted = PlayerNameGetMarkerScale(this) / sqrtf(lengthSq);

    if (fabsf(wanted - 1.0f) < 2.384185791015625e-07f || !(0.0f < wanted)) {
        return;
    }

    C44Matrix matrix = this->m_questMarker->matrixB4;
    float current = sqrtf(matrix.a0 * matrix.a0 + matrix.a2 * matrix.a2 + matrix.a1 * matrix.a1);

    if (9.999999747378752e-05f < current && 9.999999747378752e-05f < fabsf(wanted - current)) {
        C44Matrix scaled;
        scaled.Scale(wanted / current);

        this->m_questMarker->m_flag8000 = 1;
        this->m_questMarker->matrixB4 = scaled;
    }
}

int32_t CGObject_C::GetModelFileName(const char*& name) const {
    return 0;
}

// ref: FUN_004d5f90
float CGObject_C::GetScale() const {
    return this->m_scaleMultiplier * this->m_scale;
}

// ref: FUN_007442e0
void CGObject_C::OnModelLoaded(CM2Model* model) {
    if (model != this->GetObjectModel()) {
        return;
    }

    this->UpdateHeight();

    for (auto effect = this->m_effects; effect; ) {
        auto next = effect->m_linkNext;
        effect->UpdateAttachment();
        effect = next;
    }
}

// ref: FUN_00743e10
// The fade moves the alpha linearly from where it began to where it is going; the model draws at
// the alpha times the object's own ceiling.
void CGObject_C::UpdateFade(uint32_t time) {
    auto model = this->GetObjectModel();

    if (!model) {
        return;
    }

    if (this->m_fadeDuration) {
        if (static_cast<int32_t>((time - this->m_fadeStart) - this->m_fadeDuration) < 0) {
            this->m_alpha = static_cast<uint8_t>(
                static_cast<int32_t>((static_cast<uint32_t>(this->m_alphaTo) - static_cast<uint32_t>(this->m_alphaFrom))
                    * (time - this->m_fadeStart)) / static_cast<int32_t>(this->m_fadeDuration) + this->m_alphaFrom);
        } else {
            this->m_fadeDuration = 0;
            this->m_alpha = this->m_alphaTo;
        }
    }

    model->m_baseAlpha = static_cast<float>(this->m_alphaScale) * static_cast<float>(this->m_alpha)
        * 1.5378700481960550e-05f;
}

// ref: FUN_00743ec0
// The frame's target and pet glow come from the object it is drawing, the fade advances, and a
// scale ease runs over two seconds on a cosine.
void CGObject_C::UpdateForFrame(CGWorldFrame* frame) {
    if (CGGameUI::GetLockedTarget() == this->GetGUID()) {
        if (ClntObjMgrGetActivePlayer() != this->GetGUID() && s_selectionCircleCVar
            && s_selectionCircleCVar->GetInt() != 0) {
            frame->m_selectionTarget = this->GetGUID();
        }
    }

    if (CGPetInfo::GetPet(0) == this->GetGUID() && ClntObjMgrGetActivePlayer() != this->GetGUID()) {
        frame->m_selectionPet = this->GetGUID();
    }

    uint32_t now = CWorld::GetCurTimeMs();

    this->UpdateFade(now);

    uint32_t start = this->m_scaleEaseStart;

    if (start == 0 || start >= now) {
        return;
    }

    int32_t elapsed = static_cast<int32_t>(now - start);

    if (elapsed < 2000) {
        float target = this->GetBaseScale();
        float c = cosf(static_cast<float>(elapsed) * 0.0010000000474974513f * 1.5707963705062866f);

        this->m_scale = (0.5f + -c * 0.5f) * (target - this->m_scaleEaseFrom) + this->m_scaleEaseFrom;
        this->OnScaleChanged();

        return;
    }

    this->m_scale = this->CGObject::GetScale();
    this->m_scaleEaseStart = 0;
    this->OnScaleEaseDone();
}

// ref: FUN_00743330
// Put the model where the object is, turned and scaled.
int32_t CGObject_C::PlaceModel(float elapsed) {
    auto model = this->GetObjectModel();

    if (model) {
        float scale = this->GetScale();
        float facing = this->GetRenderFacing();

        model->SetWorldTransform(this->GetPosition(), facing, scale);
    }

    return 1;
}

// ref: FUN_00743300
void CGObject_C::GetHidden(uint32_t flags, int32_t* hidden, int32_t* hiddenOther) {
    if ((flags & 1) == 0) {
        *hidden = 1;
    }
}

int32_t CGObject_C::CanHighlight() {
    return 0;
}

int32_t CGObject_C::CanBeTargetted() {
    return 0;
}

// ref: FUN_00743250
// A stopped effect plays its model's dissolve (sequence 159) when it has one, and finishes when
// that ends; otherwise it finishes now, fading its sound out when it asked to.
void CGObject_C::StopEffect(CEffect* effect) {
    if (effect->m_model && effect->m_model->IsLoaded(0, 0) && effect->m_model->HasSequence(0x9F)) {
        effect->m_flags &= ~0x1000u;
        effect->m_model->SetBoneSequence(0xFFFFFFFF, 0x9F, 0xFFFFFFFF, 0, 1.0f, 1, 1);
        effect->m_model->SetSequenceDoneCallback(&CGObject_C::KitEffectDissolveDone, OwnerFromEffect(effect));

        return;
    }

    if (effect->m_flags & 0x40) {
        SI2::StopOrFadeOut(&effect->m_sound, 0, 0.5f, 1);
    }

    effect->Finish();
}

// ref: FUN_007432e0
const SpellVisualRec* CGObject_C::GetSpellVisualRec(const SpellRec* spell) {
    return GetSpellVisual(spell);
}

// ref: FUN_00744330
void CGObject_C::StartEffects(int32_t spellID, int32_t param) {
    for (auto effect = this->m_effects; effect; effect = effect->m_linkNext) {
        if (effect->m_spellID == spellID && (effect->m_flags & CEffect::EFFECT_EMISSION_COUNTDOWN)
            && effect->m_int8c == param) {
            effect->Start();
        }
    }
}

// ref: FUN_004d5fa0
void CGObject_C::GetWorldMatrix(C44Matrix& matrix) const {
    matrix = C44Matrix();
}

// ref: FUN_00743490
void CGObject_C::UpdateQuestMarkerSequence() {
    if (!this->m_questMarker) {
        return;
    }

    if (PlayerNameIsHighlighted(this->m_nameDesc)) {
        this->m_questMarker->SetBoneSequence(0xFFFFFFFF, 0xBE, 0xFFFFFFFF, 0, 1.0f, 1, 1);

        return;
    }

    this->m_questMarker->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);
}

// ref: FUN_007434e0
int32_t CGObject_C::GetNameText(int32_t a2, char* buffer, uint32_t size) {
    if (this->GetName()) {
        SStrPrintf(buffer, size, "%s", this->GetName());

        return 1;
    }

    buffer[0] = '\0';

    return 1;
}

// ref: FUN_00743530
int32_t CGObject_C::Virtual0D0(uint32_t flags) {
    if (this->m_questMarker) {
        return 0;
    }

    return (flags >> 11) & 1;
}

// ref: FUN_004d5fe0
CM2Model* CGObject_C::GetObjectModel() {
    return this->m_model;
}

// ref: FUN_006f1700
float CGObject_C::Virtual0F8() {
    return 1.0f;
}

// ------------------------------------------------------------------------------------------------
// Members
// ------------------------------------------------------------------------------------------------

// ref: FUN_00743760
// Make the object's model when it has none, and give it a world entry. The flags say how the map
// treats it: game objects 0xb, dynamic objects 10, corpses with bones 2, units 0x10 with the
// creature template's two bits, players 0x30.
//
void CGObject_C::AddWorldObject() {
    if (!this->m_model) {
        const char* fileName;

        if (this->GetModelFileName(fileName)) {
            auto model = CWorld::GetM2Scene()->CreateModel(fileName, 0);

            if (model != this->m_model) {
                if (model) {
                    model->m_refCount++;
                }

                auto previous = this->m_model;
                this->m_model = model;
                this->ModelChanged(previous);

                this->SetModelFinish(model);
            }

            if (model) {
                model->Release();
            }
        }

        if (!this->m_model) {
            return;
        }
    }

    if (ClntObjMgrGetPlayerType() != PLAYER_NORMAL) {
        return;
    }

    if (this->m_worldObject) {
        // "OBJECTALREADYACTIVE|0x%016I64X" (SysMsgPrintf) in the reference.
        return;
    }

    uint32_t objFlags = 0;

    if (this->IsA(TYPE_GAMEOBJECT)) {
        objFlags = 0xB;
    } else if (this->IsA(TYPE_DYNAMICOBJECT)) {
        objFlags = 10;
    } else if (this->IsA(TYPE_CORPSE)) {
        if (static_cast<CGCorpse_C*>(this)->Corpse()->flags & 1) {
            objFlags = 2;
        }
    } else if (this->IsA(TYPE_UNIT)) {
        auto unit = static_cast<CGUnit_C*>(this);

        uint32_t flags = 0;

        if (unit->GetCreatureTypeFlag22()) {
            flags = 8;
        }

        if (unit->GetCreatureTypeFlag25()) {
            flags |= 2;
        }

        objFlags = flags | 0x10;

        if (this->IsA(TYPE_PLAYER)) {
            objFlags = flags | 0x30;
        }
    }

    this->m_worldObject = CWorld::AddObject(this->GetObjectModel(),
                                            reinterpret_cast<void*>(&CGWorldFrame::ObjectWorldHandler),
                                            nullptr, this->GetGUID(), 0, objFlags);

    if (this->m_postInited && !this->m_inReenable) {
        this->UpdateWorldObject(0);
    }
}

// ref: FUN_00743390
int32_t CGObject_C::IsModelLoaded() {
    auto model = this->GetObjectModel();

    return model && model->IsLoaded(0, 0) ? 1 : 0;
}

int32_t CGObject_C::IsInReenable() {
    return this->m_inReenable;
}

int32_t CGObject_C::IsObjectLocked() {
    return this->m_lockCount != 0;
}

// Every type's PostInit ends the same way in the reference -- the unit's (FUN_0073fcc0), the game
// object's (FUN_0070af80), the dynamic object's (FUN_00705230, FUN_00705b20), the corpse's
// (FUN_00712f30) and the generic one (FUN_004d63b0) all call InitializeVisible -- and every frozen
// PostInit chains here, so it is made once, here.
void CGObject_C::PostInit(uint32_t time, const CClientObjCreate& init, bool a4) {
    this->InitializeVisible();
}

uint32_t CGObject_C::GetBlock(uint32_t block) const {
    auto storage = reinterpret_cast<const uint32_t*>(this->m_obj);

    return storage[block];
}

uint32_t CGObject_C::BlockIndexOf(const void* field) const {
    auto storage = reinterpret_cast<const uint32_t*>(this->m_obj);

    return static_cast<uint32_t>(static_cast<const uint32_t*>(field) - storage);
}

void CGObject_C::SetBlock(uint32_t block, uint32_t value) {
    auto storage = reinterpret_cast<uint32_t*>(this->m_obj);
    storage[block] = value;
}

void CGObject_C::SetDisablePending(int32_t pending) {
    if (pending) {
        this->m_disablePending = true;
    } else {
        this->m_disablePending = false;
    }
}

void CGObject_C::SetModel(CM2Model* model) {
    // No change
    if (this->m_model == model) {
        return;
    }

    if (model) {
        model->AddRef();
    }

    auto previous = this->m_model;
    this->m_model = model;
    this->ModelChanged(previous);

    this->SetModelFinish(model);
}

void CGObject_C::SetModelFinish(CM2Model* model) {
    // The reference registers the owner's callbacks from its own model-creation paths (the unit
    // model builder and the mount builder both call FUN_00823fe0 and FUN_00824060). frozen attaches
    // a model to an object in one place, so it happens here, for units only -- they are the only
    // owner with handlers so far.
    if (!model || !this->IsA(TYPE_UNIT)) {
        return;
    }

    // PHASE4(Unit_C): the unit's anim-event handler (FUN_00734a40 -> FUN_00732650) is not ported.
    model->SetSequenceDoneCallback(CGUnit_C::OnSequenceDone, this->GetGUID());
}

void CGObject_C::SetObjectLocked(int32_t locked) {
    if (locked) {
        if (this->m_lockCount != 0xFFFF) {
            this->m_lockCount++;
        }
    } else {
        if (this->m_lockCount != 0) {
            this->m_lockCount--;
        }
    }
}

void CGObject_C::SetStorage(uint32_t* storage, uint32_t* saved) {
    this->m_obj = reinterpret_cast<CGObjectData*>(&storage[CGObject::GetBaseOffset()]);
    this->m_objSaved = &saved[CGObject::GetBaseOffsetSaved()];
}

void CGObject_C::SetTypeID(OBJECT_TYPE_ID typeID) {
    this->m_typeID = typeID;

    switch (typeID) {
        case ID_OBJECT:
            this->m_obj->m_type = HIER_TYPE_OBJECT;
            break;

        case ID_ITEM:
            this->m_obj->m_type = HIER_TYPE_ITEM;
            break;

        case ID_CONTAINER:
            this->m_obj->m_type = HIER_TYPE_CONTAINER;
            break;

        case ID_UNIT:
            this->m_obj->m_type = HIER_TYPE_UNIT;
            break;

        case ID_PLAYER:
            this->m_obj->m_type = HIER_TYPE_PLAYER;
            break;

        case ID_GAMEOBJECT:
            this->m_obj->m_type = HIER_TYPE_GAMEOBJECT;
            break;

        case ID_DYNAMICOBJECT:
            this->m_obj->m_type = HIER_TYPE_DYNAMICOBJECT;
            break;

        case ID_CORPSE:
            this->m_obj->m_type = HIER_TYPE_CORPSE;
            break;

        default:
            break;
    }
}

// ref: FUN_00743680
// Every effect comes off the old model and onto the new one; the old one is let go; the new one
// hears when it has loaded; and the world entry follows the model.
void CGObject_C::ModelChanged(CM2Model* previous) {
    for (auto effect = this->m_effects; effect; ) {
        auto next = effect->m_linkNext;

        effect->DetachModel();

        if (effect->m_model && effect->m_model->IsLoaded(0, 0)) {
            effect->UpdateAttachment();
        }

        effect = next;
    }

    if (previous) {
        previous->SetLoadedCallback(nullptr, nullptr);

        if (previous->m_attachParent) {
            previous->DetachFromParent();
        }

        previous->Release();
    }

    if (this->m_model) {
        this->m_model->SetLoadedCallback(&CGObject_C::ModelLoadedCallback, this);
    }

    if (this->m_worldObject && this->m_model == this->GetObjectModel()) {
        CWorld::SetObjectModel(this->m_worldObject, this->m_model);
    }
}

// ref: FUN_007431e0
void CGObject_C::ReleaseModel() {
    if (!this->m_model) {
        return;
    }

    this->m_model->SetLoadedCallback(nullptr, nullptr);
    this->m_model->SetSequenceDoneCallback(nullptr, 0);
    this->m_model->SetAnimEventCallback(nullptr, 0);
    this->m_model->Release();
    this->m_model = nullptr;
}

// ref: FUN_00743af0
void CGObject_C::UpdateEffectAttachments() {
    if (!this->m_model || !this->m_model->IsLoaded(0, 0)) {
        return;
    }

    for (auto effect = this->m_effects; effect; ) {
        auto next = effect->m_linkNext;

        if (effect->m_flags & CEffect::EFFECT_PLAYED) {
            effect->UpdateAttachment();
        }

        effect = next;
    }
}

// ref: FUN_00743b40
void CGObject_C::StopEffects(int32_t spellID, int32_t includePersistent) {
    if (spellID == 0) {
        return;
    }

    for (auto effect = this->m_effects; effect; ) {
        auto next = effect->m_linkNext;

        if (effect->m_spellID == spellID
            && (includePersistent || (effect->m_flags & 0x20000) == 0)
            && (effect->m_flags & CEffect::EFFECT_NO_EXPIRE) == 0) {
            this->StopEffect(effect);
        }

        effect = next;
    }
}

// ref: FUN_00743bc0
void CGObject_C::Unhighlight(int32_t source) {
    this->m_highlight &= ~(1u << source);

    if (CGObject_C::s_highlightGUID == this->GetGUID()) {
        CGObject_C::s_highlightGUID = 0;
    }

    if (this->Virtual0B4()) {
        return;
    }

    auto model = this->GetObjectModel();

    if (model && this->m_highlight == 0) {
        model->m_baseEmissive = { 0.0f, 0.0f, 0.0f };
        model->float1B8 = 1.0f;
    }
}

// ref: FUN_00743c70
// The model glows in the day's ambient colour while a highlight source is up.
void CGObject_C::Highlight(int32_t source) {
    this->m_highlight |= 1u << source;

    if (this->Virtual0B4()) {
        return;
    }

    CGObject_C::s_highlightGUID = this->GetGUID();

    auto model = this->GetObjectModel();

    if (!model) {
        return;
    }

    auto block = DayNightGetBlock();

    model->m_baseEmissive.x = static_cast<float>(block->ambient.r) * 0.003921568859368563f;
    model->m_baseEmissive.y = static_cast<float>(block->ambient.g) * 0.003921568859368563f;
    model->m_baseEmissive.z = 0.003921568859368563f * static_cast<float>(block->ambient.b);

    if (this->m_highlight != 0x4) {
        model->float1B8 = 1.0f;
    }
}

// ref: FUN_00743d50
void CGObject_C::RemoveWorldObject() {
    if (!this->m_worldObject) {
        return;
    }

    if (this->ShouldFadeOut()) {
        if (this->GetObjectModel() && !this->GetObjectModel()->m_attachParent) {
            this->PlaceModel(CGWorldFrame::s_currentWorldFrame ? CGWorldFrame::s_currentWorldFrame->m_elapsed : 0.0f);
        }

        CWorld::FadeOutObject(this->m_worldObject, static_cast<float>(this->m_alpha) * 0.003921568859368563f,
                              this->GetTransportGUID());
        this->m_worldObject = 0;

        return;
    }

    CWorld::RemoveObject(this->m_worldObject);
    this->m_worldObject = 0;
}

// ref: FUN_00744030
void CGObject_C::SetAlpha(float alpha, uint32_t duration) {
    uint8_t target = static_cast<uint8_t>(static_cast<int32_t>(lrintf(alpha * 255.0f)));

    if (target == this->m_alpha) {
        this->m_alphaTo = target;
        this->m_fadeDuration = 0;
        this->m_alpha = target;

        return;
    }

    this->m_fadeStart = CWorld::GetCurTimeMs();
    this->m_fadeDuration = duration;
    this->m_alphaFrom = this->m_alpha;
    this->m_alphaTo = target;

    if (duration == 0) {
        this->m_alpha = target;
    }
}

// ref: FUN_007441d0
void CGObject_C::SetScaleEase(float scale) {
    this->m_scaleEaseStart = CWorld::GetCurTimeMs();
    this->m_scaleEaseFrom = scale;
    this->m_scale = scale;

    auto camera = CGWorldFrame::GetActiveCamera();

    if (camera && camera->GetTarget() == this->GetGUID()) {
        camera->SetTarget(this->GetGUID());
    }
}

// ref: FUN_00744230
// The object's height is its model's box, and the world entry follows; an unloaded model leaves a
// mark so the object asks again.
void CGObject_C::UpdateHeight() {
    auto model = this->GetObjectModel();
    int32_t loaded = model ? model->IsLoaded(0, 1) : 0;

    CAaBox box = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

    if (model) {
        model->GetBoundingBox(box);
    }

    if (box.t.x <= box.b.x || box.t.y <= box.b.y || box.t.z <= box.b.z) {
        this->m_height = 0.0f;
    } else {
        this->m_height = box.t.z - box.b.z;
    }

    if (!this->m_disabled) {
        this->UpdateWorldObject(0);
    }

    this->m_flag21 = loaded ? 0 : 1;
}

// ref: FUN_007443d0
void CGObject_C::ReleaseQuestMarker() {
    if (!this->m_questMarker) {
        return;
    }

    if (this->m_questMarker->m_attachParent) {
        this->m_questMarker->DetachFromParent();
    }

    this->m_questMarker->Release();
    this->m_questMarker = nullptr;
}

// ref: FUN_00744400
void CGObject_C::SetQuestStatus(uint32_t status) {
    if (status == this->m_questStatus && (s_questStatusMarker[status] == 0 || this->m_questMarker)) {
        return;
    }

    bool changed = !(s_questStatusMarker[status] == s_questStatusMarker[this->m_questStatus]
                     && (s_questStatusMarker[status] == 0 || this->m_questMarker));

    this->m_questStatus = status;

    if (changed) {
        this->UpdateQuestMarker();
    }
}

// ref: FUN_00744640
int32_t CGObject_C::GetQuestMarkerAnimKit(int32_t completed) {
    switch (this->m_questStatus) {
        case 2:
        case 3:
        case 4:
        case 5:
        case 7:
        case 8:
            return completed ? 0x31 : 0x17;

        case 6:
            return completed ? 0x32 : 0x18;

        case 9:
        case 10:
            return completed ? 0x33 : 0x19;

        default:
            return 0;
    }
}

// ref: FUN_007446c0
C3Vector CGObject_C::GetModelWorldPosition() {
    auto model = this->GetObjectModel();

    if (model && model->m_flag8000) {
        model->Animate();

        C3Vector view = { model->matrixF4.d0, model->matrixF4.d1, model->matrixF4.d2 };

        return view * model->m_scene->m_viewInv;
    }

    return this->GetPosition();
}

// ref: FUN_00744720
void CGObject_C::GetModelWorldMatrix(C44Matrix& out) {
    auto model = this->GetObjectModel();

    if (model && model->m_flag8000) {
        model->Animate();

        out = model->matrixF4 * model->m_scene->m_viewInv;

        return;
    }

    this->GetWorldMatrix(out);
}

// ref: FUN_00744790
void CGObject_C::AddKitEffect(int32_t attachment, uint32_t endTime, int32_t spellID, const SpellVisualKitRec* kit,
                              const SpellVisualEffectNameRec* effectName, uint32_t* flags,
                              M2SequenceDoneCallback sequenceDone, const C3Vector* offset, WOWGUID target,
                              int32_t param, const SpellVisualKitModelAttachRec* attach) {
    auto effect = CEffect::Create();

    effect->m_endTime = endTime;

    WOWGUID self = this->GetGUID();

    if (!target) {
        target = self;
    }

    effect->InitializeAttached(attachment, spellID, kit, effectName, target, self, *flags, sequenceDone,
                               offset, static_cast<uint32_t>(param), attach);

    if (*flags & CEffect::EFFECT_EMISSION_COUNTDOWN) {
        effect->SetEmission(0);
    }

    if (kit->m_flags & 0x200) {
        effect->m_flags |= CEffect::EFFECT_RIBBONS_TRAIL;
    }

    effect->Release();

    *flags = (*flags & ~0x11u) | 0x400;
}

// ref: FUN_00744a50
void CGObject_C::InitializeVisible() {
    this->m_postInited = 1;

    uint32_t duration = this->Virtual0E4() ? 1000 : 0;
    this->SetAlpha(this->GetFadeInAlpha(), duration);

    this->OnReenable();

    this->m_scaleEaseStart = 0;

    if (this->m_worldObject) {
        this->UpdateWorldObject(0);
    }
}

// ref: FUN_00744ac0
void CGObject_C::StopAllEffects(int32_t force) {
    if (force) {
        while (auto effect = this->m_effects) {
            effect->Unlink();

            if (effect->m_attachment == -1 && effect->m_endTime != 0) {
                effect->LinkToGlobalList();
            } else {
                effect->Release();
            }
        }

        return;
    }

    for (auto effect = this->m_effects; effect; ) {
        auto next = effect->m_linkNext;
        auto spell = g_spellDB.GetRecord(effect->m_spellID);

        // AttributesEx3 0x100000 keeps an effect playing through this.
        if (!spell || (spell->m_attributesEx3 & 0x100000) == 0) {
            effect->Finish();
        }

        effect = next;
    }
}

// ref: FUN_00744bd0
void CGObject_C::StopKitEffects(int32_t spellID, int32_t kitType, int32_t param, int32_t matchSpell, int32_t matchParam) {
    auto spell = g_spellDB.GetRecord(spellID);

    if (!spell) {
        return;
    }

    auto visual = this->GetSpellVisualRec(spell);

    if (!visual) {
        return;
    }

    int32_t kitID;

    switch (kitType) {
        case 0: kitID = visual->m_castKit; break;
        case 1: kitID = visual->m_impactKit; break;
        case 2: kitID = visual->m_stateKit; break;
        case 4: kitID = visual->m_precastKit; break;
        case 5: kitID = visual->m_casterImpactKit; break;
        case 6: kitID = visual->m_targetImpactKit; break;
        case 7: kitID = visual->m_missileTargetingKit; break;
        case 8: kitID = visual->m_stateDoneKit; break;
        default: return;
    }

    auto kit = g_spellVisualKitDB.GetRecord(kitID);

    if (!kit) {
        return;
    }

    for (auto effect = this->m_effects; effect; ) {
        auto next = effect->m_linkNext;

        if (effect->m_kit && effect->m_kit->m_ID == kit->m_ID
            && (!matchSpell || effect->m_spellID == spellID)
            && (!matchParam || static_cast<int32_t>(effect->m_param) == param)) {
            effect->Finish();
        }

        effect = next;
    }
}

// ref: FUN_00745140
void CGObject_C::ShowQuestMarker(int32_t show) {
    auto marker = this->m_questMarker;

    if (!marker || !marker->m_attachParent) {
        return;
    }

    uint32_t bit = show & 1;

    marker->m_flag80 = bit;
    marker->m_flag20000 = bit;
}

// ref: FUN_00745230
void CGObject_C::PlayKit(const SPELLVISUALKITPARAMS& params) {
    int32_t spellID = params.m_spell ? params.m_spell->m_ID : 0;
    auto kit = params.m_kit;

    if (!kit) {
        return;
    }

    C3Vector transported;
    const C3Vector* offset = params.m_position;

    if (params.m_position && params.m_transport) {
        C44Matrix transport;
        MovementGetTransportMatrix(params.m_transport, transport);
        transported = *params.m_position * transport;
        offset = &transported;
    }

    uint32_t flags = 0;

    if (kit->m_shakeID != 0) {
        flags = 0x10;
    }

    if (SI2::SoundKitFollowsListener(kit->m_soundID)) {
        flags |= 0x1;
    }

    if (params.m_spell && params.m_spell->m_spellMissileID > 0) {
        flags |= 0x400000;
    }

    auto visual = params.m_spell ? GetSpellVisual(params.m_spell) : nullptr;

    if (this->PrePlayKit(params.m_spell, kit, params.m_kitType, params.m_position, params.m_transport,
                         params.m_stateParam, params.m_param7, params.m_param8, params.m_param9, flags, visual)) {
        return;
    }

    if (visual && (visual->m_flags & 0x10)) {
        flags |= CEffect::EFFECT_SOUND_UNTRACKED;
    }

    if (kit->m_flags & 0xC) {
        flags |= CEffect::EFFECT_MODEL_READY_ANIM;
    }

    int32_t stateParam = params.m_stateParam;
    M2SequenceDoneCallback sequenceDone = nullptr;

    switch (params.m_kitType) {
        case 0:
            flags |= 0x80;
            // fallthrough
        case 1:
        case 5:
        case 6:
        case 8:
            sequenceDone = &CGObject_C::KitEffectOneShot;
            flags = (flags & ~0x1u) | 0x20;
            break;

        case 2:
            flags |= 0x21000;
            sequenceDone = &CGObject_C::KitEffectStateStart;

            if (params.m_spell) {
                for (int32_t i = 0; i < 3; i++) {
                    if (params.m_spell->m_effectAura[i] == 0x4E) {
                        flags |= 0x800000;
                    }
                }
            }
            break;

        case 3:
            if (params.m_spell) {
                sequenceDone = &CGObject_C::KitEffectOneShot;
                flags = (flags & ~0x1u) | 0x20;
            }

            stateParam = 0;
            flags |= 0x22000;
            break;

        case 4:
            sequenceDone = &CGObject_C::KitEffectLoop;
            flags |= 0x40;
            break;

        case 7:
            if (params.m_spell) {
                sequenceDone = &CGObject_C::KitEffectLoop;
            }

            flags |= 0x22000;
            break;

        default:
            break;
    }

    int32_t played = 0;

    auto effectName = [](int32_t id) { return g_spellVisualEffectNameDB.GetRecord(id); };

    if (auto name = effectName(kit->m_headEffect)) {
        this->AddKitEffect(0x14, 0, spellID, kit, name, &flags, sequenceDone, offset, params.m_target, 0, nullptr);
        played = 1;
    }

    // The point a kit's placed effects go to: the given one, or the object's own feet.
    auto placedPosition = [&](C3Vector& out, WOWGUID& transport) -> const C3Vector* {
        transport = params.m_transport;

        if (params.m_position) {
            return params.m_position;
        }

        flags |= 0x200;
        out = this->GetRawPosition();
        transport = this->GetTransportGUID();

        if (this->IsA(TYPE_UNIT) && (static_cast<CGUnit_C*>(this)->m_localMove.GetMoveFlags() & 0x40000000)) {
            out.z -= static_cast<CGUnit_C*>(this)->Unit()->hoverHeight;
        }

        return &out;
    };

    auto placeEffect = [&](const SpellVisualEffectNameRec* name, const SpellVisualKitModelAttachRec* attach) {
        auto effect = CEffect::Create();
        effect->m_endTime = 0;

        if (params.m_spell && !params.m_position) {
            for (int32_t i = 0; i < 3; i++) {
                if (params.m_spell->m_effectAura[i] == 0x1A) {
                    flags |= CEffect::EFFECT_FOLLOWS_OWNER;
                    break;
                }
            }
        }

        C3Vector pos;
        WOWGUID transport;
        auto where = placedPosition(pos, transport);

        effect->InitializeAtPoint(*where, static_cast<uint32_t>(OsGetAsyncTimeMs()), spellID, kit, name,
                                  this->GetGUID(), flags, sequenceDone, static_cast<uint32_t>(params.m_param),
                                  attach, transport);
        effect->Release();

        flags = (flags & ~0x11u) | 0x400;
        played = 1;
    };

    if (auto name = effectName(kit->m_worldEffect)) {
        placeEffect(name, nullptr);
    }

    if ((flags & 0x2000) == 0) {
        struct { int32_t effect; int32_t attachment; } body[] = {
            { kit->m_baseEffect, 0x13 },
            { kit->m_leftHandEffect, 0x15 },
            { kit->m_rightHandEffect, 0x16 },
            { kit->m_breathEffect, 0x11 },
            { kit->m_chestEffect, 0x22 },
        };

        for (auto& entry : body) {
            if (auto name = effectName(entry.effect)) {
                this->AddKitEffect(entry.attachment, 0, spellID, kit, name, &flags, sequenceDone, offset,
                                   params.m_target, params.m_param, nullptr);
                played = 1;
            }
        }

        this->PlayKitExtras(params.m_spell, kit, offset, params.m_target, params.m_param, flags, sequenceDone, &played);

        if (auto name = effectName(kit->m_specialEffect[0])) {
            this->AddKitEffect(0x17, 0, spellID, kit, name, &flags, sequenceDone, offset, params.m_target,
                               params.m_param, nullptr);
            played = 1;
        }

        if (auto name = effectName(kit->m_specialEffect[1])) {
            this->AddKitEffect(0x18, 0, spellID, kit, name, &flags, sequenceDone, offset, params.m_target,
                               params.m_param, nullptr);
            played = 1;
        }

        if (auto name = effectName(kit->m_specialEffect[2])) {
            this->AddKitEffect(0x19, 0, spellID, kit, name, &flags, sequenceDone, params.m_position, params.m_target,
                               params.m_param, nullptr);
            played = 1;
        }
    } else if (auto name = effectName(kit->m_baseEffect)) {
        if (params.m_position) {
            auto effect = CEffect::Create();
            effect->m_endTime = 0;

            effect->InitializeAtPoint(*params.m_position, static_cast<uint32_t>(OsGetAsyncTimeMs()), spellID, kit,
                                      name, this->GetGUID(), flags, sequenceDone,
                                      static_cast<uint32_t>(params.m_param), nullptr, params.m_transport);
            effect->Release();

            flags = (flags & ~0x11u) | 0x400;
            played = 1;
        }
    }

    int32_t attaches = SpellVisualsGetModelAttachCount(kit->m_ID);

    for (int32_t i = 0; i < attaches; i++) {
        auto attach = SpellVisualsGetModelAttach(kit->m_ID, i);
        auto name = attach ? effectName(attach->m_spellVisualEffectNameID) : nullptr;

        if (!name) {
            continue;
        }

        if ((flags & 0x2000) == 0) {
            if (attach->m_attachmentID == -1) {
                placeEffect(name, attach);
            } else {
                this->AddKitEffect(attach->m_attachmentID, 0, spellID, kit, name, &flags, sequenceDone, offset,
                                   params.m_target, params.m_param, attach);
                played = 1;
            }
        } else if (params.m_position) {
            auto effect = CEffect::Create();
            effect->m_endTime = 0;

            effect->InitializeAtPoint(*params.m_position, static_cast<uint32_t>(OsGetAsyncTimeMs()), spellID, kit,
                                      name, this->GetGUID(), flags, sequenceDone,
                                      static_cast<uint32_t>(params.m_param), attach, params.m_transport);
            effect->Release();

            flags = (flags & ~0x11u) | 0x400;
            played = 1;
        }
    }

    this->PostPlayKit(params.m_spell, kit, params.m_kitType, offset, stateParam, params.m_param7,
                      params.m_target, params.m_param, visual, flags, played);
}

// ref: FUN_00743580
void CGObject_C::KitEffectFinished(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner) {
    auto effect = EffectFromOwner(owner);

    if (a4 == 0 && effect) {
        effect->Finish();
    }
}

// ref: FUN_007435a0
void CGObject_C::KitEffectLoop(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner) {
    auto effect = EffectFromOwner(owner);

    if (a4 == 0 && effect && effect->m_model) {
        effect->m_model->SetBoneSequence(0xFFFFFFFF, animId, 0xFFFFFFFF, 0, 1.0f, 1, 1);
    }
}

// ref: FUN_00744870
// A one-shot kit effect ends with its dissolve when the model has one; otherwise it is held on its
// last frame and finished.
void CGObject_C::KitEffectOneShot(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner) {
    auto effect = EffectFromOwner(owner);

    if (a4 != 0 || !effect) {
        return;
    }

    if (effect->m_model && effect->m_model->HasSequence(0x9F)) {
        effect->m_model->SetSequenceDoneCallback(&CGObject_C::KitEffectFinished, owner);
        effect->m_model->SetBoneSequence(0xFFFFFFFF, 0x9F, 0xFFFFFFFF, 0, 1.0f, 1, 1);

        return;
    }

    M2BoneSequenceState state;
    model->GetBoneSequenceState(boneId, &state);
    model->SetBoneSequenceTime(boneId, (state.endTime - state.startTime) - 1);

    uint32_t held = effect->m_model->m_scene->m_time;
    effect->m_model->m_animationHeldTime = held ? held : 1;

    effect->Finish();
}

// ref: FUN_00744920
void CGObject_C::KitEffectStateLoop(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner) {
    auto effect = EffectFromOwner(owner);

    if (a4 != 0 || !effect || !effect->m_model) {
        return;
    }

    uint32_t end = effect->m_endTime;
    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());

    if (end != 0 && now != end && static_cast<int32_t>(now - end) > -1) {
        if (!effect->m_model->HasSequence(0x9F)) {
            effect->Finish();

            return;
        }

        effect->m_model->SetSequenceDoneCallback(&CGObject_C::KitEffectFinished, owner);
        effect->m_model->SetBoneSequence(0xFFFFFFFF, 0x9F, 0xFFFFFFFF, 0, 1.0f, 1, 1);

        return;
    }

    effect->m_model->SetBoneSequence(0xFFFFFFFF, animId, 0xFFFFFFFF, 0, 1.0f, 1, 1);
}

// ref: FUN_007449c0
// A state kit's effect plays its birth, then loops: stand when its kit says so, else sequence 158.
void CGObject_C::KitEffectStateStart(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner) {
    auto effect = EffectFromOwner(owner);

    if (a4 != 0 || !effect || !effect->m_model) {
        return;
    }

    if (effect->m_kit && (effect->m_kit->m_flags & 0x20)) {
        effect->m_model->SetSequenceDoneCallback(&CGObject_C::KitEffectStateLoop, owner);
        effect->m_model->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);

        return;
    }

    if (effect->m_model->HasSequence(0x9E)) {
        effect->m_model->SetSequenceDoneCallback(&CGObject_C::KitEffectStateLoop, owner);
        effect->m_model->SetBoneSequence(0xFFFFFFFF, 0x9E, 0xFFFFFFFF, 0, 1.0f, 1, 1);
    }
}

// ref: FUN_00743230
void CGObject_C::KitEffectDissolveDone(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner) {
    auto effect = EffectFromOwner(owner);

    if (a4 == 0 && effect) {
        effect->Finish();
    }
}

// ref: FUN_00743110
void CGObject_C::ModelLoadedCallback(CM2Model* model, void* arg) {
    if (arg) {
        static_cast<CGObject_C*>(arg)->OnModelLoaded(model);
    }
}

// ------------------------------------------------------------------------------------------------
// Module functions
// ------------------------------------------------------------------------------------------------

// ref: FUN_007440f0
void ObjectsUpdateWorldObjects() {
    ClntObjMgrEnumVisibleObjects(&UpdateVisibleWorldObject, nullptr);
}

// ref: FUN_00744140
// PHASE4(ObjectEffect): each object's manager updates through FUN_006f39b0; none exist yet.
void ObjectsUpdateObjectEffects() {
}

// ref: FUN_007450b0
CGObject_C* GetSoundListenerObject() {
    CGObject_C* object = ClntObjMgrObjectPtr(CGUnit_C::s_activeMover, TYPE_UNIT, ".\\Object_C.cpp", 0x4af);

    if (!object) {
        object = ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__);

        if (!object) {
            return nullptr;
        }
    }

    if (object->IsA(TYPE_PLAYER) && (static_cast<CGPlayer_C*>(object)->m_viewFlags & 0x1)) {
        WOWGUID farsight = static_cast<CGPlayer_C*>(object)->GetFarsightObject();

        if (farsight) {
            auto seen = ClntObjMgrObjectPtr(farsight, TYPE_OBJECT, ".\\Object_C.cpp", 0x4b7);

            if (seen) {
                return seen;
            }
        }
    }

    return object;
}

// ref: FUN_007460c0
void ObjectsInitialize() {
    s_selectionCircleCVar = CVar::Register("ObjectSelectionCircle", nullptr, 0x0, "1", nullptr, DEFAULT);

    if (s_selectionTexture) {
        HandleClose(s_selectionTexture);
    }

    CStatus status;
    s_selectionTexture = TextureCreate("Textures\\UnitSelectTexture.blp", CGxTexFlags(GxTex_LinearMipNearest, 0, 0, 0, 0, 0, 1),
                                       &status, 1);

    QuestMarkersLoad(1);

    // PHASE4(ObjectMgrClient): FUN_004d5ba0 registers ScaleEaseFinished as the object manager's
    // type-4 enumerator here.
    (void)&ScaleEaseFinished;
}

// ref: FUN_007435e0
void QuestMarkersLoad(int32_t create) {
    for (auto& marker : s_questMarkers) {
        if (marker.model) {
            marker.model->Release();
            marker.model = nullptr;
        }

        if (!create) {
            marker.model = nullptr;
        } else if (marker.fileName && marker.fileName[0]) {
            marker.model = CWorld::GetM2Scene()->CreateModel(marker.fileName, 0);
        }
    }
}

// ref: FUN_00744e50
void ObjectsShutdown() {
    if (s_selectionTexture) {
        HandleClose(s_selectionTexture);
    }

    s_selectionTexture = nullptr;

    for (auto& marker : s_questMarkers) {
        if (marker.model) {
            marker.model->Release();
            marker.model = nullptr;
        }

        marker.model = nullptr;
    }
}

// ref: FUN_00744eb0
int32_t SelectionCircleSetStates() {
    auto texture = TextureGetGxTex(s_selectionTexture, 0, nullptr);

    if (!texture) {
        return 0;
    }

    GxRsSet(GxRs_BlendingMode, 3);
    GxRsSet(GxRs_AlphaRef, CGxDevice::s_alphaRef[3]);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_DepthWrite, 0);

    g_theGxDevicePtr->RsSet(GxRs_Texture0, texture);
    g_theGxDevicePtr->RsSet(GxRs_Texture1, ShadowAddGxTex());

    GxRsSet(GxRs_ColorOp0, 0);
    GxRsSet(GxRs_AlphaOp0, 0);
    GxRsSet(GxRs_ColorOp1, 0);
    GxRsSet(GxRs_AlphaOp1, 0);

    return 1;
}
