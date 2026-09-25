#include "world/map/CMapRenderChunk.hpp"
#include "world/map/CMap.hpp"
#include "world/map/CMapArea.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/CWorld.hpp"
#include "gx/Buffer.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "gx/buffer/CGxBuf.hpp"
#include "model/CM2Lighting.hpp"
#include "model/CM2Scene.hpp"
#include <common/Handle.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Sphere.hpp>
#include <cmath>

static const float CHUNK_SIZE = 33.33333206176758f;   // DAT_00a3fdb0 is its negative

// ref: FUN_007b9cb0
CMapChunkBufBlock::CMapChunkBufBlock() {
}

CMapChunkBufBlock::~CMapChunkBufBlock() {
    this->link.Unlink();
    this->freeLink.Unlink();
    this->entries[1].link.Unlink();
    this->entries[0].link.Unlink();
}

// ref: FUN_007b9690
// The reference sets the batch's primitive type to 6 here; FillBuffers puts triangles in before
// anything draws it
CMapRenderChunk::CMapRenderChunk() {
    this->m_batch.m_primType = static_cast<EGxPrim>(6);
    this->m_batch.m_start = 0;
    this->m_batch.m_count = 0;
    this->m_batch.m_minIndex = 0;
    this->m_batch.m_maxIndex = 0;
}

// ref: FUN_007b9d60
CMapRenderChunk::~CMapRenderChunk() {
    this->ReleaseBufEntry();
    this->ReleaseLayers();

    if (this->m_chunk) {
        this->m_chunk->m_renderChunk = nullptr;
        this->m_chunk->m_renderChunkReady = 0;
        this->m_chunk = nullptr;
    }

    if (this->m_chunk2) {
        this->m_chunk2->m_renderChunk = nullptr;
        this->m_chunk2->m_renderChunkReady = 0;
        this->m_chunk2 = nullptr;
    }

    this->m_link.Unlink();
}

// ref: FUN_007b7af0
// Binds the render chunk to one chunk (or a neighbouring pair), its draw origin and its batch
// flags, and takes its centre and radius from the chunk bounds, merged when there are two.
void CMapRenderChunk::Init(CMapChunk* chunk, CMapChunk* chunk2, const C3Vector& position, uint8_t flags) {
    this->m_chunk = chunk;
    this->m_chunk2 = chunk2;
    this->m_position = position;
    this->m_flags = flags;

    CAaBox box = chunk->m_bounds;

    if (chunk2) {
        // FUN_00715130: the union of the two boxes
        const CAaBox& other = chunk2->m_bounds;
        box.b.x = other.b.x < box.b.x ? other.b.x : box.b.x;
        box.b.y = other.b.y < box.b.y ? other.b.y : box.b.y;
        box.b.z = other.b.z < box.b.z ? other.b.z : box.b.z;
        box.t.x = box.t.x < other.t.x ? other.t.x : box.t.x;
        box.t.y = box.t.y < other.t.y ? other.t.y : box.t.y;
        box.t.z = box.t.z < other.t.z ? other.t.z : box.t.z;
    }

    float dx = box.t.x - box.b.x;
    float dy = box.t.y - box.b.y;
    float dz = box.t.z - box.b.z;

    this->m_radius = sqrtf(dz * dz + dy * dy + dx * dx) * 0.5f;
    this->m_center.x = (box.b.x + box.t.x) * 0.5f;
    this->m_center.y = (box.t.y + box.b.y) * 0.5f;
    this->m_center.z = (box.t.z + box.b.z) * 0.5f;
}

// ref: FUN_007d3f70
// Everything a render chunk needs before it can draw: a buffer slot (its block then counts as
// in use), the vertices and indices in it, its layers, and a check that their textures are in.
void CMapRenderChunk::Build() {
    if (!this->m_bufEntry) {
        this->m_bufEntry = CMap::AllocBufEntry(this->m_flags & 0x3, this);
    }

    if (this->m_bufEntry) {
        CMap::s_bufBlockList.LinkToHead(this->m_bufEntry->block);
        this->FillBuffers(this->m_bufEntry->vertexBuf, this->m_bufEntry->indexBuf);
    }

    if (!this->m_layerCount) {
        this->AssignLayers();
    }

    if (!(this->m_flags & 0x8)) {
        this->CheckTexturesLoaded();
    }
}

