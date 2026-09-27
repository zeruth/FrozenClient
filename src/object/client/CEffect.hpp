#ifndef OBJECT_CLIENT_C_EFFECT_HPP
#define OBJECT_CLIENT_C_EFFECT_HPP

#include "util/GUID.hpp"

#include "model/CM2Model.hpp"

#include <tempest/Vector.hpp>

#include <cstdint>

class CM2Model;
class CM2Scene;
class SpellVisualKitRec;

// The reference's CEffect (Effect_C.cpp): a model played on or at an object. Only the fields the
// ported functions read are DECLARED; the map below records what the rest are, so the next port
// here does not have to derive them again. 0x110 bytes in the reference.
//
// Recovered 2026-09-26 by reading the constructor (FUN_006f9d70), the destructor (FUN_006f9ec0),
// Release (FUN_006fa390) and the teardown (FUN_006f87c0) together. Where two of them agree the
// fact is solid -- the refcount is the clearest case: the constructor sets +0xf4 to 1 and Release
// decrements it and frees at zero.
//
//   +0x00  CM2Model*   the effect's own model                        (declared below)
//   +0x04  CM2Model*   the model it is attached to; the destructor releases it after setting its
//                      flag 0x20 and clearing its +0x1bc/+0x1c0 pair
//   +0x08  WOWGUID     the owner object, looked up with TYPE_UNIT by the destructor
//   +0x18  int32_t     SpellVisualKit id                              (declared below)
//   +0x1c  const SpellVisualKitRec*  the row it resolves to           (declared below)
//   +0x24  int32_t     -1 from the constructor; not identified
//   +0x34  a sound handle block, stopped or faded by the destructor (SI2::StopOrFadeOut)
//   +0x48  uint32_t    flags                                          (declared below)
//   +0x8c  int32_t     -1 from the constructor; not identified
//   +0x90  a light or world resource, released with FUN_007fc9d0; when set, the destructor also
//          re-runs the owner unit's animation (CGUnit_C::UpdateAnimation)
//   +0x94  a registration cancelled with FUN_00679e10 when +0x90 is set
//   +0x9c  int32_t     12 from the constructor; not identified
//   +0xf0  a world object, removed with CWorld::RemoveObject
//   +0xf4  uint32_t    REFERENCE COUNT, 1 from the constructor        (declared below)
//   +0x104 CEffect**   link back                                      (declared below)
//   +0x108 CEffect*    link forward                                   (declared below)
//
// The constructor is not ported: it writes about sixty-eight fields, most of them the unknowns
// above, and one through a call whose receiver Ghidra dropped. Release is not ported either, and
// deliberately: it frees the object when the count reaches zero, and the destructor it must run
// first (FUN_006f9ec0) needs eight functions frozen does not have. A Release without that
// destructor would free an effect while its model, sound and world object were still attached.
class CEffect {
    public:
        // Member variables
        CM2Model* m_model = nullptr;        // +0x00
        // +0x08. The object this effect plays on, stored by the two attach paths below and looked
        // up with TYPE_UNIT.
        WOWGUID m_owner = 0;
        // +0x10. A SECOND guid, and not a copy of the first -- the anim-event callback
        // (FUN_006f7b00) resolves this one, and with TYPE_OBJECT rather than TYPE_UNIT. Initialize
        // clears both.
        WOWGUID m_eventOwner = 0;
        // +0x20. Whatever Initialize is handed as the thing to play, and all that is established
        // about it is that its +0x08 is the model filename, which is the only field read: it goes
        // straight to CM2Scene::CreateModel. A DBC row of some effect-name table is the obvious
        // guess and frozen has no such record type, so it stays a void* rather than acquiring an
        // invented one.
        void* ptr20 = nullptr;
        // +0x24. Minus one from Initialize, read nowhere the port has reached.
        int32_t int24 = -1;
        // +0xa4. A position, copied in by Initialize three floats at a time.
        C3Vector veca4 = { 0.0f, 0.0f, 0.0f };
        // +0xc0 and +0xc4. Two more Initialize arguments, stored and not yet read.
        uint32_t uintc0 = 0;
        uint32_t uintc4 = 0;
        // +0xf4. One from the constructor; Release decrements it and frees the effect at zero.
        uint32_t m_refCount = 1;
        // The SpellVisualKit this effect plays, as the id and the row it resolves to. The row's
        // m_animID is the animation the effect wants its owner to play, which is how a spell visual
        // overrides a unit's pose (CGUnit_C::ApplyEffectAnimation).
        int32_t m_kitID = 0;                // +0x18
        const SpellVisualKitRec* m_kit = nullptr;   // +0x1c
        // +0x48. The bits read by the code ported so far: 0x1000 the effect drives its owner's
        // animation, 0x800000 it is finished and no longer does, 0x1000000 its animation id is
        // already model-ready and must not go through ResolveAnimation, 0x4000 cleared in bulk when
        // the owner's state changes.
        uint32_t m_flags = 0;
        // +0xa0 and +0xcc. One argument each, stored by the two attach paths and read nowhere the
        // port has reached yet, so they keep offset names rather than invented ones.
        uint32_t uinta0 = 0;
        uint32_t uintcc = 0;
        // +0xfc. The play state. Stop writes 0 and Start writes 2, and Start does its work only
        // when it finds 0 -- so 0 means stopped, 2 means running, and starting an effect that is
        // already running moves the state without touching the model a second time. No third
        // value has turned up yet.
        uint32_t m_playState = 0;
        CEffect** m_linkPrev = nullptr;     // +0x104
        CEffect* m_linkNext = nullptr;      // +0x108

