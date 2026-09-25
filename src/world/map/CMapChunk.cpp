#include "world/map/CMapChunk.hpp"
#include "world/map/CMap.hpp"
#include "world/map/CMapArea.hpp"
#include "world/map/CMapRenderChunk.hpp"
#include "gx/Gx.hpp"
#include <cfloat>
#include <cmath>

float CMapChunk::s_vertexTable[145][3];
float CMapChunk::s_invCellSize;
uint16_t CMapChunk::s_vertexSpan;

// Row-major bit for each 2x2 cell of a chunk's 4x4 hole grid (DAT_00a3faf0)
static const uint16_t s_holeMask[16] = {
    0x0001, 0x0002, 0x0004, 0x0008,
    0x0010, 0x0020, 0x0040, 0x0080,
    0x0100, 0x0200, 0x0400, 0x0800,
    0x1000, 0x2000, 0x4000, 0x8000,
};

// The world-space fillers stage a chunk's 9 row X and 9 column Y coordinates here first
static float s_rowX[9];        // DAT_00d25b88 .. (FillVerticesWorld)
static float s_colY[9];        // DAT_00d25b64 ..
static float s_rowXColor[9];   // DAT_00d25bd0 .. (FillVerticesWorldColor)
static float s_colYColor[9];   // DAT_00d25bac ..

static const float CHUNK_SIZE = 33.33333206176758f;       // DAT_00a3e554
static const float MAP_HALF_EXTENT = 17066.666015625f;    // DAT_009e2acc
static const float CELL_SIZE = 4.166666507720947f;        // DAT_00a3fda8: CHUNK_SIZE / 8
static const float HALF_CELL = 2.0833332538604736f;       // DAT_00a3fab0
static const float INV_127 = 0.007874015718698502f;       // DAT_00a40360
static const uint32_t NO_VERTEX_COLOR = 0xFF7FFFFF;       // the float -3.3961514e+38 the reference stores

// ref: FUN_007c5c50
// The reference constructor also builds two small objects at the end of the chunk with
// FUN_0095da10 (destroyed by FUN_0095da80), not identified yet.
CMapChunk::CMapChunk() {
    this->m_type |= CMapBaseObj::Type_Chunk;
    this->m_flags |= 0x1;
}

// ref: FUN_007c5e50
// Every list is emptied and every link dropped; FUN_005bd800 between them is not identified.
CMapChunk::~CMapChunk() {
    this->m_liquidList.UnlinkAll();
    this->m_linkListE8.UnlinkAll();
    this->m_linkListDc.UnlinkAll();
    this->m_mapObjDefLinkList.UnlinkAll();
    this->m_entityLinkList.UnlinkAll();
    this->m_frameLink.Unlink();
    this->m_linkB4.Unlink();
}

// ref: FUN_007c64b0
// Binds the chunk to its MCNK: sub-chunks, area id, origin from the indices and the header's z,
// bounds, then its liquids, sound emitters and doodad/WMO references (the last three are
// FUN_007c5690, FUN_007c6060 and FUN_007c6150, not ported yet), and finally the tile's grid slot.
void CMapChunk::Load(uint8_t* data, int32_t fixSizes) {
    this->m_data = data;
    this->ParseSubChunks(fixSizes);

    this->m_areaId = this->m_header->areaId;

    this->m_position.x = -(CHUNK_SIZE * static_cast<float>(this->m_indexX)) + MAP_HALF_EXTENT;
    this->m_position.y = -(static_cast<float>(this->m_indexY) * CHUNK_SIZE) + MAP_HALF_EXTENT;
    this->m_position.z = 0.0f;
    this->m_position.z = this->m_header->position.z;

    this->m_lowQualityTextureMap = this->m_header->lowQualityTextureMap;
    this->m_predTex = &this->m_header->predTex;

    this->ComputeBounds();

    // TODO FUN_007c5690(fixSizes): liquids (MCLQ and MH2O layers)
    // TODO FUN_007c6060(fixSizes): MCSE sound emitters

    this->m_flags = 0;
    if (this->m_header->flags & 0x2) {
        this->m_flags = 0x40;
    }

    auto area = static_cast<CMapArea*>(this->m_parentLinkList.Head()->ref);

    // TODO FUN_007c6150(area, m_refs, m_header->nDoodadRefs, m_header->nMapObjRefs): MCRF references

    area->m_chunks[this->m_areaChunkY * 16 + this->m_areaChunkX] = this;
    this->m_flags |= 0x80;
}

