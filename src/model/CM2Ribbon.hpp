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
// NOT USABLE YET. The constructor and the small setters are here; `Initialize`, the per-frame
// segment update and `Draw` are not, so nothing constructs one of these outside a test. The
// update is the part with real behaviour and has not been read at all.
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

        // One segment of the trail. SIZE IS 0x18 and that is measured: the SetCount the ring
        // goes through (FUN_00980810) strides by 0x18 and zeroes six dwords per element.
        // WHAT IS IN IT is not known -- the per-frame update that fills it has not been read --
        // so this is deliberately opaque rather than six invented field names.
        struct Segment {
            uint32_t raw[6];
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
        // +0x04: the segment ring. Initialize sizes it to
        // `ceil(edgeLifetime * ceil(edgesPerSecond)) + 2`.
        TSGrowableArray<Segment> m_segments;
        // +0x14 and +0x18: the ring's ends, and the DRAW is what pins which is which -- it
        // computes `tail < head ? head - tail : head + capacity - tail`, so the live span runs
        // from tail forward to head and wraps. Equal means empty; see IsEmpty.
        uint32_t m_head = 0;
        uint32_t m_tail = 0;
        uint32_t uint1C = 0;
        // +0x20: a point, moved with the rest of the ribbon by Transform. Not read elsewhere yet.
        C3Vector vec20 = {};
        // +0x2c: where the ribbon is. The draw subtracts this from the world matrix's
        // translation row, so the geometry is stored relative to it.
        C3Vector m_origin = {};
        // +0x38: the trail geometry, built on the CPU and uploaded whole each draw.
        TSGrowableArray<Vertex> m_vertices;
        // +0x48: its indices. Initialize fills them with `i % (segments * 2)` and the draw takes
        // a triangle STRIP out of them.
        TSGrowableArray<uint16_t> m_indices;
        // +0x58: one over the edge lifetime.
        float m_invEdgeLifetime = 0.0f;
        // +0x5c and +0x60: one texture cell's size in UV, from the grid and the texture rect.
        // +0x64 and +0x68 are their reciprocals, which Initialize computes by dividing rather
        // than by keeping the numerator -- transcribed that way.
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
        // directions: no translation). Not read elsewhere yet.
        C3Vector vec7C = {};
        C3Vector vec88 = {};
        C3Vector vec94 = {};
        C3Vector vecA0 = {};
        uint32_t uintAC[18] = {};
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
        // +0x118/+0x11c and the textures at +0x12c. The third is not read by anything seen so
        // far, so its element type is not established and it is left as raw storage rather than
        // given a made-up one.
        TSGrowableArray<Material> m_materials;
        TSGrowableArray<HTEXTURE> m_textures;
        uint32_t array134[4] = {};
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
        // +0x174 and +0x178: both born 10.0 (0x009e30cc) in Initialize, not in the constructor.
        float float174 = 0.0f;
        float float178 = 0.0f;
        // +0x17c: the gravity from the M2Ribbon record, through SetGravity.
        float m_gravity = 0.0f;

        // Member functions

        CM2Ribbon();

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
