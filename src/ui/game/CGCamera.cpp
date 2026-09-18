#include <cmath>
#include "object/client/CGObject_C.hpp"
#include "ui/game/CGCamera.hpp"
#include "common/Time.hpp"
#include "console/CVar.hpp"
#include "console/Console.hpp"
#include "object/Client.hpp"
#include "object/client/CVehicleCamera_C.hpp"
#include "ui/game/Types.hpp"
#include "world/World.hpp"
#include <algorithm>
#include <storm/String.hpp>
#include <tempest/Math.hpp>

static CVar* s_cameraView;

CGCamera::CameraViewData CGCamera::s_cameraViewDataDefault[MAX_CAMERA_VIEWS] = {
    {  "0.0",   "0.0", "0.0" },     // VIEW_FIRST_PERSON
    {  "0.0",   "0.0", "0.0" },     // VIEW_THIRD_PERSON_A
    {  "5.55", "10.0", "0.0" },     // VIEW_THIRD_PERSON_B
    {  "5.55", "20.0", "0.0" },     // VIEW_THIRD_PERSON_C
    { "13.88", "30.0", "0.0" },     // VIEW_THIRD_PERSON_D
    { "13.88", "10.0", "0.0" },     // VIEW_THIRD_PERSON_E
    {  "0.0",   "0.0", "0.0" },     // VIEW_COMMENTATOR
    {  "5.0",  "10.0", "0.0" },     // VIEW_BARBER_SHOP
};

namespace {

// ref: FUN_005fd630
bool ValidateCameraView(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto view = SStrToFloat(value);

    if (0.0f < view && view < 7.0f) {
        return true;
    }

    ConsolePrintf("Value out of range (%f - %f)\n", 0.0, 7.0);

    return false;
}

// ref: FUN_005fd680
bool ValidateCameraDistance(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto distance = SStrToFloat(value);

    if (0.0f < distance && distance < 50.0f) {
        return true;
    }

    ConsolePrintf("Value out of range (%f - %f)\n", 0.0, 50.0);

    return false;
}

// ref: FUN_005fd6d0
// +-89 degrees, computed from +-1.5533 radians as the reference does
bool ValidateCameraPitch(CVar* var, const char* oldValue, const char* value, void* arg) {
    float min = -1.5533430576324463f * 57.295780181884766f;
    float max = 57.295780181884766f * 1.5533430576324463f;
    auto pitch = SStrToFloat(value);

    if (min < pitch && pitch < max) {
        return true;
    }

    ConsolePrintf("Value out of range (%f - %f)\n", static_cast<double>(min), static_cast<double>(max));

    return false;
}

// ref: FUN_005fd750
bool ValidateCameraTime(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto time = SStrToFloat(value);

    if (0.001f < time && time < 300.0f) {
        return true;
    }

    ConsolePrintf("Value out of range (%f - %f)\n", 0.0010000000474974513, 300.0);

    return false;
}

// ref: FUN_005fd7b0
bool ValidateCameraYaw(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto yaw = SStrToFloat(value);

    if (0.0f < yaw && yaw < 360.0f) {
        return true;
    }

    ConsolePrintf("Value out of range (%f - %f)\n", 0.0, 360.00001422012247);

    return false;
}

// ref: FUN_005fd800
bool ValidateCameraAngleSpeed(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto speed = SStrToFloat(value);

    if (0.1f < speed && speed < 360.0f) {
        return true;
    }

    ConsolePrintf("Value out of range (%f - %f)\n", 0.10000000039264378, 360.00001422012247);

    return false;
}

// ref: FUN_005fd860
bool ValidateCameraSpeed(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto speed = SStrToFloat(value);

    if (0.0027777778f < speed && speed < 50.0f) {
        return true;
    }

    ConsolePrintf("Value out of range (%f - %f)\n", 0.0027777778450399637, 50.0);

    return false;
}

// ref: FUN_005fd8c0
bool ValidateCameraSmoothStyle(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto style = SStrToFloat(value);

    if (0.0f < style && style < 5.0f) {
        return true;
    }

    ConsolePrintf("Value out of range (%f - %f)\n", 0.0, 5.0);

    return false;
}

}

