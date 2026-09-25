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

// Bit 0 of `enable` becomes flag 0x8; clearing it drops flag 0x1 too.
//
// ref: FUN_0097f5b0
void CM2Ribbon::SetFlag8(int32_t enable) {
    this->m_flags ^= (static_cast<uint32_t>(enable) * 8 ^ this->m_flags) & 0x8;

    if (!(this->m_flags & 0x8)) {
        this->m_flags &= ~0x1u;
    }
}

// The points go through the whole matrix; the four directions through its 3x3 part, each written
// back in place component by component in the reference's order.
//
// ref: FUN_0097fbe0
void CM2Ribbon::Transform(const C44Matrix& m) {
    C3Vector moved;
    TransformPointInPlace(moved, this->vec20, m);

    float x94 = this->vec94.x;
    float y94 = this->vec94.y;
    this->vec94.x = this->vec94.x * m.a0 + this->vec94.z * m.c0 + this->vec94.y * m.b0;
    this->vec94.y = m.b1 * y94 + x94 * m.a1 + m.c1 * this->vec94.z;
    this->vec94.z = this->vec94.z * m.c2 + m.a2 * x94 + y94 * m.b2;

    float x7C = this->vec7C.z * m.c0 + this->vec7C.x * m.a0 + this->vec7C.y * m.b0;
    float y7C = this->vec7C.x * m.a1 + m.b1 * this->vec7C.y + m.c1 * this->vec7C.z;
    float oldX7C = this->vec7C.x;
    float oldY7C = this->vec7C.y;
    this->vec7C.x = x7C;
    this->vec7C.y = y7C;
    this->vec7C.z = oldY7C * m.b2 + this->vec7C.z * m.c2 + oldX7C * m.a2;

    TransformPointInPlace(moved, this->vec164, m);

    float xA0 = this->vecA0.x;
    float yA0 = this->vecA0.y;
    this->vecA0.x = this->vecA0.x * m.a0 + this->vecA0.y * m.b0 + this->vecA0.z * m.c0;
    this->vecA0.y = m.b1 * this->vecA0.y + m.c1 * this->vecA0.z + xA0 * m.a1;
    this->vecA0.z = xA0 * m.a2 + yA0 * m.b2 + this->vecA0.z * m.c2;

    float x88 = this->vec88.z * m.c0 + this->vec88.y * m.b0 + this->vec88.x * m.a0;
    float y88 = this->vec88.y * m.b1 + this->vec88.z * m.c1 + this->vec88.x * m.a1;
    float oldX88 = this->vec88.x;
    float oldY88 = this->vec88.y;
    this->vec88.x = x88;
    this->vec88.y = y88;
    this->vec88.z = m.a2 * oldX88 + oldY88 * m.b2 + this->vec88.z * m.c2;

    for (uint32_t i = 0; i < this->m_vertices.Count(); i++) {
        TransformPointInPlace(moved, this->m_vertices[i].m_position, m);
    }
}

// ref: FUN_0097fe40
uint32_t CM2Ribbon::GetMaterialCount() const {
    return this->m_materials.Count();
}

// Tail forward to head, wrapping at the ring's capacity.
//
// ref: FUN_0097fe50
uint32_t CM2Ribbon::CountVertices() const {
    uint32_t tail = this->m_tail;
    uint32_t head = this->m_head;

    if (tail < head) {
        return (head - tail) * 2;
    }

    return (this->m_segments.Count() - tail + head) * 2;
}
