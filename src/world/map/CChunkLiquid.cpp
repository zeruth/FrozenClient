#include "world/map/CChunkLiquid.hpp"
#include "world/map/LiquidSurface.hpp"
#include "world/CWorldScene.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/map/LiquidVertexData.hpp"

#include "world/CWorld.hpp"
#include "world/map/CMapObjGroup.hpp"
#include <tempest/Matrix.hpp>
#include <cmath>

static const float CHUNK_SIZE = 33.33333206176758f;       // DAT_00a3e554
// How long a layer keeps its surface after it was last seen. DAT_00a4040c
static const float LIQUID_SURFACE_LINGER = 2.0f;

// ref: FUN_007cdf80
// The layer's vertices, placed under the chunk's own corner rather than out in the world, so
// the draw folds the origin in once through a matrix instead of once per vertex.
//
// The reference works the step out from the chunk's map-wide index -- the distance between the
// near and far edge of one chunk, an eighth of it -- which is the same number for every chunk on
// every map. It is written here the way the arithmetic resolves, negative because the map's x
// and y both count down as the index counts up.
void CChunkLiquid::BuildVertices() {
    uint32_t countX = this->m_tileEndX - this->m_tileX + 1;
    uint32_t countY = this->m_tileEndY - this->m_tileY + 1;

    float step = -(CHUNK_SIZE / 8.0f);
    uint32_t vertex = 0;
    C3Vector* out = this->m_vertices;

    for (uint32_t x = 0; x < countX; x++) {
        for (uint32_t y = 0; y < countY; y++) {
            out->x = (this->m_tileX + x) * step;
            out->y = (this->m_tileY + y) * step;
            out->z = this->m_vertexData->GetHeight(vertex) - this->m_chunk->m_position.z;

            vertex++;
            out++;
        }
    }
}

// ref: FUN_007cf9a0
// A layer that is being looked at wants a surface. Asking for one is also what makes it, so a
// layer keeps asking until it has one; and the timer resets each frame it is seen, which is what
// keeps the surface alive while it stays in view.
void CChunkLiquid::UpdateForFrame() {
    if (!this->m_surface) {
        Liquid::CreateSurface(this);
    }

    // The waves age here, once a frame however many layers reach this.
    if (this->m_surface && this->m_surface->m_waveManager) {
        this->m_surface->m_waveManager->Update(CWorldScene::s_cameraPos);
    }

    // TODO with a surface in hand the reference queues it for the frame's draw
    // (FUN_007d62a0 with the manager from FUN_00780640), gated on the surface having geometry.
    // The draw is not ported, so nothing is queued yet.
    this->m_animTime = 0.0f;
}

// ref: FUN_007cde10
// Let the surface go. The reference DROPS A REFERENCE here rather than just forgetting the
// pointer, and until now this did the latter -- so every surface a layer ever built was leaked,
// along with its geometry factory and environment. The TODO that said otherwise named
// FUN_008a1ac0, which is CInstance::Release and has been ported for a while.
void CChunkLiquid::ReleaseSurface() {
    if (this->m_surface) {
        this->m_surface->Release();

        this->m_surface = nullptr;
    }
}

