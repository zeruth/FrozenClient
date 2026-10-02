#include "world/map/CMapDoodadDef.hpp"
#include "world/CWFrustum.hpp"
#include "world/WorldFacets.hpp"
#include "world/map/CMapChunk.hpp"
#include <tempest/Intersect.hpp>
#include "world/map/CMap.hpp"
#include "world/map/CMapArea.hpp"
#include "world/map/CMapRenderChunk.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldScene.hpp"

#include "world/map/CChunkLiquid.hpp"
#include "world/map/LiquidVertexData.hpp"
#include "db/Db.hpp"
#include "world/map/DetailDoodad.hpp"
#include "gx/Gx.hpp"
#include <storm/Memory.hpp>
#include <new>
#include <cfloat>
#include <cmath>

float CMapChunk::s_vertexTable[145][3];
float CMapChunk::s_invCellSize;

const uint16_t CMapChunk::s_holeMask[16] = {
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
    this->m_groundedLinkList.UnlinkAll();
    this->m_mapObjDefLinkList.UnlinkAll();
    this->m_entityLinkList.UnlinkAll();
    this->m_frameLink.Unlink();
    this->m_rowLink.Unlink();
}

// ref: FUN_007c64b0
// Binds the chunk to its MCNK: sub-chunks, area id, origin from the indices and the header's z,
// bounds, then its liquids, sound emitters (FUN_007c6060, not ported) and doodad/WMO references,
// and finally the tile's grid slot.
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

    this->CreateLiquid();
    // TODO FUN_007c6060(fixSizes): MCSE sound emitters

    this->m_flags = 0;
    if (this->m_header->flags & 0x2) {
        this->m_flags = 0x40;
    }

    auto area = static_cast<CMapArea*>(this->m_parentLinkList.Head()->ref);

    this->CreateRefs(area, this->m_refs, this->m_header->nDoodadRefs, this->m_header->nMapObjRefs);

    area->m_chunks[this->m_areaChunkY * 16 + this->m_areaChunkX] = this;
    this->m_flags |= 0x80;
}

