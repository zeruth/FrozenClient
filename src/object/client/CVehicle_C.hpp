#ifndef OBJECT_CLIENT_C_VEHICLE_C_HPP
#define OBJECT_CLIENT_C_VEHICLE_C_HPP

#include "util/GUID.hpp"
#include <cstdint>

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
        void SetStateBit(uint8_t bit);
        void ClearStateBit(uint8_t bit);
};

void VehicleSendEjectPassenger(WOWGUID passenger);

#endif
