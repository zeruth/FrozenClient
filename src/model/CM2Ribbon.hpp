#ifndef MODEL_C_M2_RIBBON_HPP
#define MODEL_C_M2_RIBBON_HPP

#include <cstdint>
#include "math/Types.hpp"
#include "model/M2Data.hpp"
#include "gx/Buffer.hpp"
#include "gx/Texture.hpp"
#include "storm/array/TSGrowableArray.hpp"
#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>

// The reference's `CRibbonEmitter` (RTTI at 0x00b2d6bc), one per `M2Ribbon`: the trail behind a
// weapon or a banner edge. `docs/ref/parity-ribbons.md` is the map this was built from.
//
// SIZE IS 0x180, and that is measured rather than inferred: CM2Model::InitializeLoaded carves the
// emitters out of one buffer and steps by 0x180 between them at 0x833726.
//
// The layout below comes from the CONSTRUCTOR (FUN_00980630) and `Initialize` (FUN_009808a0)
// together -- the constructor says which fields exist and what they start at, and Initialize says
// what most of them mean. Fields whose meaning is NOT established keep an offset name, which is
// the idiom CM2Model already uses for uint74, float88 and friends; inventing a plausible name for
// a field nobody has read is how a later reader ends up trusting a guess.
//
// `Initialize` landed 2026-09-26 and the per-frame update on 2026-09-27. Still missing: `Draw`
// (FUN_00980b70, fully mapped in docs/ref/parity-ribbons.md) and the six small setters
// CM2Model::AnimateST feeds a ribbon each frame -- FUN_0097f5f0, FUN_0097f610, FUN_0097f620,
// FUN_0097f940, FUN_0097fb60 and FUN_0097fba0 -- which are what move vec20, vec164 and the four
// directions. Until those land, Update runs on a ribbon whose points never change, so the ring
// still does not fill and CM2Scene's gather still emits nothing.
class CM2Ribbon {
    public:
        // One material pass over the ribbon's geometry -- the reference's `CRibbonMat`, 8 bytes.
        // Both fields were read off the draw's per-material loop (FUN_00980b70) rather than a
        // declaration, and every bit lands on a frozen EGxRenderState on the nose, which is the
        // check that the assignment is right.
        struct Material {
            // 0x1 unlit (emissive black instead of white, and the same bit drives the lighting
            // state), 0x2 fog, 0x4 depth test, 0x8 depth write, 0x10 culling.
            uint32_t m_flags;
            // Straight to GxRs_BlendingMode.
            uint32_t m_blend;
        };


        // One trail vertex. Also 0x18, and the draw is the check: it asks BufStream for a 0x18
        // stride and binds GxVBF_PCT, which is 12 + 4 + 8 = 24 bytes exactly.
        struct Vertex {
            C3Vector m_position;
            CImVector m_color;
            C2Vector m_texCoord;
        };

        // Member variables. Offsets are the reference's.
        //
        // The three arrays below are the reference's own container shape -- {alloc, count, data,
        // chunk}, 0x10 bytes and NO vtable. frozen's TSGrowableArray derives from a TSBaseArray
        // that declares virtuals, so its layout is wider; that is a divergence frozen already
        // lives with everywhere else, and nothing here depends on the byte offsets.

