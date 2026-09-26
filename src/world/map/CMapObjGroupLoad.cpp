#include "world/map/CMapObjGroup.hpp"

#include <cstdio>
#include <cstdlib>
#include "world/map/CMap.hpp"
#include "world/map/CMapObj.hpp"
#include "db/Db.hpp"
#include "async/AsyncFileRead.hpp"
#include "async/CAsyncObject.hpp"
#include "gx/shader/CShaderEffect.hpp"
#include "util/SFile.hpp"
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <cstring>
#include <cmath>

// Loading one WMO group file (reference MapObjRead.cpp and the MapArea.cpp tail). A root's groups
// live in <root>_000.wmo, _001.wmo and so on; each is read on its own and parsed into the group
// object the root made for it.

// The step between two liquid grid vertices (DAT_00a403a0)
static const float LIQUID_TILE_SIZE = 4.16666651f;

// One IFF chunk of a group file: its body, and the cursor moved past it
struct GroupChunk {
    const uint8_t* body;
    uint32_t size;
};

static GroupChunk NextChunk(const uint8_t*& cursor) {
    uint32_t size = *reinterpret_cast<const uint32_t*>(cursor + 4);
    GroupChunk chunk = { cursor + 8, size };
    cursor = cursor + 8 + size;
    return chunk;
}

// ref: FUN_007d85e0
// Reads one group file. The path is the root's with its extension replaced by _NNN.wmo. A sync
// read parses before returning, which is what the map's own global WMO takes; everything else
// goes through the async queue and finishes in ReadCallback.
void CMapObjGroup::Read(CMapObj* mapObj, uint32_t groupIndex, int32_t sync) {
    auto group = mapObj->m_groups[groupIndex];

    char path[260];
    SStrCopy(path, mapObj->m_name, sizeof(path));

    char* ext = strrchr(path, '.');
    SStrPrintf(ext, 0x100, "_%03d", groupIndex);
    SStrPack(path, ".wmo", sizeof(path));

    SFile* file = nullptr;
    CMap::SafeOpen(path, &file);

    group->m_fileSize = SFile::GetFileSize(file, nullptr);
    group->m_fileBuffer = static_cast<uint8_t*>(SMemAlloc(group->m_fileSize, __FILE__, __LINE__, 0x0));
    group->m_mapObj = mapObj;

    if (sync) {
        CMap::SafeRead(path, file, group->m_fileBuffer, group->m_fileSize);
        group->ReadComplete();
        SFile::Close(file);
        return;
    }

    auto async = AsyncFileReadAllocObject();
    async->file = file;
    async->buffer = group->m_fileBuffer;
    async->size = group->m_fileSize;
    async->userArg = group;
    async->userPostloadCallback = &CMapObjGroup::ReadCallback;
    group->m_asyncObject = async;

    AsyncFileReadObject(async, 0);

    CMap::s_mapObjGroupLoadList.LinkToTail(group);
}

// ref: FUN_007d8570
void CMapObjGroup::ReadCallback(void* arg) {
    auto group = static_cast<CMapObjGroup*>(arg);

    AsyncFileReadDestroyObject(group->m_asyncObject);
    group->m_asyncObject = nullptr;
    group->m_link.Unlink();

    group->ReadComplete();
}

// ref: FUN_007d7310
// The LiquidType row a group's MOGP liquid field means. Values 1..20 fall into four families by
// their low two bits: water (13, or 14 when the group is flagged as ocean), ocean (14), magma
// (19) and slime (20). Anything else is already a row id.
uint32_t CMapObjGroup::ResolveLiquidType(uint32_t flags, uint32_t liquid) {
    if (liquid < 0x15 && liquid != 0) {
        switch ((liquid - 1) & 3) {
        case 0:
            return ((flags & 0x80000) != 0) + 0xd;
        case 1:
            return 0xe;
        case 2:
            return 0x13;
        case 3:
            return 0x14;
        }
    }

    return liquid;
}

