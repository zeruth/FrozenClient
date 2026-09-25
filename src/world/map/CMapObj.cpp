#include "world/map/CMapObj.hpp"
#include "world/map/CMap.hpp"
#include "world/map/CMapObjGroup.hpp"
#include "async/AsyncFileRead.hpp"
#include "async/CAsyncObject.hpp"
#include "util/Log.hpp"
#include "util/SFile.hpp"
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <cmath>

// The MOPY flags a group query should skip for a set of query flags; the box query takes the
// result as its skip mask. Bit 0x80 of the query short-circuits to 0x2ca, and 0x1000000 together
// with 0x40000000 to ~0x324.
// ref: FUN_007ae140
uint32_t CMapObj::QuerySkipFlags(uint32_t queryFlags) {
    uint32_t flags = 0xEE;

    if (static_cast<int8_t>(queryFlags) < 0) {
        return 0x2CA;
    }

    if (queryFlags & 0x10) {
        flags = 0xC6;
    }

    if (queryFlags & 0x20) {
        flags &= ~0x24u;
    }

    if (!(queryFlags & 0x40)) {
        flags &= ~0x2u;
    }

    if (!(queryFlags & 0x4000)) {
        flags &= ~0x40u;
    }

    if (queryFlags & 0x1000000) {
        if (queryFlags & 0x40000000) {
            return 0xFFFFFCDB;
        }

        flags |= 0x100;
    }

    return flags;
}

// ref: FUN_007af780
// The colour of the floor a probe segment lands on: the first face the group's BSP reports along
// the segment, sampled from that group's MOCV. Exterior groups (MOGP 0x8, 0x40) are never
// probed: an entity over them is lit by the sky. The reference indexes the group in its own
// array and also checks that it is loaded; here the caller hands the group over.
bool CMapObj::GroupFloorColor(CMapObjGroup* group, const C3Segment& segment, CImVector* outColor, uint8_t* outFlag) {
    float t = 1.0f;

    if (group && (group->m_flags & 0x48) == 0) {
        CMapObjGroup::s_hitFlags = 0;
        CMapObjGroup::s_hitRecordCount = 0;
        CMapObjGroup::s_hitFacePoolCount = 0;
        CMapObjGroup::s_hitIndexPoolCount = 0;
        CMapObjGroup::s_unk7538 = 0;

        uint8_t unused;

        if (!group->QuerySegment(segment, &t, 0, SMOPoly::F_COLLISION, &unused, nullptr, nullptr)) {
            return false;
        }

        C3Vector hit;
        hit.x = segment.start.x + (segment.end.x - segment.start.x) * t;
        hit.y = (segment.end.y - segment.start.y) * t + segment.start.y;
        hit.z = t * (segment.end.z - segment.start.z) + segment.start.z;

        return group->SampleColorAtFace(hit, CMapObjGroup::s_hitRecords[0].faces[0], outColor, outFlag);
    }

    return false;
}

// ----------------------------------------------------------------------------------------------
// Loading the root file (reference MapObjRead.cpp and the MapArea.cpp tail)

TSHashTable<CMapObj, HASHKEY_NONE> CMapObj::s_cache;

// ref: FUN_007b0cc0
// The root for a path: the cache hands back the one already loaded and counts another reference,
// otherwise a fresh root starts reading. The reference logs when the read fails, which its own
// CMap::SafeOpen makes unreachable (it fatals first).
CMapObj* CMapObj::Create(const char* path) {
    uint32_t hash = SStrHash(path, 0, 0);

    auto cached = CMapObj::s_cache.Ptr(hash, HASHKEY_NONE());

    if (cached) {
        cached->m_refCount++;
        return cached;
    }

    auto mapObj = CMap::AllocMapObj();

    if (!mapObj->Read(path)) {
        SysMsgPrintf(SYSMSG_ERROR, "CMapObj::Create(): mapObj->Read(\"%s\") failed", path);
    }

    CMapObj::s_cache.Insert(mapObj, hash, HASHKEY_NONE());
    mapObj->m_refCount = 1;

    return mapObj;
}

