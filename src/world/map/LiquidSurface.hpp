#ifndef WORLD_MAP_LIQUID_SURFACE_HPP
#define WORLD_MAP_LIQUID_SURFACE_HPP

#include <storm/Array.hpp>
#include <tempest/Matrix.hpp>
#include "gx/CGxBatch.hpp"
#include "gx/buffer/Types.hpp"
#include <tempest/Sphere.hpp>
#include <cstdint>

class CChunkLiquid;
class CGxBuf;

namespace Liquid {

class IMaterial;
class CMaterialSettings;

// The layers one surface is drawn from. The geometry is NOT built here: the factory only holds
// the list, and the vertices and indices are made when the surface is actually drawn. The name
// is the reference's own, off the RTTI on its allocation.
//
// THE BUILD CONTRACT, decoded from the reference so porting it is transcription. The draw reaches
// the geometry through this object's vtable (at 0x00a404c0), not through any named function:
//
//   slot 2 (FUN_007d4ab0, 1099 bytes)  Build(format, &vertexBuf, &indexBuf, &batch)
//   slot 4 (FUN_007d4790, 60 bytes)    a cheap prepare/test taking the matrix
//
// The format the draw passes is 0xb, which is GxVBF_PT2 -- position and TWO texture coordinate
// sets, no normal and no colour. Build asks GxVertexAttribOffset for attributes 0, 3, 4, 6 and 7
// and writes whichever the format actually carries, so it is written once for any format.
//
// Build works in two passes over the layers:
//
//   counting   xSpan = m_tileEndX - m_tileX, ySpan = m_tileEndY - m_tileY
//              vertices += (xSpan + 1) * (ySpan + 1)
//              indices  += xSpan * ySpan * 6
//
//   filling    for each layer, take the factory's own 4x4 at +0x34, translate it by
//              (layer->m_origin - layers[0]->m_origin) so every layer draws relative to the
//              FIRST one, and hand it to FUN_007ce390 (391 bytes) which writes that layer's
//              vertices and advances each attribute pointer.
//
// It caches: a holder at +0x1c keeps the buffer pair, and the whole build is skipped when that
// holder is present, both buffers still say they hold data, and the dirty flag at +8 is clear.
// The holder comes from a pool keyed on the EXACT byte sizes (FUN_007cf140 walks a free list
// looking for stride*vertices and indices*2, and FUN_007cefd0 makes one when nothing matches).
//
// The batch it fills says primType 4, start 0, minIndex 0 and maxIndex vertices - 1. Note that 4
// is a triangle STRIP while the index count is six per quad, which is a list -- one of the two
// readings is wrong and it has not been resolved yet, so check it against a run before trusting
// either.
class CChunkGeomFactory {
    public:
        // TODO +0x00 vtable, +0x04, +0x08 the dirty flag, +0x0c
        uint32_t m_refCount = 1;
        TSGrowableArray<CChunkLiquid*> m_layers;   // +0x10 .. +0x1c
        // TODO +0x1c the cached buffer holder, +0x20 .. +0x2c the cached batch,
        // +0x34 the 4x4 every layer's own matrix is derived from

        void AddRef();
        void Release();

        // Build the surface's vertices and indices, or hand back what was built last time.
        // ref: FUN_007d4ab0 (the vtable's slot 2)
        int32_t Build(EGxVertexBufferFormat format, CGxBuf** vertexBuf, CGxBuf** indexBuf,
                      CGxBatch* batch);

        // The buffer pair built last time, and what it was built for.
        CGxBuf* m_vertexBuf = nullptr;
        CGxBuf* m_indexBuf = nullptr;
        CGxBatch m_batch;
        uint32_t m_builtFormat = 0xffffffff;
        int32_t m_dirty = 1;
        C44Matrix m_placement;
};

// What the material reads the frame's environment through. Holds nothing yet: the reference
// keeps a vtable, a null and the argument it was made with.
class CClientEnvironment {
    public:
        uint32_t m_unk04 = 0;
        uint32_t m_unk08 = 0;

        void AddRef();
        void Release();
};

// One drawable liquid surface: the material to draw it with, the layers it covers, and where it
// sits. A layer points at the instance it belongs to and the instance counts them, so a surface
// lives while any of its layers still wants it. 0x6c bytes in the reference.
class CInstance {
    public:
        IMaterial* m_material = nullptr;              // +0x00
        CMaterialSettings* m_settings = nullptr;      // +0x04
        CChunkGeomFactory* m_geometry = nullptr;      // +0x08
        // TODO +0x0c: refcounted, released through vtable slot 2 rather than 1 like the others
        void* m_unk0c = nullptr;
        CClientEnvironment* m_environment = nullptr;  // +0x10
        C44Matrix m_placement;                        // +0x14 .. +0x50
        CAaSphere m_sphere;                           // +0x54 .. +0x60
        // +0x64: cleared by the draw as it takes the instance off its bucket
        uint32_t m_queued = 0;
        // +0x68: one per layer pointing here, plus the one the constructor starts with, which
        // CreateSurface drops when it is done. A surface with no layers left frees itself.
        uint32_t m_refCount = 1;

        void AddRef();
        void Release();
};

// Build the surface a layer belongs to, and hand it to every layer it covers.
// ref: FUN_007cf200
void CreateSurface(CChunkLiquid* liquid);

// The frame's surfaces, in two buckets, each emptied by its own draw.
//
// BUCKET_COUNT is 2 and that is not a guess: the reference picks the bucket's comparator out of a
// two-entry function-pointer table at 0x00b23f6c, and its two call sites pass 0 and 1. Bucket 0 is
// drawn from CMap::Render (0x0079acf1, right after the blob shadows) and bucket 1 from the block at
// 0x00790a80, both gated on bit 24 of the render flags at 0x00cd774c and on the manager existing.
static const uint32_t BUCKET_COUNT = 2;

// Put a surface in this frame's queue. Nothing is drawn until the bucket is.
//
// The bucket is NOT the caller's choice: it is the settings' m_procedural flag, so procedural water
// goes to bucket 1 and everything else to bucket 0. That is the whole reason there are two -- the
// procedural bucket is drawn from a later block than CMap::Render. ref: FUN_008a20c0
void Add(CInstance* instance);

// Draw one bucket and empty it. The camera position goes to every material, which is what makes
// the wave and specular terms move with the viewer. ref: FUN_008a2240
void Draw(const C3Vector& cameraPos, uint32_t bucket);

// How many surfaces are waiting in a bucket.
uint32_t Queued(uint32_t bucket);

}

#endif
