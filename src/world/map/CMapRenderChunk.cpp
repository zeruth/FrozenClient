#include "world/map/CMapRenderChunk.hpp"
#include "world/map/CMap.hpp"
#include "world/map/CMapArea.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldScene.hpp"
#include "world/ShadowMap.hpp"
#include "gx/Buffer.hpp"
#include "gx/RenderState.hpp"
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

// ----------------------------------------------------------------------------------------------
// Drawing

// The MCLY animation speed divisors (DAT_00af14f8), indexed by bits 3-5 of the layer flags
static const float s_layerScrollSpeeds[8] = { 64.0f, 48.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f };

// ref: FUN_007d06b0
// The fixed-function texture matrices for local-space chunks: chunk XY scaled into layer UV
// (with the axes swapped) and moved by the camera-relative origin, and the same at an eighth for
// the alpha map
static void BuildTexMatrices(C44Matrix* layer, C44Matrix* alpha, const C3Vector* offset, float scale) {
    layer->Scale(scale);
    float a0 = layer->a0, a1 = layer->a1, a2 = layer->a2, a3 = layer->a3;
    layer->a0 = layer->b0; layer->a1 = layer->b1; layer->a2 = layer->b2; layer->a3 = layer->b3;
    layer->b0 = a0; layer->b1 = a1; layer->b2 = a2; layer->b3 = a3;
    layer->Translate(*offset);

    alpha->Scale(scale * 0.125f);
    a0 = alpha->a0; a1 = alpha->a1; a2 = alpha->a2; a3 = alpha->a3;
    alpha->a0 = alpha->b0; alpha->a1 = alpha->b1; alpha->a2 = alpha->b2; alpha->a3 = alpha->b3;
    alpha->b0 = a0; alpha->b1 = a1; alpha->b2 = a2; alpha->b3 = a3;
    alpha->Translate(*offset);
}

// ref: FUN_007d0050
// The terrain vertex shader and its per-chunk constants: the scene lights at the chunk's bounding
// sphere (the nearest three as point lights, the rest folded into the sun), the chunk origin, the
// UV scale of every texture (the alpha map's halved along the paired axis of a two-chunk batch),
// then the permutation the chunk needs.
void CMapRenderChunk::SetupVertexShader(int32_t textureCount, int32_t chunkSpecular) {
    int32_t specular = CMap::s_terrainSpecular;
    int32_t color = CMap::s_terrainVertexFormat == 2;

    CM2Lighting lighting;
    CAaSphere sphere = { this->m_center, this->m_radius };
    lighting.Initialize(nullptr, sphere);
    CWorld::GetM2Scene()->SelectLights(&lighting);
    CMap::SetupChunkLighting(&lighting);

    auto constants = &CWorldScene::s_terrainConstants;
    const C3Vector& cameraPos = CWorld::GetCameraPos();
    int32_t lights = 0;

    for (uint32_t i = 0; i < 3; i++) {
        C3Vector pos = { 0.0f, 0.0f, 0.0f };
        auto light = &constants->lights[i];

        if (!lighting.GetLight(i, &pos, reinterpret_cast<C3Vector*>(light->color), reinterpret_cast<C3Vector*>(light->attenuation))) {
            light->pos[0] = 0.0f;
            light->pos[1] = 0.0f;
            light->pos[2] = 0.0f;
            light->pos[3] = 0.0f;
            light->color[0] = 0.0f;
            light->color[1] = 0.0f;
            light->color[2] = 0.0f;
            light->color[3] = 0.0f;
            light->attenuation[0] = 1.0f;
            light->attenuation[1] = 0.0f;
            light->attenuation[2] = 0.0f;
            light->attenuation[3] = 0.0f;
        } else {
            light->pos[0] = pos.x - cameraPos.x;
            light->pos[1] = pos.y - cameraPos.y;
            light->pos[2] = pos.z - cameraPos.z;
            light->pos[3] = 1.0f;
            lights = 1;
        }
    }

    constants->position[0] = this->m_position.x;
    constants->position[1] = this->m_position.y;
    constants->position[2] = this->m_position.z;
    constants->position[3] = 0.0f;

    int32_t layers = textureCount - 1;
    float invCellSize = CMapChunk::s_invCellSize;
    float scale = -invCellSize;

    if (layers) {
        constants->texScale[0][0] = scale;
        constants->texScale[0][1] = scale;
        constants->texScale[0][2] = 0.0f;
        constants->texScale[0][3] = 0.0f;

        for (int32_t i = 1; i < layers; i++) {
            constants->texScale[i][0] = constants->texScale[0][0];
            constants->texScale[i][1] = constants->texScale[0][1];
            constants->texScale[i][2] = constants->texScale[0][2];
            constants->texScale[i][3] = constants->texScale[0][3];
        }
    }

    float alphaX = scale * 0.125f;
    float alphaY = 0.125f * scale;

    if (this->m_flags & 0x1) {
        alphaX = invCellSize * -0.0625f;
    } else if (this->m_flags & 0x2) {
        alphaY = invCellSize * -0.0625f;
    }

    constants->texScale[layers][0] = alphaX;
    constants->texScale[layers][1] = alphaY;
    constants->texScale[layers][2] = 0.0f;
    constants->texScale[layers][3] = 0.0f;

    int32_t shadowLevel = ShadowMapGetShaderLevel();
    auto shader = CMap::GetTerrainVertexShader(lights, layers, specular, color, chunkSpecular, shadowLevel != 0);
    g_theGxDevicePtr->RsSet(GxRs_VertexShader, shader);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(constants), CWorldScene::TERRAIN_CONSTANT_COUNT);
}

