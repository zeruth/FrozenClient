#ifndef OBJECT_CLIENT_C_VEHICLE_C_HPP
#define OBJECT_CLIENT_C_VEHICLE_C_HPP

#include "object/movement/CPassenger.hpp"
#include "util/GUID.hpp"
#include <storm/List.hpp>
#include <tempest/Matrix.hpp>
#include <cstdint>

struct CClientObjCreate;
class CGUnit_C;
class VehicleRec;
class VehicleSeatRec;

// The reference's Vehicle_C.cpp object (0x180 bytes from the "Vehicle" object heap): what a unit
// that can be ridden keeps. Its Vehicle.dbc row and seats, the matrix its seats are placed by, the
// passengers riding it (threaded through CPassenger::m_transportLink like a transport's), the
// animations it plays for them, and which seats are free.
class CVehicle_C {
    public:
        // Public structs
        // A passenger-driven animation the vehicle is playing (kind 0..0x22, 0x1a meaning the whole
        // model): who asked for it, and a callback that says whether they still want it.
        typedef void (*SlotCallback)(CGUnit_C* unit, int32_t* keep, void* passengers);
        struct Slot {
            WOWGUID guid;
            uint32_t kind;
            SlotCallback callback;
        };

        // Public static functions
        // FUN_00756e30
        static void Initialize();
        // FUN_00756e90
        static void Destroy();
        // FUN_00757fa0
        // A vehicle for `owner`, placed where the create block (or the unit itself) says.
        static CVehicle_C* Create(const CClientObjCreate* init, CGUnit_C* owner, int32_t recID);

        // Public member functions
        // FUN_00757bb0
        void Release(int32_t ejectPassengers);
        // FUN_007580f0
        void SetRec(int32_t recID);
        // FUN_00756ec0
        const VehicleSeatRec* GetSeatRec(uint8_t seat) const;
        // FUN_00756f00
        // A vehicle that aims freely: not swimming or flying, and its row sets 0x40000 without
        // 0x40000000.
        bool AimsFreely() const;
        uint32_t HasFlag26() const;
        // Bit `index` of m_flags; 0x1a reads as clear and anything past 0x22 reads bit 0x1a.
        uint32_t TestFlag(uint32_t index) const;
        // Fills the first free slot; 0 when all sixteen are taken. `kind` past 0x22 is stored as 0x1a.
        int32_t AddSlot(WOWGUID guid, uint32_t kind, SlotCallback callback);
        // Frees every slot holding `guid`.
        void RemoveSlot(WOWGUID guid);
        bool HasStateBits() const;
        void SetStateBit(uint8_t bit);
        void ClearStateBit(uint8_t bit);
        // FUN_00756db0
        void SetPendingSlot(WOWGUID guid, uint32_t kind);
        // FUN_007571c0
        // The vehicle, not the seat, decides its riders' animations: either flag 26 is set or its
        // owner is driving the pose.
        bool ControlsPassengerAnimation() const;
        // FUN_00756f40
        bool OwnerIsControllingAnimation() const;
        // FUN_00756ca0
        bool CanPlay(uint32_t sequence) const;
        // FUN_00756f80
        // The owner plays `sequence` on bone `kind` for a passenger; the kind's bit is set.
        int32_t PlaySlotSequence(uint32_t kind, uint32_t sequence);
        // FUN_00757280
        // The passengers still wanting `kind` are asked; when none does the bit clears and the
        // owner's animation is put back.
        void StopSlotSequence(CGUnit_C* owner, uint32_t kind, uint32_t sequence, int32_t force, uint32_t time);
        // FUN_007577e0
        void RemoveSlotSequence(uint32_t kind, WOWGUID guid);
        // FUN_00757060
        void EndEnterSequences(uint32_t kind, uint32_t eventId);
        // FUN_007570f0
        void EndExitSequences(uint32_t kind, uint32_t eventId);
        // FUN_00757000
        // The vehicle at the top of the chain this one rides, or this one.
        CVehicle_C* GetRoot();
        // FUN_00757470
        // The seats of this vehicle and every vehicle riding it.
        uint32_t CountSeats();
        // FUN_00757550
        int32_t FindSeat(int32_t* index, CVehicle_C** vehicle, uint8_t* seat);
        // FUN_00757680
        CGUnit_C* GetPassengerInSeat(uint8_t seat);
        // FUN_007576e0
        void EjectAllPassengers();
        // FUN_00757980
        int32_t AddPassenger(CPassenger* passenger);
        // FUN_00757be0
        // The matrix the seats are placed by: the owner's position and facing, after `parent`'s.
        // A vehicle riding this one follows.
        void UpdateMatrix(const C44Matrix* parent);
        // FUN_00758130
        void UpdateMatrixFromTransport();
        // FUN_00757d10
        // How far the passengers reach: the widest loaded passenger model's bounding sphere,
        // a passenger vehicle's reach twice over; a change carries up the chain.
        void UpdatePassengerRadius();
        // FUN_00757e70
        void ResyncPassengerAnimations();
        // FUN_00757ef0
        // The seats that are free: those the row names that no passenger sits in.
        void UpdateFreeSeats();

        // Public member variables
        uint32_t m_handle = 0;                  // +0x00, the object heap's handle
        CGUnit_C* m_owner = nullptr;            // +0x04, the unit this vehicle is
        uint32_t m_animating = 0;               // +0x08, bit 0: a passenger animation was played
        // +0x0c, the Vehicle.dbc row. Several checks gate on it being present at all, which is
        // what the accessors on CGUnit_C return.
        const VehicleRec* m_rec = nullptr;
        C44Matrix m_matrix;                     // +0x10
        float m_pitch = 0.0f;                   // +0x50
        float m_passengerRadius = 0.0f;         // +0x54
        uint32_t m_flags[2] = {};               // +0x58, one bit per animation kind 0..0x22
        Slot m_slots[16] = {};                  // +0x60, free when guid is 0
        WOWGUID m_pendingGUID = 0;              // +0x160
        uint32_t m_pendingKind = 0xFFFFFFFF;    // +0x168
        uint8_t m_stateBits = 0;                // +0x16c, one bit per seat whose rider the vehicle animates
        uint8_t m_freeSeats = 0;                // +0x16d
        // +0x170: the passengers riding it.
        STORM_EXPLICIT_LIST(CPassenger, m_transportLink) m_passengers;
};

void VehicleSendEjectPassenger(WOWGUID passenger);

#endif