// ref: FUN_007c3370
// Releases everything the chunk owns before CMap::FreeChunk returns it to the heap. The entity
// and def releases the reference runs on the owners it unlinks (FUN_007c3020, FUN_007c3250),
// the detail-doodad release (FUN_007b3960), the liquid destroy (FUN_007cde10) and the sound
// handle release (FUN_007c3330 / FUN_004cb1d0) are not ported yet.
void CMapChunk::Destroy() {
    if (this->m_renderChunk) {
        CMap::FreeRenderChunk(this->m_renderChunk);
        this->m_renderChunk = nullptr;
    }

    if (this->m_ptrA4) {
        // TODO FUN_007b3960(m_ptrA4)
        this->m_ptrA4 = nullptr;
    }

    for (auto liquid = this->m_liquidList.Head(); liquid; ) {
        auto next = this->m_liquidList.Next(liquid);
        liquid->m_chunkLink.Unlink();
        // TODO FUN_007cde10(liquid)
        CMap::FreeChunkLiquid(liquid);
        liquid = next;
    }

    this->m_frameLink.Unlink();

    for (auto link = this->m_entityLinkList.Head(); link; ) {
        auto next = this->m_entityLinkList.Next(link);
        auto owner = link->owner;
        CMap::FreeBaseObjLink(link);
        if (!(owner->m_type & CMapBaseObj::Type_200)) {
            // TODO FUN_007c3020(owner): release the entity once nothing links it
        }
        link = next;
    }

    for (auto link = this->m_mapObjDefLinkList.Head(); link; ) {
        auto next = this->m_mapObjDefLinkList.Next(link);
        CMap::FreeBaseObjLink(link);
        // TODO FUN_007c3250(owner): release the map obj def once nothing links it
        link = next;
    }

    for (auto link = this->m_parentLinkList.Head(); link; ) {
        auto next = this->m_parentLinkList.Next(link);
        CMap::FreeBaseObjLink(link);
        link = next;
    }

    for (auto link = this->m_linkListDc.Head(); link; ) {
        auto next = this->m_linkListDc.Next(link);
        CMap::FreeBaseObjLink(link);
        link = next;
    }

    for (auto link = this->m_linkListE8.Head(); link; ) {
        auto next = this->m_linkListE8.Next(link);
        CMap::FreeBaseObjLink(link);
        link = next;
    }

    // TODO the sound handle list at +0xf4: FUN_004cb1d0 on each, freed by FUN_007c3330
}

// ref: FUN_007c3d90
// Once per client: the vertex table and its cell scale. The reference calls FUN_007ba340 first,
// which is not ported yet (Map.cpp region).
void CMapChunk::Initialize() {
    CMapChunk::BuildVertexTable();
    CMapChunk::s_invCellSize = -1.0f / CMapChunk::s_vertexTable[1][1];
}

// ref: FUN_007c3c60
// Nine outer rows of nine vertices at cell spacing, each followed (except the last) by an inner
// row of eight offset by half a cell, all in chunk-local space with the origin at the chunk's
// first vertex and both axes running negative.
void CMapChunk::BuildVertexTable() {
    float* v = &CMapChunk::s_vertexTable[0][0];

    for (int32_t row = 0; row < 9; row++) {
        float x = static_cast<float>(row) * -CELL_SIZE;

        for (int32_t col = 0; col < 9; col++) {
            v[0] = x;
            v[1] = static_cast<float>(col) * -CELL_SIZE;
            v += 3;
        }

        if (row < 8) {
            float xi = x - HALF_CELL;

            for (int32_t col = 0; col < 8; col++) {
                v[0] = xi;
                v[1] = static_cast<float>(col) * -CELL_SIZE - HALF_CELL;
                v += 3;
            }
        }
    }
}

