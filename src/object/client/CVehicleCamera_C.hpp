#ifndef OBJECT_CLIENT_C_VEHICLE_CAMERA_C_HPP
#define OBJECT_CLIENT_C_VEHICLE_CAMERA_C_HPP

#include <tempest/Vector.hpp>
#include <cstdint>
class CGObject_C;

class CVehicleCamera_C {
    public:
        // Public structs
        // Up to fifteen points, each with a float beside it.
        struct PointSet {
            C3Vector points[15];
            float values[15];
            uint32_t count;

            PointSet() = default;
            PointSet(const PointSet& source);
            // 1 when every point lies within 1/36 of `p`.
            int32_t AllPointsNear(const C3Vector& p) const;
        };

        // Public static functions
        static int32_t ConvertSmoothFacingFromRawToWorld(float& smoothFacing, CGObject_C* relativeTo);
};

#endif