        // +0x00. Born 1, and nothing read so far changes it.
        uint32_t uint0 = 1;
        // +0x04: the segment ring, one float per slot, sized to
        // `ceil(edgeLifetime * ceil(edgesPerSecond)) + 2`. m_head and m_tail index THIS array,
        // and its count is the ring capacity the two of them wrap against.
        //
        // CORRECTED 2026-09-27. This was declared as `TSGrowableArray<Segment>` with an opaque
        // 0x18-byte Segment, on the reasoning that FUN_00980810's SetCount strides by 0x18. That
        // was the wrong array: 0x98095d shows FUN_00980810 being called on `this + 0x38`, which is
        // m_vertices, with `2 * segmentCount`. The ring itself is grown separately at 0x98093d and
        // the per-frame update indexes it as `data + i * 4` floats.
        //
        // Each float is the slot's AGE in seconds: the update adds the frame delta to it, retires
        // the tail once `age + delta` passes m_edgeLifetime, and integrates gravity with
        // `(age * 2 + delta) * m_gravity * delta`.
        TSGrowableArray<float> m_segmentAges;
        // +0x14 and +0x18: the ring's ends, and the DRAW is what pins which is which -- it
        // computes `tail < head ? head - tail : head + capacity - tail`, so the live span runs
        // from tail forward to head and wraps. Equal means empty; see IsEmpty.
        uint32_t m_head = 0;
        uint32_t m_tail = 0;
        // +0x1c: the FRACTIONAL edge carried from one update to the next, so emission runs at the
        // ribbon's own edge rate instead of the frame rate. A FLOAT, which is how the reference
        // reads it (`*(float *)(this + 0x1c)`) -- it was declared uint32_t here until 2026-09-27,
        // and Update silently truncated the carry to zero every frame, losing the sub-edge
        // remainder. A self-test caught it: the field read 0.00000 where 0.001 was due.
        float m_edgeAccum = 0.0f;
        // +0x20: a point, moved with the rest of the ribbon by Transform. Not read elsewhere yet.
        C3Vector vec20 = {};
        // +0x2c: where the ribbon is. The draw subtracts this from the world matrix's
        // translation row, so the geometry is stored relative to it.
        C3Vector m_origin = {};
        // +0x38: the trail geometry, built on the CPU and uploaded whole each draw. TWO vertices
        // per ring slot -- the two edges of the strip -- so Initialize sizes it to twice the
        // segment count and the update walks it 0x30 bytes at a time. CountVertices doubles for
        // the same reason.
        TSGrowableArray<Vertex> m_vertices;
        // +0x48: its indices. Initialize fills them with `i % (segments * 2)` and the draw takes
        // a triangle STRIP out of them.
        TSGrowableArray<uint16_t> m_indices;
        // +0x58: one over the edge lifetime.
        float m_invEdgeLifetime = 0.0f;
        // +0x5c and +0x60: one texture cell's size in UV, from the grid and the texture rect.
        // +0x64 and +0x68 are their reciprocals, which Initialize computes by dividing rather
        // than by keeping the numerator -- transcribed that way.
        //
        // THE U AND V NAMES HERE ARE CROSSED relative to the texcoord components they end up in,
        // and that is now confirmed from two independent places rather than suspected from one.
        // Initialize divides the rect's V extent by the COLUMN count to get m_cellHeight and its U
        // extent by the ROW count to get m_cellWidth; and the per-frame update writes the
        // cellHeight/m_cellV0 family into each vertex's texcoord.X and the m_cellU0/m_cellU1 pair
        // into its texcoord.Y. So whichever way round the original names were, `cellHeight` and
        // the `V` fields drive X here. The offsets are right; only the letters mislead. Left as
        // they are rather than renamed, because a rename would have to be checked against a ribbon
        // on screen and nothing draws one yet.
        float m_cellHeight = 0.0f;
        float m_cellWidth = 0.0f;
        float m_invCellHeight = 0.0f;
        float m_invCellWidth = 0.0f;
        // +0x6c through +0x78: the current cell's UV rectangle, as two corners.
        float m_cellU0 = 0.0f;
        float m_cellV0 = 0.0f;
        float m_cellU1 = 0.0f;
        float m_cellV1 = 0.0f;
        // +0x7c, +0x88, +0x94, +0xa0: four directions, turned with the ribbon by Transform (as
        // directions: no translation).
        //
        // What they ARE was established 2026-09-27 from BuildControlPoints, which is the only
        // reader: +0x7c and +0x88 are the EDGE direction at the current and the previous point --
        // the axis the trail is widened along -- and +0x94 and +0xa0 are the TANGENT at those two
        // points. Kept under their offset names because Transform is written against them and a
        // rename there is churn for no gain; the meaning is here.
        C3Vector vec7C = {};
        C3Vector vec88 = {};
        C3Vector vec94 = {};
        C3Vector vecA0 = {};