// ref: FUN_007c3a10
// Walks the MCNK's sub-chunks and points the chunk at each. With fixSizes the sizes Blizzard's
// tools wrote wrong are corrected in place: MCNR is always 0x1c0 bytes and MCAL is sizeAlpha
// less its header. MCLQ counts only when the header says there is liquid, MCSE only when it
// says there are emitters.
void CMapChunk::ParseSubChunks(int32_t fixSizes) {
    uint8_t* data = this->m_data;
    this->m_header = reinterpret_cast<SMChunk*>(data + 8);

    uint32_t* sub = reinterpret_cast<uint32_t*>(data + 8 + sizeof(SMChunk));

    for (int32_t remaining = *reinterpret_cast<int32_t*>(data + 4) - sizeof(SMChunk); remaining > 0; ) {
        uint32_t tag = sub[0];
        uint8_t* body = reinterpret_cast<uint8_t*>(sub + 2);

        switch (tag) {
        case 'MCVT':
            this->m_heights = reinterpret_cast<float*>(body);
            break;
        case 'MCCV':
            this->m_vertexColors = reinterpret_cast<uint32_t*>(body);
            break;
        case 'MCNR':
            this->m_normals = reinterpret_cast<int8_t*>(body);
            if (fixSizes) {
                sub[1] = 0x1c0;
            }
            break;
        case 'MCLY':
            this->m_layers = reinterpret_cast<SMLayer*>(body);
            break;
        case 'MCRF':
            this->m_refs = reinterpret_cast<uint32_t*>(body);
            break;
        case 'MCSH':
            this->m_shadow = body;
            break;
        case 'MCAL':
            this->m_alpha = body;
            if (fixSizes) {
                sub[1] = this->m_header->sizeAlpha - 8;
            }
            break;
        case 'MCLQ':
            if (this->m_header->sizeLiquid > 8) {
                this->m_liquidData = body;
                sub[1] = this->m_header->sizeLiquid - 8;
            }
            break;
        case 'MCSE':
            if (this->m_header->nSndEmitters) {
                this->m_soundEmitters = body;
            }
            break;
        default:
            break;
        }

        remaining -= 8 + sub[1];
        sub = reinterpret_cast<uint32_t*>(body + sub[1]);
    }
}

// ref: FUN_007c3b60
// Four triangles per cell around its inner vertex, skipping cells the hole mask covers. Rows are
// 17 vertices apart (9 outer + 8 inner); a cell's inner vertex is 9 past its first outer one.
int16_t CMapChunk::BuildIndices(uint16_t* indices, int16_t baseVertex) {
    int16_t count = 0;

    for (int32_t row = 0; row < 8; row++) {
        for (int32_t col = 0; col < 8; col++) {
            if (s_holeMask[(col >> 1) + (row >> 1) * 4] & this->m_header->holes) {
                continue;
            }

            int16_t v = baseVertex + col;
            int16_t center = v + 9;
            int16_t below = v + 17;
            int16_t right = v + 1;
            int16_t belowRight = v + 18;

            indices[0] = center;
            indices[1] = v;
            indices[2] = below;
            indices[3] = center;
            indices[4] = right;
            indices[5] = v;
            indices[6] = center;
            indices[7] = belowRight;
            indices[8] = right;
            indices[9] = center;
            indices[10] = below;
            indices[11] = belowRight;

            indices += 12;
            count += 12;
        }

        baseVertex += 17;
    }

    return count;
}

// ref: FUN_007c51b0
// Appends this chunk's triangles to a batch, placing its vertices after the batch's current
// highest index, and widens the batch's index range to cover them.
void CMapChunk::AppendIndices(uint16_t* indices, CGxBatch* batch) {
    uint32_t base = batch->m_maxIndex ? static_cast<uint16_t>(batch->m_maxIndex + 1) : 0;

    int16_t count = this->BuildIndices(indices, static_cast<int16_t>(base));

    if (static_cast<uint16_t>(base) <= batch->m_minIndex) {
        batch->m_minIndex = static_cast<uint16_t>(base);
    }

    uint32_t top = base + CMapChunk::s_vertexSpan;
    if (top < batch->m_maxIndex) {
        top = batch->m_maxIndex;
    }
    batch->m_maxIndex = static_cast<uint16_t>(top);

    batch->m_count = static_cast<uint16_t>(batch->m_count + count);
}

