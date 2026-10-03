#include "object/client/CEffect.hpp"
#include "component/CCharacterComponent.hpp"
#include "console/CVar.hpp"
#include "db/Db.hpp"
#include "gx/Buffer.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "model/CM2Lighting.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2ParticleEmitter.hpp"
#include "model/CM2Scene.hpp"
#include "model/CM2Shared.hpp"
#include "object/Types.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/CMovement_C.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/SpellVisuals.hpp"
#include "sound/SI2.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "ui/game/CGGameUI.hpp"
#include "ui/game/PortraitButton.hpp"
#include "sound/SoundKitProperties.hpp"
#include "world/CWorld.hpp"
#include "world/Shadow.hpp"
#include "world/map/CMap.hpp"
#include "world/map/CMapEntity.hpp"
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <tempest/Math.hpp>
#include <cmath>

CEffect* CEffect::s_effectList;
CEffect* CEffect::s_finishedList;
int32_t CEffect::s_hardcodedEffects[12];

namespace {

// The twelve effects looked up by name (0x00ada764).
const char* const s_hardcodedEffectNames[12] = {
    "HARDCODED Footstep Water Run Spray",
    "HARDCODED Footstep Water Walk Spray",
    "HARDCODED Breath Underwater",
    "HARDCODED Breath Cold",
    "HARDCODED Loot Art",
    "HARDCODED Unit Level Up",
    "HARDCODED Mount Poof",
    "HARDCODED Inebriated Bubbles",
    "HARDCODED Meeting Stone Join",
    "HARDCODED Reputation",
    "HARDCODED Resist Spell",
    "HARDCODED Achievement Base",
};

// The attachment each one plays on (0x00a32af4). 50 is none: the two water sprays are placed, not
// attached, and go through InitializeHardcodedAt.
const int32_t s_hardcodedAttachments[12] = { 50, 50, 17, 17, 19, 19, 19, 17, 19, 19, 34, 19 };

// Sound_ListenerAtCharacter, looked up on first use by each of the two places that read it
// (0x00ca053c and 0x00ca0544).
CVar* ListenerAtCharacter() {
    static CVar* s_var = CVar::Lookup("Sound_ListenerAtCharacter");

    return s_var;
}

// ref: FUN_004e7a30
// An item's inventory type as the character component section it changes; 12 is none.
int32_t InventoryTypeToSection(int32_t inventoryType) {
    switch (inventoryType) {
        case 1:
            return 0;
        case 3:
            return 1;
        case 4:
            return 2;
        case 5:
        case 20:
            return 3;
        case 6:
            return 4;
        case 7:
            return 5;
        case 8:
            return 6;
        case 9:
            return 7;
        case 10:
            return 8;
        case 16:
            return 10;
        case 19:
            return 9;
        default:
            return 12;
    }
}

// The model's two draw bits, chosen by whether it hangs off a parent, set together.
void SetModelDrawn(CM2Model* model, uint32_t drawn) {
    if (!model->m_attachParent) {
        model->m_flag8 = drawn;
        model->m_flag10000 = drawn;
    } else {
        model->m_flag80 = drawn;
        model->m_flag20000 = drawn;
    }
}

bool TimeReached(uint32_t time) {
    uint32_t now = CWorld::GetCurTimeMs();

    return now != time && static_cast<int32_t>(now - time) > -1;
}

// ref: FUN_004f5930
// The handler a placed effect's world object gets: the map's visibility walk tells the effect
// whether it is drawn this frame.
int32_t PlacedEffectHandler(void* param, int32_t flags, uint32_t guidLow, uint32_t guidHigh, uint32_t param32) {
    auto effect = reinterpret_cast<CEffect*>(static_cast<uintptr_t>(param32));

    if (effect) {
        auto frame = CGWorldFrame::s_currentWorldFrame;
        effect->UpdateVisibility(frame ? frame->m_elapsed : 0.0f, static_cast<uint32_t>(flags));
    }

    return 1;
}

WOWGUID EffectContext(CEffect* effect) {
    return static_cast<WOWGUID>(reinterpret_cast<uintptr_t>(effect));
}

CEffect* EffectFromContext(WOWGUID owner) {
    return reinterpret_cast<CEffect*>(static_cast<uintptr_t>(owner));
}

void PlaceModelBounds(CM2Model* model, CAaBox& box, CAaSphere& sphere, C3Vector& collision) {
    box = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    sphere = { { 0.0f, 0.0f, 0.0f }, 0.0f };

    if (model->IsLoaded(0, 0)) {
        model->GetBoundingBox(box);
        model->GetBoundingSphere(sphere);
    }

    // The reference's collision centre is the sphere's x and y and a z of 1.0 (0x006f99d3).
    collision = { sphere.c.x, sphere.c.y, 1.0f };
}

} // namespace

// ------------------------------------------------------------------------------------------------
// ObjectEffect.cpp
// ------------------------------------------------------------------------------------------------

// ref: FUN_006f74b0
// LinkToHead's unlink half, which the reference also keeps as a function of its own -- so an
// effect can leave a list without joining another one.
void CEffect::Unlink() {
    if (this->m_linkPrev) {
        *this->m_linkPrev = this->m_linkNext;
    }

    if (this->m_linkNext) {
        this->m_linkNext->m_linkPrev = this->m_linkPrev;
    }

    this->m_linkPrev = nullptr;
    this->m_linkNext = nullptr;
}

// ref: FUN_006f74f0
void CEffect::ReleaseLightning() {
    for (auto& lightning : this->m_lightning) {
        if (lightning) {
            lightning->Release();
        }

        lightning = nullptr;
    }
}

// ref: FUN_006f7520
int32_t CEffect::LoadHardcodedEffects() {
    memset(CEffect::s_hardcodedEffects, 0, sizeof(CEffect::s_hardcodedEffects));

    for (int32_t i = 0; i < g_spellVisualEffectNameDB.GetNumRecords(); i++) {
        auto rec = g_spellVisualEffectNameDB.GetRecordByIndex(i);

        for (uint32_t j = 0; j < 12; j++) {
            if (!SStrCmp(s_hardcodedEffectNames[j], rec->m_name, STORM_MAX_STR)) {
                CEffect::s_hardcodedEffects[j] = rec->m_ID;
                break;
            }
        }
    }

    return 1;
}

// ref: FUN_006f75b0
const SpellVisualEffectNameRec* CEffect::GetHardcodedEffect(int32_t index) {
    return g_spellVisualEffectNameDB.GetRecord(CEffect::s_hardcodedEffects[index]);
}

// ref: FUN_006f75f0
// frozen leaves an emitter slot null for an emitter type it does not have (see
// CM2Model::InitializeLoaded), so a null slot is skipped; the reference has no such slots.
void CEffect::SetEmittersFlag400000(int32_t enable) {
    if (!this->m_model || !this->m_model->IsLoaded(0, 0)) {
        return;
    }

    for (uint32_t i = 0; ; i++) {
        auto model = this->m_model;

        if (!model->m_loaded) {
            model->WaitForLoad(nullptr);
        }

        if (model->m_shared->m_data->particles.Count() <= i) {
            break;
        }

        model = this->m_model;

        if (!model->m_loaded) {
            model->WaitForLoad(nullptr);
        }

        auto emitter = model->m_particleEmitters[i];

        if (!emitter) {
            continue;
        }

        if (enable == 0) {
            emitter->m_flags &= ~0x400000;
        } else {
            emitter->m_flags |= 0x400000;
        }
    }
}

// ref: FUN_006f7680
// The loaded callback every effect model gets. The bone id is 0xFFFFFFFF, which SetBoneSequence
// resolves to bone 0, and the sequence id is 0 -- the stand animation. The 0x400000 emitter flag is
// raised first when the effect asked for it, because the model has no emitters until it loads.
void CEffect::ModelLoadedCallback(CM2Model* model, void* arg) {
    auto effect = static_cast<CEffect*>(arg);

    if (effect->m_flags & EFFECT_EMITTERS_400000) {
        effect->SetEmittersFlag400000(1);
    }

    model->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);
}

// ref: FUN_006f76c0
void CEffect::LinkToHead(CEffect** head) {
    if (this->m_linkPrev) {
        *this->m_linkPrev = this->m_linkNext;
    }

    if (this->m_linkNext) {
        this->m_linkNext->m_linkPrev = this->m_linkPrev;
    }

    this->m_linkNext = nullptr;
    this->m_linkPrev = head;
    this->m_linkNext = *head;
    *head = this;

    if (this->m_linkNext) {
        this->m_linkNext->m_linkPrev = &this->m_linkNext;
    }
}

