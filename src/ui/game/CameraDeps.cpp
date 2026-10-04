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

// ref: FUN_005ff670
void CameraSetFrustumCorners(CWFrustum& frustum, const C44Matrix& view, const C44Matrix& projection, const C3Vector& sweep, float scale) {
    C3Vector corners[8] = {};

    FrustumCorners(view, projection, corners);

    C3Vector centre = {
        (corners[3].x + corners[2].x + corners[1].x + corners[0].x) * 0.25f,
        (corners[3].y + corners[2].y + corners[1].y + corners[0].y) * 0.25f,
        0.25f * (corners[1].z + corners[0].z + corners[2].z + corners[3].z),
    };

    for (int32_t i = 0; i < 4; i++) {
        corners[i].x = (corners[i].x - centre.x) * scale + centre.x;
        corners[i].y = (corners[i].y - centre.y) * scale + centre.y;
        corners[i].z = (corners[i].z - centre.z) * scale + centre.z;
    }

    for (int32_t i = 0; i < 4; i++) {
        corners[4 + i].x = sweep.x + corners[i].x;
        corners[4 + i].y = sweep.y + corners[i].y;
        corners[4 + i].z = sweep.z + corners[i].z;
    }

    frustum.SetCorners(corners);
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
    (void)immediate;

    unit->UpdateSmoothFacing(nullptr);
}

float PlayerGetSwimDepth(CGPlayer_C* player) {
    return player ? player->GetDrunkenness() : 0.0f;
}

void WorldFrameSetPlayerAlpha(uint8_t alpha) {
    // TODO port FUN_004f8660 -> FUN_00737390 (the target unit's model alpha)
}

void InputControlSetFacing(CInputControl* input, int32_t time, float facing) {
    if (input) {
        input->SetFacing(static_cast<uint32_t>(time), facing);
    }
}

void InputControlSetPitch(CInputControl* input, int32_t time, float pitch) {
    if (input) {
        input->SetPitch(static_cast<uint32_t>(time), pitch);
    }
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
