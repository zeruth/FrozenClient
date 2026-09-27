#include "world/map/LiquidSurface.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjGroup.hpp"
#include "db/Db.hpp"
#include "world/map/LiquidMaterialSettings.hpp"
#include "world/map/CChunkLiquid.hpp"
#include "world/map/LiquidVertexData.hpp"
#include <storm/Memory.hpp>
#include <storm/Error.hpp>
#include "world/map/CMapObjDef.hpp"
#include "world/map/CMapObjDefGroup.hpp"
#include "util/Log.hpp"
#include <cmath>
#include <cstdlib>
#include <new>
#include "gx/Buffer.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include <common/Time.hpp>
#include "world/CWorldScene.hpp"
#include "world/CWorld.hpp"
#include "model/CM2Scene.hpp"
#include "model/CM2Lighting.hpp"
#include "gx/buffer/CGxBuf.hpp"


namespace Liquid {

// One tile of a chunk's eight-by-eight grid. DAT_00a40408.
static const float TILE_STEP = 4.166666507720947f;

// The gather buffer, kept between calls rather than allocated per surface -- the reference has
// it as a file static and frees it at exit. DAT_00d2dd34 .. DAT_00d2dd40.
static TSGrowableArray<CChunkLiquid*> s_gather;

// Whether adjacent chunks' layers of the same liquid merge into one surface. DAT_00ce0498: the
// map load zeroes it and a settings pair turns it on. Frozen has neither setting, so it stays
// off and every layer becomes its own surface -- see the note in CreateSurface.
static int32_t s_mergeNeighbours = 0;

void IGeomFactory::AddRef() {
    this->m_refCount++;
}

void CChunkGeomFactory::Release() {
    if (--this->m_refCount) {
        return;
    }

    // The blocks go back to the map's lists. Leaking them would hold a pool per surface for the
    // life of the process, and every chunk that has ever carried water makes one.
    if (this->m_vertexBlock) {
        VBBList::s_vertexList.Free(this->m_vertexBlock);

        this->m_vertexBlock = nullptr;
    }

    if (this->m_indexBlock) {
        VBBList::s_indexList.Free(this->m_indexBlock);

        this->m_indexBlock = nullptr;
    }

    this->~CChunkGeomFactory();

    SMemFree(this, __FILE__, __LINE__, 0);
}

void CClientEnvironment::AddRef() {
}

void CClientEnvironment::Release() {
    this->~CClientEnvironment();

    SMemFree(this, __FILE__, __LINE__, 0);
}

// ref: FUN_008a1700
void CInstance::AddRef() {
    this->m_refCount++;
}

// ref: FUN_008a1ac0
void CInstance::Release() {
    if (--this->m_refCount) {
        return;
    }


    if (this->m_geometry) {
        this->m_geometry->Release();
    }

    if (this->m_environment) {
        this->m_environment->Release();
    }

    if (this->m_waveManager) {
        this->m_waveManager->Release();
    }

    this->~CInstance();

    SMemFree(this, __FILE__, __LINE__, 0);
}

// ref: FUN_007d49d0
// The factory takes its own copy of the gathered list, so the shared gather buffer is free to be
// reused by the next surface.
static CChunkGeomFactory* CreateGeomFactory(const TSGrowableArray<CChunkLiquid*>& layers) {
    auto factory = static_cast<CChunkGeomFactory*>(
        SMemAlloc(sizeof(CChunkGeomFactory), __FILE__, __LINE__, 0));

    if (!factory) {
        return nullptr;
    }

    new (factory) CChunkGeomFactory();

    for (uint32_t i = 0; i < layers.Count(); i++) {
        CChunkLiquid* layer = layers[i];

        factory->m_layers.Add(1, &layer);
    }

    return factory;
}

// ref: FUN_007d5120
static CClientEnvironment* CreateEnvironment(uint32_t arg) {
    auto environment = static_cast<CClientEnvironment*>(
        SMemAlloc(sizeof(CClientEnvironment), __FILE__, __LINE__, 0));

    if (!environment) {
        return nullptr;
    }

    new (environment) CClientEnvironment();

    environment->m_fixedLight = arg;

    return environment;
}

// ref: FUN_007cf200
// A surface is one material's worth of liquid: the layers it covers, a box big enough for all of
// them, and where it sits. Every layer it took points back at it and bumps its count, so it
// lives until the last of them lets go.
void CreateSurface(CChunkLiquid* liquid) {
    s_gather.SetCount(0);

    if (!s_mergeNeighbours) {
        // The layer on its own. This is the path the client actually takes: the switch that
        // selects the other one is zeroed at map load and only a settings pair turns it on.
        s_gather.Add(1, &liquid);
    } else {
        // TODO the merging path. The reference walks the 2x2 block of chunks around this one
        // and takes every layer of the same liquid type from each, so one surface can span four
        // chunks. Frozen never reaches it, and porting it blind would be untestable.
        s_gather.Add(1, &liquid);
    }

    auto geometry = CreateGeomFactory(s_gather);

    if (!geometry) {
        return;
    }

    // A box around every layer taken. Each layer covers a rectangle of its chunk's tile grid, so
    // its extent is the chunk corner plus the rectangle's corners a tile at a time.
    //
    // DIVERGED FROM THE REFERENCE, because the reference disagrees with itself here.
    //
    // Its own CChunkLiquid::BuildVertices (FUN_007cdf80) places a layer's vertices at
    // (m_tileX + i) * -(CHUNK_SIZE/8) along x and (m_tileY + j) * -(CHUNK_SIZE/8) along y,
    // relative to the chunk. That is where the geometry actually is, and frozen matches it.
    //
    // Its liquid bounds (inside FUN_007cf200) instead take x from the field at +0x38 and y from
    // +0x34 -- the other way round -- and add a POSITIVE CHUNK_SIZE/8 to the chunk origin, for
    // both the minimum and maximum corner. Since the origin is the corner those negative steps
    // run away from, that walks out of the chunk in the wrong direction on both axes.
    //
    // So the reference's liquid sphere does not enclose the reference's own liquid vertices. It
    // only feeds an occlusion cull, which is why a misplaced sphere can ship unnoticed: it costs
    // a little over- or under-draw at the edges and nothing else. Reproducing it would mean
    // culling frozen's surfaces against a box that is not where they are.
    //
    // This follows the vertices instead. Recorded in overrides.json against FUN_007cf200.
    float minX = 0.0f;
    float minY = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float maxZ = 0.0f;

    for (uint32_t i = 0; i < s_gather.Count(); i++) {
        const CChunkLiquid* layer = s_gather[i];

        // The pairing and the sign are BuildVertices': x from m_tileX, y from m_tileY, stepping
        // away from the chunk corner. The far corner is therefore the smaller number, so the
        // ends give the minimum and the starts the maximum.
        float x1 = layer->m_origin.x - static_cast<float>(layer->m_tileX) * TILE_STEP;
        float y1 = layer->m_origin.y - static_cast<float>(layer->m_tileY) * TILE_STEP;
        float x0 = layer->m_origin.x - static_cast<float>(layer->m_tileEndX) * TILE_STEP;
        float y0 = layer->m_origin.y - static_cast<float>(layer->m_tileEndY) * TILE_STEP;

        float z0 = layer->m_minHeight;
        float z1 = layer->m_maxHeight;

        if (i) {
            if (x0 > minX) { x0 = minX; }
            if (y0 > minY) { y0 = minY; }
            if (z1 < maxZ) { z1 = maxZ; }
            if (x1 < maxX) { x1 = maxX; }
            if (y1 < maxY) { y1 = maxY; }
            if (z0 > minZ) { z0 = minZ; }
        }

        minX = x0;
        minY = y0;
        minZ = z0;
        maxX = x1;
        maxY = y1;
        maxZ = z1;
    }

    auto instance = static_cast<CInstance*>(SMemAlloc(sizeof(CInstance), __FILE__, __LINE__, 0));

    if (!instance) {
        geometry->Release();

        return;
    }

    new (instance) CInstance();

    instance->m_material = GetMaterial(static_cast<int32_t>(liquid->m_liquidType));
    instance->m_settings = GetMaterialSettings(static_cast<int32_t>(liquid->m_liquidType));
    instance->m_geometry = geometry;
    instance->m_environment = CreateEnvironment(0);

    instance->m_waveManager = GetWaveManager();

    if (instance->m_waveManager) {
        instance->m_waveManager->AddRef();
    }

    // The surface draws at the first layer's chunk corner; everything in it is relative to that.
    C3Vector origin = s_gather[0]->m_origin;

    instance->m_placement.Translate(origin);

    instance->m_sphere.c.x = (maxX + minX) * 0.5f;
    instance->m_sphere.c.y = (maxY + minY) * 0.5f;
    instance->m_sphere.c.z = (maxZ + minZ) * 0.5f;

    instance->m_sphere.r = sqrtf((maxZ - minZ) * (maxZ - minZ)
                               + (maxX - minX) * (maxX - minX)
                               + (maxY - minY) * (maxY - minY)) * 0.5f;

    // Every layer taken now points at the surface and counts towards it.
    for (uint32_t i = 0; i < s_gather.Count(); i++) {
        s_gather[i]->m_surface = instance;

        instance->AddRef();
    }


    // The reference drops the reference CreateSurface itself holds, leaving the layers' own.
    instance->Release();
}

// ref: FUN_007ce390
// One layer's vertices. They come straight out of CChunkLiquid::m_vertices -- which are built by
// BuildVertices and verified -- transformed by the matrix the caller has already positioned for
// this layer. Every attribute the format carries is written and its pointer advanced by the
// stride, so the same body serves any format.
//
// THE TWO TEXCOORDS ARE NOT IN SLOT ORDER, and getting that wrong is why water drew untextured.
// The reference takes TexCoord1's cursor as its seventh argument and TexCoord0's as its eighth --
// FUN_007d4ab0 resolves attribute 6 into local_14 and attribute 7 into local_10, then calls this
// with `&local_10, &local_14`. So the parameters are named for what they CARRY rather than for
// their slot, and the call site passes them crossed to match:
//
//   uvRamp   is TexCoord1: (0, depth) where depth is the liquid type's ramp sampled by the
//            vertex's depth byte. This is what the procedural depth-ramp texture is indexed by.
//   uvTiling is TexCoord0: the real surface coordinate. Ordinary liquid takes the transformed
//            position's x and y times 0.06 -- one texture wrap every 16 and two thirds yards --
//            and a layer whose layout is LVF 1 (magma and slime, which carry their own scrolling
//            coordinates) takes the stored uint16 pair times 3/256 instead.
static void WriteLayerVertices(const CChunkLiquid* layer, const C44Matrix& placement,
                               uint32_t stride, uint8_t** position, uint8_t** normal,
                               uint8_t** color, uint8_t** uvRamp, uint8_t** uvTiling) {
    // 0x00a40410 and 0x00a40414.
    static const float UV_WORLD_SCALE = 0.060000002384185791f;
    static const float UV_STORED_SCALE = 0.01171875f;

    const uint32_t* ramp = LiquidTypeBlock(static_cast<int32_t>(layer->m_liquidType));

    // LVF 1 keeps coordinates with its heights instead of depths, which is what magma and slime use.
    bool stored = layer->m_vertexFormat == 1;
    uint32_t countX = layer->m_tileEndX - layer->m_tileX + 1;
    uint32_t countY = layer->m_tileEndY - layer->m_tileY + 1;

    uint32_t n = countX * countY;

    if (n > CChunkLiquid::MAX_VERTICES) {
        n = CChunkLiquid::MAX_VERTICES;
    }

    for (uint32_t i = 0; i < n; i++) {
        C3Vector p = layer->m_vertices[i] * placement;

        if (*position) {
            auto out = reinterpret_cast<float*>(*position);

            out[0] = p.x;
            out[1] = p.y;
            out[2] = p.z;

            *position += stride;
        }

        // Liquid always faces up, and is always white; the material does the colouring.
        if (*normal) {
            auto out = reinterpret_cast<float*>(*normal);

            out[0] = 0.0f;
            out[1] = 0.0f;
            out[2] = 1.0f;

            *normal += stride;
        }

        if (*color) {
            *reinterpret_cast<uint32_t*>(*color) = 0xffffffffu;

            *color += stride;
        }

        // TexCoord1: x is always zero and y is the depth, so the ramp texture is read as the 1-D
        // lookup it is. A type with no ramp writes a zero pair, which samples the ramp's shallow end.
        if (*uvRamp) {
            auto out = reinterpret_cast<uint32_t*>(*uvRamp);

            out[0] = 0;
            out[1] = ramp ? ramp[layer->m_vertexData->GetDepth(i)] : 0;

            *uvRamp += stride;
        }

        // TexCoord0: the surface coordinate the water texture actually tiles across.
        if (*uvTiling) {
            auto out = reinterpret_cast<float*>(*uvTiling);

            if (!stored) {
                out[0] = p.x * UV_WORLD_SCALE;
                out[1] = p.y * UV_WORLD_SCALE;
            } else {
                // A PAIR OF UINT16, not of bytes: the reference reads the block back through a
                // ushort* and takes both entries unsigned. The interface hands out bytes because
                // that is what the file stores them as.
                auto coord = reinterpret_cast<const uint16_t*>(layer->m_vertexData->GetCoord(i));

                out[0] = static_cast<float>(coord[0]) * UV_STORED_SCALE;
                out[1] = static_cast<float>(coord[1]) * UV_STORED_SCALE;
            }

            *uvTiling += stride;
        }
    }
}

// ref: FUN_007ce270
// One layer's indices, as a TRIANGLE STRIP. This is the part that is easy to get wrong: the
// allocation is six indices a tile, which is what a triangle LIST would need, but what is written
// is a strip -- and the strip is stitched across gaps with degenerate triangles, because a layer
// only covers the tiles CoversTile says are wet. So the count written is almost never the count
// allocated, and the batch's count is measured as it goes rather than computed up front.
static void WriteLayerIndices(CChunkLiquid* layer, uint16_t* out, uint16_t base, CGxBatch* batch) {
    uint16_t* start = out;

    bool inStrip = false;
    uint16_t other = 0;
    uint16_t row = static_cast<uint16_t>(layer->m_tileEndY - layer->m_tileY + base);

    for (uint32_t x = layer->m_tileX; x < layer->m_tileEndX; x++) {
        uint16_t next = row + 1;
        uint16_t rowFar = static_cast<uint16_t>(row + 2);

        for (uint32_t y = layer->m_tileY; y < layer->m_tileEndY; y++) {
            if (!layer->CoversTile(y, x)) {
                if (inStrip) {
                    inStrip = false;

                    *out++ = other;
                }
            } else {
                if (!inStrip) {
                    // Restart the strip: two degenerate entries, then the far row's vertex.
                    *out++ = base;
                    *out++ = base;
                    *out++ = next;

                    inStrip = true;
                }

                *out++ = static_cast<uint16_t>(base + 1);

                other = rowFar;

                *out++ = other;
            }

            next++;
            rowFar++;
            base++;
        }

        if (inStrip) {
            *out++ = other;

            inStrip = false;
        }

        base++;
        row = next;
    }

    batch->m_count += static_cast<uint32_t>(out - start);
}

// ref: FUN_007d4ab0
// The surface's geometry. Rebuilt only when it has to be: the pair from last time is handed back
// untouched when both buffers still say they hold data, nothing has marked the factory dirty, and
// the format has not changed.
int32_t CChunkGeomFactory::Build(EGxVertexBufferFormat format, CGxBuf** vertexBuf,
                                 CGxBuf** indexBuf, CGxBatch* batch) {
    CGxBuf* heldVertexBuf = this->m_vertexBlock ? this->m_vertexBlock->buf : nullptr;
    CGxBuf* heldIndexBuf = this->m_indexBlock ? this->m_indexBlock->buf : nullptr;

    bool cached = heldVertexBuf && heldIndexBuf
               && heldVertexBuf->unk1C && heldVertexBuf->unk1D
               && heldIndexBuf->unk1C && heldIndexBuf->unk1D
               && !this->m_dirty && this->m_builtFormat == static_cast<uint32_t>(format);

    if (cached) {
        *vertexBuf = heldVertexBuf;
        *indexBuf = heldIndexBuf;
        *batch = this->m_batch;

        return 1;
    }

    // Counting pass. The index figure is the worst case -- six a tile, as a list would need --
    // because the strip's real length is not known until the dry tiles have been skipped.
    uint32_t totalVertices = 0;
    uint32_t maxIndices = 0;

    static TSGrowableArray<uint16_t> s_perLayerVertices;

    s_perLayerVertices.SetCount(0);

    for (uint32_t i = 0; i < this->m_layers.Count(); i++) {
        const CChunkLiquid* layer = this->m_layers[i];

        uint32_t spanX = layer->m_tileEndX - layer->m_tileX;
        uint32_t spanY = layer->m_tileEndY - layer->m_tileY;

        uint16_t vertices = static_cast<uint16_t>((spanX + 1) * (spanY + 1));

        s_perLayerVertices.Add(1, &vertices);

        totalVertices += vertices;
        maxIndices += spanX * spanY * 6;
    }

    if (!totalVertices || !maxIndices) {
        return 0;
    }

    uint32_t stride = GxVertexBufferFormatSize(format);

    // DIVERGED, but no longer in a way that changes behaviour: the reference takes its buffer pair
    // from a pool of its own keyed on the exact byte sizes (FUN_007cf140 walks a free list for
    // stride*vertices and indices*2, FUN_007cefd0 makes one when nothing fits). Frozen takes a block
    // from VBBList, which is the map's own allocator and gives each surface a pool and a buffer to
    // itself, exactly as CMapObjGroup::AcquireLiquidBuffers does for the map objects' water.
    //
    // What this MUST NOT do is call BufStream, which is what it used to do. That returns the
    // device's single shared stream buffer, so every surface got the same one and the last writer
    // won -- which is what put green shrapnel across the scene the first frame liquid ever drew.
    //
    // The size is re-checked rather than allocated once, because the layer set behind a factory can
    // grow: a block sized for two chunks and then written for five overruns its buffer.
    if (this->m_vertexBlock && (this->m_blockVertices < totalVertices
                                || this->m_builtFormat != static_cast<uint32_t>(format))) {
        VBBList::s_vertexList.Free(this->m_vertexBlock);

        this->m_vertexBlock = nullptr;
    }

    if (this->m_indexBlock && this->m_blockIndices < maxIndices) {
        VBBList::s_indexList.Free(this->m_indexBlock);

        this->m_indexBlock = nullptr;
    }

    if (!this->m_vertexBlock) {
        VBBList::s_vertexList.Alloc(&this->m_vertexBlock, stride, totalVertices);

        this->m_blockVertices = totalVertices;

        if (this->m_vertexBlock && this->m_vertexBlock->buf) {
            this->m_vertexBlock->buf->unk1C = 0;
        }
    }

    if (!this->m_indexBlock) {
        VBBList::s_indexList.Alloc(&this->m_indexBlock, 2, maxIndices);

        this->m_blockIndices = maxIndices;

        if (this->m_indexBlock && this->m_indexBlock->buf) {
            this->m_indexBlock->buf->unk1C = 0;
        }
    }

    if (!this->m_vertexBlock || !this->m_vertexBlock->buf
            || !this->m_indexBlock || !this->m_indexBlock->buf) {
        return 0;
    }

    heldVertexBuf = this->m_vertexBlock->buf;
    heldIndexBuf = this->m_indexBlock->buf;

    *vertexBuf = heldVertexBuf;
    *indexBuf = heldIndexBuf;

    if (this->m_dirty) {
        heldVertexBuf->unk1C = 0;
        heldIndexBuf->unk1C = 0;
    }

    if (!heldVertexBuf->unk1C || !heldVertexBuf->unk1D) {
        auto locked = reinterpret_cast<uint8_t*>(GxBufLock(heldVertexBuf));

        if (locked) {
            uint8_t* position = locked + GxVertexAttribOffset(format, GxVA_Position);
            uint8_t* normal = nullptr;
            uint8_t* color = nullptr;
            uint8_t* uv0 = nullptr;
            uint8_t* uv1 = nullptr;

            // An attribute the format does not carry reads -1 out of the offset table, which is
            // the same test the reference makes before it takes each pointer.
            if (Buffer::s_vertexBufOffset[format][GxVA_Normal] >= 0) {
                normal = locked + static_cast<uint32_t>(Buffer::s_vertexBufOffset[format][GxVA_Normal]);
            }

            if (Buffer::s_vertexBufOffset[format][GxVA_Color0] >= 0) {
                color = locked + static_cast<uint32_t>(Buffer::s_vertexBufOffset[format][GxVA_Color0]);
            }

            if (Buffer::s_vertexBufOffset[format][GxVA_TexCoord0] >= 0) {
                uv0 = locked + static_cast<uint32_t>(Buffer::s_vertexBufOffset[format][GxVA_TexCoord0]);
            }

            if (Buffer::s_vertexBufOffset[format][GxVA_TexCoord1] >= 0) {
                uv1 = locked + static_cast<uint32_t>(Buffer::s_vertexBufOffset[format][GxVA_TexCoord1]);
            }

            // Every layer draws relative to the FIRST one, so a merged surface has a single
            // origin rather than one per chunk.
            const CChunkLiquid* first = this->m_layers[0];

            for (uint32_t i = 0; i < this->m_layers.Count(); i++) {
                const CChunkLiquid* layer = this->m_layers[i];

                C44Matrix placement = this->m_placement;

                C3Vector offset = {
                    layer->m_origin.x - first->m_origin.x,
                    layer->m_origin.y - first->m_origin.y,
                    layer->m_origin.z - first->m_origin.z
                };

                placement.Translate(offset);

                // CROSSED DELIBERATELY: the ramp cursor is TexCoord1 and the tiling cursor is
                // TexCoord0, which is the order the reference passes them in.
                WriteLayerVertices(layer, placement, stride, &position, &normal, &color,
                                   &uv1, &uv0);
            }

            GxBufUnlock(heldVertexBuf, 0);

            heldVertexBuf->unk1C = 1;
        }
    }

    if (!heldIndexBuf->unk1C || !heldIndexBuf->unk1D) {
        batch->m_primType = GxPrim_TriangleStrip;
        batch->m_start = 0;
        batch->m_count = 0;
        batch->m_minIndex = 0;
        batch->m_maxIndex = static_cast<uint16_t>(totalVertices - 1);

        auto locked = reinterpret_cast<uint16_t*>(GxBufLock(heldIndexBuf));

        if (locked) {
            uint16_t base = 0;

            for (uint32_t i = 0; i < this->m_layers.Count(); i++) {
                WriteLayerIndices(this->m_layers[i], locked + batch->m_count, base, batch);

                base = static_cast<uint16_t>(base + s_perLayerVertices[i]);
            }

            GxBufUnlock(heldIndexBuf, 0);

            heldIndexBuf->unk1C = 1;
        }

        this->m_batch = *batch;
    } else {
        *batch = this->m_batch;
    }

    this->m_builtFormat = static_cast<uint32_t>(format);
    this->m_dirty = 0;

    return 1;
}

// The two buckets. The reference keeps them inside a manager object it reaches through a global
// accessor; frozen has no other user for that object, so the buckets are the module's own.
static TSGrowableArray<CInstance*> s_buckets[BUCKET_COUNT];

// ref: FUN_008a20c0
void Add(CInstance* instance) {
    if (!instance || instance->m_queued) {
        return;
    }

    // Procedural water is bucket 1, everything else bucket 0.
    uint32_t bucket = (instance->m_settings && instance->m_settings->m_procedural) ? 1 : 0;

    instance->m_queued = 1;

    s_buckets[bucket].Add(1, &instance);
}

uint32_t Queued(uint32_t bucket) {
    return bucket < BUCKET_COUNT ? s_buckets[bucket].Count() : 0;
}

// Part of ref: FUN_008a2240 -- the comparators at 0x008a1980 and 0x008a19e0.
// Surfaces are ordered by material, then settings, then geometry, so runs that share device state
// draw together. There is a fourth key, and it is worth writing down that it does nothing: both
// comparators finish on the float at the instance's +0x50, which is the placement matrix's last
// element, and CreateSurface sets that to 1.0 for every surface. The two comparators differ ONLY
// in the direction of that key, so as the reference builds instances the two buckets sort
// identically. Reproduced as it is rather than repaired, because a distance sort would be an
// invention -- but if a transparent bucket ever needs back-to-front, this is where it goes.
static int SortAscending(const void* a, const void* b) {
    auto left = *static_cast<CInstance* const*>(a);
    auto right = *static_cast<CInstance* const*>(b);

    if (left->m_material != right->m_material) {
        return left->m_material < right->m_material ? -1 : 1;
    }

    if (left->m_settings != right->m_settings) {
        return left->m_settings < right->m_settings ? -1 : 1;
    }

    if (left->m_geometry != right->m_geometry) {
        return left->m_geometry < right->m_geometry ? -1 : 1;
    }

    if (left->m_placement.d3 != right->m_placement.d3) {
        return left->m_placement.d3 < right->m_placement.d3 ? -1 : 1;
    }

    return 0;
}

static int SortDescending(const void* a, const void* b) {
    auto left = *static_cast<CInstance* const*>(a);
    auto right = *static_cast<CInstance* const*>(b);

    if (left->m_material != right->m_material) {
        return left->m_material < right->m_material ? -1 : 1;
    }

    if (left->m_settings != right->m_settings) {
        return left->m_settings < right->m_settings ? -1 : 1;
    }

    if (left->m_geometry != right->m_geometry) {
        return left->m_geometry < right->m_geometry ? -1 : 1;
    }

    // The only difference from the ascending one.
    if (left->m_placement.d3 != right->m_placement.d3) {
        return right->m_placement.d3 < left->m_placement.d3 ? -1 : 1;
    }

    return 0;
}

// ref: FUN_008a2240
// One bucket's worth, sorted so shared state draws together, then emptied.
//
// The four device lights are SAVED here and put back at the end -- they are not disabled, which is
// what this comment used to claim. The materials are free to set lights while they draw, and the
// restore undoes that for the passes after this one. Only the slots that were ENABLED come back,
// because the getter copies nothing for a slot that was off.
void Draw(const C3Vector& cameraPos, uint32_t bucket) {
    if (bucket >= BUCKET_COUNT) {
        return;
    }

    CGxLight saved[4];

    for (uint32_t i = 0; i < 4; i++) {
        g_theGxDevicePtr->LightGet(i, saved[i]);
    }

    TSGrowableArray<CInstance*>& queue = s_buckets[bucket];

    if (queue.Count()) {
        // The comparator comes from a two-entry table indexed by the bucket.
        qsort(&queue[0], queue.Count(), sizeof(CInstance*),
              bucket ? SortDescending : SortAscending);

        for (uint32_t i = 0; i < queue.Count(); i++) {
            CInstance* instance = queue[i];

            if (instance->m_material) {
                instance->m_material->Draw(instance->m_environment, instance->m_geometry,
                                           instance->m_waveManager, cameraPos, &instance->m_placement,
                                           &instance->m_sphere, instance->m_settings);
            }

            instance->m_queued = 0;
        }
    }

    // The reference frees the bucket's backing array outright rather than keeping it for next
    // frame; SetCount(0) keeps the allocation, which is the same behaviour with less churn.
    queue.SetCount(0);

    for (uint32_t i = 0; i < 4; i++) {
        if (saved[i].m_flags & 1) {
            C3Vector zero = { 0.0f, 0.0f, 0.0f };

            g_theGxDevicePtr->LightSet(i, saved[i], zero);
            g_theGxDevicePtr->LightEnable(i, 1);
        }
    }
}

// ref: FUN_007d4f40
// The lighting a surface draws with. Two switches and then the scene's own lights on top, which is
// the same shape CMap::SetupChunkLighting uses for terrain -- liquid is not a separate lighting
// path, it just asks for its fog from a different half of the day/night block.
void CClientEnvironment::SetupLighting(CM2Lighting* lighting) {
    // DIVERGED, twice, and in the same way CMap::SetupChunkLighting already diverges.
    //
    // The reference reads the fog straight out of the day/night block: the outdoor colour from the
    // three bytes at +0x8c..+0x8e times the 1/255 at 0x00a45564, with the three floats at +0x90,
    // +0x94 and +0x98, or the INDOOR set at +0xa0..+0xa2 and +0xa4/+0xa8/+0xac when m_indoor is
    // set. Frozen keeps its fog computed on CWorld rather than as a day/night struct and has no
    // indoor pair at all, so both branches take the same values and the third float -- the fog
    // density -- has no source. Wiring an indoor fog pair is its own change; the offsets above are
    // what it needs.
    lighting->SetFog(CWorld::GetFogColor(), CWorld::GetFogStart(), CWorld::GetFogEnd());

    if (!this->m_fixedLight) {
        // The map's own outdoor light. The reference adds the CM2Light living at the map light
        // block's +0x58; frozen models that light as an ambient and a directional term instead,
        // exactly as CMap::SetupChunkLighting does, so this is the same divergence already taken
        // for terrain rather than a new one.
        lighting->AddAmbient(CWorld::GetOutdoorAmbient());
        lighting->AddDiffuse(CWorld::GetOutdoorDiffuse(), CWorld::GetOutdoorDirection());
    } else {
        // A fixed white light straight down. The reference builds one CM2Light for this once and
        // keeps it: type 0, direction (0, 0, -1), white diffuse, visible. Nothing frozen has
        // reaches this branch -- CreateSurface passes 0 -- but it costs nothing to be right.
        C3Vector white = { 1.0f, 1.0f, 1.0f };
        C3Vector down = { 0.0f, 0.0f, -1.0f };

        lighting->AddDiffuse(white, down);
    }

    // Then the nearby lights the scene knows about, which is what makes a torch show on water.
    auto scene = CWorld::GetM2Scene();

    if (scene) {
        scene->SelectLights(lighting);
    }
}

// ------------------------------------------------------------------------------------------------
// Liquid::CWaveManager

// How long a wave fades in, and out. Both 5000 ms (DAT_00af1694 and DAT_00af1690).
static const float WAVE_FADE_IN_MS = 5000.0f;
static const float WAVE_FADE_OUT_MS = 5000.0f;

// Where a wave is born: this far along the camera's forward, plus up to fifty more, and then jittered
// by up to fifty in each axis. DAT_009f989c.
static const float WAVE_SPAWN_DISTANCE = 150.0f;

// The randomised per-wave scalars, each `rand() % 1000 * k + base`.
static const float WAVE_S0_K = 0.03999999910593033f;      // 10 .. 50
static const float WAVE_S0_B = 10.0f;
static const float WAVE_S1_K = 0.20000000298023224f;      // 200 .. 400
static const float WAVE_S1_B = 200.0f;
static const float WAVE_AMP_K = 0.0005000000237487257f;   // 0.5 .. 1.0
static const float WAVE_AMP_B = 0.5f;
static const float WAVE_RATE_K = 3.000000106112566e-05f;  // 0.02 .. 0.05
static const float WAVE_RATE_B = 0.019999999552965164f;

// The turn applied to a new wave's direction: rand() % 2000 - 1000, times this, so +/- 45 degrees.
static const float WAVE_TURN_K = 0.0007853981805965304f;
static const float WAVE_TURN_B = 1000.0f;

// A wave lives this long, plus up to as much again.
static const uint32_t WAVE_LIFE_MS = 20000;

void CWaveManager::AddRef() {
    this->m_refCount++;
}

// ref: FUN_007d6210
void CWaveManager::Release() {
    if (--this->m_refCount) {
        return;
    }

    this->~CWaveManager();

    SMemFree(this, __FILE__, __LINE__, 0);
}

// ref: FUN_007d62a0
void CWaveManager::Update(const C3Vector& cameraPos) {
    // Once a frame, however many layers ask. The reference keeps its own stamp beside the manager
    // rather than on it, because the manager is shared.
    static uint32_t s_lastFrame = 0xffffffff;
    static uint32_t s_lastTime = 0;
    static bool s_seeded = false;

    if (s_lastFrame == CWorldScene::s_frameStamp) {
        return;
    }

    s_lastFrame = CWorldScene::s_frameStamp;

    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());

