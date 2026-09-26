#include "world/map/CChunkLiquid.hpp"
#include "world/map/LiquidSurface.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/map/LiquidVertexData.hpp"

#include "world/CWorld.hpp"

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
