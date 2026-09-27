#include "model/CM2Ribbon.hpp"
#include "gx/Device.hpp"
#include "gx/CGxDevice.hpp"
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

    this->m_edgeAccum = 0;
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

    this->m_textureSlot = 0;

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
    this->m_heightAbove = 10.0f;
    this->m_heightBelow = 10.0f;

    this->m_gravity = 0.0f;
}

// The vertex colour as the DEVICE wants its bytes. ref: FUN_00482a60
//
// A CImVector is ARGB; a device reporting GxCF_rgba wants red and blue the other way round, so
// bytes 0 and 2 trade and 1 and 3 stay. The reference reads the same capability (caps + 0x14 is
// m_colorFormat) and does the same four byte moves.
static uint32_t RibbonVertexColor(const CImVector& color) {
    if (g_theGxDevicePtr->Caps().m_colorFormat != GxCF_rgba) {
        return color.value;
    }

    CImVector out;

    out.b = color.r;
    out.g = color.g;
    out.r = color.b;
    out.a = color.a;

    return out.value;
}

// ref: FUN_0097f700
// The two edges run from the current point to the previous one, offset either side of the centre
// line along the edge direction at each end. The Hermite tangents are the stored tangents scaled
// by the DISTANCE between the two points, which is what keeps the bulge proportional to how far
// the ribbon moved this frame -- a ribbon that barely moved gets an almost straight segment.
void CM2Ribbon::BuildControlPoints() {
    float dx = this->vec20.x - this->vec164.x;
    float dy = this->vec20.y - this->vec164.y;
    float dz = this->vec20.z - this->vec164.z;

    float length = std::sqrt(dx * dx + dy * dy + dz * dz);

    // Note which height goes with which sign: BELOW subtracts, ABOVE adds.
    this->m_lowerPrev.x = this->vec20.x - this->vec7C.x * this->m_heightBelow;
    this->m_lowerPrev.y = this->vec20.y - this->vec7C.y * this->m_heightBelow;
    this->m_lowerPrev.z = this->vec20.z - this->vec7C.z * this->m_heightBelow;

    this->m_lowerCur.x = this->vec164.x - this->vec88.x * this->m_heightBelow;
    this->m_lowerCur.y = this->vec164.y - this->vec88.y * this->m_heightBelow;
    this->m_lowerCur.z = this->vec164.z - this->vec88.z * this->m_heightBelow;

    this->m_upperPrev.x = this->vec20.x + this->vec7C.x * this->m_heightAbove;
    this->m_upperPrev.y = this->vec20.y + this->vec7C.y * this->m_heightAbove;
    this->m_upperPrev.z = this->vec20.z + this->vec7C.z * this->m_heightAbove;

    this->m_upperCur.x = this->vec164.x + this->vec88.x * this->m_heightAbove;
    this->m_upperCur.y = this->vec164.y + this->vec88.y * this->m_heightAbove;
    this->m_upperCur.z = this->vec164.z + this->vec88.z * this->m_heightAbove;

    this->m_hermiteTangentPrev.x = this->vec94.x * length;
    this->m_hermiteTangentPrev.y = this->vec94.y * length;
    this->m_hermiteTangentPrev.z = this->vec94.z * length;

    this->m_hermiteTangentCur.x = this->vecA0.x * length;
    this->m_hermiteTangentCur.y = this->vecA0.y * length;
    this->m_hermiteTangentCur.z = this->vecA0.z * length;
}

// ref: FUN_0097fef0
// One ring slot: both edge positions at parameter `t`, the slot's age, and optionally the head.
//
// The age is stamped rather than zeroed because the caller back-dates it -- a segment emitted
// part-way through the frame is already that fraction of a frame old, and the emission loop passes
// a NEGATIVE value for exactly that.
void CM2Ribbon::EmitSegment(float age, float t, uint32_t advance) {
    float inv = 1.0f - t;

    Vertex* pair = &this->m_vertices[this->m_head * 2];

    // p(t) = (A*t + B) * (1 - t) + (C - D * (1 - t)) * t, transcribed in the reference's own
    // grouping rather than the expanded form, so the float rounding matches.
    pair[0].m_position.x = (this->m_hermiteTangentPrev.x * t + this->m_lowerPrev.x) * inv
                        + (this->m_lowerCur.x - inv * this->m_hermiteTangentCur.x) * t;
    pair[0].m_position.y = (this->m_hermiteTangentPrev.y * t + this->m_lowerPrev.y) * inv
                        + (this->m_lowerCur.y - inv * this->m_hermiteTangentCur.y) * t;
    pair[0].m_position.z = (this->m_hermiteTangentPrev.z * t + this->m_lowerPrev.z) * inv
                        + (this->m_lowerCur.z - inv * this->m_hermiteTangentCur.z) * t;

    pair[1].m_position.x = (this->m_hermiteTangentPrev.x * t + this->m_upperPrev.x) * inv
                        + (this->m_upperCur.x - inv * this->m_hermiteTangentCur.x) * t;
    pair[1].m_position.y = (this->m_hermiteTangentPrev.y * t + this->m_upperPrev.y) * inv
                        + (this->m_upperCur.y - inv * this->m_hermiteTangentCur.y) * t;
    pair[1].m_position.z = (this->m_hermiteTangentPrev.z * t + this->m_upperPrev.z) * inv
                        + (this->m_upperCur.z - inv * this->m_hermiteTangentCur.z) * t;

    this->m_segmentAges[this->m_head] = age;

    this->m_head += advance;

    if (this->m_head >= this->m_segmentAges.Count()) {
        this->m_head -= this->m_segmentAges.Count();
    }
}