// ref: FUN_007c3b40
// A render chunk for this chunk alone, drawn from the chunk's own origin
void CMapChunk::CreateRenderChunk() {
    this->m_renderChunk = CMap::AllocRenderChunk();
    this->m_renderChunk->Init(this, nullptr, this->m_position, 0);
}

// ref: FUN_007c5440
// Gives the chunk its render chunk once. With the shader vertex mode on (DAT_00ce0498) the
// reference instead pairs chunks two by two through FUN_007d6810 on the even cell coordinates,
// which is not ported yet, so every chunk gets its own.
void CMapChunk::EnsureRenderChunk() {
    if (this->m_renderChunkReady) {
        return;
    }

    // TODO if (CMap::s_shaderVertexMode) { cell = { m_areaChunkX & ~1, m_areaChunkY & ~1 }; FUN_007d6810(&cell); return; }

    this->CreateRenderChunk();
    this->m_renderChunkReady = 1;
}

// ref: FUN_007c54c0
// World-space chunks share one buffer and write at their vertex base; local-space chunks each
// fill their own buffer from the start. The format flag drops the colour from the vertex.
void CMapChunk::BuildVertices(void* buffer, int32_t vertexBase, const C3Vector* offset) {
    if (CMap::s_chunkVerticesWorldSpace) {
        if (CMap::s_terrainVertexFormat == 1) {
            this->FillVerticesWorld(static_cast<CMapChunkVertex*>(buffer) + vertexBase, offset);
        } else {
            this->FillVerticesWorldColor(static_cast<CMapChunkVertexColor*>(buffer) + vertexBase, offset);
        }
    } else {
        if (CMap::s_terrainVertexFormat == 1) {
            this->FillVerticesLocal(static_cast<CMapChunkVertex*>(buffer));
        } else {
            this->FillVerticesLocalColor(static_cast<CMapChunkVertexColor*>(buffer));
        }
    }
}

// The packed MCCV colour as the device wants it: the reference asks the device caps for every
// vertex, and swaps the red and blue bytes when the device is RGBA.
static inline uint32_t ChunkVertexColor(const uint32_t* color) {
    uint32_t c = *color;

    if (GxCaps().m_colorFormat == GxCF_rgba) {
        c = (c & 0xFF00FF00) | ((c >> 16) & 0xFF) | ((c & 0xFF) << 16);
    }

    return c;
}

static inline void ChunkNormal(C3Vector& n, const int8_t* src) {
    n.x = static_cast<float>(src[0]) * INV_127;
    n.y = static_cast<float>(src[1]) * INV_127;
    n.z = static_cast<float>(src[2]) * INV_127;
}

// Row X and column Y for a world-space chunk: the outer edges from the chunk indices, the seven
// between them a cell apart
static void ChunkWorldRows(const CMapChunk* chunk, float* rowX, float* colY) {
    rowX[0] = -(static_cast<float>(chunk->m_indexX) * CHUNK_SIZE) + MAP_HALF_EXTENT;
    colY[0] = -(static_cast<float>(chunk->m_indexY) * CHUNK_SIZE) + MAP_HALF_EXTENT;
    rowX[8] = -(static_cast<float>(chunk->m_indexX + 1) * CHUNK_SIZE) + MAP_HALF_EXTENT;
    colY[8] = -(static_cast<float>(chunk->m_indexY + 1) * CHUNK_SIZE) + MAP_HALF_EXTENT;

    for (int32_t i = 1; i < 8; i++) {
        rowX[i] = rowX[0] - static_cast<float>(i) * CELL_SIZE;
        colY[i] = colY[0] - static_cast<float>(i) * CELL_SIZE;
    }
}