// ref: FUN_006f7720
// The effect's position in the world: m_position, or m_position through its transport. A
// transport that has gone takes the effect with it to where it last was, and the effect stops
// riding.
C3Vector& CEffect::GetPosition(C3Vector& out) {
    if (!this->m_transport) {
        out = this->m_position;

        return out;
    }

    C44Matrix transport;

    if (!MovementGetTransportMatrix(this->m_transport, transport)) {
        this->m_position = this->m_worldPosition;
        this->m_transport = 0;

        out = this->m_worldPosition;

        return out;
    }

    this->m_worldPosition = this->m_position * transport;
    out = this->m_worldPosition;

    return out;
}

// ref: FUN_006f7850
void CEffect::DetachModel() {
    if (this->m_model && this->m_model->m_attachParent) {
        this->m_model->DetachFromParent();
    }
}

// ref: FUN_006f7870
// Particles and ribbons go on and off TOGETHER, and the float at +0x17c moves with them.
void CEffect::SetEmission(int32_t enable) {
    if (!this->m_model) {
        return;
    }

    this->m_model->float17c = enable ? 1.0f : 0.0f;
    this->m_model->SetParticleEmission(enable);
    this->m_model->SetRibbonFlag8(enable);
}

// ref: FUN_006f78b0
// Stop the effect: hold the model's animation from now, and stop its emission. Zero is the
// not-held sentinel, so a scene whose time is genuinely 0 records 1.
void CEffect::Stop() {
    CM2Model* model = this->m_model;

    this->m_playState = 0;

    if (!model) {
        return;
    }

    uint32_t heldTime = model->m_scene->m_time;

    if (heldTime == 0) {
        heldTime = 1;
    }

    model->m_animationHeldTime = heldTime;

    if (this->m_model) {
        model->float17c = 0.0f;
        model->SetParticleEmission(0);
        model->SetRibbonFlag8(0);
    }
}

// ref: FUN_006f7900
// Start the effect: release the hold and let the emitters run. The state is written either way,
// which is what makes starting a running effect harmless.
void CEffect::Start() {
    if (this->m_playState == 0 && this->m_model) {
        this->m_model->m_animationHeldTime = 0;

        if (this->m_model) {
            this->m_model->float17c = 1.0f;
            this->m_model->SetParticleEmission(1);
            this->m_model->SetRibbonFlag8(1);
        }
    }

    this->m_playState = 2;
}

// ref: FUN_006f7950
// How much bigger than a person the owner is, for effects that scale with what they play on: the
// smaller horizontal extent of its model times 0.3, never below 1, times its model data's world
// effect scale.
float CEffect::GetOwnerScale(CGObject_C* owner) {
    auto model = owner->GetObjectModel();

    if (model && !model->IsLoaded(0, 1)) {
        return 1.0f;
    }

    float scale = 1.0f;

    if (owner->IsA(TYPE_UNIT)) {
        auto modelData = static_cast<CGUnit_C*>(owner)->GetModelData();

        if (modelData) {
            scale = modelData->m_worldEffectScale;
        }
    }

    // FROZEN-ONLY guard: the reference reads the box of a null model.
    if (!model) {
        return scale;
    }

    CAaBox box;
    model->GetBoundingBox(box);

    float extent = box.t.y - box.b.y;

    if (box.t.x - box.b.x < extent) {
        extent = box.t.x - box.b.x;
    }

    if (extent * 0.30000001192092896f <= 1.0f) {
        return 1.0f * scale;
    }

    return extent * 0.30000001192092896f * scale;
}

// ref: FUN_006f7a00
void CEffect::LinkToGlobalList() {
    this->LinkToHead(&CEffect::s_effectList);
}

// ------------------------------------------------------------------------------------------------
// Effect_C.cpp
// ------------------------------------------------------------------------------------------------

// ref: FUN_006f7b00
// What an effect model's authored events do: '$SND' plays the event's sound kit on the effect's
// sound object -- at the listener when it is the active mover's and the listener sits on the
// character -- and '$HIT' plays a wound animation on the unit the effect is on.
void CEffect::AnimEventCallback(CM2Model* model, uint32_t boneId, uint32_t eventId, uint32_t eventData,
                                const C3Vector* position, uint32_t a6, WOWGUID owner) {
    auto effect = EffectFromContext(owner);
    auto object = ClntObjMgrObjectPtr(effect->m_attachedTo, TYPE_OBJECT, ".\\Effect_C.cpp", 0x100);

    if (!object) {
        return;
    }

    if (eventId == 0x444E5324) {
        SoundKitProperties properties;
        SoundKitProperties* props = nullptr;

        if (object->IsA(TYPE_UNIT) && static_cast<CGUnit_C*>(object)->IsActiveMover()) {
            properties.ResetToDefaults();

            auto listener = ListenerAtCharacter();

            if (listener && listener->GetInt() != 0) {
                properties.m_fadeOutTime = 0.6499999761581421f;
                position = nullptr;
            }

            properties.int20 = 0x6E;
            props = &properties;
        }

        SI2::PlaySoundKit(static_cast<int32_t>(eventData), position, &effect->m_sound, props, 0, nullptr, 1, 0);

        if ((effect->m_flags & EFFECT_SOUND_UNTRACKED) == 0) {
            effect->m_sound.SetObjectGUID(object->GetGUID());
        }
    } else if (eventId == 0x54494824 && object->IsA(TYPE_UNIT)) {
        static_cast<CGUnit_C*>(object)->PlayWoundAnimation(0);
    }
}

// ref: FUN_006f7480
// A mount transition model's sequence 127 running out ends the transition.
void CEffect::MountSequenceDone(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5,
                                WOWGUID owner) {
    auto effect = EffectFromContext(owner);

    if (animId == 0x7F && effect->m_mountTransition) {
        effect->m_mountTransition->SetDone();
    }
}

// ref: FUN_006f7c40
// An effect played at a point near its owner -- a kit's world effect -- facing the way the owner
// faces, created now and placed when it first plays.
void CEffect::InitializeAtPoint(const C3Vector& position, uint32_t startTime, int32_t spellID,
                                const SpellVisualKitRec* kit, const SpellVisualEffectNameRec* effectName,
                                const WOWGUID& owner, uint32_t flags, M2SequenceDoneCallback sequenceDone,
                                uint32_t param, const SpellVisualKitModelAttachRec* attach,
                                WOWGUID transport) {
    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(owner, TYPE_UNIT, ".\\Effect_C.cpp", 0x1c4));

    if (!unit) {
        return;
    }

    this->m_effectName = effectName;
    this->m_owner = owner;
    this->m_position = position;
    this->m_flags = flags | 0x8002;
    this->m_transport = transport;
    this->m_kit = kit;
    this->m_spellID = spellID;
    this->m_startTime = startTime;
    this->m_param = param;
    this->m_facing = unit->GetWorldSmoothFacing();
    this->m_modelAttach = attach;

    this->m_model = CWorld::GetM2Scene()->CreateModel(this->m_effectName->m_fileName, 0);

    if (this->m_model) {
        this->m_refCount++;

        this->m_model->SetLoadedCallback(&CEffect::ModelLoadedCallback, this);
        this->m_model->SetSequenceDoneCallback(sequenceDone, EffectContext(this));
        this->m_model->SetAnimEventCallback(&CEffect::AnimEventCallback, EffectContext(this));

        if (this->m_flags & EFFECT_EMITTERS_400000) {
            this->SetEmittersFlag400000(1);
        }
    }

    this->LinkToHead(&unit->m_effects);
}