// ref: FUN_007d02c0
// Fills whichever of the two buffers is not already holding valid data: the chunk's vertices
// (and the second chunk's after them, a chunk over along whichever axis the pairing flag says
// and its origin's height difference up), then the triangles of both into one batch.
void CMapRenderChunk::FillBuffers(CGxBuf* vertexBuf, CGxBuf* indexBuf) {
    bool verticesValid = vertexBuf->unk1C && vertexBuf->unk1D;
    bool indicesValid = indexBuf->unk1C && indexBuf->unk1D;

    if (!verticesValid) {
        C3Vector offset = { 0.0f, 0.0f, 0.0f };
        void* vertices = g_theGxDevicePtr->BufLock(vertexBuf);

        if (this->m_chunk) {
            this->m_chunk->BuildVertices(vertices, 0, &offset);
        }

        if (this->m_chunk2) {
            offset.z = this->m_chunk2->m_position.z - this->m_chunk->m_position.z;

            if (!(this->m_flags & 0x2)) {
                offset.x = -CHUNK_SIZE;
            } else {
                offset.y = -CHUNK_SIZE;
            }

            this->m_chunk2->BuildVertices(vertices, 0x91, &offset);
        }

        g_theGxDevicePtr->BufUnlock(vertexBuf, 0);
        vertexBuf->unk1C = 1;
    }

    if (!indicesValid) {
        this->m_batch.m_primType = GxPrim_Triangles;
        this->m_batch.m_start = 0;
        this->m_batch.m_count = 0;
        this->m_batch.m_minIndex = 0xFFFF;
        this->m_batch.m_maxIndex = 0;

        auto indices = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(indexBuf));

        if (this->m_chunk) {
            this->m_chunk->AppendIndices(indices, &this->m_batch);
        }

        if (this->m_chunk2) {
            this->m_chunk2->AppendIndices(indices + this->m_batch.m_count, &this->m_batch);
        }

        g_theGxDevicePtr->BufUnlock(indexBuf, 0);
        indexBuf->unk1C = 1;
    }
}

// ref: FUN_007d0420
// Without a pooled slot the render chunk draws from stream buffers filled on the spot
void CMapRenderChunk::DrawStreamed() {
    uint32_t stride = CMap::s_terrainVertexFormat == 2 ? sizeof(CMapChunkVertexColor) : sizeof(CMapChunkVertex);
    uint32_t vertexCount = CMap::s_chunkVertexCount;
    uint32_t indexCount = CMap::s_chunkIndexCount;

    if (this->m_flags & 0x3) {
        vertexCount *= 2;
        indexCount *= 2;
    }

    auto vertexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, stride, vertexCount);
    auto indexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, indexCount);

    this->FillBuffers(vertexBuf, indexBuf);

    GxPrimVertexPtr(vertexBuf, static_cast<EGxVertexBufferFormat>(CMap::s_terrainVertexFormat));
    g_theGxDevicePtr->PrimIndexPtr(indexBuf);
}

// ref: FUN_007d04a0
// Makes the render chunk current: the alpha textures are brought up to date, and, unless the
// caller says the shader vertex mode already did it, the world transform is the draw origin
// relative to the camera and the fixed-function lights and fog are set from the scene lights
// at the chunk's centre. Then the vertex and index streams point at its buffers.
void CMapRenderChunk::Bind(int32_t setup) {
    this->m_age = 0.0f;
    this->UpdateAlphaTextures();

    if (!setup || !CMap::s_chunkVerticesWorldSpace) {
        const C3Vector& cameraPos = CWorld::GetCameraPos();

        C44Matrix world;
        world.d0 = this->m_position.x - cameraPos.x;
        world.d1 = this->m_position.y - cameraPos.y;
        world.d2 = this->m_position.z - cameraPos.z;
        GxXformSet(GxXform_World, world);

        CM2Lighting lighting;
        CAaSphere sphere = { this->m_center, 0.0f };
        lighting.Initialize(nullptr, sphere);
        CWorld::GetM2Scene()->SelectLights(&lighting);
        CMap::SetupChunkLighting(&lighting);
        lighting.SetupGxLights(&cameraPos);
        lighting.SetupGxFog();
    }

    if (!this->m_bufEntry) {
        this->DrawStreamed();
        return;
    }

    GxPrimVertexPtr(this->m_bufEntry->vertexBuf, static_cast<EGxVertexBufferFormat>(CMap::s_terrainVertexFormat));
    g_theGxDevicePtr->PrimIndexPtr(this->m_bufEntry->indexBuf);
}

// ref: FUN_007b9770
// The layers of the chunk (and of the second chunk when the WDT allows pairs), in MCLY order
void CMapRenderChunk::AssignLayers() {
    int32_t chunkCount = (CMap::s_wdtHeader[0] & 0x4) ? 2 : 1;

    for (int32_t i = 0; i < chunkCount; i++) {
        auto chunk = i ? this->m_chunk2 : this->m_chunk;

        if (!chunk) {
            continue;
        }

        auto area = static_cast<CMapArea*>(chunk->m_parentLinkList.Head()->ref);
        uint32_t layerCount = chunk->m_header->nLayers;

        for (uint32_t n = 0; n < layerCount; n++) {
            this->AddLayer(area, &chunk->m_layers[n], i != 0);
        }
    }

    this->m_flags10 |= 0x20;
}

