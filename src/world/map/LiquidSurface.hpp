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
class CChunkGeomFactory {
    public:
        // TODO +0x00 vtable, +0x04 .. +0x0c
        uint32_t m_refCount = 1;
        TSGrowableArray<CChunkLiquid*> m_layers;   // +0x10 .. +0x1c

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