// ref: FUN_006f7d60
// Build an effect with no owner: record what it plays, create its model, register the three
// callbacks and join the global list. The model comes from the world's scene (0x00cd754c). The
// callbacks' owner is the effect itself, passed as the low half of a guid.
void CEffect::Initialize(int32_t spellID, const SpellVisualKitRec* kit,
                         const SpellVisualEffectNameRec* effectName, uint32_t flags,
                         M2SequenceDoneCallback sequenceDone, const C3Vector& position, uint32_t param,
                         WOWGUID transport) {
    this->m_effectName = effectName;
    this->m_attachment = -1;

    this->m_owner = 0;
    this->m_attachedTo = 0;

    this->m_position = position;
    this->m_transport = transport;

    this->m_spellID = spellID;
    this->m_kit = kit;

    // The reference stores the name a second time here.
    this->m_effectName = effectName;

    this->m_flags = flags | EFFECT_POSITIONED;
    this->m_param = param;

    this->m_model = CWorld::GetM2Scene()->CreateModel(effectName->m_fileName, 0);

    if (this->m_model) {
        this->m_refCount++;

        this->m_model->SetLoadedCallback(&CEffect::ModelLoadedCallback, this);
        this->m_model->SetSequenceDoneCallback(sequenceDone, EffectContext(this));
        this->m_model->SetAnimEventCallback(&CEffect::AnimEventCallback, EffectContext(this));

        if (this->m_flags & EFFECT_EMITTERS_400000) {
            this->SetEmittersFlag400000(1);
        }
    }

    this->LinkToHead(&CEffect::s_effectList);
}

// ref: FUN_006f7e40
// A kit's sound with no model: played at `position`, or at the owner and following it.
void CEffect::InitializeSound(int32_t soundKitID, uint32_t endTime, int32_t spellID,
                              const C3Vector* position, CGObject_C* owner, uint32_t param) {
    if (!owner) {
        return;
    }

    this->LinkToHead(&owner->m_effects);
    this->m_refCount++;

    this->m_endTime = endTime;
    this->m_spellID = spellID;
    this->m_owner = owner->GetGUID();
    this->m_param = param;

    SoundKitProperties properties;

    if (!position) {
        this->m_position = owner->GetPosition();
        position = &this->m_position;

        properties.ResetToDefaults();

        if (this->m_flags & EFFECT_SOUND_ON_OWNER) {
            properties.uint1c = 1;
            properties.uint40 = 1;
        } else {
            properties.uint1c = 2;

            if (CGUnit_C::s_activeMover == this->m_owner) {
                properties.int20 = 0x6E;
            }
        }
    } else {
        this->m_position = *position;

        properties.ResetToDefaults();

        if (this->m_flags & EFFECT_SOUND_ON_OWNER) {
            properties.uint1c = 1;
            properties.uint40 = 1;
        } else {
            properties.int30 = 0;
            properties.uint1c = 2;

            if (CGUnit_C::s_activeMover == this->m_owner) {
                properties.int20 = 0x6E;
            }
        }
    }

    SI2::PlaySoundKit(soundKitID, position, &this->m_sound, &properties, 0, nullptr, 1, 0);

    if ((this->m_flags & EFFECT_SOUND_UNTRACKED) == 0) {
        this->m_sound.SetObjectGUID(owner->GetGUID());
    }

    this->m_flags |= 0x100800;
}

// ref: FUN_006f7fd0
// Attach the effect to its owner's list and take a reference for it. Flags are OR'd and 0x800 is
// added, where Sub6f8040 assigns them.
void CEffect::AttachToOwner(int32_t spellID, const SpellVisualKitRec* kit, WOWGUID owner,
                            uint32_t flags, uint32_t param) {
    CGObject_C* object = ClntObjMgrObjectPtr(owner, TYPE_UNIT, ".\\Effect_C.cpp", 0x28e);

    if (!object) {
        return;
    }

    this->LinkToHead(&object->m_effects);

    this->m_refCount++;
    this->m_spellID = spellID;
    this->m_owner = owner;
    this->m_flags |= flags | EFFECT_PLAYED;
    this->m_kit = kit;
    this->m_param = param;
}

// ref: FUN_006f8040
// AttachToOwner's sibling: it assigns the flags rather than OR-ing, and records an end time.
void CEffect::Sub6f8040(uint32_t endTime, int32_t spellID, const SpellVisualKitRec* kit, WOWGUID owner,
                        uint32_t flags) {
    CGObject_C* object = ClntObjMgrObjectPtr(owner, TYPE_UNIT, ".\\Effect_C.cpp", 0x2ba);

    if (!object) {
        return;
    }

    this->LinkToHead(&object->m_effects);

    this->m_refCount++;
    this->m_kit = kit;
    this->m_endTime = endTime;
    this->m_spellID = spellID;
    this->m_owner = owner;
    this->m_flags = flags;
}

// ref: FUN_006f80b0
// Freeze the owner's animation (and its mount's) where it is, `holdSeconds` into the current
// sequence when that is given, until the effect ends. m_heldBody and m_heldMount record which of
// the two this effect froze, so it does not thaw one it did not.
void CEffect::HoldOwnerAnimation(uint32_t endTime, float holdSeconds, int32_t spellID,
                                 const SpellVisualKitRec* kit, const WOWGUID& owner, uint32_t flags) {
    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(owner, TYPE_UNIT, ".\\Effect_C.cpp", 0x2e5));

    if (!unit) {
        return;
    }

    this->LinkToHead(&unit->m_effects);
    this->m_refCount++;

    auto hold = [&](CM2Model* model, uint32_t& held) {
        if (!model || !model->IsLoaded(0, 0)) {
            held = 1;
            return;
        }

        held = model->m_animationHeldTime == 0 ? 1 : 0;

        if (0.0f < holdSeconds) {
            uint32_t time = static_cast<uint32_t>(llrintf(holdSeconds * 1000.0f));
            uint32_t anim = unit->GetCurrentAnimationId();

            M2SequenceInfo info;
            model->GetSequenceInfo(anim, 0, info);

            if (info.duration < time) {
                time = info.duration;
            }

            unit->SetBoneSequenceTimeOnPassengers(model, 0xFFFFFFFF, time, 0);
        }

        unit->SetAnimationHoldOnPassengers(model, 1, 0);
    };

    hold(unit->m_mountModel, this->m_heldMount);
    hold(unit->m_model, this->m_heldBody);

    this->m_kit = kit;
    this->m_endTime = endTime;
    this->m_spellID = spellID;
    this->m_owner = owner;
    this->m_flags |= flags | EFFECT_HOLDS_ANIMATION;
}

// ref: FUN_006f82d0
// An item visual takes its section of the owner's character component: the section is cleared
// and the item's display put in its place. Only a unit that draws through a component (a player,
// or section 0, the head) takes it.
void CEffect::ApplyItemSection() {
    if ((this->m_flags & EFFECT_ITEM_VISUAL) == 0 || this->m_itemSection >= 12 || this->m_displayID == 0) {
        return;
    }

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_owner, TYPE_UNIT, ".\\Effect_C.cpp", 0x33e));

    if (!unit) {
        return;
    }

    bool head = this->m_itemSection == 0;
    bool component = unit->DrawsThroughComponent() && unit->IsA(TYPE_PLAYER);

    if (!component && !head) {
        return;
    }

    auto display = g_itemDisplayInfoDB.GetRecord(this->m_displayID);

    if (!display || !unit->m_characterComponent) {
        return;
    }

    unit->m_characterComponent->RemoveItem(static_cast<ITEM_SLOT>(this->m_itemSection));
    unit->m_characterComponent->AddItem(static_cast<ITEM_SLOT>(this->m_itemSection), display, 0);

    // FUN_00512b50: the portrait refreshes.
    PortraitRefresh(this->m_owner, 3);
}

// ref: FUN_006f83d0
// A mount transition's model: the mount's own model, skinned, playing sequence 127.
int32_t CEffect::CreateMountModel() {
    auto display = this->m_displayID ? g_creatureDisplayInfoDB.GetRecord(this->m_displayID) : nullptr;

    if (!display) {
        return 0;
    }

    auto modelData = g_creatureModelDataDB.GetRecord(display->m_modelID);

    if (!modelData) {
        return 0;
    }

    this->m_model = CWorld::GetM2Scene()->CreateModel(modelData->m_modelName, 0);

    this->m_model->SetSequenceDoneCallback(&CEffect::MountSequenceDone, EffectContext(this));
    this->m_model->SetAnimEventCallback(&CEffect::AnimEventCallback, EffectContext(this));

    CCharacterComponent::ReplaceMonsterSkin(this->m_model, display, modelData);

    this->m_model->SetBoneSequence(0xFFFFFFFF, 0x7F, 0xFFFFFFFF, 0, 1.0f, 1, 1);

    SetModelDrawn(this->m_model, 0);
    this->m_model->SetAnimating(1);

    return 1;
}

