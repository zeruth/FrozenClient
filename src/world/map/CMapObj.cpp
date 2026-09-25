#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjGroup.hpp"

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
