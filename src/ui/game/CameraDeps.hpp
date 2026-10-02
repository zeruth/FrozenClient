#ifndef UI_GAME_CAMERA_DEPS_HPP
#define UI_GAME_CAMERA_DEPS_HPP

#include "util/GUID.hpp"
#include <storm/Array.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CGObject_C;
class CGPlayer_C;
class CGUnit_C;
class CInputControl;
class CVehicleCamera_C;
class CWFrustum;
class VehicleSeatRec;

// What the camera (Camera.cpp, 0x005fd630..0x00607b00) calls outside its own module. Each is the
// reference function by its address. Those whose module frozen has not ported yet -- vehicles,
// input-control facing, the world's frustum facet query -- say so and return the reference's
// "nothing there" answer, which is also what the reference returns while that state is empty.

// One world triangle a frustum query returns (three points, in world space).
struct CFacet {
    C3Vector m_points[3];
};

// ref: FUN_0052e480
float BarberShopGetMinDistance();

// ref: FUN_0077f310
// The world segment query: terrain, map objects, liquid and models along start -> end. `t` comes
// in as the furthest fraction to look and leaves as the nearest hit; `hit` gets the point.
int32_t WorldQuerySegment(const C3Vector& start, const C3Vector& end, C3Vector* hit, float* t, uint32_t flags, void* result);

// ref: FUN_0077f8d0
// The world triangles a frustum touches.
void WorldQueryFrustumFacets(CWFrustum* frustum, TSGrowableArray<CFacet>& facets);

// The part of a triangle inside a frustum, as pointers to its clipped points.
int32_t FrustumClipFacet(CWFrustum* frustum, const C3Vector* points, uint32_t count, uint32_t** clipped, uint32_t* clippedCount);

// ref: FUN_005ff670
// A frustum from the camera's view and projection whose far corners are pulled toward `origin`
// by `scale`.
void CameraSetFrustumCorners(CWFrustum& frustum, const C44Matrix& view, const C44Matrix& projection, const C3Vector& origin, float scale);

// Turn the camera's facing matrix about its right axis by `angle`.
void CameraRollFacing(C33Matrix& facing, float angle);

// The index of the component with the largest magnitude.
int32_t DominantAxis(const C3Vector& vector);

// ref: FUN_0071c1e0
// A world facing turned into the unit's raw facing, clamped to the range its vehicle seat allows;
// 1 when it had to be clamped.
int32_t UnitConvertFacingToRaw(CGUnit_C* unit, float* facing);

// ref: FUN_00735f60
void UnitUpdateSmoothFacing(CGUnit_C* unit, int32_t immediate);

// ref: FUN_004f7290
// The field of view offset the player's state asks for (degrees).
float PlayerGetSwimDepth(CGPlayer_C* player);

// ref: FUN_004f8660
// The player model's alpha as the camera closes in on it.
void WorldFrameSetPlayerAlpha(uint8_t alpha);

// ref: FUN_005fb260
void InputControlSetFacing(CInputControl* input, int32_t time, float facing);

// ref: FUN_005fbe70
void InputControlSetPitch(CInputControl* input, int32_t time, float pitch);

// The input control flags word (reference +0x4).
uint32_t InputControlGetFlags(CInputControl* input);

// The vehicle the unit rides, or its own vehicle camera.
int32_t UnitIsRidingControlledVehicle(CGUnit_C* unit);
CVehicleCamera_C* UnitGetVehicleCamera(CGUnit_C* unit);
int32_t UnitHasVehicleCamera(CGUnit_C* unit);
int32_t UnitVehicleCameraActive(CGUnit_C* unit);
int32_t UnitGetVehicleFade(CGUnit_C* unit, float* fadeNear, float* fadeFar);
int32_t UnitHasTransport(CGUnit_C* unit);
int32_t PlayerIsOnVehicleWith(CGPlayer_C* player, CGUnit_C* unit);
WOWGUID UnitGetCameraTransport(CGUnit_C* unit);

// Unit state the camera reads straight off the unit in the reference.
float UnitGetMountScale(CGUnit_C* unit);
int32_t UnitGetStandState(CGUnit_C* unit);
float UnitGetCameraSwimDepth(CGUnit_C* unit);
float UnitGetCameraMaxHeight(CGUnit_C* unit);
float UnitGetWaterDepth(CGUnit_C* unit);

// The object's scale and model height (vtable +0x9c / +0xa0 in the reference).
float ObjectGetScale(CGObject_C* object);
float ObjectGetRenderScale(CGObject_C* object);
float ObjectGetModelHeight(CGObject_C* object);

// The ground height under `position`, or leaves `height` alone when there is none.
int32_t WorldGetFloorHeight(const C3Vector& position, float* height);

// The liquid type the camera is in, and the sound system's underwater filter for it.
int32_t WorldGetCameraLiquidType();
void SoundSetUnderwater(int32_t liquid);

#endif