int32_t CGCamera::UpdateCallback(const void*, void* param) {
    auto camera = static_cast<CGCamera*>(param);

    if (!camera) {
        return true;
    }

    auto timestamp = OsGetAsyncTimeMsPrecise();

    camera->m_nearZ = CWorld::GetNearClip();
    camera->m_farZ = CWorld::GetFarClip();

    // Model camera

    if (camera->HasModel()) {
        camera->CalcModelCamera(timestamp);
        camera->CheckUnderwater();

        return true;
    }

    // Target camera

    auto target = ClntObjMgrObjectPtr(camera->m_target, TYPE_OBJECT, __FILE__, __LINE__);

    if (target) {
        camera->CalcTargetCamera(target, timestamp);
        camera->CheckUnderwater();

        return true;
    }

    // Unknown camera

    auto object90 = ClntObjMgrObjectPtr(camera->guid90, TYPE_OBJECT, __FILE__, __LINE__);

    if (object90) {
        // TODO

        return true;
    }

    return true;
}

CGCamera::CGCamera() : CSimpleCamera(CWorld::GetNearClip(), CWorld::GetFarClip(), 90.0f * CMath::DEG2RAD) {
    this->m_model = nullptr;

    this->m_target = 0;
    this->guid90 = 0;
    this->m_relativeTo = 0;

    this->m_view = s_cameraView->GetInt();

    this->m_distance = SStrToFloat(CGCamera::s_cameraViewDataDefault[this->m_view].m_distance);
    this->m_yaw = 0.0f;
    this->m_pitch = SStrToFloat(CGCamera::s_cameraViewDataDefault[this->m_view].m_pitch) * CMath::DEG2RAD;
    this->m_roll = 0.0f;

    this->m_fovOffset = 0.0f;
}

void CGCamera::CalcModelCamera(uint32_t timestamp) {
    // TODO
}

void CGCamera::CalcTargetCamera(CGObject_C* target, uint32_t timestamp) {
    // TODO smoothing, collision with the world, and the camera cvars; this places the camera
    // behind and above the target at the view's distance and pitch, looking at its chest

    auto targetPos = target->GetPosition();
    C3Vector focus = { targetPos.x, targetPos.y, targetPos.z + 1.6f };

    float yaw = target->GetFacing() + this->m_yaw;
    float pitch = this->m_pitch;
    float distance = std::max(this->m_distance, 0.5f);

    // The camera sits opposite the facing direction, raised by the pitch
    C3Vector back = { -cosf(yaw), -sinf(yaw), 0.0f };

    this->m_position = {
        focus.x + back.x * distance * cosf(pitch),
        focus.y + back.y * distance * cosf(pitch),
        focus.z + distance * sinf(pitch)
    };

    C3Vector forward = { focus.x - this->m_position.x, focus.y - this->m_position.y, focus.z - this->m_position.z };
    this->SetFacing(forward);
}

void CGCamera::CheckUnderwater() {
    // The world tracks which liquid (if any) the camera is inside, from the loaded surfaces --
    // TerrainUpdate runs the point query every frame and CWorld records the result. Mirroring it
    // here gives the camera a real submerged state instead of the stub, and keeps one source of
    // truth rather than a second query that could disagree with the one driving the lighting.
    this->m_underwater = CWorld::IsCameraUnderLiquid();
}

float CGCamera::FOV() const {
    // Clamp offset-adjusted FOV between 0pi and 1pi
    return std::min(std::max(this->m_fov + this->m_fovOffset, 0.0f), CMath::PI);
}

C3Vector CGCamera::Forward() const {
    if (this->m_relativeTo) {
        return this->CSimpleCamera::Forward() * this->ParentToWorld();
    }

    return this->CSimpleCamera::Forward();
}

const WOWGUID& CGCamera::GetTarget() const {
    return this->m_target;
}

void CGCamera::SetTarget(const WOWGUID& target) {
    this->m_target = target;
}

void CGCamera::Rotate(float deltaYaw, float deltaPitch) {
    this->m_yaw += deltaYaw;

    // Keep the pitch just short of straight up or down so the view never flips
    this->m_pitch = std::min(std::max(this->m_pitch + deltaPitch, -1.4f), 1.4f);
}