// ref: FUN_007c3370
// Releases everything the chunk owns before CMap::FreeChunk returns it to the heap. The map obj
// def release the reference runs on the owners it unlinks (FUN_007c3250),
// the detail-doodad release (FUN_007b3960), the liquid destroy (FUN_007cde10) and the sound
// handle release (FUN_007c3330 / FUN_004cb1d0) are not ported yet.
void CMapChunk::Destroy() {
    if (this->m_renderChunk) {
        CMap::FreeRenderChunk(this->m_renderChunk);
        this->m_renderChunk = nullptr;
    }

    if (this->m_detailDoodads) {
        DetailDoodad::ReleaseInstance(this->m_detailDoodads);
        this->m_detailDoodads = nullptr;
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
            CMap::ReleaseDoodadDef(static_cast<CMapDoodadDef*>(owner));
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

    for (auto link = this->m_groundedLinkList.Head(); link; ) {
        auto next = this->m_groundedLinkList.Next(link);
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
// Once per client: the render chunk state, then the vertex table and its cell scale
void CMapChunk::Initialize() {
    CMap::InitializeRenderChunks();
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
            if (CMapChunk::s_holeMask[(col >> 1) + (row >> 1) * 4] & this->m_header->holes) {
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

    uint32_t top = base + CMap::s_chunkBatch.m_maxIndex;
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

    if (CMap::s_shaderVertexMode) {
        int32_t cell[2] = { this->m_areaChunkX & ~1, this->m_areaChunkY & ~1 };
        auto area = static_cast<CMapArea*>(this->m_parentLinkList.Head()->ref);
        area->PairRenderChunks(cell);
        return;
    }

    this->CreateRenderChunk();
    this->m_renderChunkReady = 1;
}

// ref: FUN_007d66d0
// Whether two chunks can share a render chunk, which marks both as having had their render chunk
// decided either way. Classic maps need the same layers in the same order, none animated; on a
// shader-vertex map the union of both texture sets has to fit four layers and no layer may be
// animated or specular.
int32_t CMapChunk::CanPairWith(CMapChunk* other) {
    this->m_renderChunkReady = 1;
    other->m_renderChunkReady = 1;

    if (!(CMap::s_wdtHeader[0] & 0x4)) {
        uint32_t count = other->m_header->nLayers;

        if (this->m_header->nLayers != count) {
            return 0;
        }

        for (uint32_t i = 0; i < count; i++) {
            if (other->m_layers[i].flags & 0xc0) {
                return 0;
            }
            if (this->m_layers[i].flags & 0xc0) {
                return 0;
            }
            if (this->m_layers[i].textureId != other->m_layers[i].textureId) {
                return 0;
            }
        }

        return 1;
    }

    uint32_t count = this->m_header->nLayers;
    uint32_t total = count;

    if (other->m_header->nLayers == 0) {
        return count < 5;
    }

    for (uint32_t j = 0; j < other->m_header->nLayers; j++) {
        const SMLayer* layer = &other->m_layers[j];

        if (layer->flags & 0x4c0) {
            return 0;
        }

        bool found = false;

        for (uint32_t i = 0; i < count; i++) {
            if (this->m_layers[i].flags & 0x4c0) {
                return 0;
            }

            if (this->m_layers[i].textureId == layer->textureId) {
                found = true;
                break;
            }
        }

        if (!found) {
            total++;
        }
    }

    return total < 5;
}

// The width of this chunk's alpha and shadow maps in texels: 64, less the tile's MAMP shift
uint32_t CMapChunk::AlphaSize() const {
    auto area = static_cast<CMapArea*>(const_cast<CMapChunk*>(this)->m_parentLinkList.Head()->ref);
    return 0x40u >> (area->m_header->mampValue & 0x1f);
}

// ref: FUN_007c3e70
// Every update, for a chunk in view: how far along the view its nearest corner lies, and the
// distance row its vertex nearest the camera puts it in
void CMapChunk::UpdateSortDistance() {
    if (CWorldScene::BoxOutsideFrustum(this->m_bounds)) {
        return;
    }

    C3Vector point = { 0.0f, 0.0f, 0.0f };
    CWorldScene::BoxNearPoint(this->m_bounds, &point);

    const C4Plane& plane = CWorldScene::s_viewPlane;
    this->m_sortDistance = plane.n.x * point.x + plane.n.y * point.y + plane.n.z * point.z + plane.d;

    int32_t vertex = CWorldScene::s_quadrantVertex[CWorldScene::s_cameraQuadrant];
    C3Vector nearest = {
        CMapChunk::s_vertexTable[vertex][0] + this->m_position.x,
        CMapChunk::s_vertexTable[vertex][1] + this->m_position.y,
        this->m_heights[vertex] + this->m_position.z
    };
    CWorldScene::BucketChunk(this, nearest);
}

// ref: FUN_007c5b20
// Every update, for a chunk whose liquids are in view: each liquid in view goes into a distance
// row at the camera's height clamped to the liquid's range. The liquid bounds (FUN_007cde80)
// and the row insertion (FUN_00792df0) are not ported yet, so the walk stops at the chunk test.
void CMapChunk::UpdateLiquidVisibility() {
    if (this->m_liquidBounds.t.x < this->m_liquidBounds.b.x || CWorldScene::BoxOutsideFrustum(this->m_liquidBounds)) {
        return;
    }

    int32_t vertex = CWorldScene::s_quadrantVertex[CWorldScene::s_cameraQuadrant];
    C3Vector point = {
        CMapChunk::s_vertexTable[vertex][0] + this->m_position.x,
        CMapChunk::s_vertexTable[vertex][1] + this->m_position.y,
        this->m_heights[vertex] + this->m_position.z
    };

    for (auto liquid = this->m_liquidList.Head(); liquid; liquid = this->m_liquidList.Next(liquid)) {
        CAaBox box;
        liquid->GetBounds(&box);

        if (CWorldScene::BoxOutsideFrustum(box)) {
            continue;
        }

        // The point the layer is banded by is the chunk's nearest vertex, but at the height the
        // camera would meet the water at: the camera's own z, held inside the layer's range.
        // A layer far below the camera bands as if it were at its own surface, not at the
        // terrain's.
        float z = CWorldScene::s_cameraPos.z;

        if (liquid->m_maxHeight < z) {
            z = liquid->m_maxHeight;
        }

        point.z = liquid->m_minHeight <= z ? z : liquid->m_minHeight;

        CWorldScene::AddLiquid(liquid, point);
    }
}

// Where the cell's centre vertex sits relative to its first corner: half a cell in each
// direction. The reference keeps it as the fifth entry of the corner table. DAT_00adfc94
static const float CELL_MID_X = -2.0833332538604736f;
static const float CELL_MID_Y = -2.0833332538604736f;

// ref: FUN_007ad3b0
// A cell is four triangles meeting at its centre vertex, so the height comes from whichever of
// them the point is in: two cross products against the cell's diagonals pick it, and the plane
// through the centre and that triangle's two corners gives the height.
//
// The reference scales the plane by an approximate reciprocal square root before solving. Every
// term carries that factor and the division cancels it, so it makes no difference to the answer
// and is left out.
bool CMapChunk::HeightAt(const C3Vector& position, uint32_t col, uint32_t row, float* height) {
    col &= 7;
    row &= 7;

    if (CMapChunk::s_holeMask[(col >> 1) + (row >> 1) * 4] & this->m_header->holes) {
        return false;
    }

    float baseX = static_cast<float>(row) * -CELL_SIZE;
    float baseY = static_cast<float>(col) * -CELL_SIZE;

    float midX = baseX + CELL_MID_X;
    float midY = baseY + CELL_MID_Y;

    float px = position.x - this->m_position.x;
    float py = position.y - this->m_position.y;

    // Which side of each diagonal the point falls on. Corner 2 is the far corner and corner 0
    // the near one; corners 1 and 3 are the other pair.
    float d1 = ((baseY - CELL_SIZE) - py) * -CELL_SIZE - ((baseX - CELL_SIZE) - px) * -CELL_SIZE;
    float d2 = (baseY - py) * -CELL_SIZE - ((baseX - CELL_SIZE) - px) * CELL_SIZE;

    uint32_t tri = d1 <= 0.0f ? 1 : 0;

    if (d2 <= 0.0f) {
        tri += 2;
    }

    const float* heights = this->m_heights + (row * 17 + col);

    float ax = baseX + DetailDoodad::CELL_CORNER[DetailDoodad::TRI_CORNER[tri][0]][0];
    float ay = baseY + DetailDoodad::CELL_CORNER[DetailDoodad::TRI_CORNER[tri][0]][1];
    float bx = baseX + DetailDoodad::CELL_CORNER[DetailDoodad::TRI_CORNER[tri][1]][0];
    float by = baseY + DetailDoodad::CELL_CORNER[DetailDoodad::TRI_CORNER[tri][1]][1];

    float ha = heights[DetailDoodad::TRI_VERTEX[tri][0]];
    float hb = heights[DetailDoodad::TRI_VERTEX[tri][1]];
    float hm = heights[9];

    float nx = (hb - hm) * (ay - midY) - (ha - hm) * (by - midY);
    float ny = (ha - hm) * (bx - midX) - (hb - hm) * (ax - midX);
    float nz = (by - midY) * (ax - midX) - (bx - midX) * (ay - midY);

    if (nz == 0.0f) {
        return false;
    }

    float d = -(midX * nx + midY * ny + nz * hm);

    *height = -((nx * px + ny * py + d) / nz) + this->m_position.z;

    return true;
}

// ref: FUN_007d05f0
// A chunk scatters grass only once every kind its layers call for is loaded, or it would come up
// in pieces as the models arrived. Each layer names one ground effect, and each of those names up
// to four kinds.
//
// Nothing calls this yet: the reference asks from inside the scatter builder, and asking is what
// starts the loads, so calling it earlier would only pull in grass models that nothing draws.
bool CMapChunk::DetailDoodadsReady() {
    if (!this->m_header || !this->m_layers) {
        return true;
    }

    bool ready = true;

    for (uint32_t i = 0; i < this->m_header->nLayers; i++) {
        auto effect = g_groundEffectTextureDB.GetRecord(this->m_layers[i].effectId);

        if (!effect) {
            continue;
        }

        for (uint32_t slot = 0; slot < 4; slot++) {
            if (effect->m_doodadID[slot] && !DetailDoodad::IsReady(effect->m_doodadID[slot])) {
                ready = false;
            }
        }
    }

    return ready;
}

// ref: FUN_007d3fe0
// Readies a visible chunk for the frame: its render chunk exists, is marked for the half-size
// alpha beyond 777 yards or whenever the shadowLevel setting is on, and is built. Then, with detail
// doodads on and the chunk within 70 yards, its detail doodads are created and queued; that
// system (FUN_007d3390, FUN_00792fa0) is not ported yet.
//
// What is known about the builder so far: it gates on the chunk having layers and on
// DetailDoodadsReady, seeds a CRndSeed from the chunk's own indices (indexX << 16 | indexY, so
// the scatter is the same every time the chunk loads), takes an instance from the
// WDETAILDOODADINST heap into m_detailDoodads, then picks cells of the eight-by-eight grid at
// random -- one bit a cell so none is used twice -- and for each builds a plane from the
// chunk's heights with colour and normal deltas from MCCV and MCNR. Holes and the low-quality
// texture map decide which ground effect a cell takes. Its eight constant tables are established
// and sit in DetailDoodad.hpp, read out of the reference's data section rather than inferred,
// and the branch it takes is the only one -- the flag it tests is written once at startup, to
// zero, and never again. What is left is transcribing the arithmetic, not research.
void CMapChunk::PrepareRender() {
    if (!this->m_renderChunk) {
        this->EnsureRenderChunk();
    }

    if (this->m_renderChunk) {
        const C3Vector& cameraPos = CWorld::GetCameraPos();
        float dx = cameraPos.x - this->m_center.x;
        float dy = cameraPos.y - this->m_center.y;
        float dz = cameraPos.z - this->m_center.z;

        if (CWorld::s_terrainShadowLevel || 603729.0f < dy * dy + dz * dz + dx * dx) {
            this->m_renderChunk->m_flags10 |= 0x10;
        }

        this->m_renderChunk->Build();
    }

    if ((CWorld::s_enables & CWorld::Enables::Enable_DetailDoodads) &&
        this->m_sortDistance < DetailDoodad::s_fadeDistance) {
        // Scattered once, the first time the chunk comes near enough, and kept. Asking is also
        // what starts the grass models loading, so a chunk that comes up empty is asked again
        // next frame rather than being marked as having none.
        if (!this->m_detailDoodads) {
            this->m_detailDoodads = DetailDoodad::CreateInstance(this);
        }

        if (this->m_detailDoodads) {
            CWorldScene::AddDetailDoodads(this->m_detailDoodads);
        }
    }
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

// An MCLQ layer is a fixed size whichever kind it is: two heights, a nine-by-nine grid of
// eight-byte vertices, the eight-by-eight wet-tile mask, then the flow records.
static const uint32_t MCLQ_VERTEX_OFFSET = 8;
static const uint32_t MCLQ_MASK_OFFSET = 656;
static const uint32_t MCLQ_LAYER_SIZE = 804;

// ref: FUN_007c5690
// Every liquid layer the chunk should have, rebuilt from scratch: the old ones go, then either
// the MCLQ layers the chunk's own flags name or the MH2O layers the tile holds for it. A tile
// carries one format or the other, never both, so at most one of the two halves does anything.
//
// MCLQ names its four kinds by position -- river, ocean, magma, slime, in flag order -- and each
// is one LiquidType id above the last. MH2O names the type outright, so nothing is inferred.
void CMapChunk::CreateLiquid() {
    auto liquid = this->m_liquidList.Head();

    while (liquid) {
        // The reference reads the next link after unlinking this one, which zeroes it, so its
        // teardown stops after the first layer and any others stay linked into storage it has
        // handed back. Frozen reads the link first. Recorded as a divergence.
        auto next = this->m_liquidList.Next(liquid);

        liquid->m_chunkLink.Unlink();
        liquid->ReleaseSurface();
        CMap::FreeChunkLiquid(liquid);

        liquid = next;
    }

    // MCLQ: one layer per set flag, in flag order, each following the last.
    auto mclq = this->m_liquidData;

    if (mclq) {
        for (uint32_t layer = 0; layer < 4; layer++) {
            if (!(this->m_header->flags & (4 << layer))) {
                continue;
            }

            uint32_t liquidType = layer + 1;

            // Outland's magma is not the ordinary one.
            if (CMap::s_mapID == 0x212 && layer == 2) {
                liquidType = 0xf;
            }

            auto typeRec = g_liquidTypeDB.GetRecord(liquidType);
            auto materialRec = typeRec ? g_liquidMaterialDB.GetRecord(typeRec->m_materialID) : nullptr;
            auto chunkLiquid = CMap::AllocChunkLiquid();

            if (!materialRec || !chunkLiquid) {
                continue;
            }

            this->m_liquidList.LinkToTail(chunkLiquid);

            chunkLiquid->m_liquidType = liquidType;
            chunkLiquid->m_chunk = this;
            chunkLiquid->m_vertexFormat = materialRec->m_LVF;
            chunkLiquid->m_origin = this->m_position;
            chunkLiquid->m_minHeight = reinterpret_cast<const float*>(mclq)[0];
            chunkLiquid->m_maxHeight = reinterpret_cast<const float*>(mclq)[1];
            chunkLiquid->m_tileX = 0;
            chunkLiquid->m_tileY = 0;
            chunkLiquid->m_tileEndX = 8;
            chunkLiquid->m_tileEndY = 8;

            auto mem = SMemAlloc(sizeof(Liquid::CVertexDataMCLQ), __FILE__, __LINE__, 0x0);

            chunkLiquid->m_vertexData = mem
                ? new (mem) Liquid::CVertexDataMCLQ(mclq + MCLQ_VERTEX_OFFSET, chunkLiquid->m_vertexFormat)
                : nullptr;

            chunkLiquid->m_tileMask = mclq + MCLQ_MASK_OFFSET;

            mclq += MCLQ_LAYER_SIZE;

            chunkLiquid->BuildVertices();
        }
    }

    // MH2O: whatever the tile holds for this chunk, however many layers that is.
    auto area = static_cast<CMapArea*>(this->m_parentLinkList.Head()->ref);

    if (area && area->m_liquid && area->m_liquid->HasLiquid()) {
        auto entry = area->m_liquid->Chunk(this->m_areaChunkX, this->m_areaChunkY);

        // TODO BitArray::Assign on the chunk's two eight-by-eight grids, from the tile's deep
        // and fishable masks: where you can swim and where you can fish are not kept yet.

        for (uint32_t layer = 0; layer < entry->layerCount; layer++) {
            auto instance = area->m_liquid->Layer(entry, layer);
            auto chunkLiquid = CMap::AllocChunkLiquid();

            if (!chunkLiquid) {
                continue;
            }

            this->m_liquidList.LinkToTail(chunkLiquid);

            chunkLiquid->m_chunk = this;
            chunkLiquid->m_liquidType = instance->liquidType;
            chunkLiquid->m_vertexFormat = instance->material;
            chunkLiquid->m_origin = this->m_position;
            chunkLiquid->m_minHeight = instance->minHeight;
            chunkLiquid->m_maxHeight = instance->maxHeight;
            chunkLiquid->m_tileX = instance->tileY;
            chunkLiquid->m_tileY = instance->tileX;
            chunkLiquid->m_tileEndX = instance->tileHeight + instance->tileY;
            chunkLiquid->m_tileEndY = instance->tileWidth + instance->tileX;

            auto mem = SMemAlloc(sizeof(Liquid::CVertexDataMH2O), __FILE__, __LINE__, 0x0);

            chunkLiquid->m_vertexData = mem
                ? new (mem) Liquid::CVertexDataMH2O(area->m_liquid, instance)
                : nullptr;

            chunkLiquid->m_exists.Assign(
                const_cast<uint8_t*>(area->m_liquid->ExistsMask(instance)),
                instance->tileHeight * instance->tileWidth, false);

            chunkLiquid->BuildVertices();
        }
    }

    // The chunk's liquid box, or an inverted one when it ended up with no layers at all.
    if (this->m_liquidList.Head()) {
        this->GetBounds(&this->m_liquidBounds);
        return;
    }

    this->m_liquidBounds.b.x = FLT_MAX;
    this->m_liquidBounds.b.y = FLT_MAX;
    this->m_liquidBounds.b.z = FLT_MAX;
    this->m_liquidBounds.t.x = -FLT_MAX;
    this->m_liquidBounds.t.y = -FLT_MAX;
    this->m_liquidBounds.t.z = -FLT_MAX;
}

// ref: FUN_007cfb10
// A chunk hides what is behind it along its silhouette, which from any one camera is its far
// row and its far column. Which of the two edges is the far one depends on which way the
// camera is looking, so the row starts at the last of the nine rows when the view runs west,
// and the column at the last of the nine when it runs south.
void CMapChunk::FeedHorizon() {
    if (!this->m_heights || !this->m_header) {
        return;
    }

    int32_t indices[9];

    int32_t rowBase = CWorldScene::s_cameraTarget.x < CWorldScene::s_cameraPos.x ? 0x88 : 0;

    for (int32_t i = 0; i < 9; i++) {
        indices[i] = rowBase + i;
    }

    CWorldScene::ShadeHorizon(CMapChunk::s_vertexTable, this->m_heights, indices, 9,
                              this->m_position, this->m_header->holes);

    int32_t columnBase = CWorldScene::s_cameraTarget.y < CWorldScene::s_cameraPos.y ? 8 : 0;

    for (int32_t i = 0; i < 9; i++) {
        indices[i] = columnBase + i * 0x11;
    }

    CWorldScene::ShadeHorizon(CMapChunk::s_vertexTable, this->m_heights, indices, 9,
                              this->m_position, this->m_header->holes);
}

// The four triangles of a cell, as the pair of corner offsets each forms with the centre vertex
// (DAT_00a3fb30): the centre is the cell's base + 9.
static const int32_t s_cellTriangles[4][2] = { { 17, 0 }, { 0, 1 }, { 18, 17 }, { 1, 18 } };

// ref: FUN_007d8730
// The heights go into the shared vertex table's z before each test, the way the reference does it.
bool CMapChunk::IntersectCell(uint32_t cellX, uint32_t cellY, const C3Ray& ray, float* t) {
    if (CMapChunk::s_holeMask[(cellX >> 1) + (cellY >> 1) * 4] & this->m_header->holes) {
        return false;
    }

    bool hit = false;
    int32_t base = static_cast<int32_t>(cellY) * 17 + static_cast<int32_t>(cellX);

    for (auto& pair : s_cellTriangles) {
        int32_t tri[3] = { base + 9, base + pair[1], base + pair[0] };

        for (int32_t index : tri) {
            CMapChunk::s_vertexTable[index][2] = this->m_heights[index];
        }

        float hitT = 0.0f;

        if (IntersectRayTriangle(ray, reinterpret_cast<const C3Vector*>(CMapChunk::s_vertexTable), tri, &hitT, nullptr, 0.01f)) {
            hit = true;

            if (hitT < *t && 0.0f < hitT) {
                *t = hitT;
            }
        }
    }

    return hit;
}

// ref: FUN_007d8e00
// Each cell's five vertices (its corners and centre) take their heights into the shared vertex
// table and are classified against the frustum; each of the cell's four triangles that the
// frustum does not wholly reject becomes a facet, moved out to world space by the chunk origin.
void CMapChunk::GatherFacets(const CiRect& cells, const CWFrustum& frustum, CFacetList& list) {
    static const int32_t s_cellPoints[5] = { 0, 9, 17, 1, 18 };                                  // DAT_00a40618
    static const int32_t s_cellFacets[4][3] = { { 17, 9, 0 }, { 9, 1, 0 }, { 9, 17, 18 }, { 9, 18, 1 } }; // DAT_00a405e8

    auto table = reinterpret_cast<C3Vector*>(CMapChunk::s_vertexTable);

    for (int32_t row = cells.minY; row <= cells.maxY; row++) {
        for (int32_t col = cells.minX; col <= cells.maxX; col++) {
            if (CMapChunk::s_holeMask[(col >> 1) + (row >> 1) * 4] & this->m_header->holes) {
                continue;
            }

            int32_t base = row * 17 + col;
            uint8_t outcodes[20];

            for (int32_t point : s_cellPoints) {
                table[base + point].z = this->m_heights[base + point];
                ClassifyPointPlanes6(frustum.planes, table[base + point], &outcodes[point]);
            }

            for (auto& tri : s_cellFacets) {
                if (outcodes[tri[0]] & outcodes[tri[1]] & outcodes[tri[2]]) {
                    continue;
                }

                M2CollisionTriangle* facet = list.facets.New();

                facet->plane.n = { 0.0f, 0.0f, 1.0f };
                facet->plane.d = 0.0f;

                for (int32_t k = 0; k < 3; k++) {
                    const C3Vector& local = table[base + tri[k]];
                    facet->vertices[k] = {
                        this->m_position.x + local.x,
                        local.y + this->m_position.y,
                        local.z + this->m_position.z,
                    };
                }

                const C3Vector& p0 = facet->vertices[0];
                const C3Vector& p1 = facet->vertices[1];
                const C3Vector& p2 = facet->vertices[2];

                if (!CMap::s_useSse) {
                    PlaneFromPoints(&facet->plane, p0, p1, p2);
                } else {
                    float nx = (p1.y - p0.y) * (p2.z - p0.z) - (p1.z - p0.z) * (p2.y - p0.y);
                    float ny = (p1.z - p0.z) * (p2.x - p0.x) - (p2.z - p0.z) * (p1.x - p0.x);
                    float nz = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
                    float inv = FacetRsqrt(nx * nx + ny * ny + nz * nz);

                    facet->plane.n = { nx * inv, ny * inv, nz * inv };
                    facet->plane.d = -(facet->plane.n.x * p0.x + facet->plane.n.z * p0.z + p0.y * facet->plane.n.y);
                }
            }
        }
    }
}

// ref: FUN_007c55d0
bool CMapChunk::GetLiquidHeight(const float* cells, float* height) {
    int32_t whole0 = static_cast<int32_t>(std::nearbyint(cells[0] - 0.5f));
    int32_t whole1 = static_cast<int32_t>(std::nearbyint(cells[1] - 0.5f));

    float frac[2] = { cells[0] - static_cast<float>(whole0), cells[1] - static_cast<float>(whole1) };
    uint32_t tile[2] = { static_cast<uint32_t>(whole0) & 7, static_cast<uint32_t>(whole1) & 7 };

    for (auto liquid = this->m_liquidList.Head(); liquid; liquid = this->m_liquidList.Next(liquid)) {
        if (liquid->CoversTile(tile[0], tile[1]) && liquid->GetHeightAt(frac, tile, height)) {
            return true;
        }
    }

    return false;
}
