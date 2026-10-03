#ifndef OBJECT_CLIENT_C_VEHICLE_PASSENGER_C_HPP
#define OBJECT_CLIENT_C_VEHICLE_PASSENGER_C_HPP

#include "util/GUID.hpp"
#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CDataStore;
class CGUnit_C;
class CM2Model;
class CVehicle_C;
class VehicleRec;
class VehicleSeatRec;

// The reference's VehiclePassenger_C.cpp object (0xe0 bytes from the "Vehicle Passenger" heap):
// one unit's ride, held by the rider at CGUnit_C +0xf60 for as long as it is aboard or moving in or
// out. The ride is a small state machine -- 1 and 2 entering (waiting, then moving to the seat),
// 3 aboard, 4 and 5 leaving (waiting, then moving off), 0 none -- and every living ride is on one
// list the frame steps (UpdateAll).
class CVehiclePassenger_C {
    public:
        // Public static functions
        // FUN_0074a070
        static void Initialize();
        // FUN_0074a160
        static void Shutdown();
        // FUN_00749790
        static CVehiclePassenger_C* Create(CGUnit_C* unit);
        // FUN_0074b130
        static void UpdateAll(uint32_t time);
        // FUN_00747ae0
        static void ClearOverrideFacings();
        // FUN_00749cb0
        static void ProcessRescues();
        // FUN_00749c20
        static void QueueRescue(WOWGUID unit, WOWGUID vehicle, uint8_t seat, int32_t exitAnim);
        // FUN_00747980
        static void KeepSlot(CGUnit_C* unit, int32_t* keep, void* passengers);
        // FUN_00749d50
        static void ReleaseSlotCallback(CGUnit_C* unit, int32_t* keep, void* passengers);
        // FUN_00747f40
        static void OnActivePlayerVehicleChanged(CGUnit_C* vehicle);
        // FUN_00749ed0
        static void SignalActivePlayerSeat();
        // FUN_007498e0
        // A unit's transport changed while it was riding: it is seated on the new one at once.
        static void OnTransportReplaced(CGUnit_C* unit);

        // Public member functions
        // FUN_00748950
        void Free();
        // FUN_00749a30
        void Destroy();
        // FUN_007487e0
        // The rider's animation is the vehicle's to drive (flag 0x800) and the rider is alive.
        bool IsRidingLiveVehicle() const;
        // FUN_00747b20
        // The animation the seat wants the rider to play right now: the start animation for the
        // phase the ride is in, or its loop when the start is over or missing. 0x1fa when the seat
        // does not animate its rider in this phase.
        int32_t GetSeatAnimation(const VehicleSeatRec* seat) const;
        // FUN_00747bd0
        int32_t GetSeatUpperAnimation(const VehicleSeatRec* seat) const;
        // FUN_007484e0
        void OnRiderSequenceDone(uint32_t boneId);
        // FUN_00748560
        bool GetRideAnimation(uint32_t allow, int32_t* out) const;
        // FUN_007485b0
        bool GetRideUpperAnimation(uint32_t allow, int32_t* out) const;
        // FUN_00747900
        void ClearInputLock();
        // FUN_00747910
        void FreeQueuedMove();
        // FUN_00748170
        // A monster move that arrived mid-transition, kept to be played when it ends.
        void QueueMove(CDataStore* msg);
        // FUN_00747990
        void PlayQueuedMove();
        // FUN_00747930
        bool MatchesExitEvent(uint32_t eventId) const;
        // FUN_007481e0
        bool MatchesEnterEvent(uint32_t eventId) const;
        // FUN_00748230
        void ReleaseSlot();
        // FUN_007479e0
        void ClearAnimVehicle(WOWGUID vehicle, const VehicleSeatRec* seat);
        // FUN_00747a30
        void BlendFacing();
        // FUN_00747b00
        void SetOverrideFacing(float facing);
        // FUN_00747d50
        bool IsMoving() const;
        // FUN_00747d70
        void UpdateProgress(uint32_t time, const VehicleSeatRec* seat);
        // FUN_00747e90
        C3Vector FromTransitionSpace(const C3Vector& local, const C3Vector& fallback) const;
        // FUN_00747fa0
        void LockInput();
        // FUN_00747ff0
        CGUnit_C* GetVehicleUnit() const;
        // FUN_00748040
        CGUnit_C* GetRootVehicleUnit() const;
        // FUN_00749060
        CVehicle_C* GetVehicle() const;
        // FUN_00748070
        void SetPending(WOWGUID target, uint8_t seat, const VehicleSeatRec* seatRec, int32_t exiting, CVehicle_C* root);
        // FUN_007482a0
        C3Vector GetTransitionPosition() const;
        // FUN_00748400
        void UpdateAttachOffset(const VehicleSeatRec* seat, CM2Model* model);
        // FUN_007484c0
        float GetFacing() const;
        // FUN_00748620
        // The rider's model takes the vehicle's animation, bone for bone.
        void SyncToVehicle();
        // FUN_007489c0
        void SetState(int32_t state, CGUnit_C* vehicle, uint8_t seat, const VehicleSeatRec* seatRec, uint32_t time,
                      WOWGUID space);
        // FUN_007490c0
        bool CheckPendingTimeout();
        // FUN_007490f0
        void BuildSeatMatrix(C44Matrix& matrix, CM2Model** model, CGUnit_C* vehicle, const VehicleSeatRec* seat,
                             uint32_t attachment, float facing);
        // FUN_007493b0
        void GetSeatPosition(CGUnit_C* vehicle, CM2Model* model, const VehicleSeatRec* seat, C3Vector& out);
        // FUN_00749aa0
        void BeginTransition(WOWGUID vehicle, uint8_t seat, int32_t exitAnim);
        // FUN_00749d80
        C3Vector GetPosition();
        // FUN_00749e40
        C3Vector GetTransitionTarget(CGUnit_C* vehicle, const VehicleSeatRec* seat);
        // FUN_0074a200
        void PlanTransition(CGUnit_C* vehicle, const VehicleSeatRec* seat, uint32_t time, C3Vector* target);
        // FUN_0074a7f0
        void PlaceModel();
        // FUN_0074ad70
        int32_t UpdateTransition(CGUnit_C* vehicle, const VehicleSeatRec* seat, uint32_t time);
        // FUN_0074af70
        int32_t Update(CGUnit_C* vehicle, const VehicleSeatRec* seat, uint32_t time);
        // FUN_0074b0b0
        void Tick(uint32_t time);
        // FUN_0074b160
        void SetRootVehicle(WOWGUID root);
        // FUN_0074b200
        void OnTransportChanged(WOWGUID transport, uint8_t seat, CGUnit_C* vehicle, int32_t immediate);

