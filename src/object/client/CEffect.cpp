#include "object/client/CEffect.hpp"
#include "model/CM2Scene.hpp"
#include "object/Types.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2ParticleEmitter.hpp"
#include "model/CM2Shared.hpp"

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