// ref: FUN_006f84f0
// A kit model attach's offset and rotation applied to a placement: yaw, pitch and roll about the
// placement's own origin, then the offset turned by `facing` and added to where it was.
void CEffect::ApplyModelAttach(const SpellVisualKitModelAttachRec* attach, C44Matrix& matrix, float facing) {
    float x = matrix.d0;
    float y = matrix.d1;
    float z = matrix.d2;

    matrix.d0 = 0.0f;
    matrix.d1 = 0.0f;
    matrix.d2 = 0.0f;

    if (attach->m_yaw != 0.0f) {
        matrix.RotateAroundZ(-attach->m_yaw);
    }

    if (attach->m_pitch != 0.0f) {
        matrix.RotateAroundY(attach->m_pitch);
    }

    if (attach->m_roll != 0.0f) {
        matrix.RotateAroundX(attach->m_roll);
    }

    if (facing != 0.0f) {
        float s = sinf(facing - 1.5707963705062866f);
        float c = cosf(facing - 1.5707963705062866f);

        matrix.d0 = (c * attach->m_offset[1] + x) - attach->m_offset[0] * s;
        matrix.d1 = s * attach->m_offset[1] + attach->m_offset[0] * c + y;
        matrix.d2 = attach->m_offset[2] + z;

        return;
    }

    matrix.d0 = attach->m_offset[0] + x;
    matrix.d1 = y - attach->m_offset[1];
    matrix.d2 = attach->m_offset[2] + z;
}

// ref: FUN_006f8600
// The item cache's callback: the item's inventory type picks the section, its display what goes
// there.
void CEffect::OnItemArrived(uint32_t id, const WOWGUID* guid, void* param, bool found) {
    auto item = g_itemDB.GetRecord(static_cast<int32_t>(id));

    if (!item) {
        return;
    }

    auto effect = static_cast<CEffect*>(param);

    effect->m_itemSection = InventoryTypeToSection(item->m_inventoryType);
    effect->m_displayID = item->m_displayInfoID;

    effect->ApplyItemSection();
}

// ref: FUN_006f8650
void CEffect::InitializeItemVisual(uint32_t endTime, uint32_t itemID, int32_t spellID,
                                   const SpellVisualKitRec* kit, const WOWGUID& owner, uint32_t flags) {
    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(owner, TYPE_UNIT, ".\\Effect_C.cpp", 0x36b));

    if (!unit) {
        return;
    }

    this->m_endTime = endTime;
    this->m_spellID = spellID;
    this->m_kit = kit;
    this->m_owner = owner;
    this->m_flags |= flags | EFFECT_ITEM_VISUAL;
    this->m_pendingID = itemID;

    this->LinkToHead(&unit->m_effects);
    this->m_refCount++;

    if (g_itemCache.GetRecord(DBCACHEKEY32(this->m_pendingID), &owner, &CEffect::OnItemArrived, this, false)) {
        CEffect::OnItemArrived(itemID, &owner, this, true);
    }
}

// ref: FUN_006f8700
// Put the owner's own item back in the section an item visual took.
//
// The reference ends with `flags &= 0x80000`, which keeps ONLY the item-visual bit rather than
// clearing it. Transcribed as written.
void CEffect::RestoreItemSection() {
    if ((this->m_flags & EFFECT_ITEM_VISUAL) == 0) {
        return;
    }

    if (this->m_itemSection < 12) {
        auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_owner, TYPE_UNIT, ".\\Effect_C.cpp", 0x386));

        if (unit) {
            if (unit->m_characterComponent) {
                unit->m_characterComponent->RemoveItem(static_cast<ITEM_SLOT>(this->m_itemSection));
                unit->ReapplyItemSection(this->m_itemSection);

                PortraitRefresh(this->m_owner, 3);
            }

            this->m_itemSection = 12;
        }
    }

    if (this->m_pendingID) {
        g_itemCache.CancelCallback(DBCACHEKEY32(this->m_pendingID), &CEffect::OnItemArrived, nullptr);
        this->m_pendingID = 0;
    }

    this->m_flags &= EFFECT_ITEM_VISUAL;
    this->m_itemSection = 12;
}

// ref: FUN_006f87c0
// End the effect: it moves to the finished list, gives back any animation it froze, stops its
// sound, ends its mount transition, and lets its model fade on its own. Update releases it once
// the model has finished.
void CEffect::Finish() {
    this->LinkToHead(&CEffect::s_finishedList);
    this->m_flags |= 0x900;

    if (this->m_flags & EFFECT_HOLDS_ANIMATION) {
        auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_owner, TYPE_UNIT, ".\\Effect_C.cpp", 0x466));

        if (unit) {
            if (this->m_heldMount && unit->m_mountModel) {
                unit->m_mountModel->m_animationHeldTime = 0;
                this->m_heldMount = 0;
            }

            if (this->m_heldBody && unit->m_model) {
                unit->m_model->m_animationHeldTime = 0;
                this->m_heldBody = 0;
            }

            unit->UpdateAnimation(0, 0xFFFFFFFF);
        }
    }

    if (!this->m_sound.IsLooping()) {
        this->m_sound.Detach();
    } else {
        SI2::StopOrFadeOut(&this->m_sound, 0, 0.15000000596046448f, 1);
    }

    if (this->m_mountTransition) {
        auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_owner, TYPE_UNIT, ".\\Effect_C.cpp", 0x481));

        if (unit) {
            if (!this->m_mountTransition->IsDone()) {
                unit->ClearMountTransition();
            } else {
                unit->ReleaseMountTransition();
            }

            unit->UpdateAnimation(0, 0xFFFFFFFF);
        }

        MountTransitionObject::Release(this->m_mountTransition);
        this->m_mountTransition = nullptr;
    }

    if (this->m_model) {
        this->m_model->SetSequenceDoneCallback(nullptr, 0);
        this->m_model->SetAnimEventCallback(nullptr, 0);
        this->m_model->SetLoadedCallback(nullptr, nullptr);

        if (!this->m_model->m_attachParent) {
            this->m_model->m_flag8 = 0;
            this->m_model->m_flag10000 = 0;
        } else {
            this->m_model->m_flag80 = 0;
            this->m_model->m_flag20000 = 0;
        }

        if (!this->m_model->m_attachParent) {
            this->m_model->m_flag10000 = 1;
        } else {
            this->m_model->m_flag20000 = 1;
        }

        this->m_model->SetParticleEmission(0);
    }

    this->RestoreItemSection();
    this->ReleaseLightning();

    if (!this->m_linkPrev) {
        this->m_refCount++;
    }
}

// ref: FUN_006f8970
// A placed effect hears from the map walk once a frame: it starts when its time comes, is drawn
// when the walk says so (unless it is one that never fades), and finishes at its end time.
void CEffect::UpdateVisibility(float unused, uint32_t flags) {
    if ((this->m_startTime == 0 || TimeReached(this->m_startTime)) && this->m_model) {
        this->m_model->SetAnimating(1);

        if ((this->m_flags & EFFECT_NO_FADE) == 0 && this->m_model->IsLoaded(0, 0)) {
            SetModelDrawn(this->m_model, flags & 1);
            this->m_startTime = 0;
        }
    }

    if (this->m_endTime && TimeReached(this->m_endTime)) {
        this->Finish();
    }
}

// ref: FUN_006f8a60
// Finish every effect on `object` that plays the same thing at the same attachment -- a kit
// played twice replaces itself rather than stacking.
void CEffect::StopDuplicates(CGObject_C* object, const SpellVisualEffectNameRec* effectName,
                             int32_t attachment, int32_t spellID, int32_t kitID,
                             const SpellVisualKitModelAttachRec* attach) {
    if (attachment == -1) {
        return;
    }

    for (auto effect = object->m_effects; effect; ) {
        auto next = effect->m_linkNext;

        if (effect->m_effectName && effect->m_effectName->m_ID == effectName->m_ID
            && (!effect->m_kit || effect->m_kit->m_ID == kitID)
            && (!effect->m_modelAttach || effect->m_modelAttach == attach)
            && (spellID == 0 || effect->m_spellID == spellID)
            && effect->m_attachment == attachment) {
            effect->Finish();
        }

        effect = next;
    }
}

