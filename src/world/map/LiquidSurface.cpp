#include "world/map/LiquidSurface.hpp"
#include "world/map/LiquidMaterialSettings.hpp"
#include "world/map/CChunkLiquid.hpp"
#include <storm/Memory.hpp>
#include <cmath>
#include <new>


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
    // UNRESOLVED, AND IT MATTERS. This follows the reference exactly -- it adds the origin and a
    // POSITIVE tile step, and takes x from the field at +0x38 and y from the one at +0x34, which
    // are frozen's m_tileY and m_tileX. But CChunkLiquid::BuildVertices, which is verified, does
    // the opposite on both counts: x from m_tileX, and a NEGATIVE step, with no origin because
    // its vertices are chunk-relative. The two conventions cannot both describe the same
    // rectangle, so one of them is wrong.
    //
    // BuildVertices was verified only by its heights resolving to world z 0 at sea level, which
    // is blind to an x/y swap, so being "verified" does not settle it. Nor did the check on this
    // function: it compared the sphere against a layer centre computed with THIS SAME formula,
    // which proves the two agree with each other and nothing else. That check has to be rebuilt
    // against m_vertices -- which are independent and already verified -- before either the
    // bounds or BuildVertices can be trusted.
    //
    // Left matching the reference because that is the authority; do not "fix" it to agree with
    // BuildVertices without running the corrected check first.
    float minX = 0.0f;
    float minY = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float maxZ = 0.0f;

    for (uint32_t i = 0; i < s_gather.Count(); i++) {
        const CChunkLiquid* layer = s_gather[i];

        float x0 = layer->m_origin.x + static_cast<float>(layer->m_tileY) * TILE_STEP;
        float y0 = layer->m_origin.y + static_cast<float>(layer->m_tileX) * TILE_STEP;
        float x1 = layer->m_origin.x + static_cast<float>(layer->m_tileEndY) * TILE_STEP;
        float y1 = layer->m_origin.y + static_cast<float>(layer->m_tileEndX) * TILE_STEP;

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

}