// ref: FUN_007ce520
// The layer's bounding sphere, built once and kept. Its centre is the CHUNK's box in x and y --
// a layer is as wide as its chunk for culling, the same simplification GetBounds below makes --
// and the layer's own water in z. The radius reaches the box corner at the water's top.
//
// The cache is the radius itself: negative means unbuilt, so there is no separate flag and no
// invalidation to forget. A layer is built for one chunk and its heights do not move, so once is
// enough.
//
// Only CWorldScene::TraverseRowLiquids asks, and only to hand it to SphereOccludedByVolumes.
const CAaSphere& CChunkLiquid::GetBoundingSphere() {
    if (this->m_sphere.r < 0.0f) {
        const CAaBox& bounds = this->m_chunk->m_bounds;

        this->m_sphere.c.x = (bounds.b.x + bounds.t.x) * 0.5f;
        this->m_sphere.c.y = (bounds.b.y + bounds.t.y) * 0.5f;
        this->m_sphere.c.z = (this->m_maxHeight + this->m_minHeight) * 0.5f;

        float dx = bounds.t.x - this->m_sphere.c.x;
        float dy = bounds.t.y - this->m_sphere.c.y;
        float dz = this->m_maxHeight - this->m_sphere.c.z;

        // Summed in the reference's own order, z first, because a float sum is not associative
        // and this feeds a cull that a rounding difference could flip at the margin.
        this->m_sphere.r = sqrtf(dz * dz + dy * dy + dx * dx);
    }

    return this->m_sphere;
}
// ref: FUN_007cde80
// A layer is as wide as its chunk for culling purposes -- the rectangle it actually covers is
// not narrowed here -- but only as tall as its own water.
void CChunkLiquid::GetBounds(CAaBox* box) const {
    *box = this->m_chunk->m_bounds;

    box->b.z = this->m_minHeight;
    box->t.z = this->m_maxHeight;
}

// ref: FUN_007cde30
// A layer out of sight for long enough gives its surface back and marks itself finished with;
// the frame list drops it on the strength of that. A layer that is still being seen never gets
// here with a positive clock, because the visit resets it every frame.
void CChunkLiquid::UpdateAnim() {
    if (this->m_animTime >= LIQUID_SURFACE_LINGER) {
        if (this->m_surface) {
            // The layer lets go; the surface frees itself once the last of its layers has.
            this->m_surface->Release();
            this->m_surface = nullptr;
        }

        this->m_animTime = -1.0f;
    }

    if (this->m_animTime >= 0.0f) {
        this->m_animTime += CWorld::GetTickTimeSec();
    }
}

// ref: FUN_007ce180
// MCLQ keeps a byte a tile: the low nibble names the liquid, and 0xf means the tile is dry.
// The tile belongs to this layer when the nibble agrees with the layer's own type in its low
// two bits -- which is all the reference compares, so the nibble's upper bits are free.
bool CChunkLiquid::ReadTileFlags(uint32_t x, uint32_t y, uint32_t* kind, uint32_t* fishable, uint32_t* shared) const {
    if (!this->m_tileMask) {
        return false;
    }

    uint8_t flags = this->m_tileMask[x + y * 8];

    *kind = flags & 0xf;
    *fishable = (flags >> 6) & 1;
    *shared = flags >> 7;

    if (*kind == 0xf) {
        return false;
    }

    return (((this->m_liquidType - 1) ^ *kind) & 3) == 0;
}

// ref: FUN_007ce1f0
bool CChunkLiquid::CoversTile(uint32_t x, uint32_t y) const {
    if (this->m_tileMask) {
        uint32_t kind, fishable, shared;

        return this->ReadTileFlags(x, y, &kind, &fishable, &shared);
    }

    if (this->m_tileY <= x && this->m_tileX <= y
        && x < this->m_tileEndY && y < this->m_tileEndX) {
        // The wet-tile bits are per tile, not per vertex, so the row is one shorter than the
        // vertex grid's.
        uint32_t stride = this->m_tileEndY - this->m_tileY;

        return const_cast<BitArray&>(this->m_exists).IsSet((y - this->m_tileX) * stride + (x - this->m_tileY));
    }

    return false;
}

// ref: FUN_007ce0b0
// Bilinear across the four vertices of one tile: along the row first, then between the two rows.
bool CChunkLiquid::GetHeightAt(const float* frac, const uint32_t* tile, float* height) const {
    uint32_t x = tile[0];
    uint32_t y = tile[1];

    if (x < this->m_tileY || y < this->m_tileX
        || this->m_tileEndY < x || this->m_tileEndX < y) {
        return false;
    }

    uint32_t stride = this->m_tileEndY - this->m_tileY + 1;
    uint32_t rowA = (y - this->m_tileX) * stride + (x - this->m_tileY);
    uint32_t rowB = rowA + stride;

    float h00 = this->m_vertexData->GetHeight(rowA);
    float h01 = this->m_vertexData->GetHeight(rowA + 1);
    float h10 = this->m_vertexData->GetHeight(rowB);
    float h11 = this->m_vertexData->GetHeight(rowB + 1);

    float a = (h01 - h00) * frac[0] + h00;
    float b = (h11 - h10) * frac[0] + h10;

    *height = (b - a) * frac[1] + a;

    return true;
}