// ref: FUN_007d82e0
// The group's file has arrived: join the root's loaded list, take the MOGP header, resolve the
// liquid type, walk the sub-chunks, load every material's textures, and work out the three state
// bits the render pass sorts by.
void CMapObjGroup::ReadComplete() {
    auto mogp = this->m_fileBuffer + 0x14;
    auto mapObj = this->m_mapObj;

    mapObj->m_loadedGroups.LinkToTail(this);

    this->m_name = mapObj->m_mogn + *reinterpret_cast<const int32_t*>(mogp);

    uint32_t flags = *reinterpret_cast<const uint32_t*>(mogp + 0x8);
    this->m_flags = flags;

    // A root with no skybox cannot have a group that draws one
    if (!mapObj->m_mosb) {
        this->m_flags = flags & ~0x40000u;
    }

    this->m_mogpBounds = *reinterpret_cast<const CAaBox*>(mogp + 0xc);
    this->m_portalStart = *reinterpret_cast<const uint16_t*>(mogp + 0x24);
    this->m_portalCount = *reinterpret_cast<const uint16_t*>(mogp + 0x26);
    this->m_batchCountA = *reinterpret_cast<const uint16_t*>(mogp + 0x28);
    this->m_batchCountB = *reinterpret_cast<const uint16_t*>(mogp + 0x2a);
    this->m_batchCountC = *reinterpret_cast<const uint16_t*>(mogp + 0x2c);
    this->m_fogIds = *reinterpret_cast<const uint32_t*>(mogp + 0x30);
    this->m_groupID = *reinterpret_cast<const uint32_t*>(mogp + 0x38);

    uint32_t liquid = *reinterpret_cast<const uint32_t*>(mogp + 0x34);

    if (!(mapObj->m_mohd->flags & 0x4)) {
        liquid = liquid == 0xf ? 0 : liquid + 1;
    }

    this->m_liquidType = CMapObjGroup::ResolveLiquidType(this->m_flags, liquid);

    // The sub-chunks start right after the 0x44-byte MOGP header
    this->ParseChunks(mogp + 0x44);

    for (uint32_t i = 0; i < this->m_batchCount; i++) {
        this->LoadMaterialTextures(this->m_batches[i].materialId);
    }

    uint32_t state = this->m_state & ~0x2u;
    this->m_state = state | 0x1;

    if (mapObj->m_mohd->flags & 0x1) {
        this->m_state = state | 0x3;
    }

    if (!SStrCmp(this->m_name, "antiportal", STORM_MAX_STR)) {
        this->BuildAntiPortals();
    }

    this->m_state |= 0x4;

    if (!this->m_batchCount) {
        return;
    }

    this->m_minIndex = 0xFFFFFFFF;
    this->m_maxIndex = 0;
    this->m_minVertex = 0xFFFF;
    this->m_maxVertex = 0;

    // Bit 2 survives only while every batch draws opaque; the index and vertex spans cover the
    // batches walked up to that point, as the reference leaves them
    for (uint32_t i = 0; i < this->m_batchCount; i++) {
        auto batch = &this->m_batches[i];

        if (mapObj->m_materials[batch->materialId].blendMode) {
            this->m_state &= ~0x4u;
            break;
        }

        uint32_t last = batch->startIndex + batch->count - 1;

        if (this->m_maxVertex < batch->maxVertex) {
            this->m_maxVertex = batch->maxVertex;
        }
        if (batch->minVertex < this->m_minVertex) {
            this->m_minVertex = batch->minVertex;
        }
        if (batch->startIndex < this->m_minIndex) {
            this->m_minIndex = batch->startIndex;
        }
        if (this->m_maxIndex < last) {
            this->m_maxIndex = last;
        }
    }

    for (uint32_t i = 0; i < this->m_batchCount; i++) {
        if (mapObj->m_materials[this->m_batches[i].materialId].shader == 6) {
            this->m_state |= 0x8;
            return;
        }
    }
}