// ref: FUN_006f8ae0
// The placement of a placed effect: at its position, turned to its owner's facing (or its own,
// when it took one), scaled by the owner when it asks and clamped to the effect name's range, and
// moved by its kit model attach.
C44Matrix& CEffect::GetWorldTransform(C44Matrix& out, CGObject_C* owner) {
    out = C44Matrix();

    C3Vector pos;
    this->GetPosition(pos);
    out.d0 = pos.x;
    out.d1 = pos.y;
    out.d2 = pos.z;

    float facing = 0.0f;

    if (owner) {
        facing = owner->IsA(TYPE_UNIT)
            ? static_cast<CGUnit_C*>(owner)->GetWorldSmoothFacing()
            : owner->GetFacing();
    }

    if ((this->m_flags & EFFECT_FACING_FROM_OWNER) == 0) {
        if (owner) {
            out.RotateAroundZ(facing);
        }
    } else {
        out.RotateAroundZ(this->m_facing);
    }

    float scale = 1.0f;

    if ((this->m_flags & 0x200) && owner) {
        float ownerScale = CEffect::GetOwnerScale(owner);
        scale = owner->GetScale() * ownerScale;
    }

    if (this->m_effectName) {
        float s = scale * this->m_effectName->m_scale;
        float lo = this->m_effectName->m_minAllowedScale;
        float hi = this->m_effectName->m_maxAllowedScale;

        if (s < lo) {
            scale = lo;
        } else if (s < hi) {
            scale = s;
        } else {
            scale = hi;
        }
    }

    if (scale <= 0.0f) {
        scale = 1.0f;
    }

    out.Scale(scale);

    if (this->m_modelAttach) {
        CEffect::ApplyModelAttach(this->m_modelAttach, out,
                                  (this->m_flags & EFFECT_FACING_FROM_OWNER) ? this->m_facing : facing);
    }

    return out;
}

// ref: FUN_006f8c50
// Hang the effect's model on its attachment of the object it plays on, scaled to the model data's
// attached-effect scale and the effect name's range. An attachment the object's model lacks, or a
// model not yet loaded, ends the effect.
int32_t CEffect::UpdateAttachment() {
    if (this->m_attachment == -1) {
        if (this->m_model && this->m_model->m_attachParent) {
            this->m_model->DetachFromParent();
        }

        return 1;
    }

    CGObject_C* object;

    if (this->m_attachedTo) {
        object = ClntObjMgrObjectPtr(this->m_attachedTo, TYPE_OBJECT, ".\\Effect_C.cpp", 0x621);
    } else if (this->m_owner) {
        object = ClntObjMgrObjectPtr(this->m_owner, TYPE_OBJECT, ".\\Effect_C.cpp", 0x623);
    } else {
        object = nullptr;
    }

    if (!object) {
        if (this->m_model && this->m_model->m_attachParent) {
            this->m_model->DetachFromParent();
        }

        return 0;
    }

    auto parent = object->m_model;

    if (!parent || parent->IsLoaded(0, 0)) {
        auto unit = object->IsA(TYPE_UNIT) ? static_cast<CGUnit_C*>(object) : nullptr;
        auto modelData = unit ? unit->GetModelData() : nullptr;

        if ((this->m_attachment == 2 || this->m_attachment == 1) && modelData
            && unit->GetModelData() && (unit->GetModelData()->m_flags & 0x10)) {
            this->DetachModel();

            return 0;
        }

        if (parent && parent->HasAttachment(this->m_attachment)) {
            float scale = modelData ? modelData->m_attachedEffectScale : 1.0f;

            if (this->m_effectName) {
                scale = scale * this->m_effectName->m_scale;

                float attached = parent->GetAttachmentScale(this->m_attachment) * scale;

                if (9.999999974752427e-07f < attached) {
                    if (this->m_effectName->m_maxAllowedScale < attached) {
                        scale = (scale / attached) * this->m_effectName->m_maxAllowedScale;
                    } else if (attached < this->m_effectName->m_minAllowedScale) {
                        scale = scale * (this->m_effectName->m_minAllowedScale / attached);
                    }
                }
            }

            if (this->m_model && (scale != 1.0f || this->m_modelAttach)) {
                C44Matrix matrix;

                if (this->m_modelAttach) {
                    CEffect::ApplyModelAttach(this->m_modelAttach, matrix, 0.0f);
                }

                matrix.Scale(scale);

                // FUN_004d8630
                this->m_model->m_flag8000 = 1;
                this->m_model->matrixB4 = matrix;
            }

            const C3Vector* offset = (this->m_flags & EFFECT_ATTACH_OFFSET) ? &this->m_attachOffset : nullptr;

            if (this->m_model && (this->m_model->m_attachParent != parent
                                  || this->m_model->m_attachId != static_cast<uint32_t>(this->m_attachment))) {
                this->DetachModel();
                this->m_model->AttachToParent(parent, static_cast<uint32_t>(this->m_attachment), offset, 0);
            }

            if (!offset) {
                this->m_position = parent->GetAttachmentWorldPosition(static_cast<uint32_t>(this->m_attachment));
            } else {
                this->m_position = this->m_attachOffset;
            }

            this->m_transport = 0;

            if (this->m_flags & EFFECT_RIBBONS_TRAIL) {
                this->m_model->m_flag100000 = 1;
            }

            object->m_flag21 = 1;

            return 1;
        }
    }

    if (this->m_model && this->m_model->m_attachParent) {
        this->m_model->DetachFromParent();
    }

    this->Finish();

    return 0;
}

// ref: FUN_006f8f40
void CEffect::ReleaseUse() {
    this->m_uses--;

    if (this->m_uses == 0) {
        this->Finish();
    }
}

// ref: FUN_006f8f50
// A fishing line: 65 points from the rod's $CCH event to a little above the bobber, sagging in
// the middle, drawn as one line strip in the sun's ambient colour with lighting and texture off.
void CEffect::DrawFishingLine(CM2Model* model, CM2Lighting* lighting, void* arg) {
    auto effect = static_cast<CEffect*>(arg);

    if (!model->HasEvent(0x48434324)) {
        return;
    }

    auto bobber = ClntObjMgrObjectPtr(effect->m_attachedTo, TYPE_GAMEOBJECT, ".\\Effect_C.cpp", 0x130);

    if (!bobber) {
        return;
    }

    C3Vector start;
    model->GetEventWorldPosition(start, 0x48434324);

    C3Vector end = bobber->GetPosition();
    end.z = bobber->GetBaseScale() * bobber->m_height * 0.20000000298023224f + end.z;

    C3Vector d = { end.x - start.x, end.y - start.y, end.z - start.z };

    auto stream = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x10, 0x41);
    auto vertices = reinterpret_cast<uint32_t*>(g_theGxDevicePtr->BufLock(stream));

    auto channel = [](float v) -> uint32_t {
        if (!(0.0f < v)) {
            return 0;
        }

        return static_cast<uint32_t>(static_cast<int32_t>(lrintf(v < 1.0f ? v * 255.0f + 0.5f : 255.0f))) & 0xFF;
    };

    const C3Vector& ambient = lighting->m_sunAmbient;
    uint32_t color = 0xFF000000 | (channel(ambient.x) << 16) | (channel(ambient.y) << 8) | channel(ambient.z);
    DecalFixupColor(color);

    for (int32_t i = 0; i < 0x41; i++) {
        float t = static_cast<float>(i) * 0.015625f;

        auto position = reinterpret_cast<float*>(vertices + i * 4);
        position[0] = d.x * t + start.x;
        position[1] = d.y * t + start.y;
        position[2] = d.z * t + start.z;
        position[2] = position[2] - sinf(t * 3.1415927410125732f) * 0.5f;
        vertices[i * 4 + 3] = color;
    }

    g_theGxDevicePtr->BufUnlock(stream, 0);
    stream->unk1C = 1;
    GxPrimVertexPtr(stream, GxVBF_PC);

    GxRsPush();
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Texture0, static_cast<CGxTex*>(nullptr));

    CGxBatch batch;
    batch.m_primType = GxPrim_LineStrip;
    batch.m_start = 0;
    batch.m_count = 0x41;
    batch.m_minIndex = 0;
    batch.m_maxIndex = 0x40;

    g_theGxDevicePtr->Draw(&batch, 0);

    GxRsPop();
}

