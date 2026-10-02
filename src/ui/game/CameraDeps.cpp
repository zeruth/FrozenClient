#include "ui/game/CameraDeps.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "ui/InputControl.hpp"
#include "world/CWFrustum.hpp"
#include "world/CWorld.hpp"
#include <cmath>

// ref: FUN_0052e480
// The barber shop's minimum camera distance (0x00acc788).
float BarberShopGetMinDistance() {
    return 1.0f;
}

int32_t WorldQuerySegment(const C3Vector& start, const C3Vector& end, C3Vector* hit, float* t, uint32_t flags, void* result) {
    // TODO port FUN_007a3b70 (terrain cells FUN_007a39f0, map objects FUN_007a30d0); until then
    // nothing is hit and the camera does not collide.
    return 0;
}

void WorldQueryFrustumFacets(CWFrustum* frustum, TSGrowableArray<CFacet>& facets) {
    // TODO port FUN_007ad700 (the map-object facet gather behind FUN_0077f8d0)
}

int32_t FrustumClipFacet(CWFrustum* frustum, const C3Vector* points, uint32_t count, uint32_t** clipped, uint32_t* clippedCount) {
    // TODO the reference clips against the frustum planes; unreachable while
    // WorldQueryFrustumFacets returns nothing.
    *clipped = nullptr;
    *clippedCount = 0;

    return 0;
}

void CameraSetFrustumCorners(CWFrustum& frustum, const C44Matrix& view, const C44Matrix& projection, const C3Vector& origin, float scale) {
    // TODO port FUN_005ff670: FrustumCorners(view, projection), the near four corners pulled
    // toward `origin` by `scale`, then CWFrustum::SetCorners. Only CollideFrustum reads the
    // result, and only through WorldQueryFrustumFacets.
}

// ref: FUN_004c55b0
// facing = RotationY(angle) * facing.
void CameraRollFacing(C33Matrix& facing, float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);

    C33Matrix rotation(c, 0.0f, -s, 0.0f, 1.0f, 0.0f, s, 0.0f, c);
    facing = rotation * facing;
}

int32_t DominantAxis(const C3Vector& vector) {
    float x = std::fabs(vector.x);
    float y = std::fabs(vector.y);
    float z = std::fabs(vector.z);

    if (y <= x) {
        return z <= x ? 0 : 2;
    }

    return z <= y ? 1 : 2;
}

int32_t UnitConvertFacingToRaw(CGUnit_C* unit, float* facing) {
    // TODO port FUN_0071c1e0: clamps to the vehicle seat's facing range (FUN_006eaa50). Frozen
    // has no seats, so there is never a range and the reference returns 0 too.
    return 0;
}

void UnitUpdateSmoothFacing(CGUnit_C* unit, int32_t immediate) {
    // TODO port FUN_00735f60 (CGUnit_C's smoothed facing step); frozen keeps the smooth facing
    // equal to the raw one.
}

float PlayerGetSwimDepth(CGPlayer_C* player) {
    // TODO port FUN_004f7290 (reads a descriptor byte and +0x2b8, scaled by 0x009f1968)
    return 0.0f;
}

void WorldFrameSetPlayerAlpha(uint8_t alpha) {
    // TODO port FUN_004f8660 -> FUN_00737390 (the target unit's model alpha)
}

void InputControlSetFacing(CInputControl* input, int32_t time, float facing) {
    // TODO port FUN_005fb260 (turns the active mover through the movement system)
}

void InputControlSetPitch(CInputControl* input, int32_t time, float pitch) {
    // TODO port FUN_005fbe70
}

uint32_t InputControlGetFlags(CInputControl* input) {
    return input ? input->m_unk04 : 0;
}

// Vehicles: frozen creates no CVehicle_C, CVehiclePassenger_C or CVehicleCamera_C yet, so each of
// these gives the reference's answer for a unit that is not on one.
int32_t UnitIsRidingControlledVehicle(CGUnit_C* unit) {
    return 0;
}

CVehicleCamera_C* UnitGetVehicleCamera(CGUnit_C* unit) {
    return nullptr;
}

int32_t UnitHasVehicleCamera(CGUnit_C* unit) {
    return 0;
}

int32_t UnitVehicleCameraActive(CGUnit_C* unit) {
    return 0;
}

int32_t UnitGetVehicleFade(CGUnit_C* unit, float* fadeNear, float* fadeFar) {
    return 0;
}

int32_t UnitHasTransport(CGUnit_C* unit) {
    return unit->GetTransportGUID() != 0;
}

int32_t PlayerIsOnVehicleWith(CGPlayer_C* player, CGUnit_C* unit) {
    return 0;
}

WOWGUID UnitGetCameraTransport(CGUnit_C* unit) {
    return unit->GetTransportGUID();
}

float UnitGetMountScale(CGUnit_C* unit) {
    // TODO the mount's own scale (reference unit +0xa04)
    return 1.0f;
}

int32_t UnitGetStandState(CGUnit_C* unit) {
    return unit->Unit()->bytes1 & 0xFF;
}

float UnitGetCameraSwimDepth(CGUnit_C* unit) {
    // TODO the reference unit's +0x854
    return 0.0f;
}

float UnitGetCameraMaxHeight(CGUnit_C* unit) {
    // TODO the CreatureModelData camera height limit; 0 falls back to 15
    return 0.0f;
}

float UnitGetWaterDepth(CGUnit_C* unit) {
    // TODO the movement system's water depth
    return 0.0f;
}

float ObjectGetScale(CGObject_C* object) {
    return object->GetScale();
}

float ObjectGetRenderScale(CGObject_C* object) {
    return 1.0f;
}

float ObjectGetModelHeight(CGObject_C* object) {
    // TODO the reference's vtable +0xa0
    return 0.0f;
}

int32_t WorldGetFloorHeight(const C3Vector& position, float* height) {
    // TODO the reference's floor query; with no floor the camera keeps the target position.
    return 0;
}

int32_t WorldGetCameraLiquidType() {
    return CWorld::IsCameraUnderLiquid() ? 1 : 0;
}

void SoundSetUnderwater(int32_t liquid) {
    // TODO the sound system's underwater filter
}