void CGCamera::Zoom(float deltaDistance) {
    this->m_distance = std::min(std::max(this->m_distance + deltaDistance, 0.0f), 50.0f);
}

int32_t CGCamera::HasModel() const {
    return this->m_model != nullptr;
}

C33Matrix CGCamera::ParentToWorld() const {
    auto relativeTo = ClntObjMgrObjectPtr(this->m_relativeTo, TYPE_OBJECT, __FILE__, __LINE__);

    if (!relativeTo) {
        return {};
    }

    float facing;

    if (relativeTo->IsA(TYPE_UNIT)) {
        facing = static_cast<CGUnit_C*>(relativeTo)->GetRawSmoothFacing();
        auto transport = ClntObjMgrObjectPtr(relativeTo->GetTransportGUID(), TYPE_OBJECT, __FILE__, __LINE__);
        CVehicleCamera_C::ConvertSmoothFacingFromRawToWorld(facing, transport);
    } else {
        facing = relativeTo->GetFacing();
    }

    return C33Matrix::RotationAroundZ(facing);
}

C3Vector CGCamera::Right() const {
    if (this->m_relativeTo) {
        return this->CSimpleCamera::Right() * this->ParentToWorld();
    }

    return this->CSimpleCamera::Right();
}

void CGCamera::SetupWorldProjection(const CRect& projRect) {
    this->SetGxProjectionAndView(projRect);
}

C3Vector CGCamera::Target() const {
    return this->m_position + this->Forward();
}

C3Vector CGCamera::Up() const {
    if (this->m_relativeTo) {
        return this->CSimpleCamera::Up() * this->ParentToWorld();
    }

    return this->CSimpleCamera::Up();
}

