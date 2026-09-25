#include "world/map/CChunkLiquid.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/map/LiquidVertexData.hpp"

static const float CHUNK_SIZE = 33.33333206176758f;       // DAT_00a3e554

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

// ref: FUN_007cde10
void CChunkLiquid::ReleaseSurface() {
    if (this->m_surface) {
        // TODO FUN_008a1ac0: the Liquid module hands the drawn surface back to its own pool.
        this->m_surface = nullptr;
    }
}