// ref: FUN_006f9260
// One of the hard-coded effects on a unit, at its attachment -- the chest when the model lacks the
// one the table names.
void CEffect::InitializeHardcoded(int32_t index, const WOWGUID& owner, M2SequenceDoneCallback sequenceDone) {
    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(owner, TYPE_UNIT, ".\\Effect_C.cpp", 0x179));

    if (!unit || index >= 12) {
        return;
    }

    int32_t attachment = s_hardcodedAttachments[index];

    if (attachment == 0x32 || !unit->m_model) {
        return;
    }

    this->m_owner = owner;
    this->m_attachedTo = owner;
    this->m_flags = 0;
    this->m_attachment = attachment;

    if (unit->m_model->IsLoaded(0, 0) && !unit->m_model->HasAttachment(this->m_attachment)) {
        this->m_attachment = 0x13;
    }

    this->m_effectName = g_spellVisualEffectNameDB.GetRecord(CEffect::s_hardcodedEffects[index]);

    CEffect::StopDuplicates(unit, this->m_effectName, attachment, 0, 0, nullptr);

    this->m_model = CWorld::GetM2Scene()->CreateModel(this->m_effectName->m_fileName, 0);

    if (this->m_model) {
        this->m_refCount++;

        this->m_model->SetLoadedCallback(&CEffect::ModelLoadedCallback, this);
        this->m_model->SetSequenceDoneCallback(sequenceDone, EffectContext(this));
        this->m_model->SetAnimEventCallback(&CEffect::AnimEventCallback, EffectContext(this));
    }

    this->LinkToHead(&unit->m_effects);
}

// ref: FUN_006f93a0
// One of the hard-coded effects placed at a point by a unit -- the water sprays a run or walk
// kicks up.
void CEffect::InitializeHardcodedAt(int32_t index, const C3Vector& position, const WOWGUID& owner,
                                    uint32_t flags, M2SequenceDoneCallback sequenceDone) {
    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(owner, TYPE_UNIT, ".\\Effect_C.cpp", 0x1a0));

    if (!unit) {
        return;
    }

    this->m_owner = owner;
    this->m_position = position;
    this->m_flags = flags | EFFECT_POSITIONED;
    this->m_transport = 0;
    this->m_effectName = CEffect::GetHardcodedEffect(index);

    this->m_model = CWorld::GetM2Scene()->CreateModel(this->m_effectName->m_fileName, 0);

    if (this->m_model) {
        this->m_refCount++;

        this->m_model->SetLoadedCallback(&CEffect::ModelLoadedCallback, this);
        this->m_model->SetSequenceDoneCallback(sequenceDone, EffectContext(this));
        this->m_model->SetAnimEventCallback(&CEffect::AnimEventCallback, EffectContext(this));

        if (this->m_flags & EFFECT_EMITTERS_400000) {
            this->SetEmittersFlag400000(1);
        }
    }

    this->LinkToHead(&unit->m_effects);
}

// ref: FUN_006f94c0
// A kit's effect at an attachment of `target`, on behalf of `owner`. The same effect already on
// that attachment is finished first.
void CEffect::InitializeAttached(int32_t attachment, int32_t spellID, const SpellVisualKitRec* kit,
                                 const SpellVisualEffectNameRec* effectName, const WOWGUID& owner,
                                 const WOWGUID& target, uint32_t flags, M2SequenceDoneCallback sequenceDone,
                                 const C3Vector* offset, uint32_t param,
                                 const SpellVisualKitModelAttachRec* attach) {
    auto object = ClntObjMgrObjectPtr(target, TYPE_OBJECT, ".\\Effect_C.cpp", 0x1ee);

    if (!object) {
        return;
    }

    this->m_effectName = effectName;

    CEffect::StopDuplicates(object, effectName, attachment, spellID, kit ? kit->m_ID : 0, attach);

    // "**** CEffect::AddEffect called with ATTACH_NONE" in the reference when attachment is -1.

    this->m_attachment = attachment;
    this->m_owner = owner;
    this->m_attachedTo = target;
    this->m_kit = kit;
    this->m_param = param;
    this->m_modelAttach = attach;
    this->m_spellID = spellID;
    this->m_effectName = effectName;
    this->m_flags = flags;

    if (offset) {
        this->m_attachOffset = *offset;
        this->m_flags = flags | EFFECT_ATTACH_OFFSET;
    }

    this->m_model = CWorld::GetM2Scene()->CreateModel(effectName->m_fileName, 0);

    if (this->m_model) {
        this->m_refCount++;

        this->m_model->SetLoadedCallback(&CEffect::ModelLoadedCallback, this);
        this->m_model->SetSequenceDoneCallback(sequenceDone, EffectContext(this));
        this->m_model->SetAnimEventCallback(&CEffect::AnimEventCallback, EffectContext(this));

        if (this->m_flags & EFFECT_EMITTERS_400000) {
            this->SetEmittersFlag400000(1);
        }
    }

    this->LinkToHead(&object->m_effects);
}

// ref: FUN_006f9610
// The creature cache's callback for a mount: its first display becomes the transition's model.
// The guid handed to the cache is the reference's: the spell id under the 0x1fe00000 high word.
void CEffect::OnMountCreatureArrived(uint32_t id, const WOWGUID* guid, void* param, bool found) {
    auto effect = static_cast<CEffect*>(param);

    auto rec = g_creatureCache.Peek(DBCACHEKEY32(id));

    if (!rec) {
        return;
    }

    effect->m_displayID = rec->m_displayID[0];
    effect->m_pendingID = 0;

    effect->CreateMountModel();
    effect->UpdateAttachment();
}

// ref: FUN_006f9670
// The leap onto a mount: the spell's MOUNTED aura names the mount creature, whose display is the
// model the transition plays, and the unit is told a transition is running.
void CEffect::InitializeMountTransition(int32_t spellID, const SpellVisualKitRec* kit, const WOWGUID& owner) {
    this->m_owner = owner;
    this->m_spellID = spellID;
    this->m_kit = kit;
    this->m_attachment = 0x13;

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_owner, TYPE_UNIT, ".\\Effect_C.cpp", 0x3cb));

    if (!unit) {
        return;
    }

    auto spell = g_spellDB.GetRecord(spellID);

    if (!spell) {
        return;
    }

    this->m_pendingID = 0;
    this->m_displayID = 0;

    for (uint32_t i = 0; i < 3; i++) {
        if (spell->m_effect[i] == 6 && spell->m_effectAura[i] == 0x4E) {
            uint32_t creature = static_cast<uint32_t>(spell->m_effectMiscValue[i]);
            WOWGUID key = (static_cast<WOWGUID>(0x1FE00000) << 32) | static_cast<uint32_t>(spellID);

            this->m_pendingID = creature;

            auto rec = g_creatureCache.GetRecord(DBCACHEKEY32(creature), &key, &CEffect::OnMountCreatureArrived, this, false);

            if (rec) {
                this->m_displayID = rec->m_displayID[0];
                this->m_pendingID = 0;
                break;
            }
        }
    }

    int32_t ok = this->m_displayID ? this->CreateMountModel() : static_cast<int32_t>(this->m_pendingID);

    if (ok) {
        this->LinkToHead(&unit->m_effects);
        this->m_refCount++;

        this->m_mountTransition = MountTransitionObject::Create(unit);
        unit->SetMountTransition(this->m_mountTransition, this);
    }
}

// ref: FUN_006f97d0
// A fishing line: the rod is the owner's model's child on attachment 1, and the line is drawn by
// the rod's draw callback.
void CEffect::AttachFishingLine(CGObject_C* owner, const WOWGUID& bobber) {
    if (!owner->m_model) {
        return;
    }

    CM2Model* rod = nullptr;

    for (auto child = owner->m_model->m_attachList; child; child = child->m_attachNext) {
        if (child->m_attachId == 1) {
            rod = child;
            break;
        }
    }

    // FROZEN-ONLY guard: the reference dereferences a missing rod.
    if (!rod) {
        return;
    }

    this->m_lineModel = rod;
    rod->m_refCount++;

    rod->m_flag20 = 1;
    rod->m_drawCallback = &CEffect::DrawFishingLine;
    rod->m_drawCallbackArg = this;

    this->m_owner = owner->GetGUID();
    this->m_attachedTo = bobber;
}

