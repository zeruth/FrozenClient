#include "object/client/CEffect.hpp"
#include "model/CM2Scene.hpp"
#include "object/Types.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "world/CWorld.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2ParticleEmitter.hpp"
#include "model/CM2Shared.hpp"

CEffect* CEffect::s_effectList;

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

// ref: FUN_006f7850
void CEffect::DetachModel() {
    if (this->m_model && this->m_model->m_attachParent) {
        this->m_model->DetachFromParent();
    }
}

// ref: FUN_006f74b0
// LinkToHead's unlink half, which the reference also keeps as a function of its own -- so an
// effect can leave a list without joining another one.
//
// Writing through m_linkPrev is what makes this work without knowing which list holds the effect:
// the pointer aims at either the previous node's m_linkNext or at the list head itself, so the
// same two stores unlink from the middle and from the front.
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

// ref: FUN_006f7870
// Particles and ribbons go on and off TOGETHER, and the float at +0x17c moves with them -- 1.0f
// when emission is on, 0.0f when it is off. All three are one switch.
void CEffect::SetEmission(int32_t enable) {
    if (!this->m_model) {
        return;
    }

    this->m_model->float17c = enable ? 1.0f : 0.0f;
    this->m_model->SetParticleEmission(enable);
    this->m_model->SetRibbonFlag8(enable);
}

// ref: FUN_006f78b0
// Stop the effect. Two things happen: the model's animation is HELD from the current scene time,
// and emission stops.
//
// The hold is a timestamp, not a flag, and `if (heldTime == 0) heldTime = 1` is the reference's own
// line -- zero is the not-held sentinel, so a scene whose time is genuinely 0 (the first frame)
// would otherwise record a hold that reads as no hold at all. That one line is what proves the
// field is a time and not the pointer frozen used to declare it as.
//
// The inner `if (this->m_model)` is redundant, because the outer branch already established it and
// nothing between them can clear it. Kept because the reference re-reads the field there, and the
// three stores that follow are inlined rather than routed through SetEmission for the same reason:
// matching the reference's call sequence is the point.
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
// Start the effect: release the animation hold and let the emitters run.
//
// Only acts when the play state is 0, so starting an effect that is already running just restamps
// the state. The state write is OUTSIDE the branch and happens either way, which is what makes the
// call idempotent rather than merely guarded.
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

// ref: FUN_006f7fd0
// Attach the effect to its owner and record what it is playing.
//
// NOTHING HAPPENS IF THE OWNER IS GONE. The whole body sits inside the object-manager lookup, so an
// effect whose owner has already been destroyed keeps its fields unset rather than half-attaching --
// and in particular does not take the reference below.
//
// The reference count rises because the OWNER'S LIST now holds this effect, and that list is the
// thing the count is counting. The file and line handed to the lookup are the reference's own,
// Effect_C.cpp line 0x28e, kept because the object manager logs them.
//
// Flags are OR'd here and 0x800 is added unconditionally, where Sub6f8040 below ASSIGNS them. That
// is the clearest difference between the two paths and the reason they are separate functions.
void CEffect::AttachToOwner(int32_t kitID, const SpellVisualKitRec* kit, WOWGUID owner,
                            uint32_t flags, uint32_t arga0) {
    CGObject_C* object = ClntObjMgrObjectPtr(owner, TYPE_UNIT, ".\\Effect_C.cpp", 0x28e);

    if (!object) {
        return;
    }

    this->LinkToHead(&object->m_effects);

    this->m_refCount++;
    this->m_kitID = kitID;
    this->m_owner = owner;
    this->m_flags |= flags | 0x800;
    this->m_kit = kit;
    this->uinta0 = arga0;
}

// ref: FUN_006f8040
// The sibling of AttachToOwner, from Effect_C.cpp line 0x2ba. Same shape -- look the owner up, join
// its list, take a reference, record the kit -- and it differs in exactly two ways:
//
//   it ASSIGNS m_flags rather than OR-ing, and adds no 0x800, so a caller here controls the whole
//   flag word and a caller there cannot clear anything;
//   it stores its extra argument at +0xcc instead of +0xa0.
//
// Named for its address because that second difference is the part that would name it, and what
// +0xa0 and +0xcc hold is not yet known.
void CEffect::Sub6f8040(uint32_t argcc, int32_t kitID, const SpellVisualKitRec* kit, WOWGUID owner,
                        uint32_t flags) {
    CGObject_C* object = ClntObjMgrObjectPtr(owner, TYPE_UNIT, ".\\Effect_C.cpp", 0x2ba);

    if (!object) {
        return;
    }

    this->LinkToHead(&object->m_effects);

    this->m_refCount++;
    this->m_kit = kit;
    this->uintcc = argcc;
    this->m_kitID = kitID;
    this->m_owner = owner;
    this->m_flags = flags;
}