// ref: FUN_007d7f50
// The six sub-chunks every group carries, in file order and without reading a chunk id: MOPY,
// MOVI, MOVT, MONR, MOTV, MOBA. What follows them depends on the group's flags.
void CMapObjGroup::ParseChunks(const uint8_t* cursor) {
    auto mopy = NextChunk(cursor);
    this->m_polys = reinterpret_cast<SMOPoly*>(const_cast<uint8_t*>(mopy.body));
    this->m_faceCount = mopy.size >> 1;

    auto movi = NextChunk(cursor);
    this->m_indices = reinterpret_cast<const uint16_t*>(movi.body);
    this->m_indexCount = movi.size >> 1;

    auto movt = NextChunk(cursor);
    this->m_vertices = reinterpret_cast<const C3Vector*>(movt.body);
    this->m_vertexCount = movt.size / sizeof(C3Vector);

    auto monr = NextChunk(cursor);
    this->m_normals = reinterpret_cast<const C3Vector*>(monr.body);
    this->m_normalCount = monr.size / sizeof(C3Vector);

    auto motv = NextChunk(cursor);
    this->m_texCoords = reinterpret_cast<const C2Vector*>(motv.body);
    this->m_texCoordCount = motv.size >> 3;

    auto moba = NextChunk(cursor);
    this->m_batches = reinterpret_cast<SMOBatch*>(const_cast<uint8_t*>(moba.body));
    this->m_batchCount = moba.size / sizeof(SMOBatch);

    this->ParseOptionalChunks(cursor);
}

// ref: FUN_007d7c30
// Everything past MOBA, each present only when the group's flags say so, in the order the format
// writes them.
void CMapObjGroup::ParseOptionalChunks(const uint8_t* cursor) {
    uint32_t flags = this->m_flags;

    if (flags & 0x200) {
        auto molr = NextChunk(cursor);
        this->m_lightRefs = reinterpret_cast<const uint16_t*>(molr.body);
        this->m_lightRefCount = molr.size >> 1;
    }

    if (flags & 0x800) {
        auto modr = NextChunk(cursor);
        this->m_doodadRefs = reinterpret_cast<const uint16_t*>(modr.body);
        this->m_doodadRefCount = modr.size >> 1;
    }

    if (flags & 0x1) {
        auto mobn = NextChunk(cursor);
        auto mobr = NextChunk(cursor);
        this->SetBsp(
            reinterpret_cast<CAaBspNode*>(const_cast<uint8_t*>(mobn.body)),
            mobn.size >> 4,
            reinterpret_cast<uint16_t*>(const_cast<uint8_t*>(mobr.body)),
            mobr.size >> 1,
            this->m_mogpBounds
        );
    }

    // The four unlit-batch chunks (MPBV, MPBP, MPBI, MPBG) are skipped whole
    if (this->m_flags & 0x400) {
        NextChunk(cursor);
        NextChunk(cursor);
        NextChunk(cursor);
        NextChunk(cursor);
    }

    if (this->m_flags & 0x4) {
        auto mocv = NextChunk(cursor);
        this->m_colors = reinterpret_cast<CImVector*>(const_cast<uint8_t*>(mocv.body));
        this->m_colorCount = mocv.size >> 2;

        if (!(this->m_mapObj->m_mohd->flags & 0x8)) {
            this->FixVertexColors();
        }

    }

    if (this->m_flags & 0x1000) {
        auto mliq = cursor + 8;
        this->m_liquidXVerts = *reinterpret_cast<const uint32_t*>(mliq);
        this->m_liquidYVerts = *reinterpret_cast<const uint32_t*>(mliq + 0x4);
        this->m_liquidXTiles = *reinterpret_cast<const uint32_t*>(mliq + 0x8);
        this->m_liquidYTiles = *reinterpret_cast<const uint32_t*>(mliq + 0xc);
        this->m_liquidPos = *reinterpret_cast<const C3Vector*>(mliq + 0x10);
        uint16_t material = *reinterpret_cast<const uint16_t*>(mliq + 0x1c);

        this->m_liquidVerts = mliq + 0x1e;
        cursor = mliq + 0x1e + this->m_liquidYVerts * this->m_liquidXVerts * 8;
        this->m_liquidTiles = cursor;
        cursor = cursor + this->m_liquidYTiles * this->m_liquidXTiles;
        this->m_liquidMaterial = material;

        // A group whose MOGP said nothing takes the type of its first liquid tile instead
        if (!this->m_liquidType) {
            this->m_liquidType = CMapObjGroup::ResolveLiquidType(this->m_flags, this->FirstLiquidTileType());
        }

        this->m_liquidVertices.SetCount(this->m_liquidYVerts * this->m_liquidXVerts);
        this->BuildLiquidVertices();
    }

    if (this->m_flags & 0x20000) {
        auto mori = NextChunk(cursor);
        this->m_triangleStrips = reinterpret_cast<const uint16_t*>(mori.body);
        this->m_triangleStripCount = mori.size >> 1;

        // MORB follows MORI without a gap; the reference steps past MORI's header twice
        cursor = cursor + 8;
        this->m_morb = cursor;

        // The reference can swap every batch's index range for MORB's, behind a flag its map
        // load clears and never sets again (DAT_00ce049c), so the swap never runs
    }

    if (this->m_flags & 0x2000000) {
        auto motv2 = NextChunk(cursor);
        this->m_texCoords2 = reinterpret_cast<const C2Vector*>(motv2.body);
        this->m_texCoord2Count = motv2.size >> 3;
    }

    if (this->m_flags & 0x1000000) {
        auto mocv2 = NextChunk(cursor);
        this->m_colors2 = reinterpret_cast<const CImVector*>(mocv2.body);
        this->m_color2Count = mocv2.size >> 2;
    }
}