        // Member functions

        // ref: FUN_006f75f0
        // Raise or clear 0x400000 on every particle emitter of the model.
        void SetEmittersFlag400000(int32_t enable);

        // ref: FUN_006f76c0
        // Move this effect to the head of the list at head.
        void LinkToHead(CEffect** head);

        // ref: FUN_006f7850
        void DetachModel();

        // The head of the list of EVERY effect, the reference's 0x00ca0504. Initialize links each
        // new effect onto it whether or not the model was created.
        static CEffect* s_effectList;

        // ref: FUN_006f7680
        // CM2Model's loaded callback, handed to every effect model. Static because it is only ever
        // taken as a function pointer -- which is why the reference shows it with no callers.
        static void ModelLoadedCallback(CM2Model* model, void* arg);

        // ref: FUN_006f7d60
        // Build the effect: record what it plays, create its model, and register the three
        // callbacks.
        void Initialize(int32_t kitID, const SpellVisualKitRec* kit, void* effectName, uint32_t flags,
                        M2SequenceDoneCallback sequenceDone, const C3Vector& position, uint32_t arga0,
                        uint32_t argc0, uint32_t argc4);

        // ref: FUN_006f7a00
        // Put this effect back on the global list without touching anything else.
        void LinkToGlobalList();

        // ref: FUN_006f74b0
        // Take this effect out of whatever list holds it. This is LinkToHead's first half on its
        // own, and the reference keeps it as its own function.
        void Unlink();

        // ref: FUN_006f7870
        // Turn the model's particle and ribbon emission on or off together.
        void SetEmission(int32_t enable);

        // ref: FUN_006f78b0
        // Stop the effect: hold the model's animation from now, and silence its emitters.
        void Stop();

        // ref: FUN_006f7900
        // Start the effect: release the animation hold and let the emitters run again.
        void Start();

        // ref: FUN_006f7fd0
        // Attach this effect to its owner object and take a reference for the attachment.
        void AttachToOwner(int32_t kitID, const SpellVisualKitRec* kit, WOWGUID owner,
                           uint32_t flags, uint32_t arga0);

        // ref: FUN_006f8040
        // The sibling attach path -- see the note at the definition for how the two differ.
        void Sub6f8040(uint32_t argcc, int32_t kitID, const SpellVisualKitRec* kit, WOWGUID owner,
                       uint32_t flags);
};

#endif