// ref: FUN_007c3f30
void CMapChunk::FillVerticesWorld(CMapChunkVertex* dst, const C3Vector* offset) {
    ChunkWorldRows(this, s_rowX, s_colY);

    const float* height = this->m_heights;
    const int8_t* normal = this->m_normals;
    float z = this->m_position.z;

    for (int32_t row = 0; row < 9; row++) {
        for (int32_t col = 0; col < 9; col++) {
            dst->position.x = s_rowX[row];
            dst->position.y = s_colY[col];
            dst->position.z = z + *height++;
            ChunkNormal(dst->normal, normal);
            normal += 3;
            dst++;
        }

        if (row < 8) {
            for (int32_t col = 0; col < 8; col++) {
                dst->position.x = s_rowX[row] - HALF_CELL;
                dst->position.y = s_colY[col] - HALF_CELL;
                dst->position.z = z + *height++;
                ChunkNormal(dst->normal, normal);
                normal += 3;
                dst++;
            }
        }
    }
}

// ref: FUN_007c4620
void CMapChunk::FillVerticesWorldColor(CMapChunkVertexColor* dst, const C3Vector* offset) {
    ChunkWorldRows(this, s_rowXColor, s_colYColor);

    const float* height = this->m_heights;
    const int8_t* normal = this->m_normals;
    const uint32_t* color = this->m_vertexColors;
    float z = this->m_position.z;

    for (int32_t row = 0; row < 9; row++) {
        for (int32_t col = 0; col < 9; col++) {
            dst->position.x = s_rowXColor[row];
            dst->position.y = s_colYColor[col];
            dst->position.z = z + *height++;
            ChunkNormal(dst->normal, normal);
            normal += 3;
            dst->color = this->m_vertexColors ? ChunkVertexColor(color) : NO_VERTEX_COLOR;
            color++;
            dst++;
        }

        if (row < 8) {
            for (int32_t col = 0; col < 8; col++) {
                dst->position.x = s_rowXColor[row] - HALF_CELL;
                dst->position.y = s_colYColor[col] - HALF_CELL;
                dst->position.z = z + *height++;
                ChunkNormal(dst->normal, normal);
                normal += 3;
                dst->color = this->m_vertexColors ? ChunkVertexColor(color) : NO_VERTEX_COLOR;
                color++;
                dst++;
            }
        }
    }
}

// Cell step along each axis for a local-space chunk: an eighth of the chunk's extent, negative
// because both axes run negative from the origin
static inline float ChunkLocalStep(int32_t index) {
    float lo = -(static_cast<float>(index) * CHUNK_SIZE) + MAP_HALF_EXTENT;
    float hi = -(static_cast<float>(index + 1) * CHUNK_SIZE) + MAP_HALF_EXTENT;
    return (lo - hi) * -0.125f;
}

// ref: FUN_007c4960
// Local space: positions from the chunk's own origin, heights as they are (the origin's z goes
// into the chunk matrix). The inner rows use the two steps the other way round, which only
// matters if a chunk were ever not square.
void CMapChunk::FillVerticesLocal(CMapChunkVertex* dst) {
    float stepY = ChunkLocalStep(this->m_indexY);
    float stepX = ChunkLocalStep(this->m_indexX);
    float halfY = stepY * 0.5f;
    float halfX = stepX * 0.5f;

    const float* height = this->m_heights;
    const int8_t* normal = this->m_normals;

    for (int32_t row = 0; row < 9; row++) {
        float x = static_cast<float>(row) * stepX;

        for (int32_t col = 0; col < 9; col++) {
            dst->position.x = x;
            dst->position.y = static_cast<float>(col) * stepY;
            dst->position.z = *height++;
            ChunkNormal(dst->normal, normal);
            normal += 3;
            dst++;
        }

        if (row < 8) {
            float xi = static_cast<float>(row) * stepY + halfX;

            for (int32_t col = 0; col < 8; col++) {
                dst->position.x = xi;
                dst->position.y = static_cast<float>(col) * stepX + halfY;
                dst->position.z = *height++;
                ChunkNormal(dst->normal, normal);
                normal += 3;
                dst++;
            }
        }
    }
}

