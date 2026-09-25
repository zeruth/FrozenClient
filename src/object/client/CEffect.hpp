#ifndef OBJECT_CLIENT_C_EFFECT_HPP
#define OBJECT_CLIENT_C_EFFECT_HPP

#include <cstdint>

class CM2Model;

// The reference's CEffect (Effect_C.cpp): a model played on or at an object. Only the fields the
// ported functions read are declared; offsets are the reference's. The constructor (FUN_006f9d70)
// is not ported: it initialises one member through a call whose receiver Ghidra dropped.
class CEffect {
    public:
        // Member variables
        CM2Model* m_model = nullptr;        // +0x00
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
