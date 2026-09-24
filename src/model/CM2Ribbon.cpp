#include "model/CM2Ribbon.hpp"

// Zero almost everything.
//
// Three groups are deliberately NOT touched here and are set by Initialize instead: the ring ends
// at +0x14/+0x18, the edge rate and lifetime at +0x10c/+0x110, and the texture grid at
// +0x158/+0x15c. frozen gives them default member initialisers anyway, because a field that is
// only sometimes written is how allocator garbage passes a null check -- the bug class CLAUDE.md
// lists for the character appearance table.
//
// The bounds are born INVERTED, which is the same empty-box convention CM2ParticleEmitter uses and
// the same two constants: the first point absorbed sets both corners.
//
// ref: FUN_00980630
CM2Ribbon::CM2Ribbon() {
    // The reference clears bits 1 and 4 rather than assigning, on a field that has just been
    // carved out of a shared buffer. Transcribed as the mask it is; frozen's default initialiser
    // has already made it zero, so the two agree.
    this->m_flags &= ~0x12u;
}

// Head and tail meeting means the ring is empty.
//
// ref: FUN_0097f640
bool CM2Ribbon::IsEmpty() const {
    return this->m_tail == this->m_head;
}

// ref: FUN_0097f630
void CM2Ribbon::SetGravity(float gravity) {
    this->m_gravity = gravity;
}

// Set or clear flag 0x4.
//
// Clearing it also drops flag 0x1, which is the pattern the reference uses elsewhere for "this
// state is no longer valid, so the built geometry is not either".
//
// ref: FUN_0097f570
void CM2Ribbon::SetAbove(int32_t above) {
    this->m_flags = (this->m_flags & ~0x4u) | (above ? 0x4u : 0u);

    if (!(this->m_flags & 0x4)) {
        this->m_flags &= ~0x1u;
    }
}