// ref: FUN_007b9250
// One MCLY entry becomes a layer: a second chunk's layer that repeats a texture already in the
// list is skipped; the tile's texture is created on first use.
void CMapRenderChunk::AddLayer(CMapArea* area, const SMLayer* layer, int32_t second) {
    if (second) {
        for (int32_t i = 0; i < this->m_layerCount; i++) {
            if (this->m_layers[i].textureId == layer->textureId) {
                return;
            }
        }
    }

    auto entry = &this->m_layers[this->m_layerCount];
    entry->flags = static_cast<uint16_t>(layer->flags);
    entry->index = this->m_layerCount;

    auto texture = &area->m_textures[layer->textureId];
    if (!texture->texture) {
        area->LoadTexture(texture, layer->textureId);
    }

    entry->texture = texture->texture;
    entry->textureId = layer->textureId;
    entry->alphaTexture = nullptr;
    entry->owner = this;

    if (entry->flags & 0x80) {
        this->m_flags10 |= 0x1;
    }

    // The reference gates the specular flag on the three device capabilities the tile texture
    // loader reads as well (CGxCaps +0x5c, +0xb4, +0xc4), assumed present
    if (entry->flags & 0x400) {
        this->m_flags10 |= 0x4;
    }

    this->m_layerCount++;
}

// ref: FUN_007b73e0
void CMapRenderChunk::CheckTexturesLoaded() {
    for (int32_t i = 0; i < this->m_layerCount; i++) {
        if (!TextureGetGxTex(this->m_layers[i].texture, 0, nullptr)) {
            return;
        }
    }

    this->m_flags |= 0x8;
}

// ref: FUN_007b7350
void CMapRenderChunk::ReleaseLayers() {
    for (int32_t i = 0; i < this->m_layerCount; i++) {
        auto layer = &this->m_layers[i];
        layer->texture = nullptr;
        layer->textureId = 0xFFFFFFFF;
        layer->flags = 0;
        layer->index = 0;

        if (layer->alphaTexture) {
            HandleClose(layer->alphaTexture);
            layer->alphaTexture = nullptr;
        }
    }

    this->m_layerCount = 0;

    if (this->m_shadowTexture) {
        HandleClose(this->m_shadowTexture);
        this->m_shadowTexture = nullptr;
    }

    if (this->m_alphaTexture) {
        HandleClose(this->m_alphaTexture);
        this->m_alphaTexture = nullptr;
    }
}

// ref: FUN_007b9830
// The buffer slot goes back to the free list it came from: a two-chunk slot returns its whole
// block, a single slot just the entry
void CMapRenderChunk::ReleaseBufEntry() {
    auto entry = this->m_bufEntry;

    if (!entry) {
        return;
    }

    if (entry->flags & 0x3) {
        CMap::s_freeBufBlockList.LinkToHead(entry->block);
    } else {
        CMap::s_freeBufEntryList.LinkToHead(entry);
    }

    entry->renderChunk = nullptr;
    this->m_bufEntry = nullptr;
}

// ref: FUN_007ba050
// Rebuilds the alpha and shadow textures when the layer state asks for it. The state machine is
// ported; the texture builders it drives (FUN_007b9de0 per layer, FUN_007b9ee0 for the shadow,
// FUN_007b9f90 for the packed alpha, all through the "TerrainBlend" texture callbacks) are not
// yet, so no alpha texture exists and every draw is the base layer only until they are.
void CMapRenderChunk::UpdateAlphaTextures() {
    if (!this->m_layerCount) {
        this->m_flags10 &= ~0x30;
        return;
    }

    uint16_t flags = this->m_flags10;

    if (!(flags & 0x20)) {
        if (!(flags & 0x10)) {
            if (!(flags & 0x8)) {
                this->m_flags10 &= ~0x30;
                return;
            }
        } else if (flags & 0x8) {
            this->m_flags10 &= ~0x30;
            return;
        }
    }

    this->m_flags10 = flags & ~0x8;
    if (flags & 0x10) {
        this->m_flags10 = (flags & ~0x8) | 0x8;
    }

    if (!CMap::s_terrainShaders && !CMap::s_terrainSpecular) {
        for (int32_t i = 0; i < this->m_layerCount; i++) {
            // TODO FUN_007b9de0(&m_layers[i]): the layer's own alpha texture
        }

        if (!(CMap::s_wdtHeader[0] & 0x4)) {
            // TODO FUN_007b9ee0(): the shadow texture
            this->m_flags10 &= ~0x30;
            return;
        }
    } else {
        // TODO FUN_007b9f90(): the packed alpha texture
    }

    this->m_flags10 &= ~0x30;
}
