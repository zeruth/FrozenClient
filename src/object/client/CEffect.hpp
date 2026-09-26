#ifndef OBJECT_CLIENT_C_EFFECT_HPP
#define OBJECT_CLIENT_C_EFFECT_HPP

#include <cstdint>

class CM2Model;
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
};

#endif