// The string tables CameraRegisterCVars (FUN_005fd910) builds its cvar names and defaults from,
// read from the reference binary (PTR_DAT_00ad1b54 .. PTR_DAT_00ad1df8).
static const char* const s_cameraViewSuffixes[] = { "", "A", "B", "C", "D", "E", "Com", "Barber Shop" };
static const char* const s_cameraViewKinds[] = { "Distance", "Pitch", "Yaw" };
static bool (* const s_cameraViewValidators[])(CVar*, const char*, const char*, void*) = { &ValidateCameraDistance, &ValidateCameraPitch, &ValidateCameraYaw }; // PTR_FUN_00ad2054
static const char* const s_cameraViewDefaults[] = { // per view: distance, pitch, yaw
    "0.0", "0.0", "0.0",
    "0.0", "0.0", "0.0",
    "5.55", "10.0", "0.0",
    "5.55", "20.0", "0.0",
    "13.88", "30.0", "0.0",
    "13.88", "10.0", "0.0",
    "0.0", "0.0", "0.0",
    "5.0", "10.0", "0.0",
};
static const char* const s_cameraSmoothStyles[] = { "Never", "Smart", "Always", "Spline", "Smarter" };
static const char* const s_cameraSmoothStates[] = { "Idle", "Stop", "Track", "Move", "Strafe", "Turn", "Fear" };
static const char* const s_cameraSmoothParams[] = { "Delay", "Factor" };
static const char* const s_cameraSmoothDefaults[] = { // [style][state][param]
    "0.0", "0.0", "0.0", "0.0", "0.0", "0.0", "0.0", "0.0", "0.0", "0.0", "0.0", "0.0", "0.0", "0.0",
    "0.0", "0.0", "0.0", "0.0", "0.4", "10.0", "0.0", "1.0", "0.0", "1.0", "0.0", "1.0", "0.4", "10.0",
    "0.0", "1.0", "0.0", "1.0", "0.0", "1.0", "0.0", "1.0", "0.0", "1.0", "0.0", "1.0", "0.0", "1.0",
    "0.0", "4.0", "0.0", "4.0", "0.0", "4.0", "0.0", "1.0", "0.0", "1.0", "0.0", "1.0", "0.0", "4.0",
    "0.0", "0.0", "0.0", "0.0", "0.4", "10.0", "0.0", "1.0", "0.0", "1.0", "0.0", "1.0", "0.4", "10.0",
};
static const char* const s_cameraSmoothViewDataDefaults[] = { // [style][kind][param]
    "0.0", "0.0", "0.0", "0.0", "0.0", "0.0",
    "0.0", "0.0", "0.0", "0.0", "0.0", "1.0",
    "0.0", "0.0", "0.0", "1.0", "0.0", "1.0",
    "0.0", "0.0", "0.0", "1.0", "0.0", "1.0",
    "0.0", "0.0", "0.0", "1.0", "0.0", "1.0",
};
static const char* const s_cameraTiltStates[] = { "Fall", "Fear", "Idle", "Jump", "Move", "Strafe", "Swim", "Taxi", "Track", "Turn" };
static const char* const s_cameraTiltParams[] = { "Absorb", "Delay", "Factor" };
static const char* const s_cameraTerrainTiltDefaults[] = { // [style][state][param]
    "0.0", "0.0", "-1.0", "0.0", "0.0", "-1.0", "0.0", "0.0", "-1.0", "0.0", "0.0", "-1.0", "0.0", "0.0", "-1.0", "0.0", "0.0", "-1.0", "0.0", "0.0", "-1.0", "0.0", "0.0", "-1.0", "0.0", "0.0", "-1.0", "0.0", "0.0", "-1.0",
    "1.0", "0.0", "0.75", "1.0", "0.0", "1.0", "0.0", "0.0", "-1.0", "0.0", "0.0", "-1.0", "1.0", "0.0", "1.0", "1.0", "0.0", "1.0", "0.0", "0.0", "1.0", "0.0", "0.0", "1.0", "1.0", "0.0", "1.0", "1.0", "0.0", "1.0",
    "1.0", "0.0", "0.75", "1.0", "0.0", "1.0", "1.0", "0.0", "1.0", "0.0", "0.0", "-1.0", "1.0", "0.0", "1.0", "1.0", "0.0", "1.0", "0.0", "0.0", "1.0", "0.0", "0.0", "1.0", "1.0", "0.0", "1.0", "1.0", "0.0", "1.0",
    "1.0", "0.0", "0.75", "1.0", "0.0", "1.0", "0.0", "0.0", "-1.0", "0.0", "0.0", "-1.0", "1.0", "0.0", "1.0", "1.0", "0.0", "1.0", "0.0", "0.0", "1.0", "0.0", "0.0", "1.0", "1.0", "0.0", "1.0", "1.0", "0.0", "1.0",
    "1.0", "0.0", "0.75", "1.0", "0.0", "1.0", "0.0", "0.0", "-1.0", "0.0", "0.0", "-1.0", "1.0", "0.0", "1.0", "1.0", "0.0", "1.0", "0.0", "0.0", "1.0", "0.0", "0.0", "1.0", "1.0", "0.0", "1.0", "1.0", "0.0", "1.0",
};