// ref: FUN_007d28b0
// Draws a local-space render chunk through the pixel shader already selected: each layer's
// texture on its own stage with a fixed-function texture matrix (scrolled for animated layers),
// the alpha map on the stage after them, then the batch
void CMapRenderChunk::DrawLocal() {
    g_theGxDevicePtr->RsSet(GxRs_BlendingMode, 0);
    g_theGxDevicePtr->RsSetAlphaRef();

    C44Matrix layerMatrix;
    C44Matrix alphaMatrix;
    const C3Vector& cameraPos = CWorld::GetCameraPos();
    C3Vector offset = {
        cameraPos.x - this->m_position.x,
        cameraPos.y - this->m_position.y,
        cameraPos.z - this->m_position.z
    };
    BuildTexMatrices(&layerMatrix, &alphaMatrix, &offset, -CMapChunk::s_invCellSize);

    if (this->m_flags10 & 0x1) {
        float mask[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        for (uint32_t i = 0; i < this->m_layerCount; i++) {
            if (this->m_layers[i].flags & 0x80) {
                mask[i] = 0.0f;
            }
        }

        g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 1, mask, 1);
    }

    int32_t chunkSpecular = (this->m_flags10 & 0x4) != 0;
    int32_t i = 0;

    for (; i < this->m_layerCount; i++) {
        auto layer = &this->m_layers[i];
        auto texture = TextureGetGxTex(layer->texture, 0, nullptr);
        g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Texture0 + i), texture);
        GxTexSetWrap(texture, GxTex_Wrap, GxTex_Wrap);

        if (i == 0 && chunkSpecular) {
            g_theGxDevicePtr->RsSet(GxRs_TexGen0, 4);
            g_theGxDevicePtr->RsSet(GxRs_Unk61, 0);
        } else {
            C44Matrix matrix = layerMatrix;

            if (layer->flags & 0x40) {
                uint32_t dir = layer->flags & 0x7;
                float speed = (1.0f / CMapChunk::s_invCellSize) / s_layerScrollSpeeds[(layer->flags >> 3) & 0x7];
                C3Vector scroll = {
                    CWorld::s_textureScroll[dir][0] * speed,
                    CWorld::s_textureScroll[dir][1] * speed,
                    CWorld::s_textureScroll[dir][2] * speed
                };
                matrix.Translate(scroll);
            }

            g_theGxDevicePtr->XformSet(static_cast<EGxXform>(GxXform_Tex0 + i), matrix);
        }
    }

    auto alpha = TextureGetGxTex(this->m_alphaTexture, 1, nullptr);
    g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Texture0 + i), alpha);
    g_theGxDevicePtr->XformSet(static_cast<EGxXform>(GxXform_Tex0 + i), alphaMatrix);

    g_theGxDevicePtr->Draw(&this->m_batch, 1);

    for (; i >= 0; i--) {
        g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Texture0 + i), static_cast<CGxTex*>(nullptr));
    }
}