// ref: FUN_0079adc0
// The BSP over the group's faces: MOBN's nodes, MOBR's face numbers, and the box they cover.
void CMapObjGroup::SetBsp(CAaBspNode* nodes, uint32_t nodeCount, uint16_t* faceRefs, uint32_t faceRefCount, const CAaBox& bounds) {
    this->m_bspNodes = nodes;
    this->m_bspFaceRefs = faceRefs;
    this->m_bspNodeCount = nodeCount;
    this->m_bspFaceRefCount = faceRefCount;
    this->m_bounds = bounds;
}

// ref: FUN_007d7380
// MOCV holds two kinds of colour in one array: the vertices the interior batches use are stored
// at double brightness, and the rest carry their own shade in the alpha. The first run is halved,
// the rest are scaled by their alpha, halved, clamped and given a solid alpha.
void CMapObjGroup::FixVertexColors() {
    uint32_t interiorVertices = 0;

    if (this->m_batchCountA) {
        interiorVertices = this->m_batches[this->m_batchCountA - 1].maxVertex + 1;
    }

    for (uint32_t i = 0; i < this->m_colorCount; i++) {
        auto color = &this->m_colors[i];

        if (i < interiorVertices) {
            color->r >>= 1;
            color->g >>= 1;
            color->b >>= 1;
            continue;
        }

        uint32_t a = color->a;
        uint32_t r = ((color->r * a >> 6) + color->r) >> 1;
        uint32_t g = ((color->g * a >> 6) + color->g) >> 1;
        uint32_t b = ((color->b * a >> 6) + color->b) >> 1;

        color->r = static_cast<uint8_t>(r < 0x100 ? r : 0xFF);
        color->g = static_cast<uint8_t>(g < 0x100 ? g : 0xFF);
        color->b = static_cast<uint8_t>(b < 0x100 ? b : 0xFF);
        color->a = 0xFF;
    }
}

