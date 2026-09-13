#include "world/Terrain.hpp"
#include "gx/Buffer.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/Shader.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "gx/shader/CGxShader.hpp"
#include "util/CStatus.hpp"
#include "util/SFile.hpp"
#include <storm/String.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <common/Handle.hpp>
#include <storm/Memory.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>

namespace {

const float TILE_SIZE = 533.33333f;
const float CHUNK_SIZE = TILE_SIZE / 16.0f;
const float UNIT_SIZE = CHUNK_SIZE / 8.0f;

// One 16x16 map chunk: 145 vertices (9x9 outer grid interleaved with 8x8 inner), base texture
struct TerrainChunk {
    C3Vector position[145];
    C2Vector texcoord[145];
    CImVector color[145];
    HTEXTURE texture = nullptr;
    bool valid = false;
};

struct TerrainTile {
    int32_t x = -1;
    int32_t y = -1;
    bool loaded = false;
    TerrainChunk chunks[256];
};

const int32_t MAX_TILES = 9;
TerrainTile s_tiles[MAX_TILES];
char s_mapName[128] = { 0 };
int32_t s_mapID = -1;

CGxShader* s_vertexShader[2] = { nullptr, nullptr };
CGxShader* s_pixelShader = nullptr;

// The 8x8 cells, four triangles each around the inner vertex; the vertex order matches MCVT
uint16_t s_indices[768];
bool s_indicesBuilt = false;

void BuildIndices() {
    int32_t n = 0;

    for (int32_t j = 0; j < 8; j++) {
        for (int32_t i = 0; i < 8; i++) {
            uint16_t tl = j * 17 + i;
            uint16_t tr = j * 17 + i + 1;
            uint16_t bl = (j + 1) * 17 + i;
            uint16_t br = (j + 1) * 17 + i + 1;
            uint16_t c = j * 17 + 9 + i;

            s_indices[n++] = c; s_indices[n++] = tl; s_indices[n++] = tr;
            s_indices[n++] = c; s_indices[n++] = tr; s_indices[n++] = br;
            s_indices[n++] = c; s_indices[n++] = br; s_indices[n++] = bl;
            s_indices[n++] = c; s_indices[n++] = bl; s_indices[n++] = tl;
        }
    }

    s_indicesBuilt = true;
}

uint32_t ReadChunkTag(const uint8_t* data, uint32_t offset, uint32_t& size) {
    uint32_t tag = *reinterpret_cast<const uint32_t*>(data + offset);
    size = *reinterpret_cast<const uint32_t*>(data + offset + 4);
    return tag;
}

// 'MCVT' etc. are stored reversed on disk
constexpr uint32_t FourCC(const char* s) {
    return (static_cast<uint32_t>(s[3]) << 24) | (static_cast<uint32_t>(s[2]) << 16) | (static_cast<uint32_t>(s[1]) << 8) | static_cast<uint32_t>(s[0]);
}

void ParseChunk(TerrainChunk& chunk, const uint8_t* mcnk, uint32_t mcnkSize, const char** textureNames, uint32_t textureCount) {
    // MCNK header is 128 bytes; the sub-chunk offsets are relative to the MCNK data start
    const uint8_t* hdr = mcnk;
    uint32_t nLayers = *reinterpret_cast<const uint32_t*>(hdr + 0x10);
    uint32_t ofsHeight = *reinterpret_cast<const uint32_t*>(hdr + 0x14);
    uint32_t ofsNormal = *reinterpret_cast<const uint32_t*>(hdr + 0x18);
    uint32_t ofsLayer = *reinterpret_cast<const uint32_t*>(hdr + 0x1C);
    float posX = *reinterpret_cast<const float*>(hdr + 0x68);
    float posY = *reinterpret_cast<const float*>(hdr + 0x6C);
    float posZ = *reinterpret_cast<const float*>(hdr + 0x70);

    // MCVT: 145 heights, relative to posZ. The sub-chunk has its own 8-byte tag header.
    const float* heights = reinterpret_cast<const float*>(mcnk + ofsHeight + 8);
    const int8_t* normals = ofsNormal ? reinterpret_cast<const int8_t*>(mcnk + ofsNormal + 8) : nullptr;

    int32_t k = 0;

    for (int32_t row = 0; row < 17; row++) {
        bool inner = (row & 1) != 0;
        int32_t count = inner ? 8 : 9;
        int32_t r = row / 2;

        for (int32_t c = 0; c < count; c++) {
            float fx = inner ? (r + 0.5f) : static_cast<float>(r);
            float fy = inner ? (c + 0.5f) : static_cast<float>(c);

            chunk.position[k].x = posX - fx * UNIT_SIZE;
            chunk.position[k].y = posY - fy * UNIT_SIZE;
            chunk.position[k].z = posZ + heights[k];

            chunk.texcoord[k].x = (inner ? (c + 0.5f) : c) / 8.0f;
            chunk.texcoord[k].y = (inner ? (r + 0.5f) : r) / 8.0f;

            // Lambert shade from the vertex normal against a fixed sun
            float shade = 0.8f;

            if (normals) {
                float nx = normals[k * 3 + 0] / 127.0f;
                float ny = normals[k * 3 + 1] / 127.0f;
                float nz = normals[k * 3 + 2] / 127.0f;
                float d = nx * -0.3f + ny * -0.3f + nz * 0.9f;
                shade = 0.45f + 0.55f * (d < 0.0f ? 0.0f : d > 1.0f ? 1.0f : d);
            }

            uint8_t s = static_cast<uint8_t>(shade * 255.0f);
            chunk.color[k] = { s, s, s, 0xFF };

            k++;
        }
    }

    // Base texture from the first layer
    chunk.texture = nullptr;

    if (nLayers && ofsLayer) {
        uint32_t textureId = *reinterpret_cast<const uint32_t*>(mcnk + ofsLayer + 8);

        if (textureId < textureCount && textureNames[textureId]) {
            CStatus status;
            chunk.texture = TextureCreate(textureNames[textureId], CGxTexFlags(GxTex_Linear, 1, 1, 0, 0, 0, 1), &status, 0);
        }
    }

    chunk.valid = true;
}

void LoadTile(TerrainTile& tile, int32_t tileX, int32_t tileY) {
    tile.x = tileX;
    tile.y = tileY;
    tile.loaded = true;

    for (auto& chunk : tile.chunks) {
        chunk.valid = false;
        chunk.texture = nullptr;
    }

    char path[256];
    SStrPrintf(path, sizeof(path), "World\\Maps\\%s\\%s_%d_%d.adt", s_mapName, s_mapName, tileX, tileY);

    void* data = nullptr;
    size_t size = 0;

    if (!SFile::Load(nullptr, path, &data, &size, 0, 0, nullptr) || !data) {
        return;
    }

    auto bytes = static_cast<const uint8_t*>(data);

    // Gather the texture name table (MTEX) and the 256 MCNK offsets (MCIN)
    static const char* textureNames[256];
    uint32_t textureCount = 0;
    const uint8_t* mcin = nullptr;

    uint32_t offset = 0;

    while (offset + 8 <= size) {
        uint32_t chunkSize;
        uint32_t tag = ReadChunkTag(bytes, offset, chunkSize);
        const uint8_t* body = bytes + offset + 8;

        if (tag == FourCC("MTEX")) {
            const char* p = reinterpret_cast<const char*>(body);
            const char* end = p + chunkSize;

            while (p < end && textureCount < 256) {
                textureNames[textureCount++] = p;
                p += SStrLen(p) + 1;
            }
        } else if (tag == FourCC("MCIN")) {
            mcin = body;
        }

        offset += 8 + chunkSize;
    }

    if (mcin) {
        for (int32_t i = 0; i < 256; i++) {
            uint32_t mcnkOffset = *reinterpret_cast<const uint32_t*>(mcin + i * 16);

            if (mcnkOffset && mcnkOffset + 8 <= size) {
                uint32_t mcnkSize = *reinterpret_cast<const uint32_t*>(bytes + mcnkOffset + 4);
                ParseChunk(tile.chunks[i], bytes + mcnkOffset + 8, mcnkSize, textureNames, textureCount);
            }
        }
    }

    SMemFree(data, __FILE__, __LINE__, 0);
}

void FreeTile(TerrainTile& tile) {
    for (auto& chunk : tile.chunks) {
        if (chunk.texture) {
            HandleClose(chunk.texture);
            chunk.texture = nullptr;
        }

        chunk.valid = false;
    }

    tile.loaded = false;
    tile.x = -1;
    tile.y = -1;
}

} // namespace

