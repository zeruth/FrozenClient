#include "world/map/LiquidSurface.hpp"
#include "world/map/LiquidMaterialSettings.hpp"
#include "world/map/CChunkLiquid.hpp"
#include <storm/Memory.hpp>
#include <cmath>
#include <new>
#include "gx/Buffer.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
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

void CChunkGeomFactory::AddRef() {
    this->m_refCount++;
}

void CChunkGeomFactory::Release() {
    if (--this->m_refCount) {
        return;
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

    // TODO +0x0c, released through its own vtable's third slot. Nothing sets it yet.

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

    environment->m_unk08 = arg;

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

    // TODO the reference also stores a global at +0x0c here and takes a reference on it. Which
    // global is not established, and nothing reads the field yet.

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
static void WriteLayerVertices(const CChunkLiquid* layer, const C44Matrix& placement,
                               uint32_t stride, uint8_t** position, uint8_t** normal,
                               uint8_t** color, uint8_t** uv0, uint8_t** uv1) {
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

        // TODO ref: with a per-type coordinate table (FUN_0079b870) the reference indexes it by a
        // byte it reads back through the layer's vertex data. Without that table it writes zeroes,
        // which is the branch this takes.
        if (*uv0) {
            auto out = reinterpret_cast<float*>(*uv0);

            out[0] = 0.0f;
            out[1] = 0.0f;

            *uv0 += stride;
        }

        if (*uv1) {
            auto out = reinterpret_cast<float*>(*uv1);

            out[0] = 0.0f;
            out[1] = 0.0f;

            *uv1 += stride;
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
        uint16_t far = static_cast<uint16_t>(row + 2);

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

                other = far;

                *out++ = other;
            }

            next++;
            far++;
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
    bool cached = this->m_vertexBuf && this->m_indexBuf
               && this->m_vertexBuf->unk1C && this->m_vertexBuf->unk1D
               && this->m_indexBuf->unk1C && this->m_indexBuf->unk1D
               && !this->m_dirty && this->m_builtFormat == static_cast<uint32_t>(format);

    if (cached) {
        *vertexBuf = this->m_vertexBuf;
        *indexBuf = this->m_indexBuf;
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

    // DIVERGED: the reference takes its buffer pair from a pool keyed on the exact byte sizes
    // (FUN_007cf140 walks a free list for stride*vertices and indices*2, FUN_007cefd0 makes one
    // when nothing fits). Frozen streams a pair instead. That is an allocation strategy rather
    // than behaviour, and the pool is worth porting on its own merits, not inside this.
    if (!this->m_vertexBuf) {
        this->m_vertexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, stride, totalVertices);
        this->m_indexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, maxIndices);
    }

    if (!this->m_vertexBuf || !this->m_indexBuf) {
        return 0;
    }

    *vertexBuf = this->m_vertexBuf;
    *indexBuf = this->m_indexBuf;

    if (this->m_dirty) {
        this->m_vertexBuf->unk1C = 0;
        this->m_indexBuf->unk1C = 0;
    }

    if (!this->m_vertexBuf->unk1C || !this->m_vertexBuf->unk1D) {
        auto locked = reinterpret_cast<uint8_t*>(g_theGxDevicePtr->BufLock(this->m_vertexBuf));

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

                WriteLayerVertices(layer, placement, stride, &position, &normal, &color,
                                   &uv0, &uv1);
            }

            g_theGxDevicePtr->BufUnlock(this->m_vertexBuf, 0);

            this->m_vertexBuf->unk1C = 1;
        }
    }

    if (!this->m_indexBuf->unk1C || !this->m_indexBuf->unk1D) {
        batch->m_primType = GxPrim_TriangleStrip;
        batch->m_start = 0;
        batch->m_count = 0;
        batch->m_minIndex = 0;
        batch->m_maxIndex = static_cast<uint16_t>(totalVertices - 1);

        auto locked = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(this->m_indexBuf));

        if (locked) {
            uint16_t base = 0;

            for (uint32_t i = 0; i < this->m_layers.Count(); i++) {
                WriteLayerIndices(this->m_layers[i], locked + batch->m_count, base, batch);

                base = static_cast<uint16_t>(base + s_perLayerVertices[i]);
            }

            g_theGxDevicePtr->BufUnlock(this->m_indexBuf, 0);

            this->m_indexBuf->unk1C = 1;
        }

        this->m_batch = *batch;
    } else {
        *batch = this->m_batch;
    }

    this->m_builtFormat = static_cast<uint32_t>(format);
    this->m_dirty = 0;

    return 1;
}

}