    if (!s_seeded) {
        s_seeded = true;
        s_lastTime = now;
    }

    uint32_t dt = now - s_lastTime;

    s_lastTime = now;

    for (uint32_t i = 0; i < WAVE_COUNT; i++) {
        this->m_elapsed[i] += dt;

        // Fade in over the first five seconds, and out over the last five.
        float fadeIn = static_cast<float>(this->m_elapsed[i]) / WAVE_FADE_IN_MS;

        if (fadeIn > 1.0f) {
            fadeIn = 1.0f;
        }

        float fadeOut = 0.0f;

        if (this->m_elapsed[i] < this->m_period[i]) {
            fadeOut = static_cast<float>(this->m_period[i] - this->m_elapsed[i]) / WAVE_FADE_OUT_MS;

            if (fadeOut > 1.0f) {
                fadeOut = 1.0f;
            }
        }

        float fade = fadeIn < fadeOut ? fadeIn : fadeOut;

        if (fade < 0.0f) {
            fade = 0.0f;
        }

        // The exposed record is the working one with its amplitude faded. Index 6 is that amplitude.
        this->m_wavesB[i][6] = fade * this->m_working[i][6];

        if (this->m_elapsed[i] < this->m_period[i]) {
            continue;
        }

        // Time is up: respawn it.
        this->m_elapsed[i] = 0;
        this->m_period[i] = static_cast<uint32_t>(rand() % WAVE_LIFE_MS) + WAVE_LIFE_MS;

        // Out along the camera's forward -- the direction from the camera to what it is looking at.
        // 0x00cd8f68, the vector right after the camera position -- what it is looking at.
        const C3Vector& target = CWorldScene::s_cameraTarget;

        float forwardX = target.x - cameraPos.x;
        float forwardY = target.y - cameraPos.y;
        float forwardZ = target.z - cameraPos.z;

        float len2 = forwardX * forwardX + forwardY * forwardY + forwardZ * forwardZ;

        if (len2 > 9.99999997475243e-07f) {
            float inv = 1.0f / sqrtf(len2);

            forwardX *= inv;
            forwardY *= inv;
        }

        float out = static_cast<float>(rand() % 50) + WAVE_SPAWN_DISTANCE;

        float posX = cameraPos.x + out * forwardX + static_cast<float>(rand() % 100 - 50);
        float posY = cameraPos.y + out * forwardY + static_cast<float>(rand() % 100 - 50);

        this->m_working[i][0] = posX;
        this->m_working[i][1] = posY;

        // Its direction starts pointing back at the camera, then turns by up to 45 degrees.
        float dirX = cameraPos.x - posX;
        float dirY = cameraPos.y - posY;

        float dirLen2 = dirX * dirX + dirY * dirY;

        if (dirLen2 > 9.99999997475243e-07f) {
            float inv = 1.0f / sqrtf(dirLen2);

            dirX *= inv;
            dirY *= inv;
        }

        float turn = (static_cast<float>(rand() % 2000) - WAVE_TURN_B) * WAVE_TURN_K;

        C44Matrix rotation = C44Matrix::RotationAroundZ(turn);

        C3Vector direction = { dirX, dirY, 0.0f };

        direction = direction * rotation;

        this->m_working[i][2] = direction.x;
        this->m_working[i][3] = direction.y;

        this->m_working[i][4] = static_cast<float>(rand() % 1000) * WAVE_S0_K + WAVE_S0_B;
        this->m_working[i][5] = static_cast<float>(rand() % 1000) * WAVE_S1_K + WAVE_S1_B;
        this->m_working[i][6] = static_cast<float>(rand() % 1000) * WAVE_AMP_K + WAVE_AMP_B;
        this->m_working[i][7] = static_cast<float>(rand() % 1000) * WAVE_RATE_K + WAVE_RATE_B;

        for (uint32_t k = 0; k < 8; k++) {
            this->m_wavesB[i][k] = this->m_working[i][k];
        }

        // A fresh wave starts silent and fades in.
        this->m_wavesB[i][6] = 0.0f;
    }
}