// ref: FUN_006f7680
// The loaded callback every effect model gets. Two receivers that the decompilation drops and that
// the callback signature settles: the flag lives on the EFFECT, which arrives as the callback's
// argument, and the bone sequence goes to the MODEL, which is its first parameter.
//
// The bone id is 0xFFFFFFFF, which SetBoneSequence resolves to bone 0, and the sequence id is 0 --
// the stand animation. So a freshly loaded effect model is started on its first animation at full
// speed, and the 0x400000 emitter flag is raised first if the effect asked for it. That flag has to
// be set here rather than at Initialize because the model has no emitters until it loads.
void CEffect::ModelLoadedCallback(CM2Model* model, void* arg) {
    auto effect = static_cast<CEffect*>(arg);

    if (effect->m_flags & 0x400000) {
        effect->SetEmittersFlag400000(1);
    }

    model->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);
}

// ref: FUN_006f7a00
// Eleven bytes in the reference: LinkToHead against the global list and nothing else.
void CEffect::LinkToGlobalList() {
    this->LinkToHead(&CEffect::s_effectList);
}

// ref: FUN_006f7d60
// Build the effect. Records what it plays, creates the model for it, registers the three callbacks
// and joins the global list.
//
// The model comes from the GLOBAL scene, not from anything passed in: the reference loads the scene
// pointer out of 0x00cd754c at the call, which is CWorld::s_m2Scene here (the same global drives the
// scene in OnWorldRender). The filename is effectName's +0x08 and is the only field of it read.
//
// THE SEQUENCE-DONE AND ANIM-EVENT OWNERS ARE NOT GUIDS. Both setters take a WOWGUID owner, and the
// reference passes the effect POINTER as the low half with zero as the high half -- it is reusing the
// owner field as a context pointer. Transcribed as written, because a callback that arrives with a
// guid-shaped context and looks up an object with it would find nothing.
//
// LinkToHead is OUTSIDE the model test, so an effect whose model could not be created still joins the
// global list -- and the reference count is only taken when the model exists, because it is the model
// that holds the reference.
//
// GAP, AND IT IS A REAL ONE: the reference registers FUN_006f7b00 as the anim-event callback and this
// passes null, so animation events do nothing. That function is a SOUND dispatch -- it resolves
// m_eventOwner, and on the event id 0x444E5324 ('$SND') plays a sound kit through SI2::PlaySoundKit,
// on 0x54494824 ('$HIT') calls FUN_00736640 -- and reaching it means porting the sound chain below it:
// FUN_004c5990 (136 bytes, 54 callers, the 0xE8-byte play-parameter block's defaults), FUN_004c5c80
// (49 bytes, the 3D-position update behind SESound::Is3D) and FUN_00736640 (360 bytes). That is the
// sound engine rather than the render surface, which is the one thing --render is meant to keep a
// cycle out of, so it is named here instead of half-ported. The call is still made with null so the
// call sequence matches and the measurement can see the gap.
void CEffect::Initialize(int32_t kitID, const SpellVisualKitRec* kit, void* effectName, uint32_t flags,
                         M2SequenceDoneCallback sequenceDone, const C3Vector& position, uint32_t arga0,
                         uint32_t argc0, uint32_t argc4) {
    this->ptr20 = effectName;
    this->int24 = -1;

    this->m_owner = 0;
    this->m_eventOwner = 0;

    this->veca4 = position;

    this->uintc0 = argc0;
    this->uintc4 = argc4;

    this->m_kitID = kitID;
    this->m_kit = kit;

    // The reference stores effectName a second time here. Kept, because it is what it does.
    this->ptr20 = effectName;

    this->m_flags = flags | 2;
    this->uinta0 = arga0;

    this->m_model = CWorld::GetM2Scene()->CreateModel(
        *reinterpret_cast<const char**>(static_cast<char*>(effectName) + 8), 0);

    if (this->m_model) {
        this->m_refCount++;

        auto context = static_cast<WOWGUID>(reinterpret_cast<uintptr_t>(this));

        this->m_model->SetLoadedCallback(CEffect::ModelLoadedCallback, this);
        this->m_model->SetSequenceDoneCallback(sequenceDone, context);
        this->m_model->SetAnimEventCallback(nullptr, context);

        if (this->m_flags & 0x400000) {
            this->SetEmittersFlag400000(1);
        }
    }

    this->LinkToHead(&CEffect::s_effectList);
}
