#ifndef OBJECT_CLIENT_UNIT_VEHICLE_C_HPP
#define OBJECT_CLIENT_UNIT_VEHICLE_C_HPP

#include "util/GUID.hpp"
#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

struct CClientObjCreate;
class CDataStore;
class CGUnit_C;
class VehicleRec;
class VehicleSeatRec;

// The reference's UnitVehicle_C.cpp (0x0074b900 .. 0x0074d070) and the Unit_C.cpp pieces it leans
// on: a unit's side of being a vehicle or riding one.

// FUN_00717fc0
// The unit's raw position carried out through its transport (a unit transport's seat matrix, any
// other transport's world matrix).
C3Vector UnitTransportPosition(CGUnit_C* unit);
// FUN_00717ec0
void UnitSeatMatrix(CGUnit_C* unit, C44Matrix& out);
// FUN_0074c650
// The vehicle at the root of the chain the unit rides (stopping at `stop`), or the unit itself when
// it is a vehicle on nothing.
CGUnit_C* UnitGetVehicleRoot(CGUnit_C* unit, CGUnit_C* stop);
// FUN_004f6230
CGUnit_C* UnitGetRootVehicle(CGUnit_C* unit);
// FUN_0074cc40
// The unit's top-most ride along its passengers' next vehicles (stopping at `stop`).
CGUnit_C* UnitGetRideChainTop(CGUnit_C* unit, CGUnit_C* stop);
// FUN_00738440
void UnitResetSequencesForRide(CGUnit_C* unit);
// FUN_0074b9c0
void UnitSetVehicleExitAnimation(CGUnit_C* unit, const VehicleSeatRec* seat, int32_t anim);
// FUN_0074b840
int32_t UnitAllowsSeatAnimation(CGUnit_C* unit, const VehicleSeatRec* seat, int32_t leaving);
// FUN_0074ba40
// The active player's vehicle holds the camera (or the input) for now: a ride in transition, a seat
// that keeps the camera, or a vehicle controlled by someone else.
int32_t UnitVehicleHoldsControl(CGUnit_C* unit);
// FUN_0074bb90
uint32_t UnitVehicleAimFlags(CGUnit_C* unit);
// FUN_0074bbd0
void UnitSignalVehicleBegin(CGUnit_C* unit);
// FUN_0074bc50
void UnitSignalVehicleEnd(CGUnit_C* unit);
// FUN_0074c5a0
void VehicleUpdateAngleEvents(CGUnit_C* unit, const VehicleRec* rec);
// FUN_0074bcb0
// Whether the move to `transport` at `seat` plays a vehicle animation first.
int32_t UnitSeatMoveAnimates(CGUnit_C* unit, WOWGUID transport, uint8_t seat);
// FUN_0074be10
int32_t UnitStartSeatMoveAnimation(CGUnit_C* unit, WOWGUID transport, uint8_t seat);
// FUN_0074c040
// A monster move for a riding unit that waits on the vehicle's animation: queued on the ride.
int32_t UnitQueueVehicleMove(CGUnit_C* unit, CDataStore* msg, WOWGUID transport, uint8_t seat);
// FUN_0074c750
void UnitCreateVehicle(CGUnit_C* unit, const CClientObjCreate* init, int32_t recID);
// FUN_0074c7b0
void UnitDestroyVehicle(CGUnit_C* unit, int32_t eject);
// FUN_0074c7f0
void UnitRequestVehicleExit(CGUnit_C* unit);
// FUN_0074c8b0
void UnitRequestVehiclePrevSeat(CGUnit_C* unit);
// FUN_0074c9a0
void UnitRequestVehicleNextSeat(CGUnit_C* unit);
// FUN_0074ca90
int32_t UnitRequestVehicleSwitchSeat(CGUnit_C* unit, CGUnit_C* vehicle, int32_t index);
// FUN_0074cce0
void UnitVehicleExitIfSeatEjects(CGUnit_C* unit);
// FUN_0074cf30
// The unit's transport changed: its ride is made (or ended) and moves to `transport` at `seat`.
void VehicleOnTransportChanged(CGUnit_C* unit, WOWGUID transport, uint8_t seat, int32_t force);
// FUN_00737390
void UnitSetRideAlpha(CGUnit_C* unit, float alpha);
// FUN_005fb560
int32_t InputControlSeatAllows(uint32_t flag);
// FUN_007561e0
void UnitOnLeftVehicle(CGUnit_C* unit);

#endif