// ref: FUN_007c4f10
void CMapChunk::FillVerticesLocalColor(CMapChunkVertexColor* dst) {
    float stepY = ChunkLocalStep(this->m_indexY);
    float stepX = ChunkLocalStep(this->m_indexX);
    float halfY = stepY * 0.5f;
    float halfX = stepX * 0.5f;

    const float* height = this->m_heights;
    const int8_t* normal = this->m_normals;
    const uint32_t* color = this->m_vertexColors;

    for (int32_t row = 0; row < 9; row++) {
        for (int32_t col = 0; col < 9; col++) {
            dst->position.x = static_cast<float>(row) * stepX;
            dst->position.y = static_cast<float>(col) * stepY;
            dst->position.z = *height++;
            ChunkNormal(dst->normal, normal);
            normal += 3;
            dst->color = this->m_vertexColors ? ChunkVertexColor(color) : NO_VERTEX_COLOR;
            color++;
            dst++;
        }

        if (row < 8) {
            for (int32_t col = 0; col < 8; col++) {
                dst->position.x = static_cast<float>(row) * stepY + halfX;
                dst->position.y = static_cast<float>(col) * stepX + halfY;
                dst->position.z = *height++;
                ChunkNormal(dst->normal, normal);
                normal += 3;
                dst->color = this->m_vertexColors ? ChunkVertexColor(color) : NO_VERTEX_COLOR;
                color++;
                dst++;
            }
        }
    }
}

// ref: FUN_007c5220
// The chunk's box: XY from its indices, Z from the lowest and highest of its 145 heights plus the
// origin; then the centre, the radius to a corner, and a second copy of the box.
void CMapChunk::ComputeBounds() {
    this->m_bounds.b.z = FLT_MAX;
    this->m_bounds.t.z = -FLT_MAX;

    float y0 = static_cast<float>(this->m_indexY) * CHUNK_SIZE;
    float x0 = static_cast<float>(this->m_indexX) * CHUNK_SIZE;
    float y1 = static_cast<float>(this->m_indexY + 1) * CHUNK_SIZE;

    this->m_bounds.b.x = -(static_cast<float>(this->m_indexX + 1) * CHUNK_SIZE) + MAP_HALF_EXTENT;
    this->m_bounds.b.y = -y1 + MAP_HALF_EXTENT;
    this->m_bounds.t.x = -x0 + MAP_HALF_EXTENT;
    this->m_bounds.t.y = -y0 + MAP_HALF_EXTENT;

    const float* height = this->m_heights;

    for (int32_t i = 0; i < 145; i++) {
        if (height[i] < this->m_bounds.b.z) {
            this->m_bounds.b.z = height[i];
        }
        if (this->m_bounds.t.z < height[i]) {
            this->m_bounds.t.z = height[i];
        }
    }

    this->m_bounds.b.z += this->m_position.z;
    this->m_bounds.t.z += this->m_position.z;

    this->m_center.x = (this->m_bounds.b.x + this->m_bounds.t.x) * 0.5f;
    this->m_center.y = (this->m_bounds.b.y + this->m_bounds.t.y) * 0.5f;
    this->m_center.z = (this->m_bounds.b.z + this->m_bounds.t.z) * 0.5f;

    float dx = this->m_bounds.t.x - this->m_center.x;
    float dy = this->m_bounds.t.y - this->m_center.y;
    float dz = this->m_bounds.t.z - this->m_center.z;

    this->m_bounds2 = this->m_bounds;

    this->m_radius = sqrtf(dz * dz + dy * dy + dx * dx);
}

// ref: FUN_007c5530
// The chunk's box, with Z replaced by the span of its liquids when it has any: the first liquid
// seeds both ends and every liquid (the first again included) widens them.
void CMapChunk::GetBounds(CAaBox* box) {
    box->t = this->m_bounds.t;
    box->b = this->m_bounds.b;

    auto liquid = this->m_liquidList.Head();

    if (liquid) {
        box->t.z = liquid->m_maxHeight;
        box->b.z = liquid->m_minHeight;

        do {
            box->t.z = box->t.z <= liquid->m_maxHeight ? liquid->m_maxHeight : box->t.z;
            box->b.z = liquid->m_minHeight <= box->b.z ? liquid->m_minHeight : box->b.z;
            liquid = this->m_liquidList.Next(liquid);
        } while (liquid);
    }
}
