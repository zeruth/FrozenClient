#include "model/CM2Ribbon.hpp"
#include <cmath>
#include <cstring>

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

// ref: FUN_009808a0
// The edge rate is CEILED and the lifetime has a floor of 0.25 (0x00aa2d0c), so a ribbon always
// gets at least one edge per second's worth of ring and a quarter second to live in.
//
// The ring holds one segment per edge over the whole lifetime plus TWO (0x00a4040c): one spare at
// each end, because the head advances before the tail retires.
void CM2Ribbon::Initialize(float edgesPerSecond, float edgeLifetime, CImVector color,
                           const TSGrowableArray<HTEXTURE>& textures,
                           const TSGrowableArray<Material>& materials,
                           const TSGrowableArray<M2Texture*>& textureRecords,
                           const float textureRect[4], uint32_t textureRows,
                           uint32_t textureCols) {
    float edgeRate = std::ceil(edgesPerSecond);

    if (edgeLifetime < 0.25f) {
        edgeLifetime = 0.25f;
    }

    auto segmentCount = static_cast<uint32_t>(std::ceil(edgeLifetime * edgeRate) + 2.0f);

    // SetCount IS the reference's grow: it compares against both the count and the allocation,
    // asks for a chunk size when there is none, and rounds the reallocation to it -- the same
    // four steps the reference inlines here.
    this->m_segmentAges.SetCount(segmentCount);

    this->m_flags &= ~1u;

    this->uint1C = 0;
    this->m_tail = 0;
    this->m_head = 0;

    // The GEOMETRY, two vertices per ring slot. This call was MISSING until 2026-09-27, and its
    // absence was a latent heap overrun rather than a cosmetic gap: the per-frame update writes
    // through `m_vertices.data + slot * 0x30`, so an unsized array would have been written past
    // its allocation the moment that update landed. Recovered from 0x98095d, where FUN_00980810 is
    // called on `this + 0x38` with `2 * segmentCount`.
    //
    // SetCount zeroes the elements it adds, which is what FUN_00980810 does too -- it clears six
    // dwords per new element, and a Vertex is exactly six dwords. Only the NEW ones, in both: the
    // reference does not re-clear geometry it already had, and an earlier version of this function
    // wrongly memset the whole ring on the strength of misreading that loop.
    this->m_vertices.SetCount(segmentCount * 2);

    // Two vertices per segment, and four indices per segment so the strip can close each quad.
    // The modulo is what makes the ring wrap: the indices run past the end of the vertex span and
    // come back to the front.
    uint32_t indexCount = segmentCount * 4;

    this->m_indices.SetCount(indexCount);

    for (uint32_t i = 0; i < indexCount; i++) {
        this->m_indices[i] = static_cast<uint16_t>(i % (segmentCount * 2));
    }

    this->m_invEdgeLifetime = 1.0f / edgeLifetime;

    // Transcribed exactly, INCLUDING the pairing, which looks transposed and is what the reference
    // does: the height divides the rectangle's V extent by the COLUMN count and the width divides
    // its U extent by the ROW count. Both names come from the offsets they are stored at, and the
    // cell rectangle built at the end of this function confirms which axis each one moves along --
    // so the oddity is in which grid dimension is used, not in the naming. Do not "fix" it without
    // a ribbon on screen to check against.
    this->m_cellHeight = (textureRect[3] - textureRect[1]) / static_cast<float>(textureCols);
    this->m_cellWidth = (textureRect[2] - textureRect[0]) / static_cast<float>(textureRows);

    // The reference divides rather than keeping the numerators around.
    this->m_invCellHeight = 1.0f / this->m_cellHeight;
    this->m_invCellWidth = 1.0f / this->m_cellWidth;

    this->m_edgesPerSecond = edgeRate;
    this->m_edgeLifetime = edgeLifetime;

    this->m_color = color;

    // The reference takes the scratch arrays over wholesale -- count, data pointer and chunk size
    // in three assignments, leaving the model's scratch arrays pointing at storage the ribbon now
    // owns. frozen copies the elements instead, because its TSGrowableArray owns its allocation and
    // stealing the pointer would double-free. Same contents either way.
    this->m_materials.SetCount(materials.Count());

    for (uint32_t i = 0; i < materials.Count(); i++) {
        this->m_materials[i] = materials[i];
    }

    this->m_textures.SetCount(textures.Count());

    for (uint32_t i = 0; i < textures.Count(); i++) {
        this->m_textures[i] = textures[i];
    }

    this->m_textureRecords.SetCount(textureRecords.Count());

    for (uint32_t i = 0; i < textureRecords.Count(); i++) {
        this->m_textureRecords[i] = textureRecords[i];
    }

    this->m_textureRect[0] = textureRect[0];
    this->m_textureRect[1] = textureRect[1];
    this->m_textureRect[2] = textureRect[2];
    this->m_textureRect[3] = textureRect[3];

    this->m_textureRows = textureRows;
    this->m_textureCols = textureCols;

    this->uint170 = 0;

    // The starting cell is cell 0, the rectangle's own corner. The reference reaches it through
    // `0 % textureCols` and a multiply by zero -- a general cell-index expression evaluated at
    // index 0 -- which is why the two additive terms below look redundant. Kept as the corner they
    // come out as, since nothing here can vary.
    this->m_cellV0 = textureRect[1];
    this->m_cellU0 = textureRect[0];
    this->m_cellV1 = this->m_cellV0 + this->m_cellHeight;
    this->m_cellU1 = this->m_cellU0 + this->m_cellWidth;

    // Bits 1, 2 and 3. Bit 0 was cleared at the top and stays clear.
    this->m_flags |= 0xEu;

    // Both 10.0 (0x009e30cc), and both left alone by the constructor.
    this->float174 = 10.0f;
    this->float178 = 10.0f;

    this->m_gravity = 0.0f;
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

    return (this->m_segmentAges.Count() - tail + head) * 2;
}
