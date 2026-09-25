#include "object/client/CVehicle_C.hpp"
#include "client/ClientServices.hpp"
#include "net/Types.hpp"
#include <common/DataStore.hpp>

// ref: FUN_00756c90
uint32_t CVehicle_C::HasFlag26() const {
    return this->m_flags[0] & 0x4000000;
}

// ref: FUN_00756cd0
uint32_t CVehicle_C::TestFlag(uint32_t index) const {
    if (index == 0x1a) {
        return 0;
    }

    if (index > 0x22) {
        index = 0x1a;
    }

    return (1u << (index & 0x1f)) & this->m_flags[index >> 5];
}

// ref: FUN_00756d10
int32_t CVehicle_C::AddSlot(WOWGUID guid, uint32_t kind, uint32_t value) {
    if (kind > 0x22) {
        kind = 0x1a;
    }

    for (uint32_t i = 0; i < 16; i++) {
        auto& slot = this->m_slots[i];

        if (slot.guid == 0) {
            slot.guid = guid;
            slot.kind = kind;
            slot.value = value;

            return 1;
        }
    }

    return 0;
}

// ref: FUN_00756d70
void CVehicle_C::RemoveSlot(WOWGUID guid) {
    for (uint32_t i = 0; i < 16; i++) {
        if (this->m_slots[i].guid == guid) {
            this->m_slots[i].guid = 0;
        }
    }
}

// ref: FUN_00756de0
bool CVehicle_C::HasStateBits() const {
    return this->m_stateBits != 0;
}

// ref: FUN_00756df0
void CVehicle_C::SetStateBit(uint8_t bit) {
    this->m_stateBits |= static_cast<uint8_t>(1 << (bit & 0x1f));
}

// ref: FUN_00756e10
void CVehicle_C::ClearStateBit(uint8_t bit) {
    this->m_stateBits &= static_cast<uint8_t>(~(1 << (bit & 0x1f)));
}

// ref: FUN_00757200
void VehicleSendEjectPassenger(WOWGUID passenger) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_EJECT_PASSENGER));
    msg.Put(passenger);
    msg.Finalize();
    ClientServices::Send(&msg);
}