void TerrainLoad(const char* mapName, int32_t mapID) {
    TerrainUnload();
    SStrCopy(s_mapName, mapName, sizeof(s_mapName));
    s_mapID = mapID;

    if (!s_indicesBuilt) {
        BuildIndices();
    }
}

void TerrainUnload() {
    for (auto& tile : s_tiles) {
        if (tile.loaded) {
            FreeTile(tile);
        }
    }
}

void TerrainUpdate(const C3Vector& cameraPos) {
    if (!s_mapName[0]) {
        return;
    }

    // The tile the camera is over (worldX maps to the row index, worldY to the column)
    int32_t centerCol = static_cast<int32_t>(32.0f - cameraPos.y / TILE_SIZE);
    int32_t centerRow = static_cast<int32_t>(32.0f - cameraPos.x / TILE_SIZE);

    // Keep the 3x3 block of tiles around the camera resident
    bool wanted[MAX_TILES] = { false };
    int32_t slot = 0;

    for (int32_t dy = -1; dy <= 1; dy++) {
        for (int32_t dx = -1; dx <= 1; dx++) {
            int32_t tileX = centerCol + dx;
            int32_t tileY = centerRow + dy;

            if (tileX < 0 || tileX > 63 || tileY < 0 || tileY > 63) {
                continue;
            }

            // Already loaded?
            bool found = false;

            for (auto& tile : s_tiles) {
                if (tile.loaded && tile.x == tileX && tile.y == tileY) {
                    found = true;
                    break;
                }
            }

            if (found) {
                continue;
            }

            // Load into the first free slot
            for (auto& tile : s_tiles) {
                if (!tile.loaded) {
                    LoadTile(tile, tileX, tileY);
                    break;
                }
            }
        }
    }
}

