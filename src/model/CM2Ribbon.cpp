#include "model/CM2Ribbon.hpp"
#include "gx/Device.hpp"
#include "gx/CGxDevice.hpp"
#include "world/map/CMapObj.hpp"
#include "gx/shader/CShaderEffect.hpp"
#include "gx/Transform.hpp"
#include "gx/RenderState.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/Draw.hpp"
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
// Whether ribbons draw at all. The reference keeps this as static initialised data at 0x00b2d658
// holding 1, with exactly ONE reader and no writer anywhere -- the same shape as the particle
// system's 0x00b2d530. It reads as a permanently-false branch in the disassembly and is not one;
// the value was taken out of the image.
static int32_t s_ribbonsEnabled = 1;

// ref: FUN_00980b70
// The trail is built on the CPU and uploaded WHOLE, which is the opposite of the particle path's
// write-through-cursors -- a ribbon has few vertices and they all move every frame.
//
// Drawn as a triangle STRIP: two vertices per ring slot, and the index count carries a closing
// pair so the last quad joins up.
int32_t CM2Ribbon::Draw(const C44Matrix* relativeTo) {
    // An empty ring has nothing to draw, and head == tail IS empty.
    if (!s_ribbonsEnabled || this->m_head == this->m_tail) {
        return 0;
    }

    // Identity unless the geometry lives in a relative space, then MINUS the origin the trail is
    // stored relative to. The reference builds the identity inline and overwrites it wholesale when
    // the argument is non-null, so the subtraction lands on whichever matrix ended up there.
    C44Matrix world(1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f);

    if (relativeTo) {
        world = *relativeTo;
    }

    world.d0 -= this->m_origin.x;
    world.d1 -= this->m_origin.y;
    world.d2 -= this->m_origin.z;

    // Push and set in one, which is what the reference's XformPush(GxXform_World, m) does.
    GxXformPush(GxXform_World);
    GxXformSet(GxXform_World, world);

    uint32_t vertexCount = this->m_vertices.Count();
    uint32_t indexCount = this->m_indices.Count();

    // 0x18 is 24 bytes, which is GxVBF_PCT exactly -- position, colour, one texcoord.
    CGxBuf* vbuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, sizeof(Vertex), vertexCount);
    CGxBuf* ibuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, sizeof(uint16_t), indexCount);

    if (!vbuf || !ibuf) {
        GxXformPop(GxXform_World);

        return 0;
    }

    GxBufData(vbuf, reinterpret_cast<char*>(this->m_vertices.Ptr()),
              vertexCount * sizeof(Vertex), 0);
    GxPrimVertexPtr(vbuf, GxVBF_PCT);

    // The live span of the ring, which WRAPS, plus the closing pair. Two vertices per slot.
    uint32_t tail = this->m_tail;
    uint32_t span = tail < this->m_head
                  ? this->m_head - tail
                  : this->m_head + this->m_segmentAges.Count() - tail;

    uint32_t drawIndices = span * 2 + 2;

    // The index run STARTS at the tail, so the strip begins at the oldest live segment rather
    // than at the front of the array. TWO entries per slot, not four.
    //
    // THE SCALE HERE WAS WRONG AND IT PUT GARBAGE GEOMETRY ON SCREEN. The reference computes
    // `indices + tail * 4` at 0x980c79 -- but that is a BYTE offset on a byte pointer, and an
    // index is a uint16, so it advances TWO ENTRIES per slot. Transcribing the 4 onto a
    // uint16_t* made C scale it a second time: tail * 8 bytes, double the reference.
    //
    // Two indices per slot is also the only layout the array Initialize builds can support. It
    // holds segmentCount * 4 entries filled with `i % (segmentCount * 2)` -- two back-to-back
    // copies of the strip sequence -- precisely so a run starting anywhere in the first copy can
    // continue into the second instead of wrapping. At the correct stride the longest possible
    // run ends exactly at the end of the array: 2 * tail + (2 * segmentCount + 2) fits inside
    // 4 * segmentCount for every tail <= segmentCount. At the doubled stride it ran off the end
    // and read adjacent heap as indices, so triangles pointed at arbitrary vertices -- and
    // m_maxIndex below then understates the real range, letting the device fetch outside the
    // vertex buffer too.
    //
    // It also drew the WRONG SEGMENTS long before it read out of bounds, starting the strip two
    // slots along for every one it should have.
    //
    // Worth recording why this survived a probe: at tail == 0 both scales agree, and a ribbon's
    // tail only leaves 0 once the ring has filled and started recycling. A probe that sampled
    // the first frames of a trail saw sane numbers and cleared the expression. The damage
    // appears later in a trail's life, which is what made it look intermittent on screen.
    GxBufData(ibuf, reinterpret_cast<char*>(this->m_indices.Ptr() + tail * 2),
              drawIndices * sizeof(uint16_t), 0);
    g_theGxDevicePtr->PrimIndexPtr(ibuf);

    for (uint32_t i = 0; i < this->m_materials.Count(); i++) {
        const Material& material = this->m_materials[i];

        GxRsPush();

        // Bit 0 is LIT, despite reading as its opposite: set means emissive BLACK, so the surface
        // takes its brightness from the lights, and clear means emissive white, which is the unlit
        // look. The same bit drives the lighting state on the next line. (The table in
        // docs/ref/parity-ribbons.md labels this bit "unlit" while describing exactly this
        // behaviour -- the label is the part that is inverted.)
        // FLOATS, not a packed colour: the reference builds (1,1,1,0) or (0,0,0,0) on the stack and
        // SetEmissive takes a C4Vector.
        C4Vector emissive = material.m_flags & 0x1
                          ? C4Vector{ 0.0f, 0.0f, 0.0f, 0.0f }
                          : C4Vector{ 1.0f, 1.0f, 1.0f, 0.0f };

        CShaderEffect::SetEmissive(emissive);
        CShaderEffect::SetFogEnabled((material.m_flags >> 1) & 1);
        CShaderEffect::SetLightEnabled(material.m_flags & 1);

        // Each of these four lands on a frozen EGxRenderState on the nose, which is the check that
        // the bit assignments are right: 13, 15, 17 and 6.
        GxRsSet(GxRs_DepthTest, static_cast<int32_t>((material.m_flags >> 2) & 1));
        GxRsSet(GxRs_DepthWrite, static_cast<int32_t>((material.m_flags >> 3) & 1));
        GxRsSet(GxRs_Culling, static_cast<int32_t>((material.m_flags >> 4) & 1));
        GxRsSet(GxRs_BlendingMode, static_cast<int32_t>(material.m_blend));

        // The SHADER alpha reference, as a fraction -- NOT CGxDevice::RsSetAlphaRef, which sets the
        // fixed-function state from the same table with the raw 0..255 value. They are different
        // functions with different targets and the reference calls this one; substituting the device
        // one left the shader path's alpha reference unset. Same model-to-world include the particle
        // emitter already takes.
        CMapObj::SetAlphaRefForBlendMode();

        // No texture means no pass -- the reference skips the whole submit rather than drawing
        // untextured.
        CGxTex* tex = i < this->m_textures.Count()
                    ? TextureGetGxTex(this->m_textures[i], 0, nullptr)
                    : nullptr;

        if (tex) {
            GxRsSet(GxRs_Texture0, tex);
            GxTexSetWrap(tex, GxTex_Clamp, GxTex_Clamp);

            // Zero bone influences: trail geometry is unskinned, the same call the particle submit
            // makes.
            CShaderEffect::SetShadersForGeometry(0);
            CShaderEffect::SetWorldViewConstants();

            CGxBatch batch;

            batch.m_primType = GxPrim_TriangleStrip;
            batch.m_start = 0;
            batch.m_count = drawIndices;
            batch.m_minIndex = 0;
            batch.m_maxIndex = static_cast<uint16_t>(vertexCount - 1);

            GxDraw(&batch, 1);
        }

        GxRsPop();
    }

    GxXformPop(GxXform_World);

    return 1;
}
// ref: FUN_0097f940
// The pair shift. Everything the trail knows about where it is lives in two points and two frames
// of direction; this moves the newer set into the older and takes a fresh one off the placement
// matrix, which for a weapon trail is the attachment bone's.
//
// The matrix's ROW 1 is the edge direction and ROW 2 the tangent -- so the trail is widened along
// the bone's Y and swept along its Z.
void CM2Ribbon::SetPosition(const C44Matrix& placement, const C3Vector& offset,
                            const C44Matrix* relativeTo) {
    // Both of these are set by Initialize and cleared by nothing here, so in practice this gate
    // only shuts on a ribbon that was never initialised.
    if (!(this->m_flags & 0x4) || !(this->m_flags & 0x8)) {
        return;
    }

    C44Matrix local = placement;

    // The offset goes into the TRANSLATION ROW, before the relative-space change -- the reference
    // writes it back into its own copy of the matrix rather than keeping it beside it, which is why
    // the multiply below sees the offset applied.
    local.d0 += offset.x;
    local.d1 += offset.y;
    local.d2 += offset.z;

    // Into the space the geometry is expressed in. Null means world space, which is every case in
    // frozen today: CM2Model::m_particleRelative has no ported setter.
    if (relativeTo) {
        local *= relativeTo->AffineInverse();
    }

    // The OFFSET, not the resulting point. The draw subtracts this from the world matrix's
    // translation row, so the geometry is stored relative to it.
    this->m_origin = offset;

    if (!(this->m_flags & 0x1)) {
        // First placement. Seed the PREVIOUS end from this same matrix, so the first segment has
        // zero length instead of stretching from wherever the fields happened to be, and open the
        // emission gate.
        this->vec7C = { local.b0, local.b1, local.b2 };
        this->vec94 = { local.c0, local.c1, local.c2 };
        this->vec20 = { local.d0, local.d1, local.d2 };

        this->m_edgeAccum = 0.0f;

        this->m_flags |= 0x1;
    } else {
        // Every later frame: what was current becomes previous.
        this->vec20 = this->vec164;
        this->vec7C = this->vec88;
        this->vec94 = this->vecA0;
    }

    this->vec164 = { local.d0, local.d1, local.d2 };
    this->vec88 = { local.b0, local.b1, local.b2 };
    this->vecA0 = { local.c0, local.c1, local.c2 };
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

// ref: FUN_0097f900
// Close every texture handle the ribbon is holding.
//
// Freeing m_textures alone would release the ARRAY and leak every handle in it -- the array's
// element type is HTEXTURE, and a handle is a reference the texture cache is still counting.
void CM2Ribbon::ReleaseTextures() {
    for (uint32_t i = 0; i < this->m_textures.Count(); i++) {
        if (this->m_textures[i]) {
            HandleClose(this->m_textures[i]);
        }
    }
}

// NOT COUNTED AS RENDER SURFACE, and that is a measurement artifact rather than a judgement about
// this code. Both this and ReleaseTextures are filed under CSimpleHyperlinkedFrame.cpp, whose assert
// anchor at 0x00978ad0 runs on for 48KB and swallows the whole ribbon cluster -- the same
// "anchor collects unrelated code" shape recomp.py already documents for CreepTendril.cpp.
//
// Deliberately NOT fixed with a MODULE_RANGES entry, because the evidence is not there: the ribbon
// cluster's largest object-file gap is 20 bytes, which is no boundary at all, and its RTTI strings
// (.?AUCRibbonMat@@, .?AUCGxVertexPCT0@@) are referenced from descriptor structures rather than from
// code, so they cannot bracket a range the way SoundEngine.cpp's 301 assert references did. Guessing
// a boundary to move a number is what that mechanism exists to avoid.
// ref: FUN_00980590
// The ribbon's own teardown, and it is a DESTRUCTOR rather than a named release: the reference's
// 0x00980b50 is the thirty-byte deleting-destructor thunk that wraps this body, and its other
// caller destructs the emitters in place out of CM2Model's pooled buffer -- the same explicit
// call frozen already makes for CM2ParticleEmitter.
//
// The reference SMemFrees six arrays here, one per TSGrowableArray on this class. Those are
// member destructors in frozen, so they do not appear as calls -- what does NOT happen by itself
// is the texture handles, which is why ReleaseTextures runs first.
//
// Nothing had ever destructed a ribbon emitter: CM2Model's teardown had its ribbon block commented
// out, so every ribbon on every model leaked its six arrays and all of its texture handles.
CM2Ribbon::~CM2Ribbon() {
    this->ReleaseTextures();

    // Bit 1 down, as the reference leaves it. Pointless on an object that is dying, and kept
    // because the reference's own destructor does it -- the flag word outlives nothing here.
    this->m_flags &= ~0x2u;
}