// ref: FUN_006f9840
// The effect's first frame: a placed effect gets its world object and placement, the model takes
// its spell's 0x4 bit, the attachment is made, the kit's camera shake and sound play.
void CEffect::Play() {
    if (this->m_model && !this->m_model->IsLoaded(0, 0)) {
        return;
    }

    this->m_flags |= EFFECT_PLAYED;

    bool playSound = (this->m_flags & EFFECT_FROM_SPELL_NO_SOUND) == 0 && this->m_kit
        && this->m_kit->m_soundID != 0 && this->m_kit->m_soundID != -1;

    if ((this->m_flags & EFFECT_POSITIONED) && !this->m_worldObject && this->m_model) {
        auto owner = ClntObjMgrObjectPtr(this->m_owner, TYPE_OBJECT, ".\\Effect_C.cpp", 0x599);

        C44Matrix matrix;
        this->GetWorldTransform(matrix, owner);

        this->m_model->m_flag8000 = 1;
        this->m_model->matrixB4 = matrix;

        this->m_worldObject = CWorldAddPlacedObject(this->m_model, this->m_model->matrixB4,
                                                    reinterpret_cast<void*>(&PlacedEffectHandler), nullptr,
                                                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this)));

        CAaBox box;
        CAaSphere sphere;
        C3Vector collision;
        PlaceModelBounds(this->m_model, box, sphere, collision);

        CWorld::UpdateObject(this->m_worldObject, this->m_model->matrixB4, box, sphere, collision, 0, 0xFFFFFFFF);

        int32_t animate = this->m_startTime == 0 || TimeReached(this->m_startTime) ? 1 : 0;

        this->m_model->SetAnimating(animate);
        SetModelDrawn(this->m_model, static_cast<uint32_t>(animate));
    }

    auto spell = this->m_spellID ? g_spellDB.GetRecord(this->m_spellID) : nullptr;

    if (spell && (spell->m_attributesEx5 & 0x40000000)) {
        this->m_model->m_flag4 = 1;
    }

    if (!this->UpdateAttachment()) {
        return;
    }

    if ((this->m_flags & EFFECT_SHAKE_CAMERA) && this->m_kit && this->m_kit->m_shakeID > 0 && this->m_kit->m_shakeID != -1) {
        C3Vector pos;
        this->GetPosition(pos);
        SpellVisualsShakeCamera(this->m_kit->m_shakeID, pos);
    }

    if (!playSound) {
        return;
    }

    CGObject_C* listener = nullptr;

    if (this->m_attachedTo) {
        listener = ClntObjMgrObjectPtr(this->m_attachedTo, TYPE_OBJECT, ".\\Effect_C.cpp", 0x5c5);
    } else if (this->m_owner && (this->m_flags & EFFECT_WITHOUT_OWNER) == 0) {
        listener = ClntObjMgrObjectPtr(this->m_owner, TYPE_OBJECT, ".\\Effect_C.cpp", 0x5c7);
    }

    SoundKitProperties properties;
    properties.ResetToDefaults();

    bool isListener = GetSoundListenerObject() == listener;
    auto atCharacter = ListenerAtCharacter();
    bool useCharacterListener = isListener && atCharacter && atCharacter->GetInt() != 0
        && (this->m_flags & EFFECT_SOUND_UNTRACKED) == 0;

    if (isListener || this->m_owner == CGUnit_C::s_activeMover) {
        properties.int20 = 0x6E;
    }

    if (CGGameUI::InCinematic()) {
        return;
    }

    if ((this->m_flags & EFFECT_SOUND_ON_OWNER) == 0) {
        const C3Vector* position = nullptr;
        C3Vector pos;

        properties.uint1c = 2;
        properties.int30 = 0;

        if (useCharacterListener) {
            properties.m_fadeOutTime = 0.6499999761581421f;
        } else {
            this->GetPosition(pos);
            position = &pos;
        }

        SI2::PlaySoundKit(this->m_kit->m_soundID, position, &this->m_sound, &properties, 0, nullptr, 1, 0);

        if (useCharacterListener || (this->m_flags & EFFECT_SOUND_UNTRACKED) || !listener) {
            return;
        }

        this->m_sound.SetObjectGUID(listener->GetGUID());

        return;
    }

    if (!listener) {
        C3Vector pos;
        this->GetPosition(pos);
        SI2::PlaySoundKit(this->m_kit->m_soundID, &pos, &this->m_sound, nullptr, 0, nullptr, 1, 0);

        return;
    }

    C3Vector pos = listener->GetPosition();

    if (this->m_flags & 0x1000) {
        properties.byte38 = 1;
        properties.int30 = 0;
    }

    properties.uint40 = 1;

    SI2::PlaySoundKit(this->m_kit->m_soundID, &pos, &this->m_sound, &properties, 0, nullptr, 1, 0);

    if (this->m_flags & EFFECT_SOUND_UNTRACKED) {
        return;
    }

    this->m_sound.SetObjectGUID(listener->GetGUID());
}

// ref: FUN_006fa050
// Once a frame while the effect plays: play it if it has not played, count its emission down,
// follow its owner, and end it when its time is up -- giving back the owner's animation or scale
// when it held them -- or when its sound stops, for a sound-only effect.
void CEffect::Update() {
    if ((this->m_flags & 0x900) == 0) {
        this->Play();
    }

    if ((this->m_flags & EFFECT_EMISSION_COUNTDOWN) && 0 < static_cast<int32_t>(this->m_playState)) {
        this->m_playState--;

        if (this->m_playState == 0 && this->m_model) {
            this->m_model->float17c = 0.0f;
            this->m_model->SetParticleEmission(0);
            this->m_model->SetRibbonFlag8(0);
        }
    }

    if ((this->m_flags & EFFECT_FOLLOWS_OWNER) || ((this->m_flags & EFFECT_POSITIONED) && this->m_transport)) {
        auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_owner, TYPE_UNIT, ".\\Effect_C.cpp", 0x4d7));

        if (unit && this->m_worldObject && this->m_model) {
            if (this->m_flags & EFFECT_FOLLOWS_OWNER) {
                this->m_position = unit->GetRawPosition();

                if (unit->m_localMove.GetMoveFlags() & 0x40000000) {
                    this->m_position.z -= unit->Unit()->hoverHeight;
                }

                this->m_transport = unit->GetTransportGUID();
            }

            C44Matrix matrix;
            this->GetWorldTransform(matrix, unit);

            CAaBox box;
            CAaSphere sphere;
            C3Vector collision;
            PlaceModelBounds(this->m_model, box, sphere, collision);

            this->m_model->m_flag8000 = 1;
            this->m_model->matrixB4 = matrix;

            CWorld::UpdateObject(this->m_worldObject, this->m_model->matrixB4, box, sphere, collision, 0, 0xFFFFFFFF);
        }
    }

    if ((this->m_flags & EFFECT_HOLDS_ANIMATION) && this->m_endTime && TimeReached(this->m_endTime)) {
        auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_owner, TYPE_UNIT, ".\\Effect_C.cpp", 0x4f6));

        if (!unit) {
            return;
        }

        if (this->m_heldMount && unit->m_mountModel) {
            unit->m_mountModel->m_animationHeldTime = 0;
            this->m_heldMount = 0;
        }

        if (this->m_heldBody && unit->m_model) {
            unit->m_model->m_animationHeldTime = 0;
            this->m_heldBody = 0;
        }

        unit->UpdateAnimation(0, 0xFFFFFFFF);
        this->Finish();
    } else if (this->m_scaleResetTime && TimeReached(this->m_scaleResetTime)) {
        auto object = ClntObjMgrObjectPtr(this->m_owner, TYPE_OBJECT, ".\\Effect_C.cpp", 0x50c);

        if (!object) {
            return;
        }

        object->m_scaleEaseStart = 0;
        object->m_scaleMultiplier = 1.0f;
        this->Finish();
    } else if ((this->m_flags & EFFECT_NO_EXPIRE) == 0 && this->m_endTime && TimeReached(this->m_endTime)) {
        this->Finish();
    }

    if ((this->m_flags & EFFECT_SOUND_ONLY) && !SI2::IsPlaying(&this->m_sound)) {
        this->Finish();
    }
}

// ref: FUN_006f9d70
// Every field takes its constructor value from its initializer above: -1 for the attachment and
// +0x8c, 12 for the item section, 1 for the reference count, zero for the rest.
CEffect::CEffect() {
}