// ref: FUN_007d7710
// One material's textures, created once and kept on the root. An empty first name draws the
// green placeholder; the second is only loaded for the shaders that take one, and only while
// the device can shade at all.
void CMapObjGroup::LoadMaterialTextures(uint32_t materialId) {
    auto material = &this->m_mapObj->m_materials[materialId];
    auto textures = &this->m_mapObj->m_materialTextures[materialId];

    if (textures->texture1) {
        return;
    }

    auto name1 = this->m_mapObj->m_motx + material->texture1;
    auto name2 = this->m_mapObj->m_motx + material->texture2;

    if (!*name1) {
        name1 = "createcrappygreentexture.blp";
    }

    // The reference reads the flag the model shader system was started with
    // (DAT_00d43020, M2GetCacheFlags bit 3): with no shaders there is no second
    // texture to give them
    if (!CShaderEffect::s_enableShaders) {
        name2 = "";
    }

    switch (material->shader) {
    case 3:
    case 5:
    case 6:
        if (*name2) {
            break;
        }
        material->shader = 4;
        // fall through
    case 0:
    case 1:
    case 2:
    case 4:
        name2 = nullptr;
        break;
    default:
        break;
    }

    textures->texture1 = CMap::LoadTexture(name1);

    if (!name2) {
        textures->texture2 = nullptr;
        return;
    }

    textures->texture2 = CMap::LoadTexture(name2);
}

// ref: FUN_007c8d80
// The type of the group's first liquid tile, as the low nibble of its flag byte plus one; 0xf
// means the tile has none, and a grid of nothing but those answers zero.
// ref: FUN_007c8360
// Is `localPos` under this group's liquid, and if so which kind and at what height.
//
// A group whose MLIQ carries no tile grid at all answers yes at FLT_MAX: the reference treats it as
// entirely filled, which is how a sealed water volume with no per-tile mask behaves.
//
// The vertical test is `pos.z < height + bias`, so a point exactly at the surface counts as under
// it. The bias is 0.01 and applies only to liquids whose LiquidType row has flag 0x4.
bool CMapObjGroup::GetLiquidAt(const C3Vector& localPos, uint32_t* outType, float* outHeight) {
    // 1 / (CHUNK_SIZE / 8), the liquid tile step, as DAT_00aeee54 holds it.
    static const float INV_TILE_STEP = 0.23999999463558197f;

    // DAT_009f1968.
    static const float SURFACE_BIAS = 0.00999999977648258f;

    if (!this->m_liquidType) {
        return false;
    }

    if (!this->m_liquidXTiles || !this->m_liquidYTiles) {
        // No tile mask: the whole group is this liquid, unbounded above.
        *outType = this->m_liquidType;
        *outHeight = 3.4028234663852886e+38f;

        return true;
    }

    float fx = (localPos.x - this->m_liquidPos.x) * INV_TILE_STEP;
    float fy = (localPos.y - this->m_liquidPos.y) * INV_TILE_STEP;

    int32_t tileX = static_cast<int32_t>(floorf(fx));
    int32_t tileY = static_cast<int32_t>(floorf(fy));

    if (tileX < 0 || tileY < 0) {
        return false;
    }

    if (static_cast<uint32_t>(tileX) >= this->m_liquidXTiles
        || static_cast<uint32_t>(tileY) >= this->m_liquidYTiles) {
        return false;
    }

    // 0xf in the low nibble is the reference's "no liquid here" tile, the same sentinel
    // LiquidTileCount skips.
    if (!this->m_liquidTiles
        || (this->m_liquidTiles[this->m_liquidXTiles * tileY + tileX] & 0xf) == 0xf) {
        return false;
    }

    if (!this->m_liquidVerts) {
        return false;
    }

    // Bilinear over the four grid heights around the point. The entries are eight bytes each with
    // the height at +4, so the row stride is the VERTEX count, one more than the tile count.
    uint32_t row0 = this->m_liquidXVerts * static_cast<uint32_t>(tileY) + static_cast<uint32_t>(tileX);
    uint32_t row1 = row0 + this->m_liquidXVerts;

    auto heightAt = [this](uint32_t index) {
        return *reinterpret_cast<const float*>(this->m_liquidVerts + index * 8 + 4);
    };

    float tx = fx - static_cast<float>(tileX);
    float ty = fy - static_cast<float>(tileY);

    float h00 = heightAt(row0);
    float a = h00 + (heightAt(row0 + 1) - h00) * tx;

    float h10 = heightAt(row1);
    float b = h10 + (heightAt(row1 + 1) - h10) * tx;

    float height = a + (b - a) * ty;

    float bias = 0.0f;
    auto rec = g_liquidTypeDB.GetRecord(static_cast<int32_t>(this->m_liquidType));

    if (rec && (rec->m_flags & 0x4)) {
        bias = SURFACE_BIAS;
    }

    if (localPos.z >= height + bias) {
        return false;
    }

    *outHeight = height;
    *outType = this->m_liquidType;

    return true;
}