// DIVERGED: the reference makes this for the first CChunkLiquid that needs one, inside that
// constructor, and counts uses there. frozen makes it for the first SURFACE instead. The manager is
// a singleton that lives as long as any water does, so the only difference is which object's
// lifetime it is tied to, and nothing observes that.
CWaveManager* GetWaveManager() {
    static CWaveManager* s_manager = nullptr;

    if (!s_manager) {
        auto manager = static_cast<CWaveManager*>(
            SMemAlloc(sizeof(CWaveManager), __FILE__, __LINE__, 0));

        if (!manager) {
            return nullptr;
        }

        new (manager) CWaveManager();

        s_manager = manager;
    }

    return s_manager;
}

}

// Reopened: everything in this file lives in the Liquid namespace, the same as the reference's own
// ".?AVCMeshGeomFactory@Liquid@@".
namespace Liquid {
// ref: FUN_007d49b0
// Make a factory for one map-object group. The reference splits this in two -- FUN_007d4920 does the
// allocation and the field fill, and this sets the two pointers afterwards -- because the allocator
// half is the class's operator new and is shared with the placement the vtable's destructor slot
// unwinds. frozen folds them, since its allocation is a plain SMemAlloc.
//
// DIVERGENCE: the reference takes the block from CDataAllocator::GetData under the RTTI name
// ".?AVCMeshGeomFactory@Liquid@@" and gives it back with PutData. frozen has no CDataAllocator and
// its sibling CChunkGeomFactory already uses SMemAlloc/SMemFree, so this matches the sibling.
CMeshGeomFactory* CMeshGeomFactory::Create(CMapObj* mapObj, CMapObjGroup* group) {
    auto factory = static_cast<CMeshGeomFactory*>(
        SMemAlloc(sizeof(CMeshGeomFactory), __FILE__, __LINE__, 0));

    if (!factory) {
        return nullptr;
    }

    new (factory) CMeshGeomFactory();

    factory->m_mapObj = mapObj;
    factory->m_group = group;

    return factory;
}

// ref: FUN_007d43b0
// Drop a reference and tear down at zero, the same shape as CChunkGeomFactory::Release. The
// reference reaches its destructor through vtable slot 0 with a zero argument -- the scalar-deleting
// form -- and then hands the block back; frozen destructs directly and frees.
void CMeshGeomFactory::Release() {
    if (--this->m_refCount) {
        return;
    }

    this->~CMeshGeomFactory();

    SMemFree(this, __FILE__, __LINE__, 0);
}

// ref: FUN_007d43e0
void CMeshGeomFactory::SetDiffColor(const uint32_t* diffColor) {
    this->m_diffColor = *diffColor;
}

// ref: FUN_007d4360
void CMeshGeomFactory::SetLvf(int32_t lvf) {
    this->m_lvf = lvf;
}

// ref: FUN_007d4370
void CMeshGeomFactory::SetFixedLight(float fixedLight) {
    this->m_fixedLight = fixedLight;
}

// ref: FUN_007d43f0
// Build the map-object water surface, or hand back what was built last time. This is where the whole
// liquid chain comes together: the tile count, the buffer pair, the two grid writers and the tile
// emitter.
//
// THE VERTEX COUNT is the grid plus six per tile that carries its own geometry -- the grid extents
// taking the factory's two extra-extent fields, which nothing ever writes, so in practice it is the
// group's own grid. Indices are budgeted at three per vertex, which the strip never exceeds.
//
// THE CACHE HIT is both buffers reporting their two ready flags. On a hit it copies the batch out of
// the holder and returns without locking anything -- the holder points at the group's stored batch,
// which is what makes that copy meaningful.
//
// THE UNLOCKS RUN ON THE SUCCESS PATH, and this is the one place the decompilation actively misleads:
// its brace structure puts the batch fill before the unlocks with a goto past them, which would leave
// both buffers locked and nothing drawable. The disassembly settles it -- the unlock at 0x7d4685
// follows the EmitLiquidTiles call at 0x7d466f directly, and the batch fill comes after. Porting the
// decompilation's shape would have produced a client that built water once and then drew nothing.
//
// The identity matrix is built twice, once before each writer, which is the reference's own shape --
// so for map-object water the vertex positions and the computed texcoord path both pass through
// untransformed, and the group's placement is applied somewhere else.
//
// The reference returns void where frozen's IGeomFactory::Build returns int32_t, so this answers 1
// when the batch is usable and 0 when a lock failed -- which is what its sibling CChunkGeomFactory
// does.
int32_t CMeshGeomFactory::Build(EGxVertexBufferFormat format, CGxBuf** vertexBuf,
                               CGxBuf** indexBuf, CGxBatch* batch) {
    CMapObjGroup* group = this->m_group;

    if (group->m_liquidMaterial >= this->m_mapObj->m_materialCount) {
        SErrDisplayAppFatal("Water in chunk \"%s\" of object \"%s\" has no materialId.",
                            group->m_name, this->m_mapObj->m_name);
    }

    group->LiquidTileCount();

    uint32_t vertexCount =
        (group->m_liquidYVerts + static_cast<uint32_t>(this->m_extraYVerts))
        * (group->m_liquidXVerts + static_cast<uint32_t>(this->m_extraXVerts))
        + static_cast<uint32_t>(group->m_liquidTileCount) * 6;

    group->AcquireLiquidBuffers(format, vertexCount, vertexCount * 3, &this->m_bufferHolder);

    *vertexBuf = group->m_liquidVertexBuf->buf;
    *indexBuf = group->m_liquidIndexBuf->buf;

    auto cached = static_cast<CGxBatch*>(this->m_bufferHolder);

    if ((*vertexBuf)->unk1C && (*vertexBuf)->unk1D && (*indexBuf)->unk1C && (*indexBuf)->unk1D) {
        *batch = *cached;

        return 1;
    }

    char* vertexData = GxBufLock(*vertexBuf);
    char* indexData = GxBufLock(*indexBuf);

    if (!vertexData || !indexData) {
        if (vertexData) {
            GxBufUnlock(*vertexBuf, 0);
            (*vertexBuf)->unk1C = 1;
        }

        if (indexData) {
            GxBufUnlock(*indexBuf, 0);
            (*indexBuf)->unk1C = 1;
        }

        return 0;
    }

    int32_t stride = static_cast<int32_t>(GxVertexBufferFormatSize(format));

    uint8_t* cursor[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };

    static const EGxVertexAttrib kAttribs[5] = {
        static_cast<EGxVertexAttrib>(0), static_cast<EGxVertexAttrib>(3),
        static_cast<EGxVertexAttrib>(4), static_cast<EGxVertexAttrib>(6),
        static_cast<EGxVertexAttrib>(7)
    };

    // Attribute 0 is unconditional; the other four only exist when the format carries them, and a
    // null cursor is how the writers are told to skip one.
    cursor[0] = reinterpret_cast<uint8_t*>(vertexData + GxVertexAttribOffset(format, kAttribs[0]));

    for (int32_t i = 1; i < 5; i++) {
        if (GxVertexBufferFormatHasAttrib(format, kAttribs[i])) {
            cursor[i] = reinterpret_cast<uint8_t*>(
                vertexData + GxVertexAttribOffset(format, kAttribs[i]));
        }
    }

    auto indexBase = reinterpret_cast<uint16_t*>(indexData);
    uint16_t* indexCursor = indexBase;

    C44Matrix identity(1.0f, 0.0f, 0.0f, 0.0f,
                      0.0f, 1.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 1.0f, 0.0f,
                      0.0f, 0.0f, 0.0f, 1.0f);

    int32_t gridVertices = WriteLiquidGridVertices(group, identity, &this->m_diffColor, this->m_lvf,
                                                   static_cast<uint32_t>(this->m_fixedLight),
                                                   this->m_seams, stride, &cursor[0], &cursor[1],
                                                   &cursor[2], &cursor[3], &cursor[4]);

    WriteLiquidIndices(group, this->m_seams, &indexCursor, 0);

    int32_t tileVertices = EmitLiquidTiles(this->m_mapObj, group, identity, &this->m_diffColor,
                                          this->m_lvf,
                                          static_cast<uint32_t>(this->m_fixedLight), stride,
                                          &cursor[0], &cursor[1], &cursor[2], &cursor[3],
                                          &cursor[4], &indexCursor, gridVertices);

    GxBufUnlock(*vertexBuf, 0);
    (*vertexBuf)->unk1C = 1;

    GxBufUnlock(*indexBuf, 0);
    (*indexBuf)->unk1C = 1;

    cached->m_primType = GxPrim_TriangleStrip;
    cached->m_start = 0;
    cached->m_count = static_cast<uint32_t>(indexCursor - indexBase);
    cached->m_minIndex = 0;
    cached->m_maxIndex = static_cast<uint16_t>(gridVertices + tileVertices - 1);

    *batch = *cached;

    return 1;
}


// ref: FUN_007a7920
// The liquid grid's INDEX writer, and it is a TRIANGLE STRIP -- which resolves the contradiction
// this file's notes flagged and could not settle. They observed the batch asks for primType 4, a
// strip, while the index budget of three per vertex looked like a list, and said to check it against
// a run before trusting either. No run needed: the emit pattern is unmistakably a strip.
//
//   starting a run writes v0, v0, v1 -- a repeated vertex, which is the standard degenerate
//   triangle used to restart a strip without breaking the draw;
//   each rendering tile then adds just TWO indices, v0+1 and v1+1, because a strip reuses the
//   previous two;
//   a gap, or the end of a row, repeats the last index once to close the run.
//
// So three-per-vertex is an upper bound that covers the restarts, not six-per-quad. A list reading
// would have produced twice the triangles and every other one facing the wrong way.
//
// THE TILE TEST IS THE COMPLEMENT of the one in CMapObjGroup::LiquidTileCount, and the pair is not a
// contradiction. That one counts tiles with bit 0x80 SET, and those are what the vertex count adds
// six vertices each for -- tiles carrying their own geometry. This one emits grid quads for tiles
// with 0x80 CLEAR, which share the grid's vertices. Both also skip a low nibble of 0xF, which means
// no liquid in that tile at all.
//
// The row stride includes the duplicated columns, so it is m_liquidXVerts plus that list's length
// rather than the grid width -- which is why the stride is computed once outside the walk.
//
// ONE TRANSCRIBED ODDITY: the reference computes the lower row as baseVertex + stride + v0, and v0
// already contains baseVertex, so a non-zero baseVertex is counted twice. Kept as written because
// every caller passes zero -- the liquid grid starts at the front of its own buffer -- so the
// expression is only reachable in its correct form. Worth knowing before anyone reuses this with a
// real offset.
void WriteLiquidIndices(CMapObjGroup* group, const LiquidSeams& seams, uint16_t** cursor,
                        uint32_t baseVertex) {
    auto emit = [cursor](uint32_t index) {
        **cursor = static_cast<uint16_t>(index);
        *cursor += 1;
    };

    uint32_t rowStride = group->m_liquidXVerts + seams.dupColumns.Count();

    uint32_t lastIndex = 0;
    bool inStrip = false;
    uint32_t emittedRow = 0;
    uint32_t dupRowCursor = 0;

    for (uint32_t tileY = 0; tileY < group->m_liquidYTiles; tileY++) {
        uint32_t rowRepeat = 1;

        if (dupRowCursor < seams.dupRows.Count()
                && seams.dupRows[dupRowCursor] == tileY) {
            rowRepeat = 2;
            dupRowCursor++;
        }

        while (rowRepeat--) {
            uint32_t v0 = (emittedRow * rowStride + baseVertex) & 0xFFFF;
            uint32_t v1 = (baseVertex + rowStride + v0) & 0xFFFF;

            uint32_t dupColumnCursor = 0;

            for (uint32_t tileX = 0; tileX < group->m_liquidXTiles; tileX++) {
                uint8_t flag = group->m_liquidTiles[group->m_liquidXTiles * tileY + tileX];

                bool renders = (flag & 0xF) != 0xF && !(flag & 0x80);

                uint32_t columnRepeat = 1;

                if (dupColumnCursor < seams.dupColumns.Count()
                        && seams.dupColumns[dupColumnCursor] == tileX) {
                    columnRepeat = 2;
                    dupColumnCursor++;
                }

                uint32_t v2 = (v1 + 1) & 0xFFFF;

                while (columnRepeat--) {
                    if (renders) {
                        if (!inStrip) {
                            inStrip = true;

                            emit(v0);
                            emit(v0);
                            emit(v1);
                        }

                        emit(v0 + 1);
                        emit(v2);

                        lastIndex = v2;
                    } else if (inStrip) {
                        emit(lastIndex);

                        inStrip = false;
                    }

                    v0++;
                    v1++;
                    v2++;
                }
            }

            if (inStrip) {
                emit(lastIndex);

                inStrip = false;
            }

            emittedRow++;
        }
    }
}


// ref: FUN_0079e3c0 (the table-building half of it)
// The two depth ramps the per-vertex writers sample, as floats in 0..1 indexed by a depth byte.
// Ramp 0 saturates at 42 and ramp 1 at 255; see LiquidTypeBlock below for the decode and for which
// liquid types take which.
//
// DIVERGED in shape only: the reference fills two file-scope arrays from its liquid initialise and
// never rebuilds them. Frozen builds them on first use, which needs no initialisation order.
const uint32_t DEPTH_RAMP_COUNT = 2;
const uint32_t DEPTH_RAMP_ENTRIES = 256;

// The saturation depth of each ramp: ramp 0 reaches full at 42, ramp 1 at 255.
const float DEPTH_RAMP_FULL[DEPTH_RAMP_COUNT] = { 42.0f, 255.0f };

const uint32_t* DepthRamp(uint32_t which) {
    static float s_ramps[DEPTH_RAMP_COUNT][DEPTH_RAMP_ENTRIES];
    static bool s_built = false;

    if (!s_built) {
        s_built = true;

        for (uint32_t r = 0; r < DEPTH_RAMP_COUNT; r++) {
            for (uint32_t i = 0; i < DEPTH_RAMP_ENTRIES; i++) {
                float v = static_cast<float>(i) / DEPTH_RAMP_FULL[r];

                s_ramps[r][i] = v > 1.0f ? 1.0f : v;
            }
        }
    }

    // The writers copy the entry as an opaque dword straight into the vertex, which is what the
    // reference does and why the tables are reached as uint32_t rather than float.
    return reinterpret_cast<const uint32_t*>(s_ramps[which]);
}
// ref: FUN_0079b870
// Two DBC hops and a gate, resolving a liquid type to the block the per-vertex writer samples.
// Every offset in the reference landed on a field frozen already names, which is what makes the
// decode trustworthy rather than plausible:
//
//   LiquidTypeRec +0x38 is m_materialID -- it falls exactly after m_particleTexSlots;
//   LiquidMaterialRec +0x04 is m_LVF, so the gate is a VERTEX FORMAT test, admitting only formats
//   0 and 2. The other two formats are drawn some other way, which is consistent with the factory
//   keeping its own `m_LVF == 1` answer in a separate field;
//   LiquidTypeRec +0xa4 is m_int[0], the first of the four trailing ints -- m_texture[6],
//   m_color[2] and m_float[18] account for every byte between it and m_materialID.
//
// THE TWO BLOCKS ARE DEPTH RAMPS, identified 2026-09-27 and no longer a blocker. They are built by
// FUN_0079e3c0, one loop over i = 0..255 filling both 256-float tables, and every constant in it
// resolves:
//
//   0x00cdfbd0[i] = min(i * A / (255 * A), 1) -- the A at 0x00adf800 cancels, so it is i / 255,
//                   and the clamp can never fire. A plain byte-to-unit normalisation.
//   0x00cdf7d0[i] = i * (1/9) <= 14/3 ? i * (1/9) * (3/14) : 1  -- which is min(i / 42, 1). The
//                   same normalisation, saturating at a depth of 42 instead of 255.
//
// The selector at 0x00adfbb4 holds exactly those two pointers, [0] = 0x00cdf7d0 and
// [1] = 0x00cdfbd0, and m_int[0] picks between them -- so LiquidType.dbc's first trailing int
// chooses how quickly a liquid reaches full depth. Everything past the second entry is unrelated
// floats, which is what made the pair countable in the first place.
//
// So the value the per-vertex writers sample is a DEPTH IN 0..1, and the texcoord it becomes
// indexes the procedural depth ramp texture. That is what the second texcoord has always been for.
//
// The tables are generated on first use rather than carried as literals: they are two lines of
// arithmetic and a literal table would be 2KB of magic numbers nobody could check.
const uint32_t* LiquidTypeBlock(int32_t liquidType) {
    if (!liquidType) {
        return nullptr;
    }

    LiquidTypeRec* type = g_liquidTypeDB.GetRecord(liquidType);

    if (!type) {
        return nullptr;
    }

    LiquidMaterialRec* material = g_liquidMaterialDB.GetRecord(type->m_materialID);

    if (!material) {
        return nullptr;
    }

    if (material->m_LVF != 0 && material->m_LVF != 2) {
        return nullptr;
    }

    uint32_t which = static_cast<uint32_t>(type->m_int[0]);

    if (which >= DEPTH_RAMP_COUNT) {
        return nullptr;
    }

    return DepthRamp(which);
}


// ref: FUN_007d9230
// Append one point. No bounds check, which is the reference's own shape -- the emitter that fills
// this knows the grid it is walking cannot produce more than the 32 the array holds.
void MapObjPolyAddPoint(MapObjPolySet* set, const C3Vector& position, const void* value) {
    MapObjPolyPoint* point = &set->points[set->pointCount];

    point->position = position;
    point->value = const_cast<void*>(value);

    set->pointCount++;
}

// ref: FUN_007d9330
// Start a walk at the set's current cursor edge. Both ends begin at that edge's two points, so the
// first step already has somewhere to go in each direction.
//
// THE DONE FLAG IS SET FIRST and only cleared when the cursor actually names an edge, so a walk over
// a set with no edges -- or one whose cursor has run past them -- is immediately finished rather than
// reading a garbage edge. That ordering is the reference's and is the whole guard.
void MapObjPolyBeginWalk(MapObjPolyWalk* walk, MapObjPolySet* set, void* callback) {
    walk->callback = callback;
    walk->set = set;
    walk->back = -1;
    walk->forward = -1;
    walk->steps = 0;
    walk->done = 1;

    if (set->cursor < set->edgeCount) {
        walk->back = set->edges[set->cursor].from;
        walk->forward = set->edges[set->cursor].to;
        walk->done = 0;
    }
}

// ref: FUN_007d9400
// One step, and only ONE END moves per call -- the low bit of the step counter alternates which.
// The back end follows its point's incoming edge to that edge's `from`, and the forward end follows
// its point's outgoing edge to that edge's `to`, so the pair spreads outward from the seed edge in
// both directions at once.
//
// It finishes when the two ends land on the same point, which is the outline closing. The test is at
// the TOP, so the closing step is observed before it is taken rather than after -- which matters
// because a caller reads the ends between calls.
void MapObjPolyAdvance(MapObjPolyWalk* walk) {
    if (walk->forward == walk->back) {
        walk->done = 1;

        return;
    }

    MapObjPolySet* set = walk->set;

    if (!(walk->steps & 1)) {
        int32_t next = set->edges[set->points[walk->back].inEdge].from;

        walk->steps++;
        walk->back = next;

        return;
    }

    int32_t next = set->edges[set->points[walk->forward].outEdge].to;

    walk->steps++;
    walk->forward = next;
}

// ref: FUN_007d9460
// Four bytes in the reference: the done flag, returned raw rather than as a bool.
uint8_t MapObjPolyAtEnd(const MapObjPolyWalk* walk) {
    return walk->done;
}


// ref: FUN_007d9270
// Link the appended points into a closed ring and hand back the wrap count.
//
// The first pass gives point i the outgoing edge i and the incoming edge i - 1, and emits edge i as
// {i, i + 1}. That leaves two deliberate dangling ends: point 0's incoming edge is -1, and the last
// edge points at pointCount, one past the array.
//
// The second pass closes both, and it is the whole reason this function exists. Point 0's incoming
// edge becomes the last edge, and the last edge's `to` is taken MODULO the point count -- which turns
// that one-past-the-end index into 0. The quotient of the same division is what it returns, so a
// well-formed ring returns 1.
//
// Writing the ring open and then closing it, rather than special-casing the last point in the loop,
// is the reference's shape and is kept: the loop stays branch-free and the wrap is arithmetic.
int32_t MapObjPolyCloseOutline(MapObjPolySet* set) {
    for (int32_t i = 0; i < set->pointCount; i++) {
        MapObjPolyPoint* point = &set->points[i];

        point->outEdge = i;
        point->stamp = -1;          // a generation that can never match, so the memo is cold
        point->inEdge = i - 1;

        MapObjPolyEdge* edge = &set->edges[set->edgeCount];

        edge->from = i;
        edge->to = i + 1;
        edge->flag = 0;

        set->edgeCount++;
    }

    if (!set->edgeCount) {
        return 0;
    }

    set->points[0].inEdge = set->edgeCount - 1;

    MapObjPolyEdge* last = &set->edges[set->edgeCount - 1];

    auto to = static_cast<uint32_t>(last->to);
    auto count = static_cast<uint32_t>(set->pointCount);

    last->to = static_cast<int32_t>(to % count);

    return static_cast<int32_t>(to / count);
}

// ref: FUN_007d92f0
// Move the cursor to the first edge still carrying a zero flag, which is where the next walk starts.
// It STOPS on that edge rather than past it, and runs off the end when every edge is flagged -- at
// which case the cursor equals edgeCount and MapObjPolyBeginWalk's bounds test finishes a walk
// immediately. The two functions are written to fit together that way.
void MapObjPolySeekUnflaggedEdge(MapObjPolySet* set) {
    set->cursor = 0;

    while (set->cursor < set->edgeCount) {
        if (!set->edges[set->cursor].flag) {
            return;
        }

        set->cursor++;
    }
}


// ref: FUN_007d9740
// Empty the set. It clears only each point's POSITION -- not the value, the edge links, or the
// distance memo -- and then the four trailing fields. That is enough because nothing reads a point
// above pointCount, and the two fields that would matter if it did are both self-guarding: the
// memo's stamp cannot match a generation that restarts at 0 without first being written, and the
// edge links are rewritten wholesale by CloseOutline.
//
// Its loop runs 32 times, counting 31 down THROUGH zero, and that is a third independent
// confirmation of the point capacity -- after the append's stride and the edge array's position.
void MapObjPolyReset(MapObjPolySet* set) {
    for (int32_t i = 0; i < 32; i++) {
        set->points[i].position.x = 0.0f;
        set->points[i].position.y = 0.0f;
        set->points[i].position.z = 0.0f;
    }

    set->pointCount = 0;
    set->edgeCount = 0;
    set->generation = 0;
    set->cursor = 0;
}


// ref: FUN_007d91f0
// Evaluate one point's value, memoising into it. A stored zero means unevaluated, so a value that
// genuinely folds to zero is recomputed on every touch -- the reference's own cost.
//
// THE ARGUMENT ORDER IS FROM THE DISASSEMBLY, because the decompilation cannot express it. Ghidra
// drops both recursive receivers and renders the fold as `(uVar2, uVar1, operand)` with uVar1
// evaluated first, which reads as though the two children are swapped. The pushes settle it: the
// receiver at 0x7d9203 is +0x08 of the node (childB) and at 0x7d920f it is +0x04 (childA), and
// cdecl's last push is the first argument -- so childA is argument one. The EVALUATION order is the
// reverse of the argument order, which is what made the decompilation look self-inconsistent.
//
// The operand is a FLOAT: the reference loads it with flds and stores it to the argument slot with
// fstps (0x7d91fb, 0x7d9206), where Ghidra shows an integer read of the same slot. A fold taking it
// as an int would get the bit pattern rather than the number.
//
// Recurses on INDICES rather than the reference's pointers, for the reason recorded at the point's
// childA -- the reference's addresses always aim inside this same array.
void* MapObjPolyEval(MapObjPolySet* set, int32_t point, MapObjPolyFold fold) {
    MapObjPolyPoint* p = &set->points[point];

    if (p->value) {
        return p->value;
    }

    // Evaluated child-B-first, as the reference does, then folded child-A-first.
    void* b = MapObjPolyEval(set, p->childB, fold);
    void* a = MapObjPolyEval(set, p->childA, fold);

    p->value = fold(a, b, p->operand);

    return p->value;
}

// ref: FUN_007d9390
// Read whichever end of the walk is current -- the low bit of the step counter picks it, the same bit
// MapObjPolyAdvance uses to decide which end moves, so a caller reading between steps sees the end
// that is about to move.
//
// The reference INLINES the whole of MapObjPolyEval here rather than calling it, down to the same
// child order and the same float operand -- confirmed instruction for instruction at 0x7d93c4
// through 0x7d93df. frozen calls it instead. That trades one call against duplicating a memoising
// recursion, and the computation is identical; the only cost is a call the reference does not make.
void MapObjPolyDeref(const MapObjPolyWalk* walk, C3Vector* position, void** value) {
    int32_t index = (walk->steps & 1) ? walk->forward : walk->back;

    MapObjPolySet* set = walk->set;

    *position = set->points[index].position;

    *value = MapObjPolyEval(set, index, static_cast<MapObjPolyFold>(walk->callback));
}

// ref: FUN_007d9470
// Clip the outline against one plane, in place. This is the piece that makes the whole structure a
// Sutherland-Hodgman clipper, and it is the last of the ten.
//
// For each edge still in play it classifies both endpoints by signed distance, then:
//
//   both strictly inside -- nothing to do;
//   both strictly outside -- FLAG THE EDGE, which retires it from this and every later pass;
//   straddling -- append an interpolated point and remember enough to re-stitch the boundary.
//
// EXACTLY TWO CROSSINGS ARE EXPECTED, which is what a plane does to a convex ring, and the failure
// handling is the interesting part: anything else rolls the appended points back by subtracting the
// crossing count from pointCount and returns without touching the edges. So a degenerate outline is
// left exactly as it was rather than half-clipped. The early break at three crossings exists to keep
// that rollback small, not to salvage the pass.
//
// THE RE-STITCH keeps the inside vertex and replaces the outside one. If the edge's `from` was the
// inside end its `to` is repointed at the new point, and the new point's inEdge becomes that edge
// while its outEdge becomes the closing edge; if `from` was outside, both roles swap. The closing
// edge then runs from the point born on the from-inside crossing to the one born on the other, which
// is what preserves the ring's winding.
//
// The distance memo is keyed on a generation counter bumped on entry, so a point shared by two edges
// is classified once per pass. The reference accesses that counter as an INT in one comparison and as
// a FLOAT in the other -- the same slot, the same bit pattern, and the values are small integers so
// bitwise equality holds either way. frozen keeps it an int32_t throughout, which is equivalent and
// does not invite a denormal into the comparison.
//
// The epsilon is the reference's own, from 0x009ea624: 1.1920929e-07, which is FLT_EPSILON. An edge
// whose two distances sum below it is treated as touching rather than crossing, so no degenerate
// point is appended.
void MapObjPolyClipToPlane(MapObjPolySet* set, const C4Plane& plane, int32_t side) {
    const float kEpsilon = 1.1920929e-07f;

    set->generation++;

    int32_t crossings = 0;

    // Per crossing: which edge straddled, the point it produced, and whether that edge's `from` was
    // the inside end.
    int32_t straddled[2] = { 0, 0 };
    int32_t created[2] = { 0, 0 };
    int32_t fromWasInside[2] = { 0, 0 };

    // The two created points, filed by which crossing they came from, for the closing edge.
    int32_t bornFromInside = 0;
    int32_t bornFromOutside = 0;

    for (int32_t e = 0; e < set->edgeCount; e++) {
        MapObjPolyEdge* edge = &set->edges[e];

        if (edge->flag) {
            continue;
        }

        MapObjPolyPoint* a = &set->points[edge->from];
        MapObjPolyPoint* b = &set->points[edge->to];

        if (a->stamp != set->generation) {
            a->planeDistance = (plane.n.x * a->position.x + plane.n.y * a->position.y
                                + plane.n.z * a->position.z + plane.d) * static_cast<float>(side);
            a->stamp = set->generation;
        }

        if (b->stamp != set->generation) {
            b->planeDistance = (plane.n.x * b->position.x + plane.n.y * b->position.y
                                + plane.n.z * b->position.z + plane.d) * static_cast<float>(side);
            b->stamp = set->generation;
        }

        // Both strictly inside: the edge survives untouched. Written as the reference writes it --
        // NOT (both > 0) -- so a NaN distance falls through to the straddle test rather than here.
        if (a->planeDistance > 0.0f && b->planeDistance > 0.0f) {
            continue;
        }

        if (!(a->planeDistance >= 0.0f) && !(b->planeDistance >= 0.0f)) {
            edge->flag = 1;

            continue;
        }

        float sum = std::fabs(b->planeDistance) + std::fabs(a->planeDistance);

        if (sum < kEpsilon) {
            continue;
        }

        crossings++;

        if (crossings > 2) {
            break;
        }

        float t = std::fabs(a->planeDistance) / sum;

        int32_t aInside = a->planeDistance >= 0.0f ? 1 : 0;
        int32_t slot = crossings - 1;

        int32_t index = set->pointCount;

        straddled[slot] = e;
        created[slot] = index;
        fromWasInside[slot] = aInside;

        if (aInside) {
            bornFromInside = index;
        } else {
            bornFromOutside = index;
        }

        MapObjPolyPoint* np = &set->points[index];

        np->position.x = a->position.x + (b->position.x - a->position.x) * t;
        np->position.y = a->position.y + (b->position.y - a->position.y) * t;
        np->position.z = a->position.z + (b->position.z - a->position.z) * t;

        // Its value interpolates the two endpoints, computed on first read rather than now.
        np->value = nullptr;
        np->childA = edge->from;
        np->childB = edge->to;
        np->operand = t;

        if (aInside) {
            np->inEdge = e;
            np->outEdge = set->edgeCount;
        } else {
            np->outEdge = e;
            np->inEdge = set->edgeCount;
        }

        np->stamp = set->generation;
        np->planeDistance = 0.0f;

        set->pointCount++;
    }

    if (crossings != 2) {
        set->pointCount -= crossings;

        return;
    }

    for (int32_t i = 0; i < 2; i++) {
        MapObjPolyEdge* edge = &set->edges[straddled[i]];

        if (fromWasInside[i]) {
            edge->to = created[i];
        } else {
            edge->from = created[i];
        }
    }

    MapObjPolyEdge* closing = &set->edges[set->edgeCount];

    closing->from = bornFromInside;
    closing->to = bornFromOutside;
    closing->flag = 0;

    set->edgeCount++;
}


// ref: FUN_007a7b00
// Write one liquid vertex. Each attribute goes to its own cursor and ONLY where that cursor is
// non-null, so the caller selects the vertex format by which cursors it passes -- which is why the
// build gates every GxVertexAttribOffset on the format having that attribute.
//
// The normal is the constant (0, 0, 1). Liquid is flat by construction, so there is nothing to
// compute.
//
// THE COLOUR IS BYTE-SWIZZLED when the device wants RGBA. Caps().m_colorFormat of GxCF_rgba means R
// belongs in the low byte, so the reference rebuilds the dword as bytes 2, 1, 0, 3 -- exchanging R
// and B while leaving G and A alone -- and otherwise stores it unchanged. Getting this backwards
// tints every water surface, and it would look plausible rather than broken.
//
// TWO WAYS TO GET THE FIRST TEXCOORD, chosen by the caller:
//
//   from the vertex bytes -- two int16s scaled by 1/256, which is what those bytes are, authored
//   texture coordinates in 0..255;
//   computed -- the position taken RELATIVE to the group's liquid origin and turned by the
//   placement's ROTATION ONLY. The reference copies the whole matrix and then overwrites its last
//   row with (0, 0, 0, 1), which is how it strips the translation, and scales the result by 0.24.
//
// The second texcoord's y is the liquid type's 256-entry table indexed by a byte of the vertex data,
// or zero when the type resolves to nothing. That read is what settled the table's shape.
//
// The eighth argument is unused -- the reference takes it and never touches it. Kept so the signature
// matches, rather than quietly dropping a parameter a caller still passes.
void WriteLiquidVertex(CMapObjGroup* group, const C44Matrix& matrix, const C3Vector& position,
                       const uint8_t* vertexBytes, const uint32_t* color, int32_t uvFromBytes,
                       uint32_t uv2First, int32_t stride, uint8_t** positionOut,
                       uint8_t** normalOut, uint8_t** colorOut, uint8_t** uvOut,
                       uint8_t** uv2Out) {
    const uint32_t* typeBlock = LiquidTypeBlock(static_cast<int32_t>(group->m_liquidType));

    if (*positionOut) {
        C3Vector world = position * matrix;

        auto out = reinterpret_cast<float*>(*positionOut);

        out[0] = world.x;
        out[1] = world.y;
        out[2] = world.z;

        *positionOut += stride;
    }

    if (*normalOut) {
        auto out = reinterpret_cast<float*>(*normalOut);

        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 1.0f;

        *normalOut += stride;
    }

    if (*colorOut) {
        uint32_t value;

        if (g_theGxDevicePtr->Caps().m_colorFormat == GxCF_rgba) {
            auto bytes = reinterpret_cast<const uint8_t*>(color);

            value = static_cast<uint32_t>(bytes[2])
                  | (static_cast<uint32_t>(bytes[1]) << 8)
                  | (static_cast<uint32_t>(bytes[0]) << 16)
                  | (static_cast<uint32_t>(bytes[3]) << 24);
        } else {
            value = *color;
        }

        *reinterpret_cast<uint32_t*>(*colorOut) = value;

        *colorOut += stride;
    }

    if (*uvOut) {
        auto out = reinterpret_cast<float*>(*uvOut);

        if (uvFromBytes) {
            auto authored = reinterpret_cast<const int16_t*>(vertexBytes);

            out[0] = static_cast<float>(authored[0]) * (1.0f / 256.0f);
            out[1] = static_cast<float>(authored[1]) * (1.0f / 256.0f);
        } else {
            // The placement with its translation removed, so only the rotation turns the offset.
            C44Matrix rotation = matrix;

            rotation.d0 = 0.0f;
            rotation.d1 = 0.0f;
            rotation.d2 = 0.0f;
            rotation.d3 = 1.0f;

            C3Vector local = {
                position.x - group->m_liquidPos.x,
                position.y - group->m_liquidPos.y,
                position.z - group->m_liquidPos.z
            };

            C3Vector turned = local * rotation;

            out[0] = turned.x * 0.24f;
            out[1] = turned.y * 0.24f;
        }

        *uvOut += stride;
    }

    if (*uv2Out) {
        auto out = reinterpret_cast<uint32_t*>(*uv2Out);

        out[0] = uv2First;
        out[1] = typeBlock ? typeBlock[*vertexBytes] : 0;

        *uv2Out += stride;
    }
}


// The fold scratch. Both folds take EIGHT BYTES per call from here and return the slot's address,
// which is what the outline's memoised values point at.
//
// The reference keeps this at 0x00d1bf00 with its cursor at 0x00d1bee0 and neither fold bounds the
// cursor -- the emitter resets it inline instead (0x7a81e6), once per tile. That is what makes an
// unbounded-looking allocator safe: a tile's outline holds at most 32 points, so at most 28 of them
// can be interpolated, and each is folded once thanks to the memo. Sixty-four slots is frozen's own
// number and is double what that bound needs.
static uint8_t s_foldScratch[64][8];
static int32_t s_foldCursor = 0;

void MapObjPolyFoldReset() {
    s_foldCursor = 0;
}

// ref: FUN_007a7f00
// Interpolate one byte between two attribute blocks. Used for the liquid vertex layout that carries
// a colour cursor, where the byte is the only thing needing interpolation.
//
// ROUNDED, not truncated -- the reference converts the float with a round-to-nearest conversion, so
// a truncating cast here would drift the result down by up to one unit on every interpolated vertex.
void* MapObjPolyFoldByte(const void* a, const void* b, float t) {
    auto pa = static_cast<const uint8_t*>(a);
    auto pb = static_cast<const uint8_t*>(b);

    uint8_t* slot = s_foldScratch[s_foldCursor % 64];

    s_foldCursor++;

    float value = (static_cast<float>(pb[0]) - static_cast<float>(pa[0])) * t
                + static_cast<float>(pa[0]);

    slot[0] = static_cast<uint8_t>(std::lrint(value));

    return slot;
}

// ref: FUN_007a7e50
// Interpolate two uint16s. Used for the layout without a colour cursor, where the pair is what the
// first texcoord is built from.
//
// The reference writes the two halves through DIFFERENT address expressions -- the first scaled by
// the cursor as a dword index and the second as a raw byte offset -- which look inconsistent and are
// not: both land in the same eight-byte slot, at +0 and +2. Working that out is the only subtle part
// of this function.
void* MapObjPolyFoldShortPair(const void* a, const void* b, float t) {
    auto pa = static_cast<const uint16_t*>(a);
    auto pb = static_cast<const uint16_t*>(b);

    uint8_t* slot = s_foldScratch[s_foldCursor % 64];

    s_foldCursor++;

    float first = (static_cast<float>(pb[0]) - static_cast<float>(pa[0])) * t
                + static_cast<float>(pa[0]);
    float second = static_cast<float>(pa[1])
                 + (static_cast<float>(pb[1]) - static_cast<float>(pa[1])) * t;

    auto out = reinterpret_cast<uint16_t*>(slot);

    out[0] = static_cast<uint16_t>(std::lrint(first));
    out[1] = static_cast<uint16_t>(std::lrint(second));

    return slot;
}


// ref: FUN_007a7f60
// The map-object water emitter, and the piece every other function in this file exists to serve. For
// each liquid tile carrying its OWN geometry -- bit 0x80 set, the complement of the tiles
// WriteLiquidIndices emits grid quads for -- it builds the tile's quad as a four-point outline,
// clips that outline against the group's portal planes, and walks whatever survives into the vertex
// and index streams.
//
// THAT IS WHY THERE IS A CLIPPER AT ALL. A group's water must not bleed through its doorways, so
// each tile is cut back to the portals the group owns. The portal machinery is frozen's existing
// SMOPortal / SMOPortalRef and the group's m_portalStart and m_portalCount, which already sat at the
// +0x50 and +0x54 this function reads.
//
// The neighbour test before each clip is an optimisation, not a correctness condition: a portal is
// only worth clipping against when the group on its far side has liquid whose grid overlaps this
// tile's box. The box is the tile's own first and third corners, which are its min and max by
// construction of the corner order below.
//
// CORNER ORDER IS (0,0), (0,1), (1,1), (1,0) -- around the quad rather than across it, which is what
// makes the outline a ring and the first and third corners opposite. The vertex indices are built to
// match, so corner k's index and corner k's offset always agree.
//
// The fold is chosen by whether the last cursor exists, which is how the two liquid vertex layouts
// are told apart: one interpolates a single byte, the other a pair of uint16s.
//
// The index stream is a TRIANGLE STRIP with degenerate restarts -- a duplicate leading index, one
// per vertex, then a duplicate trailing index. That is the same shape WriteLiquidIndices emits for
// the shared-grid tiles, and the two agreeing is independent confirmation of the strip reading this
// file's notes once called unresolved.
//
// The fold scratch is reset per tile, which is what keeps its unbounded-looking cursor safe.
int32_t EmitLiquidTiles(CMapObj* mapObj, CMapObjGroup* group, const C44Matrix& matrix,
                        const uint32_t* color, int32_t uvFromBytes, uint32_t uv2First,
                        int32_t stride, uint8_t** positionOut, uint8_t** normalOut,
                        uint8_t** colorOut, uint8_t** uvOut, uint8_t** uv2Out,
                        uint16_t** indexOut, uint32_t baseVertex) {
    const float kStep = 4.1666665f;

    // (dx, dy) per corner, walked around the quad.
    static const int32_t kCorner[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };

    uint32_t nextVertex = baseVertex & 0xFFFF;
    int32_t written = 0;

    MapObjPolyFold fold = *uv2Out ? &MapObjPolyFoldByte : &MapObjPolyFoldShortPair;

    const SMOPortalRef* refs = mapObj->m_mopr + group->m_portalStart;
    const uint8_t* tile = group->m_liquidTiles;

    MapObjPolySet set;

    for (int32_t tileY = 0; tileY < static_cast<int32_t>(group->m_liquidYTiles); tileY++) {
        for (int32_t tileX = 0; tileX < static_cast<int32_t>(group->m_liquidXTiles); tileX++, tile++) {
            if ((*tile & 0xF) == 0xF || !(*tile & 0x80)) {
                continue;
            }

            int32_t row = static_cast<int32_t>(group->m_liquidXVerts) * tileY;

            int32_t corner[4];

            corner[0] = row + tileX;
            corner[1] = static_cast<int32_t>(group->m_liquidXVerts) + row + tileX;
            corner[2] = corner[1] + 1;
            corner[3] = corner[0] + 1;

            MapObjPolyReset(&set);

            C3Vector position[4];

            for (int32_t k = 0; k < 4; k++) {
                const uint8_t* vertex = group->m_liquidVerts + corner[k] * 8;

                position[k].x = static_cast<float>(kCorner[k][0] + tileX) * kStep
                              + group->m_liquidPos.x;
                position[k].y = kStep * static_cast<float>(kCorner[k][1] + tileY)
                              + group->m_liquidPos.y;
                // The height is the second dword of the eight-byte liquid vertex.
                position[k].z = *reinterpret_cast<const float*>(vertex + 4);

                MapObjPolyAddPoint(&set, position[k], vertex);
            }

            MapObjPolyCloseOutline(&set);

            for (uint32_t i = 0; i < group->m_portalCount; i++) {
                const SMOPortalRef& ref = refs[i];

                CMapObjGroup* other = mapObj->GetGroup(ref.groupIndex, 0);

                if (!other) {
                    continue;
                }

                // Only clip when the neighbour's liquid grid overlaps this tile's box.
                float otherMaxX = static_cast<float>(other->m_liquidXTiles) * kStep
                                + other->m_liquidPos.x;
                float otherMaxY = kStep * static_cast<float>(other->m_liquidYTiles)
                                + other->m_liquidPos.y;

                if (other->m_liquidPos.x < position[2].x && other->m_liquidPos.y < position[2].y
                        && !(otherMaxX < position[0].x) && !(otherMaxY < position[0].y)) {
                    MapObjPolyClipToPlane(&set, mapObj->m_mopt[ref.portalIndex].plane, ref.side);
                }
            }

            MapObjPolySeekUnflaggedEdge(&set);

            MapObjPolyFoldReset();

            bool started = false;

            MapObjPolyWalk walk;

            MapObjPolyBeginWalk(&walk, &set, reinterpret_cast<void*>(fold));

            while (!MapObjPolyAtEnd(&walk)) {
                C3Vector vertexPosition = { 0.0f, 0.0f, 0.0f };
                void* value = nullptr;

                MapObjPolyDeref(&walk, &vertexPosition, &value);

                if (!started) {
                    **indexOut = static_cast<uint16_t>(nextVertex);
                    *indexOut += 1;

                    started = true;
                }

                // The reference passes one more argument here that WriteLiquidVertex never reads;
                // frozen's signature leaves it out rather than carry a parameter nothing uses.
                WriteLiquidVertex(group, matrix, vertexPosition,
                                  static_cast<const uint8_t*>(value), color, uvFromBytes, uv2First,
                                  stride, positionOut, normalOut, colorOut, uvOut, uv2Out);

                written++;

                **indexOut = static_cast<uint16_t>(nextVertex);
                *indexOut += 1;

                nextVertex++;

                MapObjPolyAdvance(&walk);
            }

            if (started) {
                **indexOut = static_cast<uint16_t>(nextVertex - 1);
                *indexOut += 1;
            }
        }
    }

    return written;
}


// ref: FUN_007a7cc0
// The shared-grid vertex writer, companion to WriteLiquidIndices: one vertex per position of the
// MLIQ grid, in the same row-major order those indices assume.
//
// A DUPLICATED ROW OR COLUMN EMITS TWO VERTICES AT THE SAME COORDINATE. The position only advances
// once per grid index, outside the repeat, so a seam is a pair of coincident vertices -- which is
// the whole point of one: the same place carrying different attributes on either side. That is also
// why WriteLiquidIndices computes its row stride as m_liquidXVerts plus the duplicated column count
// rather than the grid width.
//
// The height is the second dword of the eight-byte liquid vertex, and the attribute pointer handed
// to WriteLiquidVertex is that same vertex -- so the grid path passes authored attributes straight
// through where the tile path passes interpolated ones out of the fold scratch.
//
// The return is the total including duplicates, (xVerts + dupColumns) * (yVerts + dupRows), which is
// exactly the row stride times the row count. The caller sizes its buffer from it.
//
// The reference is a __thiscall whose `this` it stores and never reads, and it passes
// WriteLiquidVertex one argument that function ignores. Neither is reproduced.
int32_t WriteLiquidGridVertices(CMapObjGroup* group, const C44Matrix& matrix,
                                const uint32_t* color, int32_t uvFromBytes, uint32_t uv2First,
                                const LiquidSeams& seams, int32_t stride,
                                uint8_t** positionOut, uint8_t** normalOut, uint8_t** colorOut,
                                uint8_t** uvOut, uint8_t** uv2Out) {
    const float kStep = 4.1666665f;

    float y = group->m_liquidPos.y;

    uint32_t dupRowCursor = 0;

    for (uint32_t row = 0; row < group->m_liquidYVerts; row++, y += kStep) {
        uint32_t rowRepeat = 1;

        if (dupRowCursor < seams.dupRows.Count() && seams.dupRows[dupRowCursor] == row) {
            rowRepeat = 2;
            dupRowCursor++;
        }

        while (rowRepeat--) {
            float x = group->m_liquidPos.x;

            uint32_t dupColumnCursor = 0;

            for (uint32_t col = 0; col < group->m_liquidXVerts; col++, x += kStep) {
                const uint8_t* vertex = group->m_liquidVerts
                                      + (group->m_liquidXVerts * row + col) * 8;

                uint32_t columnRepeat = 1;

                if (dupColumnCursor < seams.dupColumns.Count()
                        && seams.dupColumns[dupColumnCursor] == col) {
                    columnRepeat = 2;
                    dupColumnCursor++;
                }

                C3Vector position = {
                    x,
                    y,
                    *reinterpret_cast<const float*>(vertex + 4)
                };

                while (columnRepeat--) {
                    WriteLiquidVertex(group, matrix, position, vertex, color, uvFromBytes,
                                      uv2First, stride, positionOut, normalOut, colorOut, uvOut,
                                      uv2Out);
                }
            }
        }
    }

    return static_cast<int32_t>((group->m_liquidXVerts + seams.dupColumns.Count())
                                * (group->m_liquidYVerts + seams.dupRows.Count()));
}


// ref: FUN_008a1a40
// The instance's constructor: every pointer null, the placement IDENTITY, the sphere zeroed and one
// reference held.
//
// Most of it is already expressed by this class's in-class initialisers, so the body only does what
// they cannot -- the matrix and the sphere, neither of which has a default. It is written out rather
// than left implicit because the reference keeps it as a real function that its allocator calls, and
// an implicit constructor would leave nothing for the map to bind.
//
// The identity is how the 1.0f writes in the reference were recognised: they land at indices 5, 10,
// 15 and 20, a stride of five dwords, which is the diagonal of a 4x4 starting at +0x14 -- exactly
// where this class already declares m_placement.
CInstance::CInstance() {
    this->m_placement = C44Matrix(1.0f, 0.0f, 0.0f, 0.0f,
                                  0.0f, 1.0f, 0.0f, 0.0f,
                                  0.0f, 0.0f, 1.0f, 0.0f,
                                  0.0f, 0.0f, 0.0f, 1.0f);

    this->m_sphere.c.x = 0.0f;
    this->m_sphere.c.y = 0.0f;
    this->m_sphere.c.z = 0.0f;
    this->m_sphere.r = 0.0f;
}

// ref: FUN_008a1b00
// Allocate an instance and construct it, or hand back null when the allocation fails -- the reference
// only constructs on a non-null block and this keeps that order.
//
// DIVERGENCE, the same one every allocation in this file carries: the reference takes the block from
// CDataAllocator::GetData under the RTTI name ".?AVCInstance@Liquid@@" and frozen has no
// CDataAllocator, so this uses SMemAlloc as its siblings here do.
CInstance* CInstance::Create() {
    auto instance = static_cast<CInstance*>(SMemAlloc(sizeof(CInstance), __FILE__, __LINE__, 0));

    if (!instance) {
        return nullptr;
    }

    new (instance) CInstance();

    return instance;
}


// ref: FUN_00793d20
// Drain the pending-liquid list, building one surface per placed group and handing each to the draw
// queue. This is the top of the map-object water chain: everything else in this file is reached
// from here.
//
// IT UNLINKS EVERY ENTRY WHETHER OR NOT IT BUILDS ONE, which is what makes the list a queue rather
// than a set -- a group that fails any gate below is simply dropped until the traversal marks it
// visible again next frame. The unlink happens before the gates for that reason.
//
// The indoor decision is the interesting part, and it picks both the environment and the fixed
// light. A group counts as OUTDOOR when either it is not flagged 0x48 or its def group is flagged 2,
// AND its liquid type does not carry 0x200. Outdoor water then takes its vertex colour from the
// group's material and a fixed light of 1.0; indoor water takes no colour and 0.0, leaving it lit by
// the environment alone.
//
// THE TYPE REMAP applies only outdoors: a type id below 0x15 whose (id - 1) is a multiple of four
// becomes 0x11. Those are the first entries of each of the four base families in LiquidType.dbc, so
// the effect is that outdoor WMO water of any base family draws as one specific type rather than its
// own -- which is why a lake seen through a doorway matches the lake outside it.
//
// The placement comes from the DEF, not the def group: the def group carries world-space bounds but
// the surface needs the model-to-world matrix, and that lives on the CMapObjDef reached through the
// def group's parent link.
//
// WIRED IN as of 2026-09-27. CMap::Render calls this where the reference does, between the liquid
// ramp textures and the first bucket's draw. The question that held it back -- whether the draw side
// consumes a map-object surface or only a terrain one -- was settled already: the draw dispatches
// through IMaterial::Draw on the instance's own m_geometry, and CMeshGeomFactory and
// CChunkGeomFactory are both IGeomFactory, so the material asks whichever one it was handed to Build
// its buffers and never learns which it had. There was no WMO half of the draw to write.
//
// One gate frozen cannot evaluate: the reference also requires DAT_00cd8610, two 16-byte records this
// tree notes as unported. Treated as satisfied, because it reads as an "is the liquid system up"
// check and the alternative is a permanently dead path.
void BuildPendingMapObjSurfaces() {
    while (CMapObjDefGroup* defGroup = CWorldScene::s_pendingLiquidGroups.Head()) {
        CWorldScene::s_pendingLiquidGroups.UnlinkNode(defGroup);

        if (!(CWorld::s_enables & 0x100)) {
            continue;
        }

        // THE BUILD HAPPENS ONCE; THE QUEUEING HAPPENS EVERY FRAME. This used to `continue` on a
        // group that already had a surface, which reads naturally and is wrong: the reference's Add
        // is OUTSIDE its `surface == 0` test, so a group built on one frame is queued again on every
        // later frame it is visible. Skipping the Add meant a surface drew exactly once, on the frame
        // it was created, and never again.
        if (!defGroup->m_liquidSurface) {
            CMapBaseObjLink* parent = defGroup->m_parentLinkList.Head();

            if (!parent) {
                continue;
            }

            auto def = static_cast<CMapObjDef*>(parent->ref);
            CMapObj* mapObj = def->m_mapObj;

            CMapObjGroup* group = mapObj->GetGroup(defGroup->m_groupIndex, 0);

            // FROZEN-ONLY. The reference dereferences the group without checking, and is safe because the
            // traversal only enqueues a group it already resolved; this guards anyway, since a group can
            // be unloaded between being marked visible and being built.
            if (!group) {
                continue;
            }

            uint32_t liquidType = group->GetLiquidType();

            LiquidTypeRec* typeRec = g_liquidTypeDB.GetRecord(static_cast<int32_t>(liquidType));

            if (!typeRec) {
                SysMsgPrintf(SYSMSG_ERROR, "WMO: Liquid type [%d] not found, defaulting to water!",
                             liquidType);

                liquidType = 1;
                typeRec = g_liquidTypeDB.GetRecord(1);

                if (!typeRec) {
                    continue;
                }
            }

            bool indoor = true;

            if (((group->m_flags & 0x48) == 0 || (defGroup->m_flags & 2))
                    && !(typeRec->m_flags & 0x200)) {
                indoor = false;

                if (liquidType && liquidType < 0x15 && ((liquidType - 1) & 3) == 0) {
                    liquidType = 0x11;

                    LiquidTypeRec* remapped = g_liquidTypeDB.GetRecord(0x11);

                    if (remapped) {
                        typeRec = remapped;
                    }
                }
            }

            CClientEnvironment* environment = CreateEnvironment(indoor ? 0 : 1);

            CMeshGeomFactory* factory = CMeshGeomFactory::Create(mapObj, group);

            if (!factory) {
                continue;
            }

            LiquidMaterialRec* material = g_liquidMaterialDB.GetRecord(typeRec->m_materialID);

            factory->SetLvf(material && material->m_LVF == 1);

            uint32_t diffColor = 0xFFFFFFFF;
            float fixedLight = 0.0f;

            if (!indoor) {
                SMOMaterial* groupMaterial = mapObj->GetGroupLiquidMaterial(defGroup->m_groupIndex);

                if (groupMaterial) {
                    diffColor = groupMaterial->diffColor;
                }

                fixedLight = 1.0f;
            }

            factory->SetDiffColor(&diffColor);
            factory->SetFixedLight(fixedLight);

            CInstance* instance = CInstance::Create();

            if (!instance) {
                factory->Release();

                continue;
            }

            defGroup->m_liquidSurface = instance;

            instance->m_material = GetMaterial(static_cast<int32_t>(liquidType));
            instance->m_settings = GetMaterialSettings(static_cast<int32_t>(liquidType));
            instance->m_environment = environment;
            instance->m_geometry = factory;
            instance->m_placement = def->m_placement;

            instance->m_sphere.c = defGroup->m_center;
            instance->m_sphere.r = defGroup->m_radius;
        }

        Add(defGroup->m_liquidSurface);
    }
}

} // namespace Liquid