        // Public member variables
        uint32_t m_handle = 0;                      // +0x00
        CVehiclePassenger_C* m_prev = nullptr;      // +0x04, on the frame's list
        CVehiclePassenger_C* m_next = nullptr;      // +0x08
        CGUnit_C* m_unit = nullptr;                 // +0x0c, the rider
        // +0x10: 0x1 the move is planned, 0x2/0x4 the seat's start played (body / upper body),
        // 0x8 input is locked, 0x10 the model is placed by the ride, 0x20 the attachment offset is
        // known, 0x40 the seat has an attachment, 0x80 a pending move waits, 0x100 it is an exit,
        // 0x200 a switch is pending, 0x400 keep the camera, 0x800 the vehicle animates the rider,
        // 0x1000 an override facing is held.
        uint32_t m_flags = 0;
        int32_t m_state = 0;                        // +0x14
        int32_t m_prevState = 0;                    // +0x18
        WOWGUID m_vehicleGUID = 0;                  // +0x20, the vehicle left (or being left)
        WOWGUID m_nextVehicleGUID = 0;              // +0x28, the vehicle entered (or being entered)
        uint8_t m_seatIndex = 0xFF;                 // +0x30
        uint8_t m_nextSeatIndex = 0xFF;             // +0x31
        uint32_t m_transitionStart = 0;             // +0x34
        uint32_t m_transitionEnd = 0;               // +0x38
        WOWGUID m_spaceGUID = 0;                    // +0x40, the transport the transition is relative to
        float m_progress = 0.0f;                    // +0x48
        uint32_t m_inputLockTime = 0;               // +0x4c
        const VehicleRec* m_vehicleRec = nullptr;   // +0x50
        const VehicleSeatRec* m_seat = nullptr;     // +0x54
        WOWGUID m_rootGUID = 0;                     // +0x58
        CDataStore* m_queuedMove = nullptr;         // +0x60
        uint32_t m_pendingTime = 0;                 // +0x64
        WOWGUID m_pendingRoot = 0;                  // +0x68
        WOWGUID m_animVehicleGUID = 0;              // +0x70
        WOWGUID m_pendingTarget = 0;                // +0x78
        uint8_t m_pendingSeat = 0xFF;               // +0x80
        C3Vector m_startPosition = {};              // +0x84
        C3Vector m_startLocal = {};                 // +0x90
        C3Vector m_startTransport = {};             // +0x9c
        C3Vector m_startTransportLocal = {};        // +0xa8
        float m_startFacing = 0.0f;                 // +0xb4
        float m_startSmoothFacing = 0.0f;           // +0xb8
        float m_facing = 0.0f;                      // +0xbc
        float m_targetFacing = 0.0f;                // +0xc0
        float m_gravity = 0.0f;                     // +0xc4
        float m_arcScale = 1.0f;                    // +0xc8
        C3Vector m_attachOffset = {};               // +0xcc
        float m_overrideFacing = 0.0f;              // +0xd8
};

#endif
