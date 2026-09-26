#ifndef OBJECT_CLIENT_C_VEHICLE_C_HPP
#define OBJECT_CLIENT_C_VEHICLE_C_HPP

#include "util/GUID.hpp"
#include <cstdint>

// Vehicle.dbc. Not loaded yet -- the reference keeps the row on every CVehicle_C and reads its flags
// and its eight seat ids out of it, and nothing ported does either, so the type stays a name.
class CGUnit_C;
class VehicleRec;

// The reference's Vehicle_C.cpp object. Only the state its ported functions touch is declared;
// nothing creates one yet.
class CVehicle_C {
    public:
        // Public structs
        struct Slot {
            WOWGUID guid;
            uint32_t kind;
            uint32_t value;
        };

        // Public member variables
        CGUnit_C* m_owner = nullptr;    // ref +0x04, the unit this vehicle is
        // ref +0x0c, the Vehicle.dbc row. Several checks gate on it being present at all, which is
        // what the accessors on CGUnit_C return.
        const VehicleRec* m_rec = nullptr;
        uint32_t m_flags[2] = {};   // ref +0x58, bits 0..0x22
        Slot m_slots[16] = {};      // ref +0x60, free when guid is 0
        uint8_t m_stateBits = 0;    // ref +0x16c

        // Public member functions
        uint32_t HasFlag26() const;
        // Bit `index` of m_flags; 0x1a reads as clear and anything past 0x22 reads bit 0x1a.
        uint32_t TestFlag(uint32_t index) const;
        // Fills the first free slot; 0 when all sixteen are taken. `kind` past 0x22 is stored as 0x1a.
        int32_t AddSlot(WOWGUID guid, uint32_t kind, uint32_t value);
        // Frees every slot holding `guid`.
        void RemoveSlot(WOWGUID guid);
        bool HasStateBits() const;

        // ref: FUN_007571c0
        // The vehicle, not the seat, decides its riders' animations: either flag 26 is set or its
        // owner is driving the pose.
        bool ControlsPassengerAnimation() const;

        // ref: FUN_00756f40
        // The vehicle's row allows it (Vehicle.dbc flag 0x10000) and its owner is either holding an
        // animation or has something pending: the owner, not the seat, drives the pose.
        bool OwnerIsControllingAnimation() const;
        void SetStateBit(uint8_t bit);
        void ClearStateBit(uint8_t bit);
};

void VehicleSendEjectPassenger(WOWGUID passenger);

#endif