// ref: FUN_007d80c0
// Starts the root file coming in. A path that will not open falls back to the test WMO -- which
// the reference cannot actually reach, because its CMap::SafeOpen fatals on the tenth failed
// try rather than returning; frozen's does the same, so the branch is kept as the reference
// wrote it.
int32_t CMapObj::Read(const char* path) {
    SStrCopy(this->m_name, path, STORM_MAX_STR);

    SFile* file = nullptr;

    if (!CMap::SafeOpen(path, &file)) {
        CMap::SafeOpen("world\wmo\Dungeon\test\missingwmo.wmo", &file);
        path = "world\wmo\Dungeon\test\missingwmo.wmo";
    }

    SStrCopy(this->m_name, path, STORM_MAX_STR);

    this->m_fileSize = SFile::GetFileSize(file, nullptr);
    this->m_fileBuffer = SMemAlloc(this->m_fileSize, __FILE__, __LINE__, 0x0);

    this->m_asyncObject = AsyncFileReadAllocObject();
    this->m_asyncObject->file = file;
    this->m_asyncObject->userArg = this;
    this->m_asyncObject->buffer = this->m_fileBuffer;
    this->m_asyncObject->size = this->m_fileSize;
    this->m_asyncObject->userPostloadCallback = &CMapObj::ReadCallback;
    this->m_asyncObject->priority = 0x7c;

    AsyncFileReadObject(this->m_asyncObject, 0);

    CMap::s_mapObjLoadList.LinkToTail(this);

    return 1;
}

// ref: FUN_007d8050
void CMapObj::ReadCallback(void* arg) {
    auto mapObj = static_cast<CMapObj*>(arg);

    AsyncFileReadDestroyObject(mapObj->m_asyncObject);
    mapObj->m_asyncObject = nullptr;
    mapObj->m_link.Unlink();

    mapObj->ReadComplete();
}

// ref: FUN_007d7eb0
// The bytes are in: point at every chunk, drop the material textures the last load left, take
// the header's ambient colour and bounds, and make an empty group for each MOGI entry. The
// group files are read separately.
void CMapObj::ReadComplete() {
    this->ParseChunks();
    this->ClearMaterialTextures();

    this->m_ambientColor = this->m_mohd->ambColor;
    this->m_bounds = this->m_mohd->bounds;

    this->m_groupsToLoad = this->m_groupCount;

    for (uint32_t i = 0; i < this->m_groupCount; i++) {
        this->m_groups[i] = CMap::AllocMapObjGroup();
    }

    this->m_rootLoaded = 1;
}

// One IFF chunk of the root: its body, and the cursor moved past it
struct WmoChunk {
    const uint8_t* body;
    uint32_t size;
};

static WmoChunk NextChunk(const uint8_t*& cursor) {
    uint32_t size = *reinterpret_cast<const uint32_t*>(cursor + 4);
    WmoChunk chunk = { cursor + 8, size };
    cursor = cursor + 8 + size;
    return chunk;
}