// ref: FUN_005fd910
// Every camera cvar the reference registers, in its order, with its defaults. The reference
// installs clamping callbacks on many of them (noted by address); whoa validates only cameraView.
void CameraRegisterCVars() {
    char name[64];
    int32_t view = SStrToInt("2"); // the cameraView default, which picks the saved-distance/pitch defaults

    CVar::Register("cameraSavedDistance", nullptr, 0x20, s_cameraViewDefaults[view * 3 + 0], nullptr, DEFAULT);
    CVar::Register("cameraSavedVehicleDistance", nullptr, 0x20, "-1.0", nullptr, DEFAULT);
    CVar::Register("cameraSavedPitch", nullptr, 0x20, s_cameraViewDefaults[view * 3 + 1], nullptr, DEFAULT);
    CVar::Register("mouseInvertYaw", nullptr, 0x10, "0", nullptr, DEFAULT);
    CVar::Register("mouseInvertPitch", nullptr, 0x10, "0", nullptr, DEFAULT);
    CVar::Register("cameraBobbing", nullptr, 0x0, "0", nullptr, DEFAULT);
    // TODO the three smoothing globals the reference seeds here (DAT_00c24e5c..64 from DAT_00a4040c / DAT_00a34c18)
    CVar::Register("cameraDistanceMoveSpeed", nullptr, 0x10, "8.33", &ValidateCameraSpeed, DEFAULT);
    CVar::Register("cameraPitchMoveSpeed", nullptr, 0x10, "90", &ValidateCameraAngleSpeed, DEFAULT);
    CVar::Register("cameraYawMoveSpeed", nullptr, 0x10, "180", &ValidateCameraAngleSpeed, DEFAULT);
    CVar::Register("cameraBobbingSmoothSpeed", nullptr, 0x10, "0.8", &ValidateCameraSpeed, DEFAULT);
    CVar::Register("cameraFoVSmoothSpeed", nullptr, 0x10, "0.5", &ValidateCameraAngleSpeed, DEFAULT);
    CVar::Register("cameraDistanceSmoothSpeed", nullptr, 0x10, "8.33", &ValidateCameraSpeed, DEFAULT);
    CVar::Register("cameraGroundSmoothSpeed", nullptr, 0x10, "7.5", &ValidateCameraAngleSpeed, DEFAULT);
    CVar::Register("cameraHeightSmoothSpeed", nullptr, 0x10, "1.2", &ValidateCameraSpeed, DEFAULT);
    CVar::Register("cameraPitchSmoothSpeed", nullptr, 0x10, "45", &ValidateCameraAngleSpeed, DEFAULT);
    CVar::Register("cameraTargetSmoothSpeed", nullptr, 0x10, "90", &ValidateCameraAngleSpeed, DEFAULT);
    CVar::Register("cameraYawSmoothSpeed", nullptr, 0x10, "180", &ValidateCameraAngleSpeed, DEFAULT);
    CVar::Register("cameraFlyingMountHeightSmoothSpeed", nullptr, 0x10, "2.0", &ValidateCameraSpeed, DEFAULT);
    CVar::Register("cameraViewBlendStyle", nullptr, 0x10, "1", nullptr, DEFAULT);
    s_cameraView = CVar::Register("cameraView", nullptr, 0x10, "2", &ValidateCameraView, DEFAULT);

    // camera{Distance,Pitch,Yaw}{"",A,B,C,D,E,Com,Barber Shop}: the saved view presets
    for (int32_t i = 0; i < 8; i++) {
        for (int32_t j = 0; j < 3; j++) {
            name[0] = 0;
            SStrPack(name, "camera", sizeof(name));
            SStrPack(name, s_cameraViewKinds[j], sizeof(name));
            SStrPack(name, s_cameraViewSuffixes[i], sizeof(name));
            CVar::Register(name, nullptr, 0x50, s_cameraViewDefaults[i * 3 + j], s_cameraViewValidators[j], DEFAULT);
        }
    }

    CVar::Register("camerasmooth", nullptr, 0x10, "1", nullptr, DEFAULT);
    CVar::Register("cameraSmoothPitch", nullptr, 0x10, "1", nullptr, DEFAULT);
    CVar::Register("cameraSmoothYaw", nullptr, 0x10, "1", nullptr, DEFAULT);
    CVar::Register("cameraSmoothStyle", nullptr, 0x10, "4", &ValidateCameraSmoothStyle, DEFAULT);
    CVar::Register("cameraSmoothTrackingStyle", nullptr, 0x10, "4", &ValidateCameraSmoothStyle, DEFAULT);
    CVar::Register("cameraCustomViewSmoothing", nullptr, 0x10, "0", nullptr, DEFAULT);

    // Per smoothing style: cameraSmooth<Style><State><Delay|Factor>,
    // cameraSmoothViewData<Style><Kind><Delay|Factor> and cameraTerrainTilt<Style><State><param>
    for (int32_t style = 0; style < 5; style++) {
        for (int32_t state = 0; state < 7; state++) {
            for (int32_t param = 0; param < 2; param++) {
                name[0] = 0;
                SStrPack(name, "cameraSmooth", sizeof(name));
                SStrPack(name, s_cameraSmoothStyles[style], sizeof(name));
                SStrPack(name, s_cameraSmoothStates[state], sizeof(name));
                SStrPack(name, s_cameraSmoothParams[param], sizeof(name));
                CVar::Register(name, nullptr, 0x10, s_cameraSmoothDefaults[(style * 7 + state) * 2 + param], nullptr, DEFAULT);
            }
        }

        for (int32_t kind = 0; kind < 3; kind++) {
            for (int32_t param = 0; param < 2; param++) {
                name[0] = 0;
                SStrPack(name, "cameraSmoothViewData", sizeof(name));
                SStrPack(name, s_cameraSmoothStyles[style], sizeof(name));
                SStrPack(name, s_cameraViewKinds[kind], sizeof(name));
                SStrPack(name, s_cameraSmoothParams[param], sizeof(name));
                CVar::Register(name, nullptr, 0x10, s_cameraSmoothViewDataDefaults[(style * 3 + kind) * 2 + param], nullptr, DEFAULT);
            }
        }

        for (int32_t state = 0; state < 10; state++) {
            for (int32_t param = 0; param < 3; param++) {
                name[0] = 0;
                SStrPack(name, "cameraTerrainTilt", sizeof(name));
                SStrPack(name, s_cameraSmoothStyles[style], sizeof(name));
                SStrPack(name, s_cameraTiltStates[state], sizeof(name));
                SStrPack(name, s_cameraTiltParams[param], sizeof(name));
                CVar::Register(name, nullptr, 0x10, s_cameraTerrainTiltDefaults[(style * 10 + state) * 3 + param], nullptr, DEFAULT);
            }
        }
    }

    CVar::Register("cameraTerrainTilt", nullptr, 0x10, "0", nullptr, DEFAULT);
    CVar::Register("cameraTerrainTiltTimeMin", nullptr, 0x10, "3.0", &ValidateCameraTime, DEFAULT);
    CVar::Register("cameraTerrainTiltTimeMax", nullptr, 0x10, "10.0", &ValidateCameraTime, DEFAULT);
    CVar::Register("cameraWaterCollision", nullptr, 0x10, "1", nullptr, DEFAULT);
    CVar::Register("cameraHeightIgnoreStandState", nullptr, 0x10, "0", nullptr, DEFAULT);
    CVar::Register("cameraPivot", nullptr, 0x10, "1", nullptr, DEFAULT);
    CVar::Register("cameraPivotDXMax", nullptr, 0x10, "0.05", nullptr, DEFAULT);
    CVar::Register("cameraPivotDYMin", nullptr, 0x10, "0.00", nullptr, DEFAULT);
    CVar::Register("cameraDive", nullptr, 0x10, "1", nullptr, DEFAULT);
    CVar::Register("cameraSurfacePitch", nullptr, 0x10, "0.0", &ValidateCameraPitch, DEFAULT);
    CVar::Register("cameraSubmergePitch", nullptr, 0x10, "18.0", &ValidateCameraPitch, DEFAULT);
    CVar::Register("cameraSurfaceFinalPitch", nullptr, 0x10, "5.0", &ValidateCameraPitch, DEFAULT);
    CVar::Register("cameraSubmergeFinalPitch", nullptr, 0x10, "5.0", &ValidateCameraPitch, DEFAULT);
    CVar::Register("cameraDistanceMax", nullptr, 0x10, "15.0", &ValidateCameraDistance, DEFAULT);
    CVar::Register("cameraDistanceMaxFactor", nullptr, 0x10, "1.0", nullptr, DEFAULT);
    CVar::Register("cameraPitchSmoothMin", nullptr, 0x10, "0.0", &ValidateCameraPitch, DEFAULT);
    CVar::Register("cameraPitchSmoothMax", nullptr, 0x10, "30.0", &ValidateCameraPitch, DEFAULT);
    CVar::Register("cameraYawSmoothMin", nullptr, 0x10, "0.0", &ValidateCameraYaw, DEFAULT);
    CVar::Register("cameraYawSmoothMax", nullptr, 0x10, "0.0", &ValidateCameraYaw, DEFAULT);
    CVar::Register("cameraSmoothTimeMin", nullptr, 0x10, "0.1", &ValidateCameraTime, DEFAULT);
    CVar::Register("cameraSmoothTimeMax", nullptr, 0x10, "2.0", &ValidateCameraTime, DEFAULT);
}