// ================================================================================================
// The layer as a decal receiver. A blob shadow, or any other projected decal, lands on water the
// same way it lands on the ground: its triangles go into the shared hit-record pool and the decal
// module re-draws them.
// ================================================================================================

namespace {

// DAT_00a3fdb8, the slack the outcode allows: a vertex this far outside a face of the box still
// counts as inside it. The same constant, and the same bit assignment, as the terrain collector's
// corner classification in CMapObjGroup.cpp -- only the two z bits are wanted here, because x and y
// are already settled by clipping the tile rectangle.
constexpr float CORNER_EPSILON = 0.0194444433f;

// Which of the box's two z faces the vertex lies outside of, in bits 2 and 5 of the same outcode the
// terrain collector's six-face ClassifyCorner builds. The reference has this as its own function with
// exactly one caller, the classify loop below.
// ref: FUN_007c7790
uint8_t ClassifyCornerZ(const CAaBox& box, const C3Vector& v) {
    uint8_t code = 0;

    code |= std::signbit(v.z - box.b.z + CORNER_EPSILON) ? 0x04 : 0;
    code |= std::signbit(box.t.z - v.z + CORNER_EPSILON) ? 0x20 : 0;

    return code;
}

// The two triangles of one tile, as offsets from its first vertex, where `stride` is however many
// entries a row of the array being indexed holds. The vertex array and the outcode array have
// different strides -- the layer's whole row against the queried rectangle's -- so the same split is
// applied twice with different numbers (the pairs of stack triples at 0x007ce5eb and 0x007ce5f9).
struct TileTriangles {
    int32_t offset[2][3];
};

TileTriangles TileSplit(int32_t stride) {
    TileTriangles split;
    split.offset[0][0] = 0;
    split.offset[0][1] = stride;
    split.offset[0][2] = stride + 1;
    split.offset[1][0] = 0;
    split.offset[1][1] = stride + 1;
    split.offset[1][2] = 1;

    return split;
}

} // namespace

// The layer's own tile rectangle, +0x34 through +0x40, which the reference reads as a CiRect. Its
// "Y" slot is the m_tileX axis and its "X" slot the m_tileY one, the same way round as the two
// fields: see the note on m_tileX.
const CiRect& CChunkLiquid::TileRect() const {
    return *reinterpret_cast<const CiRect*>(&this->m_tileX);
}

