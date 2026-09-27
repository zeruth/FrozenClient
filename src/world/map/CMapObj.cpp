#include "world/map/CMapObj.hpp"
#include <cfloat>
#include "world/map/CMap.hpp"
#include "world/map/CMapObjGroup.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldScene.hpp"
#include "gx/shader/CShaderEffectManager.hpp"
#include <common/ObjectAlloc.hpp>
#include "async/AsyncFileRead.hpp"
#include <tempest/Box.hpp>
#include "async/CAsyncObject.hpp"
#include "util/Log.hpp"
#include "util/SFile.hpp"
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include "world/map/CMapObjDef.hpp"
#include "world/map/CMapObjDefGroup.hpp"
#include <cmath>
#include <tempest/Intersect.hpp>
#include <tempest/Ray.hpp>

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
// probed: an entity over them is lit by the sky.
//
// Takes the group INDEX, as the reference does, and keeps all three of its gates: the root's
// chunks parsed, the group loaded, and the group not exterior. It used to take a resolved
// CMapObjGroup* and test only the last of those, leaving the other two to its one caller --
// which was fine while that caller was the only one, and stopped being fine when FloorLight
// became a virtual with two overrides that would each have had to repeat them.
// CMapObj::GroupFaceColor is the same three gates without the probe.
bool CMapObj::GroupFloorColor(uint32_t groupIndex, const C3Segment& segment, CImVector* outColor, uint8_t* outFlag) {
    float t = 1.0f;

    if (!this->m_rootLoaded) {
        return false;
    }

    CMapObjGroup* group = this->m_groups[groupIndex];

    if (group && (group->m_state & 0x1) && (group->m_flags & 0x48) == 0) {
        CMapObjGroup::s_hitFlags = 0;
        CMapObjGroup::s_hitRecordCount = 0;
        CMapObjGroup::s_hitFacePoolCount = 0;
        CMapObjGroup::s_hitIndexPoolCount = 0;
        CMapObjGroup::s_hitPlacementCount = 0;

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

// ref: FUN_007f9480
// Woo's candidate-plane segment/box test, and the one place in this file where the decompilation
// could not be trusted: Ghidra loses the whole "which plane is furthest" step -- the variable that
// holds the largest entry distance is never assigned in its output, which would make this always
// return 0. The selection below is read off the disassembly at 0x007f958f.
int32_t SegmentIntersectsBox(const CAaBox& box, const C3Vector& start, const C3Vector& end) {
    // A box is two C3Vectors, min then max, so it indexes as six floats -- which is how the
    // reference walks it.
    auto min = reinterpret_cast<const float*>(&box.b);
    auto max = reinterpret_cast<const float*>(&box.t);
    auto from = reinterpret_cast<const float*>(&start);
    auto to = reinterpret_cast<const float*>(&end);

    float dir[3] = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };

    // How far along the segment each axis' entry plane is crossed. Woo's original also keeps the
    // plane's coordinate in a parallel array and the reference dutifully writes one, but neither
    // this function nor anything else ever reads it -- the distance is computed straight from the
    // face -- so that dead store is not reproduced.
    float t[3] = { -1.0f, -1.0f, -1.0f };

    int32_t inside = 1;

    for (uint32_t i = 0; i < 3; i++) {
        if (min[i] <= from[i]) {
            if (max[i] < from[i]) {
                // Start is past the far face. If the end is too, the segment never reaches the box.
                if (max[i] < to[i]) {
                    return 0;
                }

                inside = 0;

                if (dir[i] != 0.0f) {
                    t[i] = (max[i] - from[i]) / dir[i];
                }
            }

            // Otherwise the start is between the faces on this axis and there is no candidate.
        } else {
            // Start is before the near face.
            if (to[i] < min[i]) {
                return 0;
            }

            inside = 0;

            if (dir[i] != 0.0f) {
                t[i] = (min[i] - from[i]) / dir[i];
            }
        }
    }

    // Start is inside the box on all three axes, so there is nothing to compute.
    if (inside) {
        return 1;
    }

    // The entry point is the LAST of the three planes to be crossed.
    uint32_t whichPlane = 0;

    if (t[0] < t[1]) {
        whichPlane = 1;
    }

    if (t[whichPlane] < t[2]) {
        whichPlane = 2;
    }

    // A negative distance means the box is behind the start, not along the segment. The reference
    // tests the sign bit rather than comparing, which also rejects a negative zero.
    if (t[whichPlane] < 0.0f || (t[whichPlane] == 0.0f && std::signbit(t[whichPlane]))) {
        return 0;
    }

    // The entry point has to be within the box on the two axes that did not pick the plane. The
    // 1e-5 slack is the reference's, at 0x009ea558, and it is applied to both ends.
    static const float SLACK = 9.999999747378752e-06f;

    for (uint32_t i = 0; i < 3; i++) {
        if (i == whichPlane) {
            continue;
        }

        float hit = from[i] + t[whichPlane] * dir[i];

        if (min[i] - SLACK > hit) {
            return 0;
        }

        if (max[i] + SLACK < hit) {
            return 0;
        }
    }

    return 1;
}

// ref: FUN_007aeb10
SMOGroupInfo* CMapObj::GroupInfo(uint32_t groupIndex) {
    if (!this->m_rootLoaded) {
        return nullptr;
    }

    return &this->m_mogi[groupIndex];
}

// ref: FUN_007ae840
bool CMapObj::SegmentVsBounds(const C3Vector& start, const C3Vector& end) {
    if (!this->m_rootLoaded) {
        return false;
    }

    return SegmentIntersectsBox(this->m_bounds, start, end) != 0;
}

// ref: FUN_007ae880
bool CMapObj::SegmentVsGroupBounds(const C3Vector& start, const C3Vector& end,
                                   uint32_t groupIndex) {
    if (!this->m_rootLoaded) {
        return false;
    }

    CMapObjGroup* group = this->m_groups[groupIndex];

    // State bit 0 is the group's own file having arrived. An unloaded group has no geometry, so a
    // hit on its bounds would be a hit on nothing.
    if (!group || !(group->m_state & 0x1)) {
        return false;
    }

    return SegmentIntersectsBox(this->m_mogi[groupIndex].bounds, start, end) != 0;
}

// ref: FUN_007ae970
bool CMapObj::PointInGroupBounds(const C3Vector& point, uint32_t groupIndex, float slack) {
    if (!this->m_rootLoaded) {
        return false;
    }

    CMapObjGroup* group = this->m_groups[groupIndex];

    if (!group || !(group->m_state & 0x1)) {
        return false;
    }

    const CAaBox& bounds = this->m_mogi[groupIndex].bounds;

    // Grown by the slack on both ends, one axis at a time, bailing at the first miss.
    if (point.x + slack < bounds.b.x || bounds.t.x < point.x - slack) {
        return false;
    }

    if (point.y + slack < bounds.b.y || bounds.t.y < point.y - slack) {
        return false;
    }

    if (point.z + slack < bounds.b.z || bounds.t.z < point.z - slack) {
        return false;
    }

    return true;
}

// ref: FUN_007aeae0
const char* CMapObj::GroupName(uint32_t groupIndex) {
    if (!this->m_rootLoaded) {
        return nullptr;
    }

    CMapObjGroup* group = this->m_groups[groupIndex];

    if (!group || !(group->m_state & 0x1)) {
        return nullptr;
    }

    return group->m_name;
}

// ref: FUN_007af280
// Where a segment crosses from one group into another. This is the query that tells the client
// which room it is in: not by asking what the segment hits, but by finding the last PORTAL it
// passes through, which is the only thing that actually connects two groups.
bool CMapObj::QuerySegmentPortals(const C3Segment& segment, float* t, uint32_t* outGroups,
                                  int32_t fromPoint) {
    // The ray form the plane intersection wants, plus the length so distances can go back out as
    // the fraction the caller passed in.
    C3Ray ray;

    ray.origin = segment.start;
    ray.dir.x = segment.end.x - segment.start.x;
    ray.dir.y = segment.end.y - segment.start.y;
    ray.dir.z = segment.end.z - segment.start.z;

    float length = sqrtf(ray.dir.x * ray.dir.x
                       + ray.dir.y * ray.dir.y
                       + ray.dir.z * ray.dir.z);

    float invLength = 1.0f / length;

    ray.dir.x *= invLength;
    ray.dir.y *= invLength;
    ray.dir.z *= invLength;

    // The search window, in world units rather than as a fraction.
    float nearest = length * *t;

    bool found = false;

    // The slack the point filter grows the bounds by, and the epsilon the plane test treats as
    // parallel. DAT_009f1968 and DAT_009e3004.
    static const float POINT_SLACK = 0.009999999776482582f;
    static const float PLANE_EPSILON = 0.10000000149011612f;

    for (uint32_t groupIndex = 0; groupIndex < this->m_groupCount; groupIndex++) {
        // Either "the segment touches this group" or "the segment starts in it".
        bool consider = fromPoint
            ? this->PointInGroupBounds(segment.start, groupIndex, POINT_SLACK)
            : this->SegmentVsGroupBounds(segment.start, segment.end, groupIndex);

        if (!consider) {
            continue;
        }

        // Both filters already checked these; the reference checks them again here and so does
        // this, because the point filter's version does not imply the group is still loaded by the
        // time the portals are walked.
        if (!this->m_rootLoaded) {
            continue;
        }

        CMapObjGroup* group = this->m_groups[groupIndex];

        if (!group || !(group->m_state & 0x1)) {
            continue;
        }

        const SMOPortalRef* ref = &this->m_mopr[group->m_portalStart];

        for (uint32_t i = 0; i < group->m_portalCount; i++, ref++) {
            const SMOPortal* portal = &this->m_mopt[ref->portalIndex];

            C3Vector hit;
            float distance;

            if (!IntersectRayPlane(ray, portal->plane, &distance, &hit, PLANE_EPSILON)) {
                continue;
            }

            // Behind the start, or further than the window allows.
            if (distance < 0.0f || nearest < distance) {
                continue;
            }

            uint32_t axis = DominantAxis(portal->plane.n);

            if (!PointInPolygon(hit, &this->m_mopv[portal->startVertex],
                                static_cast<int32_t>(portal->count), axis)) {
                continue;
            }

            // A real crossing, and the nearest so far.
            nearest = distance;
            found = true;

            // Which of the two groups the segment is arriving IN. MOPR's `side` says which side of
            // the portal's plane the owning group sits on; when the segment's start is on the other
            // side, the group it is heading into is the portal's other one, so that goes first.
            float side = portal->plane.n.x * segment.start.x
                       + portal->plane.n.y * segment.start.y
                       + portal->plane.n.z * segment.start.z
                       + portal->plane.d;

            bool swap = (side < 0.0f) ? (ref->side > 0) : (ref->side < 1);

            if (swap) {
                outGroups[0] = ref->groupIndex;
                outGroups[1] = groupIndex;
            } else {
                outGroups[0] = groupIndex;
                outGroups[1] = ref->groupIndex;
            }
        }
    }

    // Left alone when nothing was crossed, so the caller keeps whatever it asked with.
    if (found) {
        *t = nearest * invLength;
    }

    return found;
}

// ref: FUN_007d59b0
int32_t QuerySegmentMapObjs(const C3Vector& start, const C3Vector& end, float maxT,
                            CMapObjDef** outDefs, uint32_t* outGroups) {
    // How far along the segment each slot has found something. A later hit only counts if it is
    // nearer, which is what makes the nested building win over the one containing it.
    float nearest[2] = { maxT, maxT };

    outDefs[0] = nullptr;
    outDefs[1] = nullptr;
    outGroups[0] = 0xffff;
    outGroups[1] = 0xffff;
    outGroups[2] = 0xffff;
    outGroups[3] = 0xffff;

    // The window QuerySegmentPortals starts with, and the slack a portal hit has to beat a geometry
    // hit by. DAT_00a40314 and DAT_009e8cd0.
    static const float PORTAL_WINDOW = 1.0499999523162842f;
    static const float NEARER_SLACK = 9.999999747378752e-05f;

    // Every placed building. The reference walks the list inside the uniqueId hash table rather
    // than a list of its own -- Storm's hash keeps one -- so this walk is that same walk.
    for (auto def = CMapObjDef::s_uniqueIds.Head(); def;
         def = CMapObjDef::s_uniqueIds.Next(def)) {
        if (def->m_flags & 0x20) {
            continue;
        }

        // Flag 0x400 sends the result to the second slot. Not a distance or an order -- the flag.
        uint32_t slot = (def->m_flags & 0x400) ? 1 : 0;

        // World-space bounds first, because it rejects almost everything for almost nothing.
        if (!SegmentIntersectsBox(def->m_bounds, start, end)) {
            continue;
        }

        CMapObj* mapObj = def->m_mapObj;

        if (!mapObj) {
            continue;
        }

        // Into the building's own space, where its bounds and BSP live. Everything below is local,
        // so no hit needs transforming back.
        C3Vector localStart = start * def->m_inversePlacement;
        C3Vector localEnd = end * def->m_inversePlacement;

        if (!mapObj->SegmentVsBounds(localStart, localEnd)) {
            continue;
        }

        C3Segment segment;
        segment.start = localStart;
        segment.end = localEnd;

        // Whether the group that was hit is an exterior one, which disqualifies the whole slot.
        uint8_t exterior = 0;

        // Every group this def actually placed, through the links the groups hold on it.
        for (auto link = def->m_defGroupLinkList.Head(); link;
             link = def->m_defGroupLinkList.Next(link)) {
            auto defGroup = static_cast<CMapObjDefGroup*>(link->owner);

            if (!defGroup) {
                continue;
            }

            uint32_t groupIndex = defGroup->m_groupIndex;

            CMapObjGroup* group = mapObj->GetGroup(groupIndex, 0);

            if (!group) {
                continue;
            }

            // Exterior, skybox and the rest: a group with any of these is not a room.
            if (group->m_flags & 0x410080) {
                continue;
            }

            if (!mapObj->SegmentVsGroupBounds(localStart, localEnd, groupIndex)) {
                continue;
            }

            CMapObjGroup::s_hitFlags = 0;
            CMapObjGroup::s_hitRecordCount = 0;
            CMapObjGroup::s_hitFacePoolCount = 0;
            CMapObjGroup::s_hitIndexPoolCount = 0;
            CMapObjGroup::s_hitPlacementCount = 0;

            // The reference hands a throwaway byte here -- it points at the top byte of its own
            // maxT argument, which nothing reads afterwards.
            uint8_t scratch = 0;

            // No placement: the segment is already in this building's space, so a hit needs no
            // transform, and the def is what owns the hit.
            if (group->QuerySegment(segment, &nearest[slot], 0, 0, &scratch, nullptr, def)) {
                exterior = (group->m_flags >> 3) & 1;

                outDefs[slot] = def;
                outGroups[slot * 2] = groupIndex;
                outGroups[slot * 2 + 1] = 0xffff;
            }
        }

        // Then the portals, which can put the answer in a room the geometry walk did not reach --
        // a doorway the segment passes through rather than a floor it lands on.
        float portalT = PORTAL_WINDOW;
        uint32_t portalGroups[2] = { 0xffff, 0xffff };

        if (mapObj->QuerySegmentPortals(segment, &portalT, portalGroups, 0)
            && portalT - nearest[slot] < NEARER_SLACK) {
            SMOGroupInfo* info = mapObj->GroupInfo(portalGroups[0]);

            if (info) {
                exterior = (info->flags >> 3) & 1;
            }

            nearest[slot] = portalT;

            outDefs[slot] = def;
            outGroups[slot * 2] = portalGroups[0];

            // The far side of the portal is kept only when it is a room too.
            SMOGroupInfo* farInfo = mapObj->GroupInfo(portalGroups[1]);

            outGroups[slot * 2 + 1] = (farInfo && !(farInfo->flags & 0x8))
                ? portalGroups[1]
                : 0xffff;
        }

        if (exterior) {
            outDefs[slot] = nullptr;
        }
    }

    // Nothing ordinary, but something flagged: move it down so slot 0 is always the answer.
    if (!outDefs[0]) {
        if (!outDefs[1]) {
            return 0;
        }

        outDefs[0] = outDefs[1];
        outGroups[0] = outGroups[2];
        outGroups[1] = outGroups[3];

        outDefs[1] = nullptr;
        outGroups[2] = 0;
        outGroups[3] = 0;
    }

    return 1;
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

    // The separators are DOUBLED because this is a C string, not a path literal: written
    // singly the compiler ate them, and the escapes it did not recognise (\w, \D, \m) at
    // least drew a warning while \t silently became a TAB -- so the fallback resolved to
    // "worldwmoDungeon<tab>estmissingwmo.wmo" and could never open. The reference's own string
    // is "world\wmo\Dungeon\test\missingwmo.wmo", which is what this now produces.
    if (!CMap::SafeOpen(path, &file)) {
        CMap::SafeOpen("world\\wmo\\Dungeon\\test\\missingwmo.wmo", &file);
        path = "world\\wmo\\Dungeon\\test\\missingwmo.wmo";
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

    // m_portalCount MUST be taken from MOPT before it is used to size the rect array. It used to
    // be assigned after, so SetCount always ran with the stale 0 and every root ended up with an
    // EMPTY rect array -- which made WalkPortals reject every doorway on
    // `portalIndex >= m_portalRects.Count()`. No portal was ever crossed, so a building's interior
    // groups were never marked visible and its rooms drew nothing: Acherus was a shell with its
    // doodads hanging in mid-air. The portals themselves parsed fine; only the array was empty.
    this->m_portalCount = mopt.size / sizeof(SMOPortal);

    this->m_portalRects.SetCount(this->m_portalCount);

    for (uint32_t i = 0; i < this->m_portalCount; i++) {
        this->m_portalRects[i].flags = 0;
        this->m_portalRects[i].stamp = 0;
    }

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

// ----------------------------------------------------------------------------------------------
// What the streaming and visibility passes ask a root. Every one of these answers nothing until
// the root's own file has parsed, because until then there are no chunks to read.

// ref: FUN_007ae520
void CMapObj::BoundingSphere(C3Vector* center, float* radius) {
    if (!this->m_rootLoaded) {
        center->x = 0.0f;
        center->y = 0.0f;
        center->z = 0.0f;
        *radius = 0.0f;
        return;
    }

    center->x = (this->m_bounds.t.x + this->m_bounds.b.x) * 0.5f;
    center->y = (this->m_bounds.t.y + this->m_bounds.b.y) * 0.5f;
    center->z = (this->m_bounds.t.z + this->m_bounds.b.z) * 0.5f;

    float dx = this->m_bounds.t.x - center->x;
    float dy = this->m_bounds.t.y - center->y;
    float dz = this->m_bounds.t.z - center->z;
    *radius = sqrtf(dz * dz + dy * dy + dx * dx);
}

// ref: FUN_007ae5e0
void CMapObj::Bounds(CAaBox* bounds) {
    if (!this->m_rootLoaded) {
        *bounds = {};
        return;
    }

    *bounds = this->m_bounds;
}

// ref: FUN_007ae670
void CMapObj::GroupBoundingSphere(uint32_t index, C3Vector* center, float* radius) {
    if (!this->m_rootLoaded) {
        center->x = 0.0f;
        center->y = 0.0f;
        center->z = 0.0f;
        *radius = 0.0f;
        return;
    }

    const CAaBox& box = this->m_mogi[index].bounds;

    center->x = (box.t.x + box.b.x) * 0.5f;
    center->y = (box.t.y + box.b.y) * 0.5f;
    center->z = (box.t.z + box.b.z) * 0.5f;

    float dx = box.t.x - center->x;
    float dy = box.t.y - center->y;
    float dz = box.t.z - center->z;
    *radius = sqrtf(dz * dz + dy * dy + dx * dx);
}

// ref: FUN_007ae720
void CMapObj::GroupBounds(uint32_t index, CAaBox* bounds) {
    if (!this->m_rootLoaded) {
        *bounds = {};
        return;
    }

    *bounds = this->m_mogi[index].bounds;
}

// ref: FUN_007ae7b0
uint32_t CMapObj::GroupFlags(uint32_t index) {
    if (!this->m_rootLoaded) {
        return 0;
    }

    return this->m_mogi[index].flags;
}

// ref: FUN_007aea80
// The group object for an index, or nothing when the root has not parsed or the group's own file
// has not; a caller that can cope with an empty group passes allowUnloaded.
CMapObjGroup* CMapObj::GetGroup(uint32_t index, int32_t allowUnloaded) {
    if (!this->m_rootLoaded) {
        return nullptr;
    }

    auto group = this->m_groups[index];

    if (!(group->m_state & 0x1) && !allowUnloaded) {
        return nullptr;
    }

    return group;
}

// ref: FUN_007ae1c0
// Blocks until the root's read has landed. The loop re-reads the pointer because the callback
// that clears it runs inside the wait.
void CMapObj::WaitForRoot() {
    while (this->m_asyncObject) {
        AsyncFileReadWait(this->m_asyncObject);
    }
}

// ref: FUN_007aeab0
void CMapObj::WaitForGroup(uint32_t index) {
    auto group = this->m_groups[index];

    while (group->m_asyncObject) {
        AsyncFileReadWait(group->m_asyncObject);
    }
}

// ref: FUN_007ae1a0
void CMapObj::ReadGroup(uint32_t index) {
    CMapObjGroup::Read(this, index, 0);
}

// ----------------------------------------------------------------------------------------------
// What every loaded root does once a frame

// ref: FUN_007cbd70
// The group gives its buffers back. Each block clears the slot that named it, so the next
// draw builds them again.
void CMapObjGroup::FreeBuffers() {
    VBBList::s_vertexList.Free(this->m_vertexBuf);
    VBBList::s_vertexList.Free(this->m_colorBuf);
    VBBList::s_indexList.Free(this->m_indexBuf);
}

// ref: FUN_007a8520
// The self-illuminated materials of a root track the day: each keeps its authored colour and
// draws it scaled by how bright the sun is now.
void CMapObj::UpdateMaterialColors() {
    int32_t scale = static_cast<int32_t>(roundf(CWorld::GetSidnScale() * 255.0f - 0.5f));

    for (uint32_t i = 0; i < this->m_materialCount; i++) {
        auto material = &this->m_materials[i];

        if (!(material->flags & 0x10)) {
            continue;
        }

        material->frameSidnColor = material->sidnColor;

        auto color = reinterpret_cast<uint8_t*>(&material->frameSidnColor);
        color[0] = static_cast<uint8_t>(color[0] * scale >> 8);
        color[1] = static_cast<uint8_t>(color[1] * scale >> 8);
        color[2] = static_cast<uint8_t>(color[2] * scale >> 8);
    }
}

// ref: FUN_007ad020
// Every root, once a map update: its self-illuminated colours follow the sun, a group that has
// not drawn for five seconds gives its buffers back, and a root no def references any more is
// dropped ten seconds later.
void CMapObj::UpdateAll() {
    CWorldScene::s_visibleCallbackDef = nullptr;

    // TODO the reference picks its two WMO draw entry points here by the shader level, which is
    // always 5: the group draw without vertex colours and the one with them, the second swapped
    // for a cheaper path when the world's 0x800 or 0x200 enables are off. Neither draw is ported.

    float dt = CWorld::GetTickTimeSec();

    for (auto mapObj = CMapObj::s_cache.Head(); mapObj; ) {
        auto next = CMapObj::s_cache.Next(mapObj);

        mapObj->UpdateMaterialColors();

        for (auto group = mapObj->m_loadedGroups.Head(); group; ) {
            auto nextGroup = mapObj->m_loadedGroups.Next(group);

            group->m_bufferIdleTime += dt;

            if (5.0f < group->m_bufferIdleTime) {
                group->FreeBuffers();
            }

            group = nextGroup;
        }

        if (!mapObj->m_refCount) {
            mapObj->m_idleTime += dt;

            if (10.0f < mapObj->m_idleTime) {
                CMapObj::s_cache.Unlink(mapObj);
                CMap::FreeMapObj(mapObj);
            }
        }

        mapObj = next;
    }

    if (CMap::s_streamingMode) {
        // TODO FUN_007d9810(): the streaming queue's own pass over the roots still arriving
    }
}

CShaderEffect* CMapObj::s_effects[CMapObj::SHADER_COUNT];
CShaderEffect* CMapObj::s_effectsUnlit[CMapObj::SHADER_COUNT];
uint32_t* CMapObj::s_occlusionHeap;

// ref: FUN_007afee0
// Once, when the map comes up: bind each material shader id to the effect that draws it.
// The names are the ones MapObj.wfx and MapObjU.wfx register, and the order is the order
// the MOMT shader field numbers them.
void CMapObj::Initialize() {
    // TODO the reference also reserves 0x400 more entries in the interior walk's frustum
    // record array (the object at 0x00d1bee8, grown by FUN_004c46c0) and clears its
    // high-water marks. frozen has no portal walk yet, so there is no array to reserve.

    CMapObj::s_effects[0] = CShaderEffectManager::GetEffect("MapObjDiffuse");
    CMapObj::s_effects[1] = CShaderEffectManager::GetEffect("MapObjSpecular");
    CMapObj::s_effects[2] = CShaderEffectManager::GetEffect("MapObjMetal");
    CMapObj::s_effects[3] = CShaderEffectManager::GetEffect("MapObjEnv");
    CMapObj::s_effects[4] = CShaderEffectManager::GetEffect("MapObjOpaque");
    CMapObj::s_effects[5] = CShaderEffectManager::GetEffect("MapObjEnvMetal");
    CMapObj::s_effects[6] = nullptr;

    CMapObj::s_effectsUnlit[0] = CShaderEffectManager::GetEffect("MapObjUDiffuse");
    CMapObj::s_effectsUnlit[1] = CShaderEffectManager::GetEffect("MapObjUSpecular");
    CMapObj::s_effectsUnlit[2] = CShaderEffectManager::GetEffect("MapObjUMetal");
    CMapObj::s_effectsUnlit[3] = CShaderEffectManager::GetEffect("MapObjUEnv");
    CMapObj::s_effectsUnlit[4] = CShaderEffectManager::GetEffect("MapObjUOpaque");
    CMapObj::s_effectsUnlit[5] = CShaderEffectManager::GetEffect("MapObjUEnvMetal");
    CMapObj::s_effectsUnlit[6] = CShaderEffectManager::GetEffect("MapObjUComposite");

    for (uint32_t i = 0; i < CMapObj::SHADER_COUNT; i++) {
        if (CMapObj::s_effects[i]) {
        }
    }

    CMapObj::s_occlusionHeap = STORM_NEW(uint32_t)(
        ObjectAllocAddHeap(CMapObj::OCCLUSION_RECORD_SIZE, 128, "MAPOBJOCC", true)
    );
}

// ref: FUN_007a6da0
// The four floats of a portal rect added componentwise. Four separate scalar adds in the reference,
// not a vector op, and it is its own function there rather than inlined.
//
// NOT WIRED UP YET: frozen's CMapObj::WalkPortals does not call either of these, which is part of why
// its call-order fidelity sits at 39% -- the rest of that gap is seven callees that are still
// unlinked. They go in now because they are unambiguous; whoever raises WalkPortals to the
// reference's shape will need them.
void CMapObj::RectAdd(float* result, const float* a, const float* b) {
    result[0] = a[0] + b[0];
    result[1] = a[1] + b[1];
    result[2] = a[2] + b[2];
    result[3] = a[3] + b[3];
}

// ref: FUN_007a6dd0
// The same four floats divided componentwise. No guard on a zero divisor, which is the reference's
// own shape -- the portal walk only ever divides by an extent it has already measured as non-empty.
void CMapObj::RectDivide(float* result, const float* a, const float* b) {
    result[0] = a[0] / b[0];
    result[1] = a[1] / b[1];
    result[2] = a[2] / b[2];
    result[3] = a[3] / b[3];
}

// ref: FUN_007d77c0
// Walk outward through this group's portals, narrowing `best` to the distance of the nearest portal
// polygon. Recursive, and the recursion is what makes it a reachability question rather than a
// geometric one: a portal only counts if you can get to it.
//
// THE DEPTH CAP IS FOUR, tested on entry, so the walk gives up rather than exploring a building
// exhaustively. Combined with the cameFrom check -- which stops it immediately turning back through
// the portal it arrived by -- that bounds the work without needing a visited set.
//
// THE MASK DECIDES WHETHER A NEIGHBOUR TERMINATES THE WALK. Testing it against the neighbour's MOGI
// flags, a zero result means recurse into that group, and a non-zero one means stop and MEASURE the
// portal leading to it. So the mask names the kind of group whose doorways are the ones worth
// measuring to, and everything else is just corridor.
//
// Only portals within 25 units count -- the reference's own limit, from 0x00a2e868 -- and `best` only
// ever narrows, so a caller seeds it with FLT_MAX and reads it back.
//
// Three arrays, all already declared on this class with the offsets this function uses: m_mopr for
// the portal references, m_mopt for the portals themselves, m_mopv for their vertices, and m_mogi for
// the per-group info whose first field is the flags word.
void CMapObj::AccumulateNearestPortalDistance(uint32_t depth, uint32_t stopMask,
                                              CMapObjGroup* group, CMapObjGroup* cameFrom,
                                              const C3Vector& point, float* best) {
    if (depth >= 4) {
        return;
    }

    for (uint32_t i = 0; i < group->m_portalCount; i++) {
        const SMOPortalRef& ref = this->m_mopr[group->m_portalStart + i];

        CMapObjGroup* other = this->m_groups[ref.groupIndex];

        if (!other || other == cameFrom) {
            continue;
        }

        if (!(this->m_mogi[ref.groupIndex].flags & stopMask)) {
            this->AccumulateNearestPortalDistance(depth + 1, stopMask, other, group, point, best);

            continue;
        }

        const SMOPortal& portal = this->m_mopt[ref.portalIndex];

        float distance = DistancePointPolygonInPlane(point, &this->m_mopv[portal.startVertex],
                                                    portal.count, portal.plane);

        if (distance < 25.0f && distance < *best) {
            *best = distance;
        }
    }
}

// ref: FUN_007d8010
// Seed the walk and hand back what it found. FLT_MAX means nothing within range, which is the same
// answer a group with no portals gives, so a caller cannot tell those apart -- and does not need to.
//
// The mask is 0x8 on its own, or 0x8 together with 0x40 when the caller asks for the wider one. The
// reference selects between them on a FLOAT argument tested against zero rather than a flag, which is
// transcribed as written.
float CMapObj::NearestPortalDistance(CMapObjGroup* group, const C3Vector& point,
                                     float includeFlag40) {
    uint32_t stopMask = includeFlag40 != 0.0f ? 0x48 : 0x8;

    float best = FLT_MAX;

    this->AccumulateNearestPortalDistance(0, stopMask, group, nullptr, point, &best);

    return best;
}

// ref: FUN_007a6d70
// The material a group's liquid surface draws with. Returns null for a group that is not loaded,
// which is the same answer GetGroup gives and is why the caller only needs one check.
//
// This is the function the liquid geometry factory's texture id comes through: the caller takes the
// material this returns and reads its +0x1c. Both offsets it uses were already declared --
// CMapObjGroup::m_liquidMaterial at +0x130 and CMapObj::m_materials at +0x160 -- and the 0x40 stride
// is sizeof(SMOMaterial), which frozen already asserts.
SMOMaterial* CMapObj::GetGroupLiquidMaterial(uint32_t groupIndex) {
    CMapObjGroup* group = this->GetGroup(groupIndex, 0);

    if (!group) {
        return nullptr;
    }

    return &this->m_materials[group->m_liquidMaterial];
}

// The MOMT entry the reference indexes with a shift of six, which is a free check that this
// struct is the right shape: it ends on four runtime words at +0x30, so it measures 0x40.
static_assert(sizeof(SMOMaterial) == 0x40, "SMOMaterial must match the reference's 0x40 stride");

// ref: FUN_007b39b0
// The ground type of one polygon's material -- the building's answer to CMap::GetTerrainType,
// and the other half of what the entity placement stores. A thing standing on open terrain takes
// its type from the ground effect of the layer showing under it; a thing standing on a WMO takes
// it from the material of the polygon it is on, which is this.
//
// SMOMaterial::groundType is at +0x20 and SMOPoly is the two bytes {flags, material}, both of
// which frozen already had at those offsets; the reference reads the material byte at +1 of a
// two-byte stride and scales the material by 0x40, and all three agree.
//
// The group index is the FIRST argument and the poly index the second, which is worth stating
// because the decompilation loses the `this` -- it is in ecx, and the group is fetched from
// this->m_mapObj rather than from anything passed in.
uint32_t CMapObjDef::GetPolyGroundType(uint32_t groupIndex, uint16_t polyIndex) {
    if (!this->m_mapObj) {
        return 0xFFFFFFFF;
    }

    CMapObjGroup* group = this->m_mapObj->GetGroup(groupIndex, 0);

    if (!group || polyIndex >= group->m_faceCount) {
        return 0xFFFFFFFF;
    }

    // The reference tests the ADDRESS of the polygon rather than the array, which can only be
    // null when the array is and the index is zero. Testing the array says the same thing and
    // says it for every index.
    if (!group->m_polys || !this->m_mapObj->m_materials) {
        return 0xFFFFFFFF;
    }

    return this->m_mapObj->m_materials[group->m_polys[polyIndex].material].groundType;
}

// ref: FUN_007aeb40
// The vertex colour at one known face, for the floor-light path that already knows which face
// it is standing on. Three gates before it samples: the root's chunks have to be parsed, the
// group itself has to be loaded, and the group must not carry either of the two MOGP flags in
// 0x48 -- the same pair the interior/exterior decision tests, so a group that counts as outside
// never lends its floor colour to anything.
//
// The reference reads the group's state without checking the slot first, which is safe for it
// because the index comes from a query that just walked that group. The null check here costs a
// compare and makes the function safe to call with an index from anywhere.
bool CMapObj::GroupFaceColor(uint32_t groupIndex, const C3Vector& point, uint16_t face, CImVector* outColor, uint8_t* outFlag) {
    if (!this->m_rootLoaded) {
        return false;
    }

    CMapObjGroup* group = this->m_groups[groupIndex];

    if (!group || !(group->m_state & 0x1)) {
        return false;
    }

    if (group->m_flags & 0x48) {
        return false;
    }

    return group->SampleColorAtFace(point, face, outColor, outFlag);
}

// ref: FUN_007b3990
// Does a segment reach this building's box? Twenty-seven bytes in the reference and nothing but
// a forward to SegmentIntersectsBox against m_bounds, which is the MODF extents in world space.
//
// Worth having as its own function rather than inlining the test at the three call sites, because
// that is what the reference does and because those three are the entry to every walk over the
// placed buildings: the segment queries reject most instances here before touching a group.
int32_t CMapObjDef::SegmentVsBounds(const C3Vector& start, const C3Vector& end) {
    return SegmentIntersectsBox(this->m_bounds, start, end);
}

// ref: FUN_007ae920
// Is the point inside one group's bounds, with no slack? The strict sibling of
// PointInGroupBounds (FUN_007ae970) -- same two gates, same MOGI box, and the reference keeps
// them as two bodies 0x50 bytes apart rather than one with a slack of zero, so they stay two
// here. This one hands the box to CAaBox::IsPointInside where the other inlines three widened
// comparisons.
//
// THE BOX IS THE ROOT'S, NOT THE GROUP'S. It comes from m_mogi[groupIndex].bounds (+0x130, stride
// 0x20, box at +4 -- the shift and the lea in the disassembly say all three), which is the MOGI
// record the root always carries, not CMapObjGroup::m_bounds at +0xb0, which is the BSP's own
// copy. They describe the same group and are not guaranteed to be the same numbers.
bool CMapObj::PointInGroupBox(const C3Vector& point, uint32_t groupIndex) {
    if (!this->m_rootLoaded) {
        return false;
    }

    CMapObjGroup* group = this->m_groups[groupIndex];

    // The reference reads the state without checking the slot, which is safe for it because the
    // index always comes from a walk over the groups it already has. The null check costs a
    // compare and matches what PointInGroupBounds beside it already does.
    if (!group || !(group->m_state & 0x1)) {
        return false;
    }

    return this->m_mogi[groupIndex].bounds.IsPointInside(point) != 0;
}
