#include "world/map/DetailDoodad.hpp"
#include "world/CWorldParam.hpp"
#include "console/CVar.hpp"
#include "db/Db.hpp"
#include "world/CWorld.hpp"
#include "model/CM2Scene.hpp"
#include "model/CM2Model.hpp"
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <new>
#include "gx/Buffer.hpp"
#include "gx/Device.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/buffer/CGxBuf.hpp"
#include "gx/buffer/CGxPool.hpp"

namespace DetailDoodad {

TSGrowableArray<CDoodadModel*> s_models;
uint32_t s_perChunk = 0;
uint32_t s_vertexBytes = 0;
uint32_t s_indexCount = 0;
CGxPool* s_vertexPool = nullptr;
CGxPool* s_indexPool = nullptr;
TSGrowableArray<CGxBuf*> s_buffers;
int32_t s_rebuild = 1;

// How many the density setting scatters per chunk before the ceiling applies.
static const uint32_t PER_DENSITY_UNIT = 0x40;

// ref: FUN_007b29b0
void ReleaseBuffers() {
    for (uint32_t i = 0; i < s_buffers.Count(); i++) {
        if (s_buffers[i]) {
            // TODO the reference hands each buffer back through CGxDevice::BufStream and drops
            // it from the chunk's block array when the device returns a different one. Frozen
            // has neither the block array nor that path, so the buffers are simply forgotten.
            s_buffers[i] = nullptr;
        }
    }

    s_buffers.SetCount(0);

    // TODO the reference gives both pools back through a device method frozen has not mapped
    // (vtable slot 0xd4). Dropping the pointers without it would leak the pool; keeping them
    // means a rebuild reuses the pools it already has, which is wrong only if the density
    // changes mid-session. Left pointing at them deliberately until the device method is
    // identified -- the alternative is a guess about which slot frees a pool.
}

// ref: FUN_007b2a80
// One pool for vertices and one for indices, each big enough for the whole ring, and the ring
// carved out of them at fixed offsets. Nothing is allocated per chunk afterwards: a chunk that
// wants to scatter takes the next pair and writes over whatever was there.
void CreateBuffers() {
    if (!s_rebuild) {
        return;
    }

    // TODO FUN_0079e730 runs first. Not identified.

    ReleaseBuffers();

    uint32_t density = CWorldParam::cvar_groundEffectDensity
        ? static_cast<uint32_t>(CWorldParam::cvar_groundEffectDensity->GetInt())
        : 0;

    s_perChunk = density * PER_DENSITY_UNIT;

    if (s_perChunk > MAX_PER_CHUNK) {
        s_perChunk = MAX_PER_CHUNK;
    }

    s_vertexBytes = s_perChunk * VERTEX_STRIDE;
    s_indexCount = s_perChunk * 2;

    uint32_t pairs = BUFFER_COUNT / 2;

    if (!s_vertexPool) {
        s_vertexPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Vertex, GxPoolUsage_Dynamic,
                                                    s_perChunk * VERTEX_STRIDE * pairs,
                                                    GxPoolHintBit_Unk3, "CDetailDoodad_vtx");
    }

    if (!s_indexPool) {
        s_indexPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Index, GxPoolUsage_Dynamic,
                                                   s_perChunk * 2 * pairs,
                                                   GxPoolHintBit_Unk0, "CDetailDoodad_idx");
    }

    if (!s_vertexPool || !s_indexPool) {
        return;
    }

    s_buffers.Reserve(BUFFER_COUNT, 1);

    for (uint32_t i = 0; i < pairs; i++) {
        auto vertexBuf = GxBufCreate(s_vertexPool, VERTEX_STRIDE, s_perChunk,
                                     i * s_perChunk * VERTEX_STRIDE);
        s_buffers.Add(1, &vertexBuf);

        auto indexBuf = GxBufCreate(s_indexPool, 2, s_perChunk, i * s_perChunk * 2);
        s_buffers.Add(1, &indexBuf);
    }

    s_rebuild = 0;
}

// ref: FUN_007b2760
// The table is as long as the highest id the DBC carries, so a kind can be looked up by its id
// with no search; the ids it does not use stay null.
void Initialize() {
    // TODO the reference also makes the "WDETAILDOODADINST" object heap the scattered instances
    // come from, and loads the module's own shaders. Frozen's stand-in already holds a detail
    // pixel shader; the heap waits for the scatter builder that would use it.

    // The reference takes the highest id straight off the DBC; frozen's WowClientDB keeps that
    // private, so it is found by looking, which comes to the same table.
    int32_t maxId = -1;

    for (int32_t i = 0; i < g_groundEffectDoodadDB.GetNumRecords(); i++) {
        auto rec = g_groundEffectDoodadDB.GetRecordByIndex(i);

        if (rec && rec->m_ID > maxId) {
            maxId = rec->m_ID;
        }
    }

    uint32_t count = maxId < 0 ? 0 : static_cast<uint32_t>(maxId) + 1;

    s_models.SetCount(count);

    for (uint32_t i = 0; i < count; i++) {
        s_models[i] = nullptr;
    }

    for (int32_t i = 0; i < g_groundEffectDoodadDB.GetNumRecords(); i++) {
        auto rec = g_groundEffectDoodadDB.GetRecordByIndex(i);

        if (!rec || rec->m_ID < 0) {
            continue;
        }

        auto entry = static_cast<CDoodadModel*>(
            SMemAlloc(sizeof(CDoodadModel), __FILE__, __LINE__, 0x0));

        if (!entry) {
            continue;
        }

        new (entry) CDoodadModel();

        entry->m_rec = rec;
        s_models[rec->m_ID] = entry;
    }
}

// ref: FUN_007b3050
// The DBC gives a name relative to one folder, so the path is that folder and the name.
bool EnsureModel(CDoodadModel* entry) {
    if (entry->m_model) {
        return true;
    }

    char path[260];

    uint32_t n = SStrCopy(path, "World\\NoDXT\\Detail\\", sizeof(path));
    SStrCopy(path + n, entry->m_rec->m_doodadPath, sizeof(path) - n);

    auto scene = CWorld::GetM2Scene();

    entry->m_model = scene ? scene->CreateModel(path, 0) : nullptr;

    if (!entry->m_model) {
        return false;
    }

    // TODO FUN_007b1b10 as the loaded callback: the reference is told when the model lands so it
    // can work out the kind's bounds. Not ported, so nothing reacts to the load.

    return true;
}

// ref: FUN_007b3530
// Asking is what starts the load, so a chunk that keeps asking will eventually be told yes.
bool IsReady(int32_t doodadId) {
    if (doodadId < 0 || static_cast<uint32_t>(doodadId) >= s_models.Count()) {
        return false;
    }

    auto entry = s_models[doodadId];

    if (!entry) {
        return false;
    }

    if (entry->m_model) {
        return entry->m_model->IsLoaded(0, 0) != 0;
    }

    EnsureModel(entry);

    return false;
}

}
