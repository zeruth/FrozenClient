#ifndef OBJECT_CLIENT_C_VEHICLE_CAMERA_C_HPP
#define OBJECT_CLIENT_C_VEHICLE_CAMERA_C_HPP

#include "util/GUID.hpp"
#include <tempest/Vector.hpp>
#include <cstdint>

class CGObject_C;
class CGUnit_C;
class VehicleRec;
class VehicleSeatRec;

// The reference's VehicleCamera_C.cpp object (200 bytes from the "Vehicle Camera" heap): the camera
// point a rider's seat gives, held by the rider at CGUnit_C +0xf64 while it rides. It blends from
// where the camera was to the seat's point as the ride starts (states 1 wait, 2 move, 3 aboard) and
// back as it ends (4 wait, 5 move), springing its position and facing along with the vehicle, and
// tells CGCamera what to be relative to.
class CVehicleCamera_C {
    public:
        // Public structs
        // Up to fifteen points, each with a float beside it (the collision code's).
        struct PointSet {
            C3Vector points[15];
            float values[15];
            uint32_t count;

            PointSet() = default;
            PointSet(const PointSet& source);
            // 1 when every point lies within 1/36 of `p`.
            int32_t AllPointsNear(const C3Vector& p) const;
        };

        // FUN_0075b480 / FUN_0075b4f0: the springs' state, saved and restored as a block.
        struct State {
            C3Vector springPosition;
            C3Vector springVelocity;
            float smoothFacing;
            C3Vector followPosition;
            C3Vector followVelocity;
            float collideScale;
            float facingSpring;
        };

        // Public static functions
        static void Initialize();
        static void Shutdown();
        static CVehicleCamera_C* Create(CGUnit_C* unit, uint32_t time);
        static int32_t ConvertSmoothFacingFromRawToWorld(float& smoothFacing, CGObject_C* relativeTo);
        static int32_t ConvertSmoothFacingFromWorldToRaw(float& smoothFacing, CGObject_C* relativeTo);
        static int32_t ToWorld(C3Vector& point, CGObject_C* space);
        static int32_t ToLocal(C3Vector& point, CGObject_C* space);

        // Public member functions
        void Free();
        C3Vector ComputeSeatPosition(CGUnit_C* vehicle);
        // The point the camera sits at, out to it from the vehicle's root as far as the world allows.
        void UpdatePosition();
        int32_t IsControllingFacing();
        void UpdateFlags();
        float GetBlend(uint32_t time);
        WOWGUID GetRelativeGUID();
        void Detach();
        int32_t IsInChain(CVehicleCamera_C* other);
        void UpdatePositionBlend(CGUnit_C* vehicle, uint32_t time, float blend);
        void UpdateFacing(CGUnit_C* vehicle, uint32_t time, float blend, CGObject_C* relative);
        void ApplyCameraBlend(uint32_t time, int32_t duration);
        int32_t AttachToActiveCamera();
        void SetTarget(WOWGUID target, WOWGUID fallback);
        void ChooseTarget(CGUnit_C* vehicle, const VehicleSeatRec* seat, int32_t exiting, WOWGUID* out);
        void BeginTransition(uint32_t time);
        void SetTransitionTime(uint32_t time, int32_t duration);
        void Update(uint32_t time);
        void SaveState(State& out) const;
        void RestoreState(const State& state);

        // Public member variables
        uint32_t m_handle = 0;                      // +0x00
        CGUnit_C* m_unit = nullptr;                 // +0x04, the rider
        // +0x08: 0x1 a transition time is set, 0x2 the seat holds the yaw, 0x4 the yaw is held now,
        // 0x8 the camera blend was applied, 0x10 owns the free look, 0x20 just begun, 0x40 position
        // is current, 0x80 retargeting.
        uint32_t m_flags = 0;
        const VehicleRec* m_vehicleRec = nullptr;   // +0x0c
        const VehicleSeatRec* m_seat = nullptr;     // +0x10
        C3Vector m_startPosition = {};              // +0x14
        C3Vector m_startLocal = {};                 // +0x20
        C3Vector m_seatPosition = {};               // +0x2c
        C3Vector m_position = {};                   // +0x38, after the world's collision
        float m_collideScale = 1.0f;                // +0x44
        float m_collideVelocity = 0.0f;             // +0x48
        C3Vector m_springPosition = {};             // +0x4c
        C3Vector m_springVelocity = {};             // +0x58
        C3Vector m_followPosition = {};             // +0x64
        C3Vector m_followVelocity = {};             // +0x70
        float m_startFacing = 0.0f;                 // +0x7c
        float m_smoothFacing = 0.0f;                // +0x80
        float m_rawFacing = 0.0f;                   // +0x84
        float m_targetFacing = 0.0f;                // +0x88
        float m_facingSpring[2] = {};               // +0x8c
        float m_followFacing[2] = {};               // +0x94
        int32_t m_transitionDuration = 0;           // +0x9c
        uint32_t m_lastTime = 0;                    // +0xa0
        int32_t m_state = 0;                        // +0xa4
        uint32_t m_stateStart = 0;                  // +0xa8
        int32_t m_stateDuration = 0;                // +0xac
        WOWGUID m_unitGUID = 0;                     // +0xb0, the unit the camera targets
        WOWGUID m_relativeGUID = 0;                 // +0xb8, the vehicle the camera is relative to
        WOWGUID m_transportGUID = 0;                // +0xc0
};

#endif
