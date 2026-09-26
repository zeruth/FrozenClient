#include "object/client/CVehiclePassenger_C.hpp"
#include "object/client/CGUnit_C.hpp"

// ref: FUN_007487e0
bool CVehiclePassenger_C::IsRidingLiveVehicle() const {
    if (!(this->m_flags & 0x800)) {
        return false;
    }

    return this->m_vehicle && this->m_vehicle->Unit()->health > 0;
}
