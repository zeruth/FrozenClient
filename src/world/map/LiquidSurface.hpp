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
class CM2Lighting;

namespace Liquid {

// The shared wave animator. Three waves drift past the camera and their parameters become shader
// constants, which is what makes a water surface move rather than sit still.
//
// A refcounted SINGLETON: the reference makes one for the first CChunkLiquid that needs it, keeps a
// use count beside the pointer, and every surface holds a reference. 0x00d2dd2c and 0x00d2dd30.
//
// The layout matters, because the material draw does not ask for fields -- it asks for a POINTER
// and a DWORD COUNT and walks them. The reference's vtable slot 3 is `return this + 8` and slot 4 is
// `return 42`, so the records are 42 contiguous dwords at +0x08: three of six floats and then three
// of eight. Keep m_wavesA and m_wavesB adjacent or the draw reads the wrong thing.
class CWaveManager {
    public:
        static const uint32_t WAVE_COUNT = 3;
        // What slot 4 answers. 42 dwords is exactly the 0xa8 bytes the constructor's first memset
        // clears, which is the independent check on this layout.
        static const uint32_t RECORD_DWORDS = 42;

        uint32_t m_refCount = 1;

        // The first three records. NOTHING fills these -- the constructor zeroes them and Update
        // never touches them -- so the draw's first three wave registers take zeroes even with a
        // manager present. Reproduced rather than repaired: that is what the reference does, and
        // whatever fed them in an earlier build is gone.
        float m_wavesA[WAVE_COUNT][6] = {};
        // The three that move: position, direction, two scalars, the faded amplitude, and a rate.
        float m_wavesB[WAVE_COUNT][8] = {};

        // Milliseconds since each wave respawned, and how long it lives.
        uint32_t m_elapsed[WAVE_COUNT] = {};
        uint32_t m_period[WAVE_COUNT] = {};

        // The unfaded copy Update works on; m_wavesB is this with the amplitude scaled by the fade.
        float m_working[WAVE_COUNT][8] = {};

        void AddRef();
        // Vtable slot 2, which is why CInstance releases this through a different slot than the
        // per-surface objects beside it.
        void Release();

        // Vtable slot 3 (FUN_008c6c80 is `return this + 8`), inline so it binds nothing.
        const float* Records() const { return &this->m_wavesA[0][0]; }
        // Vtable slot 4 (FUN_007d6200 is `return 42`), inline so it binds nothing.
        uint32_t RecordDwords() const { return RECORD_DWORDS; }

        // Age the three waves and respawn any whose time is up. Guarded to run once a frame however
        // many layers call it.
        void Update(const C3Vector& cameraPos);
};

// The singleton, made on first use.
CWaveManager* GetWaveManager();

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
// What a surface asks for its lighting. It carries no lighting itself -- two switches and a
// virtual that fills a CM2Lighting on the caller's stack, which is the same shape the reference's
// per-terrain-chunk setup (FUN_007d04a0) uses. See docs/ref/parity-liquid.md.
//
// The reference gives it a vtable at 0x00a404e8 with four slots; slot 3 is SetupLighting below,
// slot 2 the m_indoor setter, slot 1 the release. See docs/ref/parity-liquid.md.
class CClientEnvironment {
    public:
        // Which half of the DayNight block the fog comes from: the outdoor set at +0x8c..+0x98 or
        // the indoor set at +0xa0..+0xac. Set through the reference's vtable slot 2, which is one
        // `*(this+4) = arg` folded with CMapLiquidData::SetBody. +0x04
        uint32_t m_indoor = 0;
        // Which light the surface is lit by: 0 takes the map light block's own CM2Light (at
        // 0x00ce04a8 + 0x58), anything else a fixed straight-down white directional light the
        // reference builds once and keeps. CreateSurface passes 0. +0x08
        uint32_t m_fixedLight = 0;

        void AddRef();
        void Release();

        // Fill a lighting block for this surface: the fog, one light, and then whatever the scene
        // finds nearby. The caller owns the block and has already constructed it over the
        // surface's sphere, which is what the reference's FUN_00790440 does one call earlier.
        // The reference's vtable slot 3. ref: FUN_007d4f40
        void SetupLighting(CM2Lighting* lighting);
};

// One drawable liquid surface: the material to draw it with, the layers it covers, and where it
// sits. A layer points at the instance it belongs to and the instance counts them, so a surface
// lives while any of its layers still wants it. 0x6c bytes in the reference.
class CInstance {
    public:
        IMaterial* m_material = nullptr;              // +0x00
        CMaterialSettings* m_settings = nullptr;      // +0x04
        CChunkGeomFactory* m_geometry = nullptr;      // +0x08
        // +0x0c: the wave manager, a Liquid::CWaveManager -- identified from the RTTI name the
        // reference's allocator passes (".?AVCWaveManager@Liquid@@" at 0x00af16a0). It is a
        // refcounted SINGLETON at 0x00d2dd2c, made by the first CChunkLiquid to need it and shared
        // by every surface, which is why it is released through a different vtable slot than the
        // per-surface objects beside it.
        //
        // It supplies the wave records the material draw turns into shader constants: its vtable
        // slot 3 hands back a pointer and slot 4 a count, and the draw walks that range six dwords
        // at a time for three records and then eight dwords at a time for three more.
        //
        // Nothing frozen creates it yet, so the draw sees a null provider and writes the reference's
        // own zero-fill values into those registers. CWaveManager itself is FUN_007d6240 (the
        // constructor) and FUN_007d62a0 (the per-frame update).
        CWaveManager* m_waveManager = nullptr;
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