// ref: FUN_007d7470
// The root's chunks in the order the format writes them, walked without reading a single chunk
// id: MVER, MOHD, MOTX, MOMT, MOGN, MOGI, MOSB, MOPV, MOPT, MOPR, MOVV, MOVB, MOLT, MODS, MODN,
// MODD, MFOG, and MCVP only when one is there. Two fixups ride along: a root with no skybox
// clears the skybox flag on every group, and a portal whose plane distance is not a number gets
// a flat one far away.
void CMapObj::ParseChunks() {
    auto base = static_cast<const uint8_t*>(this->m_fileBuffer);

    // Past MVER (its 8-byte header, a 4-byte body) and MOHD's header
    this->m_mohd = reinterpret_cast<SMOHeader*>(const_cast<uint8_t*>(base) + 0x14);
    const uint8_t* cursor = base + 0x14 + *reinterpret_cast<const uint32_t*>(base + 0x10);

    auto motx = NextChunk(cursor);
    this->m_motx = reinterpret_cast<const char*>(motx.body);
    this->m_motxSize = motx.size;

    auto momt = NextChunk(cursor);
    this->m_materials = reinterpret_cast<SMOMaterial*>(const_cast<uint8_t*>(momt.body));
    this->m_materialCount = momt.size >> 6;

    auto mogn = NextChunk(cursor);
    this->m_mogn = reinterpret_cast<const char*>(mogn.body);
    this->m_mognSize = mogn.size;

    auto mogi = NextChunk(cursor);
    this->m_mogi = reinterpret_cast<SMOGroupInfo*>(const_cast<uint8_t*>(mogi.body));
    this->m_groupCount = mogi.size >> 5;

    auto mosb = NextChunk(cursor);
    this->m_mosb = reinterpret_cast<const char*>(mosb.body);

    if (!*this->m_mosb) {
        this->m_mosb = nullptr;

        for (uint32_t i = 0; i < this->m_groupCount; i++) {
            this->m_mogi[i].flags &= ~0x40000u;
        }
    }

    auto mopv = NextChunk(cursor);
    this->m_mopv = reinterpret_cast<const C3Vector*>(mopv.body);
    this->m_portalVertexCount = mopv.size / sizeof(C3Vector);

    auto mopt = NextChunk(cursor);
    this->m_mopt = reinterpret_cast<SMOPortal*>(const_cast<uint8_t*>(mopt.body));
    this->m_portalCount = mopt.size / sizeof(SMOPortal);

    for (uint32_t i = 0; i < this->m_portalCount; i++) {
        auto portal = &this->m_mopt[i];

        if (isnan(portal->plane.d)) {
            portal->plane.n = { 0.0f, 0.0f, 1.0f };
            portal->plane.d = 800000.0f;
        }
    }

    auto mopr = NextChunk(cursor);
    this->m_mopr = reinterpret_cast<const SMOPortalRef*>(mopr.body);
    this->m_portalRefCount = mopr.size >> 3;

    auto movv = NextChunk(cursor);
    this->m_movv = reinterpret_cast<const C3Vector*>(movv.body);
    this->m_visibleBlockVertexCount = movv.size / sizeof(C3Vector);

    auto movb = NextChunk(cursor);
    this->m_movb = movb.body;
    this->m_visibleBlockCount = movb.size >> 2;

    auto molt = NextChunk(cursor);
    this->m_molt = molt.body;
    this->m_lightCount = molt.size / 0x30;

    auto mods = NextChunk(cursor);
    this->m_mods = mods.body;
    this->m_doodadSetCount = mods.size >> 5;

    auto modn = NextChunk(cursor);
    this->m_modn = reinterpret_cast<const char*>(modn.body);
    this->m_modnSize = modn.size;

    auto modd = NextChunk(cursor);
    this->m_modd = modd.body;
    this->m_doodadDefCount = modd.size / 0x28;

    auto mfog = NextChunk(cursor);
    this->m_mfog = mfog.body;
    this->m_fogCount = mfog.size / 0x30;

    // MCVP is optional and last; anything past the end of the file is not one
    // "MCVP" as it sits in the file: the id is written back to front
    const uint32_t ID_MCVP = 0x4D435650;

    if (cursor < base + this->m_fileSize && *reinterpret_cast<const uint32_t*>(cursor) == ID_MCVP) {
        this->m_mcvp = reinterpret_cast<const C4Plane*>(cursor + 8);
        this->m_convexVolumePlaneCount = *reinterpret_cast<const uint32_t*>(cursor + 4) >> 4;
    }
}

// ref: FUN_007d72d0
// The texture handles of every material are runtime state, not file data; the groups fill them
// as they load.
void CMapObj::ClearMaterialTextures() {
    this->m_materialTextures.SetCount(this->m_materialCount);

    for (uint32_t i = 0; i < this->m_materialCount; i++) {
        this->m_materialTextures[i].texture1 = nullptr;
        this->m_materialTextures[i].texture2 = nullptr;
    }
}