        // +0xac through +0xf0: the six control vectors of ONE segment's pair of edges, rebuilt
        // every update by BuildControlPoints and consumed by EmitSegment. This was `uintAC[18]`,
        // opaque storage, until those two functions were read -- 18 dwords is exactly six vectors.
        //
        // EmitSegment evaluates, for each edge,
        //
        //     p(t) = (A*t + B) * (1 - t) + (C - D * (1 - t)) * t
        //          = B*(1-t) + C*t + (A - D)*t*(1-t)
        //
        // which is a cubic Hermite: a straight line from B to C plus a bulge along (A - D). `t`
        // runs from the CURRENT point at 0 to the PREVIOUS point at 1, so B is the current end.
        // A and D are shared by both edges; B and C are per edge.
        C3Vector m_hermiteTangentCur = {};   // +0xac, A: vec94 scaled by the segment length
        C3Vector m_hermiteTangentPrev = {};  // +0xb8, D: vecA0 scaled by the same
        C3Vector m_lowerCur = {};            // +0xc4, B for the below edge
        C3Vector m_lowerPrev = {};           // +0xd0, C for the below edge
        C3Vector m_upperCur = {};            // +0xdc, B for the above edge
        C3Vector m_upperPrev = {};           // +0xe8, C for the above edge
        // +0xf4 and +0x100: the bounds, born INVERTED -- min at +FLT_MAX and max at -FLT_MAX, the
        // same empty-box convention and the same two constants (0x009ea8fc, 0x00a37f1c) the
        // particle emitter uses.
        C3Vector m_boundsMin = { 3.4028234663852886e+38f, 3.4028234663852886e+38f,
                                 3.4028234663852886e+38f };
        C3Vector m_boundsMax = { -3.4028234663852886e+38f, -3.4028234663852886e+38f,
                                 -3.4028234663852886e+38f };
        // +0x10c and +0x110: the edge rate, already passed through ceil, and the edge lifetime,
        // floored at 0.25 (0x00aa2d0c). Both left alone by the constructor and set by Initialize.
        float m_edgesPerSecond = 0.0f;
        float m_edgeLifetime = 0.0f;
        // +0x114, +0x124, +0x134: three arrays Initialize copies in wholesale from scratch arrays
        // the model builds. The first two are pinned by the draw, which walks the materials at
        // +0x118/+0x11c and the textures at +0x12c.
        //
        // The THIRD one's element type was established 2026-09-26, from the site that fills the
        // scratch array rather than from any declaration: CM2Model::InitializeLoaded stores
        // `&m_shared->m_data->textures[textureIndex]` into it, one per texture, so it is a
        // parallel array of M2Texture RECORDS beside m_textures' resolved handles -- the record
        // is what carries the wrap-mode flags the handle does not. Not a guess: the stride at the
        // fill site is 0x10, which is sizeof(M2Texture), and the base is m_data + 0x54, which is
        // the textures array whose count the destructor reads at m_data + 0x50.
        TSGrowableArray<Material> m_materials;
        TSGrowableArray<HTEXTURE> m_textures;
        TSGrowableArray<M2Texture*> m_textureRecords;
        // +0x144: a flat colour, white at construction.
        CImVector m_color = {};
        // +0x148 through +0x154: the sub-rectangle of the texture the grid divides up.
        float m_textureRect[4] = {};
        // +0x158 and +0x15c: the texture grid.
        uint32_t m_textureRows = 0;
        uint32_t m_textureCols = 0;
        // +0x160. Bit 0 is cleared by Initialize and set alongside bits 1..3; bit 2 is what
        // SetAbove drives. The rest is unread.
        uint32_t m_flags = 0;
        // +0x164: a point, moved with the rest of the ribbon by Transform.
        C3Vector vec164 = {};
        uint32_t uint170 = 0;
        // +0x174 and +0x178: how far the trail extends either side of its centre line. Both born
        // 10.0 (0x009e30cc) in Initialize, not in the constructor.
        //
        // NAMED 2026-09-27 from BuildControlPoints, which offsets the two edges by exactly these:
        // the above edge at `point + dir * m_heightAbove` and the below edge at
        // `point - dir * m_heightBelow`. They are the runtime side of M2Ribbon's own
        // heightAboveTrack and heightBelowTrack, which is the independent confirmation.
        float m_heightAbove = 0.0f;
        float m_heightBelow = 0.0f;
        // +0x17c: the gravity from the M2Ribbon record, through SetGravity.
        float m_gravity = 0.0f;

        // Member functions

        CM2Ribbon();

        // Size the ring, the indices and the texture grid, and take copies of the three arrays the
        // model built. Everything the constructor deliberately left alone is set here.
        //
        // The argument order is the reference's. `textureRect` is the sub-rectangle of the texture
        // the grid divides up, as (u0, v0, u1, v1).
        void Initialize(float edgesPerSecond, float edgeLifetime, CImVector color,
                        const TSGrowableArray<HTEXTURE>& textures,
                        const TSGrowableArray<Material>& materials,
                        const TSGrowableArray<M2Texture*>& textureRecords,
                        const float textureRect[4], uint32_t textureRows, uint32_t textureCols);

        // Rebuild the six control vectors above from the ribbon's current and previous point, its
        // two edge directions and its two tangents. Called once per update, before any segment is
        // emitted.
        void BuildControlPoints();

        // Write the vertex PAIR at m_head by evaluating both edges at `t`, stamp the slot's age,
        // and advance the head by `advance` (0 leaves it, which is how the live leading edge is
        // written without committing a slot).
        void EmitSegment(float age, float t, uint32_t advance);

        // One frame. Retires expired slots from the tail, emits whatever whole edges the elapsed
        // time has earned, then ages every live slot, applies gravity to it and rebuilds its
        // texture coordinates and the trail's bounds.
        //
        // `suppressEmit` non-zero skips the emission and only ages what is already there.
        void Update(float delta, int32_t suppressEmit);

        void SetGravity(float gravity);

        // Set or clear flag 0x4, and drop flag 0x1 with it when clearing.
        void SetAbove(int32_t above);

        // Does this ribbon have any trail at all? CM2Scene::Animate asks before
        // spending an element on it.
        bool IsEmpty() const;

        // Set or clear flag 0x8 from bit 0 of `enable`, dropping flag 0x1 when it ends up clear --
        // SetAbove's pattern on the next bit.
        void SetFlag8(int32_t enable);

        // Move the ribbon through `m`: its points (+0x20, +0x164, every vertex) as points, its
        // four directions through the 3x3 part.
        void Transform(const C44Matrix& m);

        // How many material passes the ribbon draws with.
        uint32_t GetMaterialCount() const;

        // The live span of the segment ring in vertices, two per segment.
        uint32_t CountVertices() const;
};

#endif
