#ifndef OBJECT_CLIENT_C_VEHICLE_CAMERA_C_HPP
#define OBJECT_CLIENT_C_VEHICLE_CAMERA_C_HPP

#include "util/GUID.hpp"
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

        // Public member variables. What CGCamera reads off the seat camera it follows; the
        // reference offsets are not mapped yet. Nothing constructs a CVehicleCamera_C in frozen,
        // so none of these is reached until the vehicle module is ported.
        uint32_t m_flags = 0;           // ref +0x8: 0x10 owns the free look, 0x40 position is current
        WOWGUID m_unitGUID = 0;         // the unit whose seat this is
        C3Vector m_position = { 0.0f, 0.0f, 0.0f };
        float m_smoothFacing = 0.0f;

        // Public member functions. TODO port from VehicleCamera_C.cpp (0x00759580..0x0075af40).
        int32_t IsControllingFacing() const { return 0; }
        void UpdatePosition() {}
        WOWGUID GetRelativeGUID() const { return 0; }
        void Detach() {}
        void Update(int32_t worldTime) {}
};

#endif