uint8_t CMapObjGroup::FirstLiquidTileType() const {
    int32_t count = this->m_liquidYTiles * this->m_liquidXTiles;

    for (int32_t i = 0; i < count; i++) {
        if ((this->m_liquidTiles[i] & 0xf) != 0xf) {
            return static_cast<uint8_t>((this->m_liquidTiles[i] & 0xf) + 1);
        }
    }

    return 0;
}

// ref: FUN_007c8c60
// The liquid grid's vertex positions: a regular lattice from the grid's corner, with each
// vertex's height read out of MLIQ, and the height range it spans.
void CMapObjGroup::BuildLiquidVertices() {
    if (!this->m_liquidVertices.Count()) {
        return;
    }

    float x = this->m_liquidPos.x;
    float y = this->m_liquidPos.y;

    auto dst = &this->m_liquidVertices[0];
    auto src = this->m_liquidVerts;

    this->m_liquidMinZ = *reinterpret_cast<const float*>(src + 4);
    this->m_liquidMaxZ = this->m_liquidMinZ;

    for (uint32_t row = 0; row < this->m_liquidYVerts; row++) {
        for (uint32_t col = 0; col < this->m_liquidXVerts; col++) {
            float z = *reinterpret_cast<const float*>(src + 4);

            dst->x = x;
            dst->y = y;
            dst->z = z;
            dst++;

            if (z < this->m_liquidMinZ) {
                this->m_liquidMinZ = z;
            }
            if (this->m_liquidMaxZ < z) {
                this->m_liquidMaxZ = z;
            }

            src += 8;
            x += LIQUID_TILE_SIZE;
        }

        x = this->m_liquidPos.x;
        y += LIQUID_TILE_SIZE;
    }
}

// ref: FUN_007d81c0
// A group named "antiportal" is not drawn: its faces become occluder quads instead. Each face
// whose vertices straddle the face's own average height contributes the two that sit above it.
// The occluder list (FUN_007b0160's pool) is not ported, so the faces are walked and dropped.
void CMapObjGroup::BuildAntiPortals() {
    for (uint32_t face = 0; face < this->m_faceCount; face++) {
        const C3Vector* corners[3] = {
            &this->m_vertices[this->m_indices[face * 3 + 0]],
            &this->m_vertices[this->m_indices[face * 3 + 1]],
            &this->m_vertices[this->m_indices[face * 3 + 2]]
        };

        float mid = (corners[1]->z + corners[2]->z + corners[0]->z) * 0.333333343f;

        int32_t above[3];
        uint32_t count = 0;

        for (int32_t i = 0; i < 3; i++) {
            if (mid < corners[i]->z) {
                above[count++] = i;
            }
        }

        if (count > 1) {
            // TODO FUN_007b0250(): the occluder this edge becomes, from (corners[above[0]],
            // corners[above[1]]), onto the list at DAT_00aeee5c
        }
    }

    this->m_batchCountB = 0;
    this->m_batchCountC = 0;
}
