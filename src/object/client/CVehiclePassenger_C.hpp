#ifndef OBJECT_CLIENT_C_VEHICLE_PASSENGER_C_HPP
#define OBJECT_CLIENT_C_VEHICLE_PASSENGER_C_HPP

#include <cstdint>

class CGUnit_C;

// The reference's VehiclePassenger_C.cpp object: one unit's ride on a vehicle, held by the rider at
// CGUnit_C +0xf60 for as long as it is aboard. Only the state its ported functions touch is
// declared, with the reference's offsets in comments; nothing creates one yet, so a unit's
// m_vehiclePassenger is always null and every check against it reads as "not riding".
class CVehiclePassenger_C {
    public:
        // Public member variables
        CGUnit_C* m_vehicle = nullptr;   // ref +0x0c, the unit being ridden
        uint32_t m_flags = 0;            // ref +0x10; 0x800 = the seat animates the rider
        // ref +0x14, where the ride is: 1 and 2 entering, 3 aboard, 4 and 5 leaving. The seat's
        // animation is picked from this (VehicleSeat.dbc has a start/loop pair per phase).
        int32_t m_state = 0;

        // Public member functions

        // ref: FUN_007487e0
        // The rider's animation is the seat's to drive: the seat animates its rider and the vehicle
        // is still alive. A unit for which this is true ignores sequences set on it directly -- the
        // vehicle sets them instead.
        bool IsRidingLiveVehicle() const;
};

#endif