// ref: FUN_007d2d70
// Draws a world-space render chunk through the pixel shader already selected: the layers and
// alpha map on their stages, the scroll of animated layers in the vertex constants, the vertex
// shader set up for the chunk, then the batch
void CMapRenderChunk::DrawWorld() {
    g_theGxDevicePtr->RsSet(GxRs_BlendingMode, 0);
    g_theGxDevicePtr->RsSetAlphaRef();

    if (this->m_flags10 & 0x1) {
        float mask[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        for (uint32_t i = 0; i < this->m_layerCount; i++) {
            if (this->m_layers[i].flags & 0x80) {
                mask[i] = 0.0f;
            }
        }

        g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 1, mask, 1);
    }

    int32_t chunkSpecular = (this->m_flags10 & 0x4) != 0;
    auto constants = &CWorldScene::s_terrainConstants;
    int32_t i = 0;

    for (; i < this->m_layerCount; i++) {
        auto layer = &this->m_layers[i];
        auto texture = TextureGetGxTex(layer->texture, 0, nullptr);
        g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Texture0 + i), texture);
        GxTexSetWrap(texture, GxTex_Wrap, GxTex_Wrap);

        if (layer->flags & 0x40) {
            uint32_t dir = layer->flags & 0x7;
            float speed = (1.0f / CMapChunk::s_invCellSize) / s_layerScrollSpeeds[(layer->flags >> 3) & 0x7];
            float y = 0.125f * CWorld::s_textureScroll[dir][1] * speed;
            constants->layerScroll[i][0] = -(CWorld::s_textureScroll[dir][0] * speed * 0.125f);
            constants->layerScroll[i][1] = -y;
        }
    }

    CGxTex* alpha = nullptr;
    if (this->m_alphaTexture) {
        alpha = TextureGetGxTex(this->m_alphaTexture, 1, nullptr);
    }
    g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Texture0 + i), alpha);

    this->SetupVertexShader(this->m_layerCount + 1, chunkSpecular);

    g_theGxDevicePtr->Draw(&this->m_batch, 1);

    for (uint32_t n = 0; n < this->m_layerCount; n++) {
        if (this->m_layers[n].flags & 0x40) {
            constants->layerScroll[n][0] = 0.0f;
            constants->layerScroll[n][1] = 0.0f;
            g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, CWorldScene::TERRAIN_CONSTANT_LAYER_SCROLL + n, constants->layerScroll[n], 1);
        }
    }

    for (; i >= 0; i--) {
        g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Texture0 + i), static_cast<CGxTex*>(nullptr));
    }
}

// ref: FUN_007d3010
// A local-space chunk with no textures of its own: the grey placeholder on stage 0 and, with
// terrain shaders, black on stage 1 (grey again without), identity texture matrices
void CMapRenderChunk::DrawSolidLocal() {
    C44Matrix identity;
    g_theGxDevicePtr->XformSet(GxXform_Tex0, identity);
    g_theGxDevicePtr->XformSet(GxXform_Tex1, identity);

    g_theGxDevicePtr->RsSet(GxRs_BlendingMode, 0);
    g_theGxDevicePtr->RsSetAlphaRef();

    auto texture = TextureGetGxTex(CWorldScene::s_solidTexture, 1, nullptr);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, texture);
    GxTexSetWrap(texture, GxTex_Wrap, GxTex_Wrap);

    auto second = CMap::s_terrainShaders ? CWorldScene::s_blackTexture : CWorldScene::s_solidTexture;
    g_theGxDevicePtr->RsSet(GxRs_Texture1, TextureGetGxTex(second, 1, nullptr));

    g_theGxDevicePtr->Draw(&this->m_batch, 1);

    g_theGxDevicePtr->RsSet(GxRs_Texture0, static_cast<CGxTex*>(nullptr));
    g_theGxDevicePtr->RsSet(GxRs_Texture1, static_cast<CGxTex*>(nullptr));
}

