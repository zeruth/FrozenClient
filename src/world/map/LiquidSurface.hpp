#ifndef WORLD_MAP_LIQUID_SURFACE_HPP
#define WORLD_MAP_LIQUID_SURFACE_HPP

#include <storm/Array.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Sphere.hpp>
#include <cstdint>

class CChunkLiquid;

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

}

#endif