// ref: FUN_007ce5d0
// Append the layer's wet triangles inside `rect` to the hit-record pool, skipping any triangle whose
// three corners are all beyond one face of the caster's box. `outcodes` is one byte per vertex of
// `rect`, row-major, which the caller has already classified.
//
// One record covers the whole layer, allocated on the first triangle kept, and it is stamped with
// the layer's CHUNK rather than the layer -- the same owner a terrain record carries. Its `heights`
// stays null, so a liquid receiver takes the stream builders' triangle-expansion path with the
// winding test: the layer's own vertices carry their z.
bool CChunkLiquid::RecordHits(void* object, const uint8_t* outcodes, const CiRect& rect, const int32_t* span) {
    // The reference threads its caller's owner argument in here and never reads it: a liquid record
    // is stamped with the layer's chunk, the way a terrain one is stamped with its own.
    (void)object;

    // How many vertices a row of the layer's array holds, and of the queried rectangle's.
    int32_t layerStride = static_cast<int32_t>(this->m_tileEndY - this->m_tileY) + 1;
    int32_t rectStride = span[0] + 1;

    TileTriangles vertexSplit = TileSplit(layerStride);
    TileTriangles outcodeSplit = TileSplit(rectStride);

    // The layer vertex the rectangle starts at, and how far to skip at the end of each of its rows.
    int32_t vertex = (rect.minY - static_cast<int32_t>(this->m_tileX)) * layerStride
                   - static_cast<int32_t>(this->m_tileY) + rect.minX;
    int32_t vertexRowSkip = layerStride - span[0];

    CMapObjHitRecord* record = nullptr;
    uint16_t* indices = nullptr;

    for (int32_t row = 0; row < span[1]; row++) {
        for (int32_t col = 0; col < span[0]; col++) {
            bool wet = this->CoversTile(static_cast<uint32_t>(rect.minX + col),
                                        static_cast<uint32_t>(rect.minY + row));

            for (int32_t t = 0; wet && t < 2; t++) {
                const int32_t* oc = outcodeSplit.offset[t];

                if ((outcodes[oc[0]] & outcodes[oc[1]] & outcodes[oc[2]]) != 0) {
                    continue;
                }

                if (!record) {
                    record = CMapObjGroup::AllocHitRecord();

                    // Unlike the terrain collector, this one gives up on the whole layer the moment
                    // a pool is full rather than carrying on with a record it cannot fill.
                    if (!record) {
                        return false;
                    }

                    record->object = this->m_chunk;

                    C44Matrix* placement = CMapObjGroup::AllocHitPlacement();

                    if (placement) {
                        placement->Identity();
                        placement->d0 = this->m_chunk->m_position.x;
                        placement->d1 = this->m_chunk->m_position.y;
                        placement->d2 = this->m_chunk->m_position.z;
                    }

                    record->placement = placement;
                    record->vertices = this->m_vertices;

                    indices = CMapObjGroup::AllocHitIndices(static_cast<uint32_t>(span[0] * span[1] * 6));
                    record->indices = indices;

                    if (!indices) {
                        return false;
                    }
                }

                const int32_t* vs = vertexSplit.offset[t];

                for (int32_t k = 0; k < 3; k++) {
                    uint16_t index = static_cast<uint16_t>(vs[k] + vertex);

                    indices[record->indexCount] = index;
                    record->indexCount++;

                    if (index < record->minIndex) {
                        record->minIndex = index;
                    }

                    if (index > record->maxIndex) {
                        record->maxIndex = index;
                    }
                }

                record->faceCount += 1;
            }

            vertex++;
            outcodes++;
        }

        outcodes++;
        vertex += vertexRowSkip;
    }

    return record != nullptr;
}

// ref: FUN_007ce960
// The layer half of the world box query: clip the caster's cell rectangle to the tiles this layer
// covers, classify each vertex of what is left against the box, and hand the outcodes to RecordHits.
//
// Only the z bits are computed. The caller's rectangle has already settled x and y, and the clip
// against the layer's own rectangle settles the rest -- which is why the classification here is two
// comparisons a vertex rather than six.
//
// `cellRect` counts CELLS; the clip wants vertices, so both maxima grow by one first.
bool CChunkLiquid::QueryBox(void* object, const CAaBox& box, const CiRect& cellRect) {
    CiRect wanted;
    wanted.minY = cellRect.minY;
    wanted.minX = cellRect.minX;
    wanted.maxY = cellRect.maxY + 1;
    wanted.maxX = cellRect.maxX + 1;

    CiRect rect = CiRect::Intersection(this->TileRect(), wanted);

    int32_t span[2] = { rect.maxX - rect.minX, rect.maxY - rect.minY };

    // A layer covers at most nine vertices each way, so the reference's stack buffer is 84 bytes.
    uint8_t outcodes[84];

    int32_t layerStride = static_cast<int32_t>(this->m_tileEndY - this->m_tileY) + 1;
    int32_t vertex = (rect.minY - static_cast<int32_t>(this->m_tileX)) * layerStride
                   - static_cast<int32_t>(this->m_tileY) + rect.minX;

    uint8_t* out = outcodes;

    for (int32_t row = 0; row <= span[1]; row++) {
        for (int32_t col = 0; col <= span[0]; col++) {
            *out++ = ClassifyCornerZ(box, this->m_vertices[vertex + col]);
        }

        vertex += layerStride;
    }

    return this->RecordHits(object, outcodes, rect, span);
}
