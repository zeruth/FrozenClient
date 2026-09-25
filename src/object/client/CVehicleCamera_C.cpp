#include "object/client/CVehicleCamera_C.hpp"
#include "object/Client.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include <cstring>

// ref: FUN_00759c60
int32_t CVehicleCamera_C::ConvertSmoothFacingFromRawToWorld(float& smoothFacing, CGObject_C* relativeTo) {
    if (!relativeTo) {
        return 0;
    }

    if (relativeTo->IsA(TYPE_UNIT)) {
        auto transport = ClntObjMgrObjectPtr(relativeTo->GetTransportGUID(), TYPE_OBJECT, __FILE__, __LINE__);
        float facing = static_cast<CGUnit_C*>(relativeTo)->GetRawSmoothFacing();
        CVehicleCamera_C::ConvertSmoothFacingFromRawToWorld(facing, transport);
        smoothFacing = facing + smoothFacing;

        return 1;
    }

    smoothFacing = relativeTo->GetFacing() + smoothFacing;

    return 1;
}

// ref: FUN_0075b610
CVehicleCamera_C::PointSet::PointSet(const PointSet& source) {
    this->count = source.count;
    memcpy(this->points, source.points, source.count * sizeof(C3Vector));
    memcpy(this->values, source.values, source.count * sizeof(float));
}

// ref: FUN_0075c6f0
int32_t CVehicleCamera_C::PointSet::AllPointsNear(const C3Vector& p) const {
    for (uint32_t i = 0; i < this->count; i++) {
        const C3Vector& point = this->points[i];

        // 1/1296 (0x00aa2cec)
        if (0.0007716049440205097f < (p.y - point.y) * (p.y - point.y) + (p.z - point.z) * (p.z - point.z) + (p.x - point.x) * (p.x - point.x)) {
            return 0;
        }
    }

    return 1;
}