void TerrainRender() {
    if (!s_mapName[0]) {
        return;
    }

    if (!s_pixelShader && g_theGxDevicePtr) {
        g_theGxDevicePtr->ShaderCreate(s_vertexShader, GxSh_Vertex, "Shaders\\Vertex", "UI", 2);
        g_theGxDevicePtr->ShaderCreate(&s_pixelShader, GxSh_Pixel, "Shaders\\Pixel", "UI", 1);
    }

    if (!s_vertexShader[0] || !s_vertexShader[0]->Valid() || !s_pixelShader || !s_pixelShader->Valid()) {
        return;
    }

    GxRsPush();

    GxRsSet(GxRs_DepthTest, 1);
    GxRsSet(GxRs_DepthWrite, 1);
    GxRsSet(GxRs_DepthFunc, 0);
    GxRsSet(GxRs_Culling, 1);
    GxRsSet(GxRs_BlendingMode, GxBlend_Opaque);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);

    GxRsSet(GxRs_VertexShader, s_vertexShader[0]);
    GxRsSet(GxRs_PixelShader, s_pixelShader);

    C44Matrix viewProj;
    GxXformViewProjNativeTranspose(viewProj);
    GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<float*>(&viewProj), 4);

    for (auto& tile : s_tiles) {
        if (!tile.loaded) {
            continue;
        }

        for (auto& chunk : tile.chunks) {
            if (!chunk.valid || !chunk.texture) {
                continue;
            }

            GxRsSet(GxRs_Texture0, TextureGetGxTex(chunk.texture, 0, nullptr));

            GxPrimLockVertexPtrs(
                145,
                chunk.position, sizeof(C3Vector),
                nullptr, 0,
                chunk.color, sizeof(CImVector),
                nullptr, 0,
                chunk.texcoord, sizeof(C2Vector),
                nullptr, 0
            );
            GxDrawLockedElements(GxPrim_Triangles, 768, s_indices);
            GxPrimUnlockVertexPtrs();
        }
    }

    GxRsPop();
}