// ref: FUN_00980090
// One frame of trail.
//
// The delta is REPLACED rather than used when flag 0x10 is clear and the edge rate is positive:
// the ribbon then advances by exactly one edge's worth of time plus 1e-4 (0x009e8cd0), which makes
// the first update after Initialize lay down a full edge instead of a sliver. Flag 0x10 is set at
// the end of this function, so that substitution happens ONCE per ribbon.
void CM2Ribbon::Update(float delta, int32_t suppressEmit) {
    if (!(this->m_flags & 0x10) && this->m_edgesPerSecond > 0.0f) {
        delta = 1.0f / this->m_edgesPerSecond + 0.000099999997f;
    }

    // Clamped to [0, edgeLifetime]. A negative delta becomes zero rather than running backwards.
    float dt = 0.0f;

    if (delta >= 0.0f) {
        dt = delta;

        if (delta >= this->m_edgeLifetime) {
            dt = this->m_edgeLifetime;
        }
    }

    uint32_t capacity = this->m_segmentAges.Count();

    // Retire from the tail while the oldest slot would age past its lifetime this frame. Stops at
    // the head, so the ring never empties past it.
    while (this->m_tail != this->m_head) {
        if (this->m_segmentAges[this->m_tail] + dt <= this->m_edgeLifetime) {
            break;
        }

        this->m_tail++;

        if (this->m_tail >= capacity) {
            this->m_tail -= capacity;
        }
    }

    // Emission needs all three of flags 0x1, 0x4 and 0x8 -- Initialize sets 0x4 and 0x8 and clears
    // 0x1, so a ribbon does not start emitting until something sets 0x1.
    if (!suppressEmit && (this->m_flags & 0x4) && (this->m_flags & 0x8) && (this->m_flags & 0x1)) {
        // m_edgeAccum is the FRACTIONAL edge carried over from last frame, so emission stays on the
        // ribbon's own rate instead of the frame rate.
        float earned = dt * this->m_edgesPerSecond + this->m_edgeAccum;

        uint32_t color = RibbonVertexColor(this->m_color);

        this->BuildControlPoints();

        if (earned >= 1.0f) {
            float carried = this->m_edgeAccum;
            auto whole = static_cast<int32_t>(std::floor(earned - 1.0f)) + 1;
            float n = 1.0f;

            for (; whole != 0; whole--) {
                // Where this edge falls between last frame's position and this one. Evenly spaced
                // in EARNED edges, not in time.
                float t = (n - this->m_edgeAccum) * (1.0f / (earned - carried));

                Vertex* pair = &this->m_vertices[this->m_head * 2];
                pair[0].m_color.value = color;
                pair[1].m_color.value = color;

                // Back-dated by the fraction of the frame that has already passed for it.
                this->EmitSegment(-(t * dt), t, 1);

                n += 1.0f;
            }
        }

        this->m_edgeAccum = earned - std::floor(earned);

        // The live leading edge, at t = 1 and WITHOUT advancing: it is rewritten every frame from
        // the ribbon's current position rather than committed to the ring.
        this->EmitSegment(0.0f, 1.0f, 0);

        Vertex* pair = &this->m_vertices[this->m_head * 2];

        // Age zero, so the leading edge sits at the near end of the texture cell. The crossed
        // naming is real -- see the note on m_cellHeight: the V family drives texcoord.x.
        pair[0].m_texCoord.x = this->m_cellV0;
        pair[0].m_texCoord.y = this->m_cellU0;
        pair[1].m_texCoord.x = this->m_cellV0;
        pair[1].m_texCoord.y = this->m_cellU1;

        pair[0].m_color.value = color;
        pair[1].m_color.value = color;
    }

    // Bounds are rebuilt from scratch every frame, born INVERTED so the first slot sets both
    // corners -- the same empty-box convention the constructor uses.
    this->m_boundsMin = { 3.4028234663852886e+38f, 3.4028234663852886e+38f,
                          3.4028234663852886e+38f };
    this->m_boundsMax = { -3.4028234663852886e+38f, -3.4028234663852886e+38f,
                          -3.4028234663852886e+38f };

    uint32_t slot = this->m_tail;

    while (slot != this->m_head) {
        Vertex* pair = &this->m_vertices[slot * 2];

        // Gravity over the slot's whole life, integrated with the age BEFORE this frame's
        // increment: (age * 2 + dt) * gravity * dt. Applied to Z on both edges.
        float fall = (this->m_segmentAges[slot] * 2.0f + dt) * this->m_gravity * dt;

        pair[0].m_position.z += fall;
        pair[1].m_position.z += fall;

        for (uint32_t e = 0; e < 2; e++) {
            const C3Vector& p = pair[e].m_position;

            if (p.x < this->m_boundsMin.x) { this->m_boundsMin.x = p.x; }
            if (p.y < this->m_boundsMin.y) { this->m_boundsMin.y = p.y; }
            if (p.z < this->m_boundsMin.z) { this->m_boundsMin.z = p.z; }
            if (p.x > this->m_boundsMax.x) { this->m_boundsMax.x = p.x; }
            if (p.y > this->m_boundsMax.y) { this->m_boundsMax.y = p.y; }
            if (p.z > this->m_boundsMax.z) { this->m_boundsMax.z = p.z; }
        }

        this->m_segmentAges[slot] += dt;

        // The texture walks along the trail with age, which is what makes it look like it is
        // flowing. Uses the age AFTER the increment.
        float coord = this->m_segmentAges[slot] * this->m_cellHeight * this->m_invEdgeLifetime
                    + this->m_cellV0;

        pair[0].m_texCoord.x = coord;
        pair[0].m_texCoord.y = this->m_cellU0;
        pair[1].m_texCoord.x = coord;
        pair[1].m_texCoord.y = this->m_cellU1;

        slot++;

        if (slot >= capacity) {
            slot -= capacity;
        }
    }

    // Drop 0x20 and set 0x10. Setting 0x10 is what stops the one-edge delta substitution above
    // from happening again.
    this->m_flags = (this->m_flags & ~0x20u) | 0x10u;
}
// ref: FUN_0097f510
// The slot walks the grid: the remainder picks the position along one axis and the quotient the
// other, both from the COLUMN count. That is the same crossed pairing m_cellHeight already
// carries -- the remainder goes with cellHeight and the quotient with cellWidth.
//
// Initialize's tail is this function evaluated at slot 0, where both scaled terms vanish; that is
// why it looks like it has two redundant additions.
void CM2Ribbon::UpdateCellRect() {
    // The reference divides by m_textureCols without checking it. The only writer of the grid is
    // Initialize, straight from the file record, so this guards what the reference leaves to the
    // .m2 being sane.
    if (!this->m_textureCols) {
        return;
    }

    uint32_t quotient = this->m_textureSlot / this->m_textureCols;
    uint32_t remainder = this->m_textureSlot % this->m_textureCols;

    this->m_cellV0 = static_cast<float>(remainder) * this->m_cellHeight + this->m_textureRect[1];
    this->m_cellU0 = static_cast<float>(quotient) * this->m_cellWidth + this->m_textureRect[0];

    this->m_cellV1 = this->m_cellV0 + this->m_cellHeight;
    this->m_cellU1 = this->m_cellU0 + this->m_cellWidth;
}