// ref: FUN_006f9ec0
CEffect::~CEffect() {
    if (this->m_model) {
        if (this->m_model->m_attachParent) {
            this->m_model->DetachFromParent();
        }

        this->m_model->SetSequenceDoneCallback(nullptr, 0);
        this->m_model->SetAnimEventCallback(nullptr, 0);
        this->m_model->SetLoadedCallback(nullptr, nullptr);

        if (!this->m_model->m_attachParent) {
            this->m_model->m_flag8 = 0;
            this->m_model->m_flag10000 = 0;
        } else {
            this->m_model->m_flag80 = 0;
            this->m_model->m_flag20000 = 0;
        }

        this->m_model->Release();
        this->m_model = nullptr;
    }

    if (this->m_lineModel) {
        this->m_lineModel->m_flag20 = 1;
        this->m_lineModel->m_drawCallback = nullptr;
        this->m_lineModel->m_drawCallbackArg = nullptr;
        this->m_lineModel->Release();
    }

    if (this->m_linkPrev) {
        *this->m_linkPrev = this->m_linkNext;
    }

    if (this->m_linkNext) {
        this->m_linkNext->m_linkPrev = this->m_linkPrev;
    }

    this->m_linkNext = nullptr;
    this->m_linkPrev = nullptr;

    if (this->m_worldObject) {
        CWorld::RemoveObject(this->m_worldObject);
    }

    if (!this->m_sound.IsLooping()) {
        this->m_sound.Detach();
    } else {
        SI2::StopOrFadeOut(&this->m_sound, 0, 0.15000000596046448f, 1);
    }

    if (this->m_mountTransition) {
        auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_owner, TYPE_UNIT, ".\\Effect_C.cpp", 0xa4));

        if (unit) {
            unit->ClearMountTransition();
            unit->UpdateAnimation(0, 0xFFFFFFFF);
        }

        MountTransitionObject::Release(this->m_mountTransition);
        this->m_mountTransition = nullptr;

        if (this->m_pendingID) {
            g_creatureCache.CancelCallback(DBCACHEKEY32(this->m_pendingID), &CEffect::OnMountCreatureArrived, this);
            this->m_pendingID = 0;
        }
    }

    this->RestoreItemSection();
    this->ReleaseLightning();
}

// ref: FUN_006fa390
void CEffect::Release() {
    this->m_refCount--;

    if (this->m_refCount == 0) {
        this->~CEffect();
        SMemFree(this, "delete", 0xFFFFFFFF, 0);
    }
}

CEffect* CEffect::Create() {
    void* mem = SMemAlloc(sizeof(CEffect), ".\\Object_C.cpp", 0x5a3, 0);

    return mem ? new (mem) CEffect() : nullptr;
}

// ref: FUN_006fa3c0
// FROZEN-ONLY: an effect whose count does not reach zero is unlinked rather than left at the head,
// where the reference's loop would take it again forever.
void CEffect::ReleaseAll() {
    while (auto effect = CEffect::s_finishedList) {
        effect->m_refCount--;

        if (effect->m_refCount == 0) {
            effect->~CEffect();
            SMemFree(effect, "delete", 0xFFFFFFFF, 0);
        } else {
            effect->Unlink();
        }
    }

    CEffect::s_finishedList = nullptr;

    while (auto effect = CEffect::s_effectList) {
        effect->m_refCount--;

        if (effect->m_refCount == 0) {
            effect->~CEffect();
            SMemFree(effect, "delete", 0xFFFFFFFF, 0);
        } else {
            effect->Unlink();
        }
    }

    CEffect::s_effectList = nullptr;
}

// ref: FUN_006fa450
// Every playing effect updates when its start time has come and is let go at its end time; every
// finished effect is let go once its model has stopped drawing particles.
void CEffect::UpdateAll() {
    for (auto effect = CEffect::s_effectList; effect; ) {
        auto next = effect->m_linkNext;

        if (effect->m_startTime == 0 || TimeReached(effect->m_startTime)) {
            effect->Update();
        }

        if (effect->m_endTime != 0 && TimeReached(effect->m_endTime)) {
            effect->Unlink();
            effect->m_refCount--;

            if (effect->m_refCount == 0) {
                effect->~CEffect();
                SMemFree(effect, "delete", 0xFFFFFFFF, 0);
            }
        }

        effect = next;
    }

    for (auto effect = CEffect::s_finishedList; effect; ) {
        auto next = effect->m_linkNext;

        if (!effect->m_model || !effect->m_model->m_flag400) {
            effect->Unlink();
            effect->m_refCount--;

            if (effect->m_refCount == 0) {
                effect->~CEffect();
                SMemFree(effect, "delete", 0xFFFFFFFF, 0);
            }
        }

        effect = next;
    }
}

// ref: FUN_006fa5b0
// The sparkle a lootable corpse shows: hard-coded effect 4 at the chest.
void CEffect::InitializeLootArt(CGObject_C* object, M2SequenceDoneCallback sequenceDone) {
    this->m_owner = object->GetGUID();
    this->m_flags = EFFECT_LOOT_ART;
    this->m_attachment = 0x13;
    this->m_effectName = CEffect::GetHardcodedEffect(4);

    this->m_model = CWorld::GetM2Scene()->CreateModel(this->m_effectName->m_fileName, 0);

    if (!this->m_model) {
        this->~CEffect();
        SMemFree(this, "delete", 0xFFFFFFFF, 0);

        return;
    }

    if (this->m_flags & EFFECT_EMITTERS_400000) {
        this->SetEmittersFlag400000(1);
    }

    this->m_model->SetLoadedCallback(&CEffect::ModelLoadedCallback, this);
    this->m_model->SetSequenceDoneCallback(sequenceDone, EffectContext(this));
    this->m_model->SetAnimEventCallback(&CEffect::AnimEventCallback, EffectContext(this));

    this->LinkToHead(&object->m_effects);
    this->m_refCount++;
}

// ------------------------------------------------------------------------------------------------
// World.cpp
// ------------------------------------------------------------------------------------------------

// ref: FUN_00782000
// AddObject's sibling for a world object placed by a matrix: the entity takes its position and
// scale from the matrix, starts lit by the sun's ambient, and is linked into the map at once rather
// than waiting for its first update. Its state word keeps only the bits outside 0x586f and takes
// 0x400 (keep animating).
HWORLDOBJECT CWorldAddPlacedObject(CM2Model* model, const C44Matrix& matrix, void* handler,
                                   void* handlerParam, uint32_t param32) {
    auto entity = CMap::AllocEntity(false);

    entity->m_type |= CMapBaseObj::Type_200;
    entity->m_flags7c = (entity->m_flags7c & 0xFFFFA790) | 0x400;
    entity->m_flags = 1;
    entity->m_handler = nullptr;

    const C3Vector& amb = CMap::s_outdoorLight->m_light.m_ambColor;

    auto channel = [](float v) {
        if (!(0.0f < v)) {
            return 0.0f;
        }

        return v < 1.0f ? v * 255.0f + 0.5f : 255.0f;
    };

    CImVector start;
    start.b = static_cast<uint8_t>(static_cast<int32_t>(channel(amb.z)));
    start.g = static_cast<uint8_t>(static_cast<int32_t>(channel(amb.y)));
    start.r = static_cast<uint8_t>(static_cast<int32_t>(channel(amb.x)));
    start.a = 0xFF;
    entity->m_ambientTarget = start;
    entity->m_ambient = start;

    entity->m_param32 = param32;
    entity->m_model = model;

    entity->m_sphere.c = { matrix.d0, matrix.d1, matrix.d2 };
    entity->m_sphere.r = 0.0f;
    entity->m_dirLightScale = 1.0f;
    entity->m_dirLightScaleTarget = 1.0f;
    entity->m_collisionCenter = entity->m_sphere.c;

    entity->m_scale = sqrtf(matrix.a0 * matrix.a0 + matrix.a1 * matrix.a1 + matrix.a2 * matrix.a2);
    entity->m_position = { matrix.d0, matrix.d1, matrix.d2 };
    entity->m_flags7c |= 0x400;

    RelinkEntity(entity);

    if (entity->m_model) {
        if (!SStrCmpI("InvisibleStalker.m2", entity->m_model->m_shared->m_filePath, STORM_MAX_STR)) {
            entity->m_flags7c |= 0x4000;
        }

        entity->m_model->m_lightingCallback = &CWorld::LightingCallback;
        entity->m_model->m_lightingArg = entity;
        entity->m_model->m_refCount++;
    }

    entity->m_handler = handler;
    entity->m_handlerParam = handlerParam;

    return reinterpret_cast<HWORLDOBJECT>(entity);
}