// ref: FUN_007d3240
// The world-space counterpart: grey and black on the first two stages, the vertex shader set up
// for one layer and the alpha map
void CMapRenderChunk::DrawSolidWorld() {
    g_theGxDevicePtr->RsSet(GxRs_BlendingMode, 0);
    g_theGxDevicePtr->RsSetAlphaRef();

    auto texture = TextureGetGxTex(CWorldScene::s_solidTexture, 1, nullptr);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, texture);
    GxTexSetWrap(texture, GxTex_Wrap, GxTex_Wrap);

    g_theGxDevicePtr->RsSet(GxRs_Texture1, TextureGetGxTex(CWorldScene::s_blackTexture, 1, nullptr));

    this->SetupVertexShader(2, 0);

    g_theGxDevicePtr->Draw(&this->m_batch, 1);

    g_theGxDevicePtr->RsSet(GxRs_Texture0, static_cast<CGxTex*>(nullptr));
    g_theGxDevicePtr->RsSet(GxRs_Texture1, static_cast<CGxTex*>(nullptr));
}

// ref: FUN_007d40a0
// The normal-vector debug pass (CWorld enable bit 0x40000000): one white line from every vertex
// along its normal, streamed from world-space vertices with no shaders, fog or lighting
void CMapRenderChunk::DrawDebug() {
    GxRsPush();

    g_theGxDevicePtr->RsSet(GxRs_Fog, 0);
    g_theGxDevicePtr->RsSet(GxRs_Lighting, 0);
    g_theGxDevicePtr->RsSet(GxRs_VertexShader, static_cast<CGxShader*>(nullptr));
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, static_cast<CGxShader*>(nullptr));

    int32_t vertexCount = this->m_chunk2 ? 0x122 : 0x91;
    int32_t lineVertexCount = vertexCount * 2;

    auto vertexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x10, lineVertexCount);
    auto vertices = reinterpret_cast<float*>(g_theGxDevicePtr->BufLock(vertexBuf));
    auto indexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, lineVertexCount);
    auto indices = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(indexBuf));

    CMapChunkVertex chunkVertices[0x91];
    uint16_t index = 0;

    for (int32_t n = 0; n < 2; n++) {
        auto chunk = n ? this->m_chunk2 : this->m_chunk;

        if (!chunk) {
            continue;
        }

        C3Vector offset = { 0.0f, 0.0f, 0.0f };
        chunk->FillVerticesWorld(chunkVertices, &offset);

        for (int32_t v = 0; v < 0x91; v++) {
            auto src = &chunkVertices[v];
            vertices[0] = src->position.x;
            vertices[1] = src->position.y;
            vertices[2] = src->position.z;
            reinterpret_cast<uint32_t*>(vertices)[3] = 0xFFFFFFFF;
            vertices[4] = src->position.x + src->normal.x * 0.75f;
            vertices[5] = src->position.y + src->normal.y * 0.75f;
            vertices[6] = src->position.z + src->normal.z * 0.75f;
            reinterpret_cast<uint32_t*>(vertices)[7] = 0xFFFFFFFF;
            vertices += 8;

            indices[0] = index;
            indices[1] = index + 1;
            indices += 2;
            index += 2;
        }
    }

    g_theGxDevicePtr->BufUnlock(vertexBuf, 0);
    vertexBuf->unk1C = 1;
    GxPrimVertexPtr(vertexBuf, GxVBF_PC);

    g_theGxDevicePtr->BufUnlock(indexBuf, 0);
    indexBuf->unk1C = 1;
    g_theGxDevicePtr->PrimIndexPtr(indexBuf);

    CGxBatch batch;
    batch.m_primType = GxPrim_Lines;
    batch.m_start = 0;
    batch.m_count = lineVertexCount;
    batch.m_minIndex = 0;
    batch.m_maxIndex = static_cast<uint16_t>(lineVertexCount);
    g_theGxDevicePtr->Draw(&batch, 1);

    GxRsPop();
}