// ref: FUN_0097f5f0
// Guarded on the slot actually changing, so the animated track can call this every frame without
// redoing the cell arithmetic.
void CM2Ribbon::SetTextureSlot(uint32_t slot) {
    if (this->m_textureSlot == slot) {
        return;
    }

    this->m_textureSlot = slot;

    this->UpdateCellRect();
}

// ref: FUN_0097f610
void CM2Ribbon::SetHeightAbove(float height) {
    this->m_heightAbove = height;
}

// ref: FUN_0097f620
void CM2Ribbon::SetHeightBelow(float height) {
    this->m_heightBelow = height;
}

// ref: FUN_0097fba0
// Alpha alone, into the top byte of m_color.
void CM2Ribbon::SetAlpha(float alpha) {
    this->m_color.a = static_cast<uint8_t>(static_cast<int32_t>(alpha * 255.0f + 0.5f));
}

// ref: FUN_0097fb60
// The three colour channels, KEEPING the alpha that is already there.
//
// The reference reaches that by round-tripping the stored alpha byte back out through
// CImVector::Set: it passes `alphaByte * 255.0` as Set's alpha, and Set multiplies by 255 again
// before truncating to a byte. The double scaling is harmless, and not by a luck that would need
// re-checking -- 255 * 255 is 65025, and 65025 mod 256 is 1, so the truncating cast recovers the
// original byte exactly. Written as "keep the alpha" here, which is what it comes to.
void CM2Ribbon::SetColor(float r, float g, float b) {
    uint8_t alpha = this->m_color.a;

    this->m_color.r = static_cast<uint8_t>(static_cast<int32_t>(r * 255.0f + 0.5f));
    this->m_color.g = static_cast<uint8_t>(static_cast<int32_t>(g * 255.0f + 0.5f));
    this->m_color.b = static_cast<uint8_t>(static_cast<int32_t>(b * 255.0f + 0.5f));

    this->m_color.a = alpha;
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
