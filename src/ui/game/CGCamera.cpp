#include "ui/game/CGCamera.hpp"
#include "console/Command.hpp"
#include "math/Utils.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2Scene.hpp"
#include "ui/game/CameraDeps.hpp"
#include "db/Db.hpp"
#include "gx/Coordinate.hpp"
#include "model/CM2Shared.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "sound/SI2.hpp"
#include "ui/InputControl.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "util/Log.hpp"
#include "world/CWFrustum.hpp"
#include "world/CWorld.hpp"
#include "console/Command.hpp"
#include "ui/FrameScript.hpp"
#include "util/Lua.hpp"
#include "gx/Transform.hpp"
#include <storm/Memory.hpp>
#include "object/client/CGObject_C.hpp"
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
#include <cmath>
#include <cstring>

// 180 / pi as the reference stores it (0x009e9e38)
static const float CAMERA_RAD2DEG = 57.29578f;

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

    ConsoleWriteA("Value out of range (%f - %f)\n", DEFAULT_COLOR,0.0, 7.0);

    return false;
}

// ref: FUN_005fd680
bool ValidateCameraDistance(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto distance = SStrToFloat(value);

    if (0.0f < distance && distance < 50.0f) {
        return true;
    }

    ConsoleWriteA("Value out of range (%f - %f)\n", DEFAULT_COLOR,0.0, 50.0);

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

    ConsoleWriteA("Value out of range (%f - %f)\n", DEFAULT_COLOR,static_cast<double>(min), static_cast<double>(max));

    return false;
}

// ref: FUN_005fd750
bool ValidateCameraTime(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto time = SStrToFloat(value);

    if (0.001f < time && time < 300.0f) {
        return true;
    }

    ConsoleWriteA("Value out of range (%f - %f)\n", DEFAULT_COLOR,0.0010000000474974513, 300.0);

    return false;
}

// ref: FUN_005fd7b0
bool ValidateCameraYaw(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto yaw = SStrToFloat(value);

    if (0.0f < yaw && yaw < 360.0f) {
        return true;
    }

    ConsoleWriteA("Value out of range (%f - %f)\n", DEFAULT_COLOR,0.0, 360.00001422012247);

    return false;
}

// ref: FUN_005fd800
bool ValidateCameraAngleSpeed(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto speed = SStrToFloat(value);

    if (0.1f < speed && speed < 360.0f) {
        return true;
    }

    ConsoleWriteA("Value out of range (%f - %f)\n", DEFAULT_COLOR,0.10000000039264378, 360.00001422012247);

    return false;
}

// ref: FUN_005fd860
bool ValidateCameraSpeed(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto speed = SStrToFloat(value);

    if (0.0027777778f < speed && speed < 50.0f) {
        return true;
    }

    ConsoleWriteA("Value out of range (%f - %f)\n", DEFAULT_COLOR,0.0027777778450399637, 50.0);

    return false;
}

// ref: FUN_005fd8c0
bool ValidateCameraSmoothStyle(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto style = SStrToFloat(value);

    if (0.0f < style && style < 5.0f) {
        return true;
    }

    ConsoleWriteA("Value out of range (%f - %f)\n", DEFAULT_COLOR,0.0, 5.0);

    return false;
}

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

// ------------------------------------------------------------------------------------------------
// Module state
// ------------------------------------------------------------------------------------------------

namespace Camera {

float s_bobFrequency;       // 0x00c24e5c
float s_bobAmplitudeH;      // 0x00c24e60
float s_bobAmplitudeV;      // 0x00c24e64
float s_pitchMax = 1.5533430576324463f;   // 0x00ad1b4c, 89 degrees
float s_pitchMin = -1.5533430576324463f;  // 0x00ad1b50
float s_heightOffset;       // 0x00c24e80
bool s_barberShopActive;    // 0x00bd19b8
bool s_belowWorld;          // 0x00c24ea8
float s_lastCollisionHeight;  // 0x00ad2178

CVar* s_savedDistance;
CVar* s_savedVehicleDistance;
CVar* s_savedPitch;
CVar* s_mouseInvertYaw;
CVar* s_mouseInvertPitch;
CVar* s_bobbing;
CVar* s_distanceMoveSpeed;
CVar* s_pitchMoveSpeed;
CVar* s_yawMoveSpeed;
CVar* s_bobbingSmoothSpeed;
CVar* s_fovSmoothSpeed;
CVar* s_distanceSmoothSpeed;
CVar* s_groundSmoothSpeed;
CVar* s_heightSmoothSpeed;
CVar* s_pitchSmoothSpeed;
CVar* s_targetSmoothSpeed;
CVar* s_yawSmoothSpeed;
CVar* s_flyingMountHeightSmoothSpeed;
CVar* s_viewBlendStyle;
CVar* s_view;
CVar* s_views[MAX_CAMERA_VIEWS][3];
CVar* s_smooth;
CVar* s_smoothPitch;
CVar* s_smoothYaw;
CVar* s_smoothStyle;
CVar* s_smoothTrackingStyle;
CVar* s_customViewSmoothing;
CVar* s_smoothState[5][7][2];
CVar* s_smoothViewData[5][3][2];
CVar* s_terrainTiltState[5][10][3];
CVar* s_terrainTilt;
CVar* s_terrainTiltTimeMin;
CVar* s_terrainTiltTimeMax;
CVar* s_waterCollision;
CVar* s_heightIgnoreStandState;
CVar* s_pivot;
CVar* s_pivotDXMax;
CVar* s_pivotDYMin;
CVar* s_dive;
CVar* s_surfacePitch;
CVar* s_submergePitch;
CVar* s_surfaceFinalPitch;
CVar* s_submergeFinalPitch;
CVar* s_distanceMax;
CVar* s_distanceMaxFactor;
CVar* s_pitchSmoothMin;
CVar* s_pitchSmoothMax;
CVar* s_yawSmoothMin;
CVar* s_yawSmoothMax;
CVar* s_smoothTimeMin;
CVar* s_smoothTimeMax;

// Which of a view's three values are angles, stored in degrees (0x00ad1b80)
const int32_t s_viewKindIsAngle[3] = { 0, 1, 1 };

static float Float(CVar* var) {
    return var->m_floatValue;
}

static int32_t Int(CVar* var) {
    return var->m_intValue;
}

// The time of the input event being handled (0x00b499a4). The reference's input dispatch stamps it;
// frozen's does not carry it, so the clock stands in.
static int32_t EventTime() {
    return static_cast<int32_t>(OsGetAsyncTimeMs());
}

static int32_t Now() {
    return static_cast<int32_t>(OsGetAsyncTimeMs());
}

// An unsigned millisecond difference as the reference converts it: a negative int picks up 2^32.
static float Elapsed(int32_t ms) {
    float value = static_cast<float>(ms);

    if (ms < 0) {
        value += 4294967296.0f;
    }

    return value;
}

}

using namespace Camera;

// ref: FUN_004c5090
// An angle brought into [0, 2pi).
float NormalizeAngle(float angle) {
    float value = std::fmod(angle, CMath::TWO_PI);

    if (value < 0.0f) {
        value += CMath::TWO_PI;
    }

    return value;
}

// ref: FUN_005fd5c0
// `angle` moved by whole turns until `reference - angle` lies in (low, high].
float CameraWrapAngleNear(float reference, float angle, float low, float high) {
    while (reference - angle < low) {
        angle -= CMath::TWO_PI;
    }

    while (high < reference - angle) {
        angle += CMath::TWO_PI;
    }

    return angle;
}

// ref: FUN_00482940
// `value` held inside [low, high].
float ClampRange(float value, float low, float high) {
    if (value < low) {
        value = low;
    }

    if (high <= value) {
        return high;
    }

    return value;
}

// ref: FUN_008ca080
// From `from` to `to` along half a cosine.
float CosineInterp(float from, float to, float t) {
    return (1.0f - std::cos(t * CMath::PI)) * 0.5f * (to - from) + from;
}

// ref: FUN_005fe800
// `value` split into whole and fraction, flooring toward minus infinity.
void CameraSplitFloor(float value, float* fraction, int32_t* whole) {
    if (0.0f < value) {
        *whole = static_cast<int32_t>(lrintf(value));
        *fraction = value - static_cast<float>(*whole);
        return;
    }

    *whole = static_cast<int32_t>(lrintf(value)) - 1;
    *fraction = value - static_cast<float>(*whole);
}

// ref: FUN_005fff80
// A polynomial sine of t * pi, sign flipped on odd half turns.
float CGCamera::SineEase(float t) {
    float fraction;
    int32_t whole;
    CameraSplitFloor(t * 0.31830987334251404f, &fraction, &whole);

    float value = 1.0f - (6.0f - 4.0f * fraction) * fraction * fraction;

    if (whole & 1) {
        value = -value;
    }

    return value;
}

// ref: FUN_005fffd0
// 0 at t = 0 easing to 1 at t = 1.
float CGCamera::CosineEase(float t) {
    float fraction;
    int32_t whole;
    CameraSplitFloor(t * CMath::PI * 0.31830987334251404f, &fraction, &whole);

    float value = 1.0f - (6.0f - 4.0f * fraction) * fraction * fraction;

    if (whole & 1) {
        value = -value;
    }

    return 0.5f - value * 0.5f;
}

// ref: FUN_005fe570
float CGCamera::GetMaxDistance() {
    return 50.0f;
}

// ref: FUN_005fec50
// The world geometry the camera collides with: terrain, buildings, doodads, and water when
// cameraWaterCollision asks.
static uint32_t CameraCollisionFlags() {
    return (Int(s_waterCollision) ? 0x20000u : 0u) + 0x100171u;
}

// ref: FUN_005fecf0
static float CameraDistance(const C3Vector& a, const C3Vector& b) {
    return sqrtf((a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z) + (a.x - b.x) * (a.x - b.x));
}

// ref: FUN_005fee10
// Falling (movement 0x1000) with a fall time.
int32_t CGCamera::IsFalling(CGUnit_C* unit) {
    return (unit->m_localMove.GetMoveFlags() & 0x1000) && unit->m_localMove.GetFloatB8() != 0;
}

// ------------------------------------------------------------------------------------------------
// The model camera
// ------------------------------------------------------------------------------------------------

// ref: FUN_005fe310
int32_t CGCamera::HasModelCamera() const {
    return this->m_modelCamera != nullptr;
}

// ref: FUN_005fe320
void CGCamera::StartModelCamera() {
    this->m_modelTime = static_cast<uint32_t>(OsGetAsyncTimeMs());
    this->GetScene()->SetTime(0);
    this->m_model->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 0, 1);
}

// ref: FUN_005fe360
void CGCamera::ReleaseModel() {
    if (!this->m_model) {
        return;
    }

    this->m_flags &= ~0x4u;

    if (this->m_modelCamera) {
        HandleClose(this->m_modelCamera);
        this->m_modelCamera = nullptr;
    }

    this->m_model->Release();
    this->m_model = nullptr;
}

// ref: FUN_005fe3a0
int32_t CGCamera::IsFirstPerson() const {
    return this->m_distance < 1.8315001726150513f;
}

// ref: FUN_005fe400
// pitchLimit <degrees>: the camera pitch limit, both ways.
static int32_t CameraPitchLimitCommand(const char* command, const char* arguments) {
    s_pitchMax = SStrToFloat(arguments) * CMath::DEG2RAD;
    s_pitchMin = SStrToFloat(arguments) * -CMath::DEG2RAD;

    return 1;
}

// ------------------------------------------------------------------------------------------------
// Views
// ------------------------------------------------------------------------------------------------

// ref: FUN_005fe440
void CGCamera::SaveViewToCVars(int32_t view, float distance, float pitch, float yaw) {
    this->m_views[view][0] = distance;
    this->m_views[view][1] = pitch;
    this->m_views[view][2] = yaw;

    for (int32_t kind = 0; kind < 3; kind++) {
        float value = this->m_views[view][kind];

        if (s_viewKindIsAngle[kind]) {
            value *= CAMERA_RAD2DEG;
        }

        char text[40];
        SStrPrintf(text, sizeof(text), "%f", value);
        s_views[view][kind]->Set(text, true, false, false, true);
    }
}

// ref: FUN_005fe4e0
void CGCamera::SaveView(int32_t view) {
    this->SaveViewToCVars(view, this->m_distanceBlend.m_target, this->m_pitchBlend.m_target, this->m_yawBlend.m_target);
}

// ref: FUN_005fe510
void CGCamera::LoadViewsFromCVars() {
    for (int32_t view = 0; view < MAX_CAMERA_VIEWS; view++) {
        for (int32_t kind = 0; kind < 3; kind++) {
            float value = Float(s_views[view][kind]);

            if (s_viewKindIsAngle[kind]) {
                value *= CMath::DEG2RAD;
            }

            this->m_views[view][kind] = value;
        }
    }
}

// ref: FUN_005ff8e0
int32_t CGCamera::IsViewDefault(int32_t view) {
    for (int32_t kind = 0; kind < 3; kind++) {
        float value = SStrToFloat(s_cameraViewDefaults[view * 3 + kind]);

        if (s_viewKindIsAngle[kind]) {
            value *= CMath::DEG2RAD;
        }

        if (0.0010000000474974513f <= std::fabs(this->m_views[view][kind] - value)) {
            return 0;
        }
    }

    return 1;
}

// ref: FUN_006048a0
void CGCamera::ResetView(int32_t view) {
    if (view < 0) {
        for (int32_t i = 0; i < MAX_CAMERA_VIEWS; i++) {
            this->ResetView(i);
        }

        return;
    }

    for (int32_t kind = 0; kind < 3; kind++) {
        float value = SStrToFloat(s_cameraViewDefaults[view * 3 + kind]);

        if (s_viewKindIsAngle[kind]) {
            value *= CMath::DEG2RAD;
        }

        this->m_views[view][kind] = value;
    }

    if (view == this->m_view) {
        this->SetView(this->m_view, Int(s_viewBlendStyle), 0);
    }
}

// ref: FUN_005ff320
void CGCamera::SaveDistanceToCVars() {
    float distance;

    if (this->m_flags & 0x8) {
        distance = this->m_savedBarberDistance;
    } else if (this->m_flags2 & 0x20) {
        distance = this->m_unk2D0;
    } else {
        distance = this->m_distanceBlend.m_target;
    }

    char text[32];
    SStrPrintf(text, sizeof(text), "%f", distance);
    s_savedDistance->Set(text, true, false, false, true);

    SStrPrintf(text, sizeof(text), "%f", this->m_pitchBlend.m_target * CAMERA_RAD2DEG);
    s_savedPitch->Set(text, true, false, false, true);
}

// ref: FUN_005ff3e0
void CGCamera::LoadSavedDistance() {
    float saved = Float(s_savedDistance);
    float distance = 0.0f;

    if (0.0f <= saved) {
        distance = saved < 50.0f ? saved : 50.0f;
    }

    this->m_distance = distance;
    this->m_distanceBlend.m_target = distance;

    float pitch = Float(s_savedPitch) * CMath::DEG2RAD;
    this->m_pitch = pitch;
    this->m_pitchBlend.m_target = pitch;
}

// ------------------------------------------------------------------------------------------------
// Rotation and zoom
// ------------------------------------------------------------------------------------------------

// ref: FUN_005fe5f0
void CGCamera::AdjustYaw(float delta) {
    this->m_yawBlend.m_target = NormalizeAngle(delta + this->m_yawBlend.m_target);
    this->m_yawBlend.m_from = CameraWrapAngleNear(this->m_yawBlend.m_target, NormalizeAngle(delta + this->m_yawBlend.m_from), -CMath::PI, CMath::PI);
    this->m_yaw = CameraWrapAngleNear(this->m_yawBlend.m_target, NormalizeAngle(this->m_yaw + delta), -CMath::PI, CMath::PI);
}

// ref: FUN_005ffc20
void CGCamera::AdjustPitch(float delta) {
    float target = std::min(std::max(delta + this->m_pitchBlend.m_target, s_pitchMin), s_pitchMax);
    this->m_pitchBlend.m_target = target;

    float from = std::min(std::max(this->m_pitchBlend.m_from + delta, s_pitchMin), s_pitchMax);
    this->m_pitchBlend.m_from = CameraWrapAngleNear(target, from, -CMath::PI, CMath::PI);

    float pitch = std::min(std::max(this->m_pitch + delta, s_pitchMin), s_pitchMax);
    this->m_pitch = CameraWrapAngleNear(target, pitch, -CMath::PI, CMath::PI);
}

// ref: FUN_005ff530
void CGCamera::ClampAngles() {
    float pitch = this->m_pitch;

    if (pitch < s_pitchMin || !(pitch < s_pitchMax)) {
        pitch = pitch < s_pitchMin ? s_pitchMin : s_pitchMax;
    }

    this->m_pitch = pitch;
    this->m_yaw = NormalizeAngle(this->m_yaw);
    this->m_yawOffset = NormalizeAngle(this->m_yawOffset);
}

// ref: FUN_005fe6a0
void CGCamera::SetMouseLookActive(int32_t active) {
    if (active) {
        this->m_flags2 |= 0x1;
        return;
    }

    this->m_flags2 &= ~0x1u;
}

// ref: FUN_005ff950
// Zoom in by `distance`, over `duration` seconds or at cameraDistanceMoveSpeed.
void CGCamera::ZoomIn(float distance, int32_t time, float duration) {
    if (this->m_flags2 & 0x40) {
        return;
    }

    float rate = 1.0f;
    int32_t length;

    if (!(duration <= 0.0f)) {
        length = static_cast<int32_t>(lrintf(duration * 1000.0f));
        int32_t amount = static_cast<int32_t>(lrintf(distance * 1000.0f));
        rate = Elapsed(amount) / (Float(s_distanceMoveSpeed) * duration * 1000.0f);
    } else {
        length = static_cast<int32_t>(lrintf((distance / Float(s_distanceMoveSpeed)) * 1000.0f));
    }

    if (this->m_timedBits & 0x4) {
        this->m_timedStop[1] = time;
        this->m_timedBits |= 0x8;
    }

    if (this->m_timedEnd[0] && (this->m_timedBits & 0x1)) {
        this->m_timedEnd[0] += length;
        return;
    }

    this->SetTimedValue(0, time, length, rate);
}

// ref: FUN_005ffa60
void CGCamera::ZoomOut(float distance, int32_t time, float duration) {
    if (this->m_flags2 & 0x40) {
        return;
    }

    float rate = 1.0f;
    int32_t length;

    if (!(duration <= 0.0f)) {
        length = static_cast<int32_t>(lrintf(duration * 1000.0f));
        int32_t amount = static_cast<int32_t>(lrintf(distance * 1000.0f));
        rate = Elapsed(amount) / (Float(s_distanceMoveSpeed) * duration * 1000.0f);
    } else {
        length = static_cast<int32_t>(lrintf((distance / Float(s_distanceMoveSpeed)) * 1000.0f));
    }

    if (this->m_timedBits & 0x1) {
        this->m_timedStop[0] = time;
        this->m_timedBits |= 0x2;
    }

    if (this->m_timedEnd[1] && (this->m_timedBits & 0x4)) {
        this->m_timedEnd[1] += length;
        return;
    }

    this->SetTimedValue(1, time, length, rate);
}

// ref: FUN_005ffb70
void CGCamera::ZoomTo(float distance, int32_t time, float duration, int32_t lock) {
    if ((this->m_flags2 & 0x40) || std::fabs(distance - this->m_distance) < 0.0010000000474974513f) {
        return;
    }

    if (this->m_timedBits & 0x1) {
        this->m_timedStop[0] = time;
        this->m_timedBits &= ~0x3u;
    }

    if (this->m_timedBits & 0x4) {
        this->m_timedStop[1] = time;
        this->m_timedBits &= ~0xCu;
    }

    if (distance <= this->m_distance) {
        this->ZoomIn(this->m_distance - distance, time, duration);
    } else {
        this->ZoomOut(distance - this->m_distance, time, duration);
    }

    if (lock) {
        this->m_flags2 |= 0x40;
    }
}

// ref: FUN_006000e0
// The six timed moves (zoom in and out, yaw left and right, pitch up and down), each advanced by
// the time it has been held.
void CGCamera::AdvanceTimedValues(int32_t time) {
    float minDistance;

    if (this->m_flags2 & 0x8) {
        minDistance = this->m_minDistance;
    } else if (s_barberShopActive) {
        minDistance = BarberShopGetMinDistance();
    } else {
        minDistance = 0.0f;
    }

    float maxDistance;

    if (this->m_flags2 & 0x10) {
        maxDistance = this->m_maxDistance;
    } else if (this->m_vehicleDistanceMode == 0) {
        float limit = Float(s_distanceMaxFactor) * Float(s_distanceMax);
        maxDistance = 0.0f;

        if (0.0f <= limit) {
            maxDistance = limit < 50.0f ? limit : 50.0f;
        }
    } else {
        maxDistance = 50.0f;
    }

    this->m_flags |= 0x40;

    for (int32_t index = 0; index < 6; index++) {
        uint32_t bit = 1u << (index * 2);

        if (!(this->m_timedBits & bit)) {
            continue;
        }

        int32_t end = this->m_timedEnd[index];

        if (end && time - end >= 0) {
            this->m_timedStop[index] = end;
            this->m_timedBits |= 1u << (index * 2 + 1);
        }

        int32_t held;

        if (!(this->m_timedBits & (1u << (index * 2 + 1)))) {
            held = time - this->m_timedStart[index];

            if (held < 0) {
                continue;
            }

            this->m_timedStart[index] = time;
        } else {
            held = this->m_timedStop[index] - this->m_timedStart[index];
            held &= -static_cast<int32_t>(held >= 0);
            this->m_timedBits &= ~(3u << (index * 2));

            if (!(bit & this->m_timedBits)) {
                this->m_flags2 &= ~0x40u;
            }
        }

        float elapsed = Elapsed(held);

        switch (index) {
            case 0: {
                float distance = this->m_distanceBlend.m_target - elapsed * Float(s_distanceMoveSpeed) * this->m_timedValue[0] * 0.0010000000474974513f;
                this->m_distanceBlend.m_target = distance < minDistance ? minDistance : distance;
                break;
            }

            case 1: {
                float distance = elapsed * Float(s_distanceMoveSpeed) * this->m_timedValue[1] * 0.0010000000474974513f + this->m_distanceBlend.m_target;
                this->m_distanceBlend.m_target = maxDistance < distance ? maxDistance : distance;
                break;
            }

            case 2: {
                this->m_yawBlend.m_target = elapsed * Float(s_yawMoveSpeed) * this->m_timedValue[2] * 0.0010000000474974513f * CMath::DEG2RAD + this->m_yawBlend.m_target;

                while (CMath::TWO_PI < this->m_yawBlend.m_target) {
                    this->m_yawBlend.m_target -= CMath::TWO_PI;
                }

                if (!(this->m_flags & 0x1000000)) {
                    this->m_yaw = this->m_yawBlend.m_target;
                }

                continue;
            }

            case 3: {
                this->m_yawBlend.m_target = this->m_yawBlend.m_target - elapsed * Float(s_yawMoveSpeed) * this->m_timedValue[3] * 0.0010000000474974513f * CMath::DEG2RAD;

                while (this->m_yawBlend.m_target < 0.0f) {
                    this->m_yawBlend.m_target += CMath::TWO_PI;
                }

                if (!(this->m_flags & 0x1000000)) {
                    this->m_yaw = this->m_yawBlend.m_target;
                }

                continue;
            }

            case 4: {
                float pitch = elapsed * Float(s_pitchMoveSpeed) * this->m_timedValue[4] * 0.0010000000474974513f * CMath::DEG2RAD + this->m_pitchBlend.m_target;
                this->m_pitchBlend.m_target = s_pitchMax < pitch ? s_pitchMax : pitch;

                if (!(this->m_flags & 0x2000000)) {
                    this->m_pitch = this->m_pitchBlend.m_target;
                }

                continue;
            }

            case 5: {
                float pitch = this->m_pitchBlend.m_target - elapsed * Float(s_pitchMoveSpeed) * this->m_timedValue[5] * 0.0010000000474974513f * CMath::DEG2RAD;
                this->m_pitchBlend.m_target = pitch < s_pitchMin ? s_pitchMin : pitch;

                if (!(this->m_flags & 0x2000000)) {
                    this->m_pitch = this->m_pitchBlend.m_target;
                }

                continue;
            }
        }

        if (!(this->m_flags & 0x4000000)) {
            this->m_distance = this->m_distanceBlend.m_target;
        }
    }
}

// ref: FUN_00600590
void CGCamera::SetVehicleDistanceMode(float mode, float time, int32_t smooth) {
    int32_t newMode = static_cast<int32_t>(mode);

    if (static_cast<float>(this->m_vehicleDistanceMode) == mode) {
        return;
    }

    if (time < 0.5f) {
        time = 0.5f;
    }

    this->SaveDistanceToCVars();

    float distance;

    if (mode == 0.0f) {
        float saved = Float(s_savedDistance);
        distance = 0.0f;

        if (0.0f <= saved) {
            distance = saved < 50.0f ? saved : 50.0f;
        }
    } else {
        distance = Float(s_savedVehicleDistance);

        if (!(0.0f <= distance)) {
            distance = 50.0f;
        }
    }

    if (smooth && 0.0010000000474974513f <= std::fabs(distance - this->m_distance)) {
        if (this->m_distance < distance) {
            this->ZoomOut(distance - this->m_distance, Now(), time);
        } else {
            this->ZoomIn(this->m_distance - distance, Now(), time);
        }
    }

    this->m_vehicleDistanceMode = newMode;
}

// ref: FUN_00600840
void CGCamera::SetMinDistance(float distance) {
    this->m_flags2 |= 0x8;
    this->m_minDistance = distance;

    if (this->m_distanceBlend.m_target < distance) {
        this->m_flags &= ~0x4000000u;
        this->m_distanceBlend.m_target = this->m_distance;
        this->m_distanceBlend.m_start = 0;
        this->m_distanceBlend.m_duration = 0.0f;
    }
}

// ref: FUN_00600890
void CGCamera::SetMaxDistance(float distance) {
    this->m_flags2 |= 0x10;
    this->m_maxDistance = distance;

    if (distance < this->m_distanceBlend.m_target) {
        this->m_flags &= ~0x4000000u;
        this->m_distanceBlend.m_target = this->m_distance;
        this->m_distanceBlend.m_start = 0;
        this->m_distanceBlend.m_duration = 0.0f;
    }
}

// ref: FUN_006008e0
void CGCamera::ClearMinDistance() {
    this->m_flags2 &= ~0x8u;

    if (this->m_distanceBlend.m_target < 0.0f) {
        this->m_flags &= ~0x4000000u;
        this->m_distanceBlend.m_target = this->m_distance;
        this->m_distanceBlend.m_start = 0;
        this->m_distanceBlend.m_duration = 0.0f;
    }
}

// ref: FUN_00600920
void CGCamera::ClearMaxDistance() {
    this->m_flags2 &= ~0x10u;

    if (50.0f < this->m_distanceBlend.m_target) {
        this->m_flags &= ~0x4000000u;
        this->m_distanceBlend.m_target = this->m_distance;
        this->m_distanceBlend.m_start = 0;
        this->m_distanceBlend.m_duration = 0.0f;
    }
}

// ------------------------------------------------------------------------------------------------
// The smoothed values: Start* begins a move, Smooth* derives its duration from a delay, a factor
// and the matching smooth-speed cvar, and skips a request identical to the one in flight.
// ------------------------------------------------------------------------------------------------

// ref: FUN_005fe950
int32_t CGCamera::StartDistanceBlend(float target, float duration, int32_t start) {
    if (std::fabs(this->m_distance - target) < 0.0010000000474974513f) {
        return 0;
    }

    this->m_flags |= 0x4000000;
    this->m_distanceBlend.m_duration = duration;
    this->m_distanceBlend.m_start = start;
    this->m_distanceBlend.m_from = this->m_distance;
    this->m_distanceBlend.m_target = target;

    return 1;
}

// ref: FUN_005fe9b0
int32_t CGCamera::StartTiltBlend(float target, float duration, int32_t start) {
    float from = CameraWrapAngleNear(target, this->m_tiltPitch, -CMath::PI, CMath::PI);
    this->m_tiltPitch = from;

    if (std::fabs(from - target) < 0.0010000000474974513f) {
        return 0;
    }

    this->m_flags |= 0x10000000;
    this->m_tiltBlend.m_from = from;
    this->m_tiltBlend.m_start = start;
    this->m_tiltBlend.m_target = target;
    this->m_tiltBlend.m_duration = duration;

    return 1;
}

// ref: FUN_005fea40
int32_t CGCamera::StartPitchBlend(float target, float duration, int32_t start) {
    float from = CameraWrapAngleNear(target, this->m_pitch, -CMath::PI, CMath::PI);
    this->m_pitch = from;

    if (std::fabs(from - target) < 0.0010000000474974513f) {
        return 0;
    }

    this->m_flags |= 0x2000000;
    this->m_pitchBlend.m_from = from;
    this->m_pitchBlend.m_start = start;
    this->m_pitchBlend.m_target = target;
    this->m_pitchBlend.m_duration = duration;

    return 1;
}

// ref: FUN_005fead0
int32_t CGCamera::StartPitchOffsetBlend(float target, float duration, int32_t start) {
    float from = CameraWrapAngleNear(target, this->m_pitchOffset, -CMath::PI, CMath::PI);
    this->m_pitchOffset = from;

    if (std::fabs(from - target) < 0.0010000000474974513f) {
        return 0;
    }

    this->m_flags |= 0x8000000;
    this->m_pitchOffsetBlend.m_from = from;
    this->m_pitchOffsetBlend.m_start = start;
    this->m_pitchOffsetBlend.m_target = target;
    this->m_pitchOffsetBlend.m_duration = duration;

    return 1;
}

// ref: FUN_005feb60
int32_t CGCamera::StartYawBlend(float target, float duration, int32_t start) {
    float from = CameraWrapAngleNear(target, this->m_yaw, -CMath::PI, CMath::PI);
    this->m_yaw = from;

    if (std::fabs(from - target) < 0.0010000000474974513f) {
        return 0;
    }

    this->m_flags |= 0x1000000;
    this->m_yawBlend.m_from = from;
    this->m_yawBlend.m_start = start;
    this->m_yawBlend.m_target = target;
    this->m_yawBlend.m_duration = duration;

    return 1;
}

// ref: FUN_005febf0
int32_t CGCamera::StartMountHeightBlend(float target, float duration, int32_t start) {
    if (std::fabs(this->m_mountHeight - target) < 0.0010000000474974513f) {
        return 0;
    }

    this->m_flags |= 0x400000;
    this->m_mountHeightBlend.m_from = this->m_mountHeight;
    this->m_mountHeightBlend.m_start = start;
    this->m_mountHeightBlend.m_target = target;
    this->m_mountHeightBlend.m_duration = duration;

    return 1;
}

// ref: FUN_00600f00
int32_t CGCamera::StartFovBlend(float target, float duration, int32_t start) {
    float from = CameraWrapAngleNear(target, this->m_fovOffset, -CMath::PI, CMath::PI);
    this->m_fovOffset = from;

    if (std::fabs(from - target) < 0.0010000000474974513f && !(this->m_flags & 0x80008000)) {
        return 0;
    }

    this->m_flags |= 0x40000000;
    this->m_fovBlend.m_from = from;
    this->m_fovBlend.m_start = start;
    this->m_fovBlend.m_target = target;
    this->m_fovBlend.m_duration = duration;

    return 1;
}

// ref: FUN_006010e0
int32_t CGCamera::StartHeightBlend(float target, float duration, int32_t start) {
    if (std::fabs(this->m_height - target) < 0.0010000000474974513f) {
        return 0;
    }

    this->m_flags |= 0x20000000;
    this->m_heightBlend.m_from = this->m_height;
    this->m_heightBlend.m_start = start;
    this->m_heightBlend.m_target = target;
    this->m_heightBlend.m_duration = duration;

    // Until the first height lands (0x80), it is taken at once.
    if (!(this->m_flags & 0x80)) {
        this->m_height = target;
        this->m_heightBlend.m_target = target;
        this->m_flags = (this->m_flags & ~0x20000000u) | 0x80;
        this->m_heightBlend.m_duration = 0.0f;
        this->m_heightBlend.m_start = 0;
    }

    return 1;
}

// ref: FUN_00600e00
int32_t CGCamera::SmoothDistance(float target, float delay, float factor, int32_t time) {
    if ((this->m_flags & 0x4000000)
        && std::fabs(this->m_distanceBlend.m_target - target) < 0.0010000000474974513f
        && std::fabs(this->m_distanceBlend.m_delay - delay) < 0.0010000000474974513f
        && std::fabs(this->m_distanceBlend.m_factor - factor) < 0.0010000000474974513f) {
        return 1;
    }

    if (std::fabs(this->m_distance - target) < 0.0010000000474974513f) {
        return 0;
    }

    this->m_distanceBlend.m_delay = delay;
    this->m_distanceBlend.m_factor = factor;

    int32_t lead = static_cast<int32_t>(lrintf(delay * -1000.0f));

    return this->StartDistanceBlend(target, factor * (std::fabs(target - this->m_distance) / Float(s_distanceSmoothSpeed)), time - lead);
}

// The angle variants share one shape: wrap toward the target, skip a repeat, and run at the
// angular smooth-speed cvar.
#define CAMERA_SMOOTH_ANGLE(name, field, blend, flag, speed, start)                                          \
int32_t CGCamera::name(float target, float delay, float factor, int32_t time) {                              \
    float current = CameraWrapAngleNear(target, this->field, -CMath::PI, CMath::PI);                         \
    this->field = current;                                                                                  \
                                                                                                            \
    if ((this->m_flags & flag)                                                                              \
        && std::fabs(this->blend.m_target - target) < 0.0010000000474974513f                                \
        && std::fabs(this->blend.m_delay - delay) < 0.0010000000474974513f                                 \
        && std::fabs(this->blend.m_factor - factor) < 0.0010000000474974513f) {                              \
        return 1;                                                                                           \
    }                                                                                                       \
                                                                                                            \
    if (!(0.0010000000474974513f <= std::fabs(current - target))) {                                         \
        return 0;                                                                                           \
    }                                                                                                       \
                                                                                                            \
    this->blend.m_delay = delay;                                                                           \
    this->blend.m_factor = factor;                                                                           \
                                                                                                            \
    int32_t lead = static_cast<int32_t>(lrintf(delay * -1000.0f));                                         \
                                                                                                            \
    return this->start(target, (std::fabs(target - current) / (Float(speed) * CMath::DEG2RAD)) * factor, time - lead); \
}

// ref: FUN_00600fa0
CAMERA_SMOOTH_ANGLE(SmoothTilt, m_tiltPitch, m_tiltBlend, 0x10000000, s_groundSmoothSpeed, StartTiltBlend)
// ref: FUN_00601190
CAMERA_SMOOTH_ANGLE(SmoothPitch, m_pitch, m_pitchBlend, 0x2000000, s_pitchSmoothSpeed, StartPitchBlend)
// ref: FUN_006012d0
CAMERA_SMOOTH_ANGLE(SmoothPitchOffset, m_pitchOffset, m_pitchOffsetBlend, 0x8000000, s_targetSmoothSpeed, StartPitchOffsetBlend)
// ref: FUN_00601410
CAMERA_SMOOTH_ANGLE(SmoothYaw, m_yaw, m_yawBlend, 0x1000000, s_yawSmoothSpeed, StartYawBlend)

#undef CAMERA_SMOOTH_ANGLE

// ref: FUN_00601550
int32_t CGCamera::SmoothMountHeight(float target, float delay, float factor, int32_t time) {
    if ((this->m_flags & 0x400000)
        && std::fabs(this->m_mountHeightBlend.m_target - target) < 0.0010000000474974513f
        && std::fabs(this->m_mountHeightBlend.m_delay - delay) < 0.0010000000474974513f
        && std::fabs(this->m_mountHeightBlend.m_factor - factor) < 0.0010000000474974513f) {
        return 1;
    }

    if (std::fabs(this->m_mountHeight - target) < 0.0010000000474974513f) {
        return 0;
    }

    this->m_mountHeightBlend.m_delay = delay;
    this->m_mountHeightBlend.m_factor = factor;

    int32_t lead = static_cast<int32_t>(lrintf(delay * -1000.0f));

    return this->StartMountHeightBlend(target, factor * (std::fabs(target - this->m_mountHeight) / Float(s_flyingMountHeightSmoothSpeed)), time - lead);
}

// ref: FUN_006030e0
int32_t CGCamera::SmoothFov(float target, float delay, float factor, int32_t time) {
    float current = CameraWrapAngleNear(target, this->m_fovOffset, -CMath::PI, CMath::PI);
    this->m_fovOffset = current;

    if ((this->m_flags & 0x40000000)
        && std::fabs(this->m_fovBlend.m_target - target) < 0.0010000000474974513f
        && std::fabs(this->m_fovBlend.m_delay - delay) < 0.0010000000474974513f
        && std::fabs(this->m_fovBlend.m_factor - factor) < 0.0010000000474974513f) {
        return 1;
    }

    if (!(0.0010000000474974513f <= std::fabs(current - target))) {
        return 0;
    }

    this->m_fovBlend.m_delay = delay;
    this->m_fovBlend.m_factor = factor;

    float duration;

    if (!(this->m_flags & 0x80008000)) {
        duration = (std::fabs(target - current) / (Float(s_fovSmoothSpeed) * CMath::DEG2RAD)) * factor;
    } else {
        duration = 0.5f;
    }

    int32_t lead = static_cast<int32_t>(lrintf(delay * -1000.0f));

    return this->StartFovBlend(target, duration, time - lead);
}

// ref: FUN_00603230
int32_t CGCamera::SmoothHeight(float target, float delay, float factor, int32_t time) {
    if ((this->m_flags & 0x20000000)
        && std::fabs(this->m_heightBlend.m_target - target) < 0.0010000000474974513f
        && std::fabs(this->m_heightBlend.m_delay - delay) < 0.0010000000474974513f
        && std::fabs(this->m_heightBlend.m_factor - factor) < 0.0010000000474974513f) {
        return 1;
    }

    if (std::fabs(this->m_height - target) < 0.0010000000474974513f) {
        return 0;
    }

    this->m_heightBlend.m_delay = delay;
    this->m_heightBlend.m_factor = factor;

    int32_t lead = static_cast<int32_t>(lrintf(delay * -1000.0f));

    return this->StartHeightBlend(target, factor * (std::fabs(target - this->m_height) / Float(s_heightSmoothSpeed)), time - lead);
}

// ref: FUN_005feee0
void CGCamera::StopPitchBlend() {
    this->m_flags &= ~0x2000000u;
    this->m_pitchBlend.m_target = this->m_pitch;
    this->m_pitchBlend.m_start = 0;
    this->m_pitchBlend.m_duration = 0.0f;
}

// ref: FUN_005fef10
void CGCamera::StopPitchOffsetBlend() {
    this->m_flags &= ~0x8000000u;
    this->m_pitchOffsetBlend.m_target = this->m_pitchOffset;
    this->m_pitchOffsetBlend.m_start = 0;
    this->m_pitchOffsetBlend.m_duration = 0.0f;
}

// ref: FUN_005fef40
void CGCamera::StopYawBlend() {
    this->m_flags &= ~0x1000000u;
    this->m_yawBlend.m_target = this->m_yaw;
    this->m_yawBlend.m_start = 0;
    this->m_yawBlend.m_duration = 0.0f;
}

// ref: FUN_006006a0
void CGCamera::UpdateBlendA(int32_t time) {
    float elapsed = Elapsed(time - this->m_unk2D4);
    float length = Elapsed(this->m_unk2D8 - this->m_unk2D4);
    float t = elapsed / length;

    if (1.0f < t) {
        this->m_unk2D4 = this->m_unk2D8;
        this->m_unk2E4 = this->m_unk2E0;
        return;
    }

    float from;
    float to;
    memcpy(&from, &this->m_unk2DC, sizeof(from));
    memcpy(&to, &this->m_unk2E0, sizeof(to));

    float value = (to - from) * CGCamera::CosineEase(t) + from;
    memcpy(&this->m_unk2E4, &value, sizeof(value));
}

// ref: FUN_00600730
// The view turned about Z by blend A's angle.
void CGCamera::ApplyBlendA(C3Vector& vector) const {
    float angle;
    memcpy(&angle, &this->m_unk2E4, sizeof(angle));

    if (angle == 0.0f) {
        return;
    }

    C44Matrix rotation;
    rotation.RotateAroundZ(angle);

    C3Vector out;
    TransformPointInPlace(out, vector, rotation);
}

// ref: FUN_006007b0
void CGCamera::UpdateBlendB(int32_t time) {
    float elapsed = Elapsed(time - this->m_unk2E8);
    float length = Elapsed(this->m_unk2EC - this->m_unk2E8);
    float t = elapsed / length;

    if (1.0f < t) {
        this->m_unk2E8 = this->m_unk2EC;
        this->m_unk2F8 = this->m_unk2F4;
        return;
    }

    float from;
    float to;
    memcpy(&from, &this->m_unk2F0, sizeof(from));
    memcpy(&to, &this->m_unk2F4, sizeof(to));

    float value = (to - from) * CGCamera::CosineEase(t) + from;
    memcpy(&this->m_unk2F8, &value, sizeof(value));
}

// ref: FUN_00604940
int32_t CGCamera::SetFovOffset(float degrees, int32_t immediate) {
    if (this->m_flags & 0x80008000) {
        return 0;
    }

    float offset = degrees * 1.5533429384231567f;

    if (immediate) {
        int32_t changed = NotEqual(this->m_fovOffset, offset);
        this->m_fovOffset = offset;
        return changed & 0xFF;
    }

    return this->SmoothFov(offset, 0.0f, 1.0f, Now());
}

// ref: FUN_005fd910
// Every camera cvar the reference registers, in its order, with its defaults. The reference
// installs clamping callbacks on many of them (noted by address); frozen validates only cameraView.
void CameraRegisterCVars() {
    char name[64];
    int32_t view = SStrToInt("2"); // the cameraView default, which picks the saved-distance/pitch defaults

    s_savedDistance = CVar::Register("cameraSavedDistance", nullptr, 0x20, s_cameraViewDefaults[view * 3 + 0], nullptr, DEFAULT);
    s_savedVehicleDistance = CVar::Register("cameraSavedVehicleDistance", nullptr, 0x20, "-1.0", nullptr, DEFAULT);
    s_savedPitch = CVar::Register("cameraSavedPitch", nullptr, 0x20, s_cameraViewDefaults[view * 3 + 1], nullptr, DEFAULT);
    s_mouseInvertYaw = CVar::Register("mouseInvertYaw", nullptr, 0x10, "0", nullptr, DEFAULT);
    s_mouseInvertPitch = CVar::Register("mouseInvertPitch", nullptr, 0x10, "0", nullptr, DEFAULT);
    s_bobbing = CVar::Register("cameraBobbing", nullptr, 0x0, "0", nullptr, DEFAULT);
    // TODO the three smoothing globals the reference seeds here (DAT_00c24e5c..64 from DAT_00a4040c / DAT_00a34c18)
    s_distanceMoveSpeed = CVar::Register("cameraDistanceMoveSpeed", nullptr, 0x10, "8.33", &ValidateCameraSpeed, DEFAULT);
    s_pitchMoveSpeed = CVar::Register("cameraPitchMoveSpeed", nullptr, 0x10, "90", &ValidateCameraAngleSpeed, DEFAULT);
    s_yawMoveSpeed = CVar::Register("cameraYawMoveSpeed", nullptr, 0x10, "180", &ValidateCameraAngleSpeed, DEFAULT);
    s_bobbingSmoothSpeed = CVar::Register("cameraBobbingSmoothSpeed", nullptr, 0x10, "0.8", &ValidateCameraSpeed, DEFAULT);
    s_fovSmoothSpeed = CVar::Register("cameraFoVSmoothSpeed", nullptr, 0x10, "0.5", &ValidateCameraAngleSpeed, DEFAULT);
    s_distanceSmoothSpeed = CVar::Register("cameraDistanceSmoothSpeed", nullptr, 0x10, "8.33", &ValidateCameraSpeed, DEFAULT);
    s_groundSmoothSpeed = CVar::Register("cameraGroundSmoothSpeed", nullptr, 0x10, "7.5", &ValidateCameraAngleSpeed, DEFAULT);
    s_heightSmoothSpeed = CVar::Register("cameraHeightSmoothSpeed", nullptr, 0x10, "1.2", &ValidateCameraSpeed, DEFAULT);
    s_pitchSmoothSpeed = CVar::Register("cameraPitchSmoothSpeed", nullptr, 0x10, "45", &ValidateCameraAngleSpeed, DEFAULT);
    s_targetSmoothSpeed = CVar::Register("cameraTargetSmoothSpeed", nullptr, 0x10, "90", &ValidateCameraAngleSpeed, DEFAULT);
    s_yawSmoothSpeed = CVar::Register("cameraYawSmoothSpeed", nullptr, 0x10, "180", &ValidateCameraAngleSpeed, DEFAULT);
    s_flyingMountHeightSmoothSpeed = CVar::Register("cameraFlyingMountHeightSmoothSpeed", nullptr, 0x10, "2.0", &ValidateCameraSpeed, DEFAULT);
    s_viewBlendStyle = CVar::Register("cameraViewBlendStyle", nullptr, 0x10, "1", nullptr, DEFAULT);
    s_view = CVar::Register("cameraView", nullptr, 0x10, "2", &ValidateCameraView, DEFAULT);

    // camera{Distance,Pitch,Yaw}{"",A,B,C,D,E,Com,Barber Shop}: the saved view presets
    for (int32_t i = 0; i < 8; i++) {
        for (int32_t j = 0; j < 3; j++) {
            name[0] = 0;
            SStrPack(name, "camera", sizeof(name));
            SStrPack(name, s_cameraViewKinds[j], sizeof(name));
            SStrPack(name, s_cameraViewSuffixes[i], sizeof(name));
            s_views[i][j] = CVar::Register(name, nullptr, 0x50, s_cameraViewDefaults[i * 3 + j], s_cameraViewValidators[j], DEFAULT);
        }
    }

    s_smooth = CVar::Register("camerasmooth", nullptr, 0x10, "1", nullptr, DEFAULT);
    s_smoothPitch = CVar::Register("cameraSmoothPitch", nullptr, 0x10, "1", nullptr, DEFAULT);
    s_smoothYaw = CVar::Register("cameraSmoothYaw", nullptr, 0x10, "1", nullptr, DEFAULT);
    s_smoothStyle = CVar::Register("cameraSmoothStyle", nullptr, 0x10, "4", &ValidateCameraSmoothStyle, DEFAULT);
    s_smoothTrackingStyle = CVar::Register("cameraSmoothTrackingStyle", nullptr, 0x10, "4", &ValidateCameraSmoothStyle, DEFAULT);
    s_customViewSmoothing = CVar::Register("cameraCustomViewSmoothing", nullptr, 0x10, "0", nullptr, DEFAULT);

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
                s_smoothState[style][state][param] = CVar::Register(name, nullptr, 0x10, s_cameraSmoothDefaults[(style * 7 + state) * 2 + param], nullptr, DEFAULT);
            }
        }

        for (int32_t kind = 0; kind < 3; kind++) {
            for (int32_t param = 0; param < 2; param++) {
                name[0] = 0;
                SStrPack(name, "cameraSmoothViewData", sizeof(name));
                SStrPack(name, s_cameraSmoothStyles[style], sizeof(name));
                SStrPack(name, s_cameraViewKinds[kind], sizeof(name));
                SStrPack(name, s_cameraSmoothParams[param], sizeof(name));
                s_smoothViewData[style][kind][param] = CVar::Register(name, nullptr, 0x10, s_cameraSmoothViewDataDefaults[(style * 3 + kind) * 2 + param], nullptr, DEFAULT);
            }
        }

        for (int32_t state = 0; state < 10; state++) {
            for (int32_t param = 0; param < 3; param++) {
                name[0] = 0;
                SStrPack(name, "cameraTerrainTilt", sizeof(name));
                SStrPack(name, s_cameraSmoothStyles[style], sizeof(name));
                SStrPack(name, s_cameraTiltStates[state], sizeof(name));
                SStrPack(name, s_cameraTiltParams[param], sizeof(name));
                s_terrainTiltState[style][state][param] = CVar::Register(name, nullptr, 0x10, s_cameraTerrainTiltDefaults[(style * 10 + state) * 3 + param], nullptr, DEFAULT);
            }
        }
    }

    s_terrainTilt = CVar::Register("cameraTerrainTilt", nullptr, 0x10, "0", nullptr, DEFAULT);
    s_terrainTiltTimeMin = CVar::Register("cameraTerrainTiltTimeMin", nullptr, 0x10, "3.0", &ValidateCameraTime, DEFAULT);
    s_terrainTiltTimeMax = CVar::Register("cameraTerrainTiltTimeMax", nullptr, 0x10, "10.0", &ValidateCameraTime, DEFAULT);
    s_waterCollision = CVar::Register("cameraWaterCollision", nullptr, 0x10, "1", nullptr, DEFAULT);
    s_heightIgnoreStandState = CVar::Register("cameraHeightIgnoreStandState", nullptr, 0x10, "0", nullptr, DEFAULT);
    s_pivot = CVar::Register("cameraPivot", nullptr, 0x10, "1", nullptr, DEFAULT);
    s_pivotDXMax = CVar::Register("cameraPivotDXMax", nullptr, 0x10, "0.05", nullptr, DEFAULT);
    s_pivotDYMin = CVar::Register("cameraPivotDYMin", nullptr, 0x10, "0.00", nullptr, DEFAULT);
    s_dive = CVar::Register("cameraDive", nullptr, 0x10, "1", nullptr, DEFAULT);
    s_surfacePitch = CVar::Register("cameraSurfacePitch", nullptr, 0x10, "0.0", &ValidateCameraPitch, DEFAULT);
    s_submergePitch = CVar::Register("cameraSubmergePitch", nullptr, 0x10, "18.0", &ValidateCameraPitch, DEFAULT);
    s_surfaceFinalPitch = CVar::Register("cameraSurfaceFinalPitch", nullptr, 0x10, "5.0", &ValidateCameraPitch, DEFAULT);
    s_submergeFinalPitch = CVar::Register("cameraSubmergeFinalPitch", nullptr, 0x10, "5.0", &ValidateCameraPitch, DEFAULT);
    s_distanceMax = CVar::Register("cameraDistanceMax", nullptr, 0x10, "15.0", &ValidateCameraDistance, DEFAULT);
    s_distanceMaxFactor = CVar::Register("cameraDistanceMaxFactor", nullptr, 0x10, "1.0", nullptr, DEFAULT);
    s_pitchSmoothMin = CVar::Register("cameraPitchSmoothMin", nullptr, 0x10, "0.0", &ValidateCameraPitch, DEFAULT);
    s_pitchSmoothMax = CVar::Register("cameraPitchSmoothMax", nullptr, 0x10, "30.0", &ValidateCameraPitch, DEFAULT);
    s_yawSmoothMin = CVar::Register("cameraYawSmoothMin", nullptr, 0x10, "0.0", &ValidateCameraYaw, DEFAULT);
    s_yawSmoothMax = CVar::Register("cameraYawSmoothMax", nullptr, 0x10, "0.0", &ValidateCameraYaw, DEFAULT);
    s_smoothTimeMin = CVar::Register("cameraSmoothTimeMin", nullptr, 0x10, "0.1", &ValidateCameraTime, DEFAULT);
    s_smoothTimeMax = CVar::Register("cameraSmoothTimeMax", nullptr, 0x10, "2.0", &ValidateCameraTime, DEFAULT);
}

// ref: FUN_005fe580
void CGCamera::SetTimedValue(int32_t index, int32_t startTime, int32_t duration, uint32_t value) {
    if (this->m_flags2 & 0x40) {
        return;
    }

    uint32_t bit = 1u << ((index * 2) & 0x1F);

    if (!(bit & this->m_timedBits)) {
        this->m_timedBits |= bit;
        this->m_timedStart[index] = startTime;
    }

    memcpy(&this->m_timedValue[index], &value, sizeof(value));

    if (duration) {
        this->m_timedEnd[index] = startTime + duration;
        return;
    }

    this->m_timedEnd[index] = 0;
}

// ref: FUN_005fe890
void CGCamera::SetBlendA(uint32_t value, int32_t start, int32_t end) {
    this->m_unk2D4 = start;
    this->m_unk2D8 = end;

    if (start != end) {
        this->m_unk2DC = this->m_unk2E4;
        this->m_unk2E0 = value;
        return;
    }

    this->m_unk2E4 = value;
}

// ref: FUN_005fe8d0
void CGCamera::SetBlendB(uint32_t value, int32_t start, int32_t end) {
    this->m_unk2E8 = start;
    this->m_unk2EC = end;

    if (start != end) {
        this->m_unk2F0 = this->m_unk2F8;
        this->m_unk2F4 = value;
        return;
    }

    this->m_unk2F8 = value;
}

// ref: FUN_005fe910
void CGCamera::SetUnk2FC(uint32_t value) {
    this->m_unk2FC = value;
}

// ref: FUN_005fe920
void CGCamera::SetOverride2D0(float value) {
    this->m_flags2 |= 0x20;
    this->m_unk2D0 = value;
}

// ref: FUN_005fe940
void CGCamera::ClearOverride2D0() {
    this->m_flags2 &= ~0x20u;
}

// ref: FUN_005fe3c0
float CameraWrapAngleOnce(float angle) {
    if (angle < 0.0f) {
        return angle + CMath::TWO_PI;
    }

    if (CMath::TWO_PI < angle) {
        return angle - CMath::TWO_PI;
    }

    return angle;
}

// ------------------------------------------------------------------------------------------------
// Dependencies outside the camera module. Each is the reference function by its address; those
// whose module frozen has not ported say what they stand in for.
// ------------------------------------------------------------------------------------------------


// The click-to-move action (0x00ca11f4) and facing (0x00ca11d8), FUN_00715c60 / FUN_00715cf0.
// Click-to-move is not ported, so the action stays 13, "none", as the reference initialises it.
static int32_t s_clickToMoveAction = 13;
static float s_clickToMoveFacing = 0.0f;

// ref: FUN_00715c60
static int32_t ClickToMoveGetAction() {
    return s_clickToMoveAction;
}

// ref: FUN_00715cf0
static float ClickToMoveGetFacing() {
    return s_clickToMoveFacing;
}

// ref: FUN_00721f90
// The active player, while click-to-move is driving it.
static int32_t UnitIsClickMoving(CGUnit_C* unit) {
    return unit->GetGUID() == ClntObjMgrGetActivePlayer() && s_clickToMoveAction != 13;
}

// ref: FUN_004cee50
static int32_t ObjectIsActivePlayer(CGObject_C* object) {
    return object->GetGUID() == ClntObjMgrGetActivePlayer();
}

// The facing the camera follows for a unit: its smoothed facing, turned through every transport
// it rides. ref: FUN_00717e50 (CGUnit_C::GetWorldSmoothFacing).
static float UnitWorldSmoothFacing(CGUnit_C* unit) {
    float facing = unit->GetRawSmoothFacing();
    auto transport = ClntObjMgrObjectPtr(unit->GetTransportGUID(), TYPE_OBJECT, __FILE__, __LINE__);
    CVehicleCamera_C::ConvertSmoothFacingFromRawToWorld(facing, transport);

    return facing;
}

// ------------------------------------------------------------------------------------------------
// Free look and the locks
// ------------------------------------------------------------------------------------------------

// ref: FUN_006019b0
// The first lock freezes the yaw where the target faces now.
void CGCamera::LockYaw() {
    int32_t previous = this->m_yawLock++;

    if (previous != 0) {
        return;
    }

    auto target = ClntObjMgrObjectPtr(this->m_target, TYPE_OBJECT, __FILE__, __LINE__);

    if (!target) {
        this->AdjustYaw(0.0f);
        return;
    }

    if (!target->IsA(TYPE_UNIT)) {
        this->AdjustYaw(target->GetFacing());
        return;
    }

    if (this->m_vehicleCamera) {
        this->AdjustYaw(this->m_vehicleCamera->m_smoothFacing);
        return;
    }

    this->AdjustYaw(static_cast<CGUnit_C*>(target)->m_smoothFacing);
}

// ref: FUN_00601a70
// The last unlock hands the yaw back to the target's facing.
void CGCamera::UnlockYaw() {
    if (--this->m_yawLock != 0) {
        return;
    }

    auto target = ClntObjMgrObjectPtr(this->m_target, TYPE_OBJECT, __FILE__, __LINE__);

    if (!target) {
        this->AdjustYaw(-0.0f);
        return;
    }

    if (!target->IsA(TYPE_UNIT)) {
        this->AdjustYaw(-target->GetFacing());
        return;
    }

    if (!this->m_vehicleCamera || !this->m_vehicleCamera->IsControllingFacing()) {
        if (this->m_flags & 0x4000) {
            this->AdjustYaw(-this->m_lockedYaw);
            return;
        }

        if (!this->m_vehicleCamera) {
            this->AdjustYaw(-target->GetRawFacing());
            return;
        }
    }

    this->AdjustYaw(-this->GetVehicleFacing(target));
}

// ref: FUN_00601ff0
void CGCamera::BeginFreeLook() {
    if (this->m_flags & 0x1) {
        return;
    }

    this->m_flags |= 0x1;
    this->LockYaw();

    if (this->m_pitchLock++ == 0) {
        this->AdjustPitch(this->m_tiltPitch);
    }

    this->m_flags &= 0xF4FFBFFF;
    this->m_flags2 &= ~0x4u;

    this->m_pitchBlend.m_target = this->m_pitch;
    this->m_pitchBlend.m_start = 0;
    this->m_pitchBlend.m_duration = 0.0f;
    this->m_pitchOffsetBlend.m_start = 0;
    this->m_yawBlend.m_start = 0;
    this->m_pitchOffsetBlend.m_target = this->m_pitchOffset;
    this->m_pitchOffsetBlend.m_duration = 0.0f;
    this->m_yawBlend.m_target = this->m_yaw;
    this->m_yawBlend.m_duration = 0.0f;
}

// ref: FUN_00601f70
void CGCamera::EndFreeLook() {
    uint32_t flags = this->m_flags;

    if (!(flags & 0x1) || (this->m_flags2 & 0x2)) {
        return;
    }

    this->m_flags = flags & ~0x1u;

    if (this->m_vehicleCamera && (this->m_vehicleCamera->m_flags & 0x10)) {
        this->m_flags = flags & 0xFFFFBFFE;
    }

    this->UnlockYaw();

    if (--this->m_pitchLock == 0) {
        this->AdjustPitch(-this->m_tiltPitch);
    }

    this->m_flags &= ~0x4000u;
}

// ref: FUN_006047e0
// Free look, unless the player is on a taxi with a seat that forbids it.
void CGCamera::BeginFreeLookIfAllowed() {
    auto target = ClntObjMgrObjectPtr(this->m_target, TYPE_OBJECT, __FILE__, __LINE__);

    if (target && target->IsA(TYPE_PLAYER)
        && (static_cast<CGUnit_C*>(target)->Unit()->flags & 0x100000)
        && (static_cast<CGPlayer_C*>(target)->Player()->flags & 0x20000)) {
        return;
    }

    this->BeginFreeLook();
}

// ref: FUN_00604850
void CGCamera::SetViewLocked(int32_t locked) {
    this->EndFreeLook();

    if (locked) {
        this->m_flags |= 0x20;
    } else {
        this->m_flags &= ~0x20u;
    }

    this->m_pitchBlend.m_target = this->m_pitch;
    this->m_yawBlend.m_target = this->m_yaw;
    this->m_pitchOffsetBlend.m_target = this->m_pitchOffset;
}

// ref: FUN_006020b0
// Mouse look: the cursor's motion, in normalized device units, turns the yaw and pitch at the
// move-speed cvars; with smart pivot the vertical motion goes to the tracking pitch first.
void CGCamera::MouseLook(float deltaX, float deltaY, float* pitchOut) {
    if (pitchOut) {
        *pitchOut = 0.0f;
    }

    if (this->m_flags & 0x8000) {
        return;
    }

    auto target = ClntObjMgrObjectPtr(this->m_target, TYPE_OBJECT, __FILE__, __LINE__);

    if (target && target->IsA(TYPE_PLAYER)
        && (static_cast<CGUnit_C*>(target)->Unit()->flags & 0x100000)
        && (static_cast<CGPlayer_C*>(target)->Player()->flags & 0x20000)) {
        return;
    }

    this->m_flags |= 0x40;

    float pitchSpeed = Float(s_pitchMoveSpeed);
    float yawSpeed = Float(s_yawMoveSpeed);
    float pivotDXMax = Float(s_pivotDXMax);
    float pivotDYMin = Float(s_pivotDYMin);

    DDCToNDC(deltaX, deltaY, &deltaX, &deltaY);

    float yaw = deltaX * 0.0012499999720603228f * yawSpeed * CMath::DEG2RAD;
    float pitch = deltaY * 0.0016666667070239782f * CMath::DEG2RAD * pitchSpeed;

    float pitchSign = Int(s_mouseInvertPitch) ? -1.0f : 1.0f;
    float yawSign = Int(s_mouseInvertYaw) ? -1.0f : 1.0f;

    if (pitchOut) {
        *pitchOut = pitchSign * pitch;
        pitch = 0.0f;
    }

    int32_t pivot = this->CanPivot(target);
    bool tracking = !(this->m_flags & 0x8000000) && 0.0010000000474974513f <= std::fabs(this->m_pitchOffset);

    if (pivot) {
        if (pivotDYMin < std::fabs(pitch) && std::fabs(yaw) < pivotDXMax) {
            tracking = true;
        }

        if (0.0f < pitch) {
            if (this->m_pitchOffset < 0.0f && 0.0f < pitchSign * pitch + this->m_pitchOffset) {
                pitch = this->m_pitchOffset + pitch;
            } else if (2.384185791015625e-07f <= std::fabs(this->m_pitchOffset)) {
                goto adjust;
            }

            tracking = false;
            this->m_pitchOffset = 0.0f;
            this->m_pitchOffsetBlend.m_target = 0.0f;
        }
    }

adjust:
    bool apply = tracking;

    if (tracking) {
        float offset = pitchSign * pitch + this->m_pitchOffset;
        this->m_pitchOffset = offset;

        float limit = s_pitchMin - this->m_pitch;

        if (this->m_pitch < 0.0f && offset < limit) {
            this->m_pitchOffset = limit;
        }

        apply = 0.0f < this->m_pitchOffset;
    }

    if (!apply) {
        this->m_pitchOffsetBlend.m_target = this->m_pitchOffset;
        this->m_pitchOffsetBlend.m_start = 0;
        this->m_flags &= ~0x8000000u;
        this->m_pitchOffsetBlend.m_duration = 0.0f;
    } else {
        this->SmoothPitchOffset(0.0f, 0.0f, 1.0f, Now());
        this->AdjustPitch(pitchSign * pitch);
    }

    this->AdjustYaw(-(yawSign * yaw));
    this->ClampAngles();
}

// ref: FUN_006023d0
// Mouse look turns the player as well: the facing goes through input control, and the pitch with
// it when swimming or flying (with cameraDive, clamped toward the surface while diving or
// surfacing).
void CGCamera::SyncPlayerFacing() {
    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_target, TYPE_UNIT, __FILE__, __LINE__));
    float facing = this->m_yaw;

    if (!unit || !UnitConvertFacingToRaw(unit, &facing)) {
        this->m_flags |= 0x4000;
        this->m_flags2 |= 0x4;
        this->m_lockedYaw = this->m_yaw;
    } else if (this->m_flags2 & 0x4) {
        this->m_flags |= 0x4000;
        this->m_yawBlend.m_target = facing;
        this->m_yaw = facing;
        this->m_lockedYaw = facing;
    } else {
        this->m_flags &= ~0x4000u;
    }

    float pitch = std::min(std::max(this->m_pitchOffset + this->m_pitch, s_pitchMin), s_pitchMax);

    auto player = ClntObjMgrObjectPtr(this->m_target, TYPE_PLAYER, __FILE__, __LINE__);
    uint32_t flags = this->m_flags;

    if (!(flags & 0x100) && ((flags & 0x100000) || (flags & 0x200000)) && Int(s_dive)) {
        if (!player || PlayerGetSwimDepth(static_cast<CGPlayer_C*>(player)) == 0.0f) {
            float surface = Float(s_surfacePitch) * CMath::DEG2RAD;
            float submerge = CMath::DEG2RAD * Float(s_submergePitch);

            if ((flags & 0x200000) && surface < 0.0f && pitch < 0.0f && surface < pitch) {
                pitch = 0.0f;
            }

            if ((flags & 0x100000) && 0.0f < submerge && 0.0f < pitch && pitch < submerge) {
                pitch = 0.0f;
            }
        }
    }

    int32_t time = Now();
    auto input = InputControlGetActive();
    InputControlSetFacing(input, time, this->m_yaw);

    if (!unit || !UnitIsRidingControlledVehicle(unit)) {
        InputControlSetPitch(input, time, pitch);
    }
}

// ref: FUN_00602dc0
// Tracking (the camera following the target's turns) on or off, and the yaw lock that goes with
// the smart tracking styles.
void CGCamera::SetTracking(int32_t tracking) {
    uint32_t flags = this->m_flags;
    this->m_flags = flags & ~0x4000u;

    if (!(flags & 0x100)) {
        if (tracking) {
            this->m_flags = (flags & ~0x4000u) | 0x100;
        }
    } else if (!tracking) {
        this->m_flags = flags & 0xFFFFBEFF;
    }

    flags = this->m_flags;
    bool locked;

    if (!(flags & 0x100)) {
        locked = false;
    } else if (this->m_unk2FC == 1) {
        locked = true;
    } else if (this->m_unk2FC == 2) {
        locked = false;
    } else if (Int(s_smoothTrackingStyle)) {
        if (Int(s_smoothTrackingStyle) == 2) {
            locked = false;
        } else {
            int32_t action = ClickToMoveGetAction();
            locked = !(action == 1 || action == 3);
        }
    } else {
        locked = true;
    }

    if (!(flags & 0x2000)) {
        if (locked) {
            this->LockYaw();
            this->m_flags |= 0x2000;
        }
    } else if (!locked) {
        this->UnlockYaw();
        this->m_flags &= ~0x2000u;
    }

    this->CalcSmoothing(InputControlGetActive(), 0.0f);
}

// ref: FUN_00602eb0
// On a taxi the yaw is held where it was while the ride lasts.
void CGCamera::SetTaxiLook(int32_t taxi) {
    if (taxi) {
        if (!(this->m_flags & 0x1000)) {
            this->LockYaw();
            this->m_flags = (this->m_flags & ~0x4000u) | 0x1000;
        }

        this->CalcSmoothing(InputControlGetActive(), 0.0f);
    }

    if ((this->m_flags & 0x1000) && !taxi) {
        this->UnlockYaw();
        this->m_flags &= 0xFFFFAFFF;
        this->CalcSmoothing(InputControlGetActive(), 0.0f);
    }
}

// ------------------------------------------------------------------------------------------------
// Smoothing
// ------------------------------------------------------------------------------------------------

// ref: FUN_005ffeb0
int32_t CGCamera::CanPitchSmooth(float minPitch, float maxPitch) const {
    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_target, TYPE_UNIT, __FILE__, __LINE__));

    return unit && !(this->m_flags & 0x1) && Int(s_smoothPitch)
        && !(unit->m_localMove.GetMoveFlags() & 0x2200000)
        && (this->m_pitch < minPitch || maxPitch < this->m_pitch);
}

// ref: FUN_005fff40
int32_t CGCamera::IsSmoothing() const {
    if (this->m_flags & 0x20) {
        return 0;
    }

    if (!Int(s_customViewSmoothing) && !const_cast<CGCamera*>(this)->IsViewDefault(this->m_view)) {
        return 0;
    }

    return ~(this->m_flags >> 15) & 1;
}

// ref: FUN_00602600
int32_t CGCamera::CanTrackPitch() const {
    if (!(0.0010000000474974513f <= std::fabs(this->m_pitchOffset))) {
        return 0;
    }

    if (this->m_flags & 0x100) {
        if (this->m_unk2FC == 1) {
            return 0;
        }

        if (this->m_unk2FC != 2 && !(Int(s_smoothTrackingStyle) && ClickToMoveGetAction())) {
            return 0;
        }
    }

    auto target = ClntObjMgrObjectPtr(this->m_target, TYPE_OBJECT, __FILE__, __LINE__);

    return this->CanPivot(target) == 0;
}

// ref: FUN_00602680
int32_t CGCamera::CanYawSmooth(float minYaw, float maxYaw) const {
    auto target = ClntObjMgrObjectPtr(this->m_target, TYPE_OBJECT, __FILE__, __LINE__);

    if (!target || (this->m_flags & 0x1) || !Int(s_smoothYaw)) {
        return 0;
    }

    if (this->m_yawLock < 1) {
        return !(minYaw <= this->m_yaw && this->m_yaw <= maxYaw);
    }

    float facing;

    if (!target->IsA(TYPE_UNIT)) {
        facing = target->GetFacing();
    } else if (!(this->m_flags & 0x100)) {
        UnitUpdateSmoothFacing(static_cast<CGUnit_C*>(target), 0);
        facing = this->GetVehicleFacing(target);
    } else {
        facing = ClickToMoveGetFacing();
    }

    return NotEqual(this->m_yaw, facing);
}

// ref: FUN_00602760
// Picks the smoothing state from what the player is doing (idle, stop, track, move, strafe, turn,
// fear) and the smooth style, reads its delay and factor and the view's, and blends the pitch,
// the tracking pitch and the yaw toward the view with one shared duration.
void CGCamera::CalcSmoothing(void* inputPtr, float strafe) {
    auto input = static_cast<CInputControl*>(inputPtr);

    if (!input) {
        return;
    }

    uint32_t states = (this->m_flags & 0x1000) ? 0x40 : 0;
    uint32_t moving = InputControlGetFlags(input);

    if (((moving & 0x300) && !(moving & 0x2000001)) || (moving & 0x2000001)) {
        states |= 0x20;
    }

    if ((moving & 0xC0) || ((moving & 0x2000001) && (moving & 0x300))) {
        states |= 0x10;
    }

    if ((moving & 0x1030) || ((moving & 0x1) && (moving & 0x2))) {
        states |= 0x8;
    }

    if (this->m_flags & 0x100) {
        if (ClickToMoveGetAction() == 3 && !(this->m_flags2 & 0x1)) {
            states |= 0x8;
        } else {
            states |= 0x4;
        }
    }

    if (strafe != 0.0f) {
        states |= 0x2;
    }

    moving = InputControlGetFlags(input);

    if (!(moving & 0x1030) && !(moving & 0xC0) && (!(moving & 0x2000001) || !(moving & 0x300))
        && (!(moving & 0x300) || (moving & 0x2000001)) && !(moving & 0x1E00000)) {
        states |= 0x1;
    }

    this->m_flags &= ~0x4000u;

    if (!states || !this->IsSmoothing()) {
        return;
    }

    int32_t time = Now();
    uint32_t style;

    if (!(states & 0x44)) {
        style = Int(s_smoothStyle);
    } else if (this->m_unk2FC == 1) {
        style = 0;
    } else if (this->m_unk2FC == 2) {
        style = 3;
    } else {
        style = Int(s_smoothTrackingStyle);
    }

    // The highest state bit set picks the delay and factor.
    float smooth[3][2] = {};
    float stateDelay = 0.0f;
    float stateFactor = 0.0f;

    for (int32_t state = 6; state >= 0; state--) {
        if (!(states & (1u << state)) || 4 < style) {
            continue;
        }

        float delay = Float(s_smoothState[style][state][0]);
        stateDelay = 0.0f;

        if (0.0f <= delay) {
            stateDelay = delay;

            if (!(delay < 100.0f)) {
                stateDelay = 99.0f;
            }
        }

        float factor = Float(s_smoothState[style][state][1]);
        stateFactor = 0.0f;

        if (0.0f <= factor) {
            stateFactor = factor;

            if (!(factor < 100.0f)) {
                stateFactor = 99.0f;
            }
        }

        break;
    }

    for (int32_t kind = 2; kind >= 0; kind--) {
        if (style < 5) {
            for (int32_t param = 1; param >= 0; param--) {
                float value = Float(s_smoothViewData[style][kind][param]);

                if (param == 0) {
                    value += stateDelay;
                } else {
                    value *= stateFactor;
                }

                if (value < 0.0f) {
                    value = 0.0f;
                }

                if (!(value < 100.0f)) {
                    value = 99.0f;
                }

                smooth[kind][param] = value;
            }
        }
    }

    float pitchMin = Float(s_pitchSmoothMin) * CMath::DEG2RAD;
    float pitchMax = Float(s_pitchSmoothMax) * CMath::DEG2RAD;
    float yawMin = Float(s_yawSmoothMin) * CMath::DEG2RAD;
    float yawMax = Float(s_yawSmoothMax) * CMath::DEG2RAD;

    int32_t pitchSmooth = this->CanPitchSmooth(pitchMin, pitchMax);
    int32_t trackSmooth = this->CanTrackPitch();
    int32_t yawSmooth = this->CanYawSmooth(yawMin, yawMax);

    float duration = 0.0f;

    if (pitchSmooth) {
        if (smooth[1][1] == 0.0f) {
            this->StopPitchBlend();
            pitchSmooth = 0;
        } else {
            float pitch = this->m_views[this->m_view][1];

            if (this->m_pitch < pitchMin) {
                pitch = pitchMin;
            }

            if (pitchMax < this->m_pitch) {
                pitch = pitchMax;
            }

            pitchSmooth = this->SmoothPitch(pitch, smooth[1][0], smooth[1][1], time);

            if (pitchSmooth && 0.0f <= this->m_pitchBlend.m_duration) {
                duration = this->m_pitchBlend.m_duration;
            }
        }
    }

    if (trackSmooth) {
        if (stateFactor == 0.0f) {
            this->StopPitchOffsetBlend();
            trackSmooth = 0;
        } else {
            trackSmooth = this->SmoothPitchOffset(0.0f, stateDelay, stateFactor, time);

            if (trackSmooth && duration <= this->m_pitchOffsetBlend.m_duration) {
                duration = this->m_pitchOffsetBlend.m_duration;
            }
        }
    }

    int32_t yawSmoothing = 0;

    if (yawSmooth) {
        if (smooth[2][1] == 0.0f) {
            this->StopYawBlend();
        } else {
            float viewYaw = this->m_views[this->m_view][2];
            float yaw = viewYaw;

            if (this->m_yawLock < 1) {
                if (this->m_yaw < yawMin) {
                    yaw = yawMin;
                }

                if (yawMax < this->m_yaw) {
                    yaw = yawMax;
                }
            } else {
                auto target = ClntObjMgrObjectPtr(this->m_target, TYPE_OBJECT, __FILE__, __LINE__);

                if (!target || !target->IsA(TYPE_UNIT)) {
                    yaw = NormalizeAngle(target->GetFacing() + viewYaw);
                } else if (!(this->m_flags & 0x100)) {
                    UnitUpdateSmoothFacing(static_cast<CGUnit_C*>(target), 0);

                    if (!input->m_unk44) {
                        yaw = NormalizeAngle(this->GetVehicleFacing(target) + viewYaw);
                    } else {
                        yaw = NormalizeAngle(input->m_unk48 + viewYaw);
                    }
                } else {
                    yaw = NormalizeAngle(ClickToMoveGetFacing() + viewYaw);
                }
            }

            yawSmoothing = this->SmoothYaw(yaw, smooth[2][0], smooth[2][1], time);

            if (yawSmoothing && duration <= this->m_yawBlend.m_duration) {
                duration = this->m_yawBlend.m_duration;
            }
        }
    }

    if (duration < Float(s_smoothTimeMin)) {
        duration = Float(s_smoothTimeMin);
    }

    if (Float(s_smoothTimeMax) < duration) {
        duration = Float(s_smoothTimeMax);
    }

    if (yawSmoothing) {
        this->m_yawBlend.m_duration = duration;
    }

    if (pitchSmooth) {
        this->m_pitchBlend.m_duration = duration;
    }

    if (trackSmooth) {
        this->m_pitchOffsetBlend.m_duration = duration;
    }
}

// ------------------------------------------------------------------------------------------------
// Bobbing
// ------------------------------------------------------------------------------------------------

// ref: FUN_005ffd20
// First person on foot, moving, with cameraBobbing on.
int32_t CGCamera::CanBob(CGObject_C* target) const {
    if (0.1666666716337204f < this->m_distance) {
        return 0;
    }

    if (!target || !target->IsA(TYPE_UNIT)) {
        return 0;
    }

    auto unit = static_cast<CGUnit_C*>(target);
    uint32_t moveFlags = unit->m_localMove.GetMoveFlags();

    if ((this->m_flags & 0x8) || !Int(s_bobbing) || (moveFlags & 0x2200000) || (moveFlags & 0x1000)) {
        return 0;
    }

    if (CGCamera::IsFalling(unit) || unit->Unit()->mountDisplayID || (unit->Unit()->flags & 0x100000)) {
        return 0;
    }

    return this->m_flags & 0x200;
}

// ref: FUN_005ffde0
int32_t CGCamera::CanPivot(CGObject_C* target) const {
    if (!target || !target->IsA(TYPE_UNIT) || !Int(s_pivot)) {
        return 0;
    }

    uint32_t moveFlags = static_cast<CGUnit_C*>(target)->m_localMove.GetMoveFlags();

    if ((moveFlags & 0xF) || 0.0f < this->m_pitch || (this->m_flags & 0x8)) {
        return 0;
    }

    if (!(moveFlags & 0x2000000)) {
        return this->m_flags & 0x30000;
    }

    return this->m_flags & 0x10000;
}

// ref: FUN_005ffe50
// A bob offset left over that can no longer bob, and has to settle.
int32_t CGCamera::IsBobbing() const {
    if (std::fabs(this->m_bob.z + this->m_bob.y + this->m_bob.x) < 0.0010000000474974513f) {
        return 0;
    }

    auto target = ClntObjMgrObjectPtr(this->m_target, TYPE_OBJECT, __FILE__, __LINE__);

    return this->CanBob(target) == 0;
}

// ref: FUN_00600030
// Settle the bob from where it is, at cameraBobbingSmoothSpeed along its largest axis.
void CGCamera::StopBobbing() {
    this->m_flags &= ~0x200u;

    int32_t axis = DominantAxis(this->m_bob);
    float value = (&this->m_bob.x)[axis];

    this->m_bobFrom = this->m_bob;
    this->m_bobDuration = std::fabs(value) / Float(s_bobbingSmoothSpeed);
    this->m_bobStart = Now();
}

// ref: FUN_00600090
void CGCamera::SetBobbing(int32_t moving) {
    if (!(this->m_flags & 0x200) && moving) {
        this->m_flags |= 0x200;
        this->m_bobStart = Now();
    }

    if ((this->m_flags & 0x200) && !moving) {
        this->StopBobbing();
    }
}

// ref: FUN_00602f30
// The bob: a sideways sway at the step frequency and a vertical bounce at twice it, both scaled
// by the unit's speed.
void CGCamera::CalcBobbing(C3Vector* offset) {
    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_target, TYPE_UNIT, __FILE__, __LINE__));

    if (!unit || !this->CanBob(unit)) {
        return;
    }

    float facing = this->m_vehicleCamera ? this->m_vehicleCamera->m_smoothFacing : unit->m_smoothFacing;

    float sway = s_bobAmplitudeV * 0.02777777798473835f;
    float bounce = 0.02777777798473835f * s_bobAmplitudeH;

    float speed = unit->m_localMove.GetCurrentSpeed(0) * 0.1428571492433548f;
    float rate = 0.5f;

    if (0.5f <= speed) {
        rate = speed < 1.5f ? speed : 1.5f;
    }

    float elapsed = Elapsed(Now() - this->m_bobStart);
    float frequency = rate * s_bobFrequency;

    float side = std::sin(elapsed * 0.0010000000474974513f * frequency * CMath::TWO_PI) * sway;

    offset->x = std::cos(facing + 1.5707963705062866f) * side;
    offset->y = side * std::sin(facing + 1.5707963705062866f);
    offset->z = std::sin((frequency + frequency) * (elapsed * 0.0010000000474974513f) * CMath::TWO_PI) * bounce;
}

// ------------------------------------------------------------------------------------------------
// The target frame
// ------------------------------------------------------------------------------------------------

// ref: FUN_006009e0
float CGCamera::GetVehicleFacing(CGObject_C* target) const {
    if (this->m_vehicleCamera) {
        return this->m_vehicleCamera->m_smoothFacing;
    }

    return static_cast<CGUnit_C*>(target)->m_smoothFacing;
}

// ref: FUN_00603090
C3Vector& CGCamera::GetTargetPosition(C3Vector& out, CGObject_C* target) const {
    if (this->m_vehicleCamera) {
        if (!(this->m_vehicleCamera->m_flags & 0x40)) {
            this->m_vehicleCamera->UpdatePosition();
        }

        out = this->m_vehicleCamera->m_position;
        return out;
    }

    out = target->GetPosition();
    return out;
}

// ref: FUN_005ff5a0
// The facing the yaw follows, with a moving target's turn rate capped: 15 degrees at most per
// frame and nothing under 5.
float CGCamera::WrapTargetYaw(float facing, int32_t moved) {
    if (!Int(s_smooth) || !moved) {
        return facing;
    }

    float delta = facing - this->m_targetFacing;

    if (CMath::PI < delta) {
        delta -= CMath::TWO_PI;
    } else if (delta < -CMath::PI) {
        delta += CMath::TWO_PI;
    }

    float step;

    if (std::fabs(delta) <= 0.2617993950843811f) {
        if (std::fabs(delta) <= 0.09000000357627869f) {
            return facing;
        }

        step = delta < 0.0f ? -0.09000000357627869f : 0.09000000357627869f;
    } else {
        step = delta < 0.0f ? -0.2617993950843811f : 0.2617993950843811f;
    }

    return CameraWrapAngleOnce(step + this->m_targetFacing);
}

// ref: FUN_00604490
// The view angles this frame: the yaw (with FlipCameraYaw, and the target's facing unless locked),
// the pitch (with the terrain tilt unless locked), and the roll.
void CGCamera::GetAngles(CGObject_C* target, float* yaw, float* pitch, float* roll) {
    *yaw = this->m_yaw;
    *pitch = this->m_pitch;
    *roll = this->m_roll;

    if (!target) {
        return;
    }

    C3Vector position;
    this->GetTargetPosition(position, target);

    int32_t moved = !(this->m_lastTargetPosition.x == position.x && this->m_lastTargetPosition.y == position.y
        && this->m_lastTargetPosition.z == position.z);

    float facing;

    if (!target->IsA(TYPE_UNIT)) {
        facing = target->GetFacing();
    } else {
        UnitUpdateSmoothFacing(static_cast<CGUnit_C*>(target), 0);

        facing = this->m_vehicleCamera ? this->m_vehicleCamera->m_smoothFacing : static_cast<CGUnit_C*>(target)->m_smoothFacing;

        if (!ObjectIsActivePlayer(target) && !this->m_vehicleCamera) {
            facing = this->WrapTargetYaw(facing, moved);
        }
    }

    this->m_targetFacing = facing;

    float value = this->m_yawOffset + *yaw;
    *yaw = value;

    if (this->m_yawLock < 1) {
        *yaw = value + facing;
    }

    if (this->m_pitchLock < 1) {
        *pitch = this->m_tiltPitch + *pitch;
    }

    *yaw = NormalizeAngle(*yaw);
    *pitch = std::min(std::max(*pitch, s_pitchMin), s_pitchMax);

    this->SetFacing(*yaw, *pitch, *roll);
}

// ref: FUN_00600b60
C33Matrix CGCamera::ParentToWorld() const {
    auto relativeTo = ClntObjMgrObjectPtr(this->m_relativeTo, TYPE_OBJECT, __FILE__, __LINE__);

    if (!relativeTo) {
        return {};
    }

    float facing;

    if (!relativeTo->IsA(TYPE_UNIT)) {
        facing = relativeTo->GetFacing();
    } else {
        facing = static_cast<CGUnit_C*>(relativeTo)->GetRawSmoothFacing();
        auto transport = ClntObjMgrObjectPtr(relativeTo->GetTransportGUID(), TYPE_OBJECT, __FILE__, __LINE__);
        CVehicleCamera_C::ConvertSmoothFacingFromRawToWorld(facing, transport);
    }

    return C33Matrix::RotationAroundZ(facing);
}

// ref: FUN_00600c20
C3Vector CGCamera::Forward() const {
    if (!this->m_relativeTo) {
        return this->CSimpleCamera::Forward();
    }

    return this->CSimpleCamera::Forward() * this->ParentToWorld();
}

// ref: FUN_00600cc0
C3Vector CGCamera::Right() const {
    if (!this->m_relativeTo) {
        return this->CSimpleCamera::Right();
    }

    return this->CSimpleCamera::Right() * this->ParentToWorld();
}

// ref: FUN_00600d60
C3Vector CGCamera::Up() const {
    if (!this->m_relativeTo) {
        return this->CSimpleCamera::Up();
    }

    return this->CSimpleCamera::Up() * this->ParentToWorld();
}

// ref: FUN_00604a70
// The camera's angles taken relative to a new transport: the yaw moves from the old frame to the
// new one so the view does not jump.
void CGCamera::SetRelativeTo(WOWGUID relativeTo) {
    if (relativeTo == this->m_relativeTo) {
        return;
    }

    if (this->m_relativeTo && 0 < this->m_yawLock) {
        auto previous = ClntObjMgrObjectPtr(this->m_relativeTo, TYPE_OBJECT, __FILE__, __LINE__);

        if (previous) {
            float facing = previous->IsA(TYPE_UNIT) ? UnitWorldSmoothFacing(static_cast<CGUnit_C*>(previous)) : previous->GetFacing();
            this->AdjustYaw(facing);
        }
    }

    this->m_relativeTo = relativeTo;

    if (relativeTo && 0 < this->m_yawLock) {
        auto next = ClntObjMgrObjectPtr(relativeTo, TYPE_OBJECT, __FILE__, __LINE__);

        if (next) {
            float facing = next->IsA(TYPE_UNIT) ? UnitWorldSmoothFacing(static_cast<CGUnit_C*>(next)) : next->GetFacing();
            this->AdjustYaw(-facing);
        }
    }

    auto target = ClntObjMgrObjectPtr(this->m_target, TYPE_OBJECT, __FILE__, __LINE__);

    if (target) {
        float yaw;
        float pitch;
        float roll;
        this->GetAngles(target, &yaw, &pitch, &roll);
    }
}

// ref: FUN_00604b90
// The vehicle camera to drive by: the active player's seat camera when it is this camera's
// target's, else the target's own.
void CGCamera::UpdateVehicle() {
    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));
    auto target = ClntObjMgrObjectPtr(this->m_target, TYPE_OBJECT, __FILE__, __LINE__);

    if (!player || !target || !target->IsA(TYPE_UNIT)) {
        this->m_vehicleCamera = nullptr;
        return;
    }

    auto previous = this->m_vehicleCamera;
    auto camera = UnitGetVehicleCamera(player);
    this->m_vehicleCamera = camera;

    if (!camera || camera->m_unitGUID != this->m_target) {
        this->m_vehicleCamera = UnitGetVehicleCamera(static_cast<CGUnit_C*>(target));
    }

    if (this->m_vehicleCamera) {
        this->SetRelativeTo(this->m_vehicleCamera->GetRelativeGUID());
    }

    if (previous && previous != this->m_vehicleCamera) {
        previous->Detach();
    }
}

// ref: FUN_00604c70
void CGCamera::ClearVehicle() {
    this->m_vehicleCamera = nullptr;
    this->UpdateVehicle();
}

// ref: FUN_00600970
void CGCamera::UpdateVehicleRelative(int32_t worldTime) {
    if (!this->m_vehicleCamera) {
        return;
    }

    this->m_vehicleCamera->Update(worldTime);

    if (!this->m_vehicleCamera) {
        return;
    }

    WOWGUID relative = this->m_vehicleCamera->GetRelativeGUID();

    if (relative == 0 || ClntObjMgrObjectPtr(relative, TYPE_OBJECT, __FILE__, __LINE__)) {
        this->m_relativeTo = relative;
    }
}

// ref: FUN_00600530
// Turn the camera to face `yaw` in the world, while the free-look yaw is held.
void CGCamera::FaceYaw(float yaw) {
    if (!(this->m_flags & 0x1)) {
        return;
    }

    auto target = ClntObjMgrObjectPtr(this->m_target, TYPE_OBJECT, __FILE__, __LINE__);
    float facing = target ? target->GetFacing() : 0.0f;

    this->AdjustYaw(yaw - facing);
}

// ------------------------------------------------------------------------------------------------
// Heights
// ------------------------------------------------------------------------------------------------

// ref: FUN_00600a10
// A flying mount's rider sits at the mount's "CAM$" (mounted camera) attachment height, and the
// seat height from "CFM$" is kept for flight.
void CGCamera::CheckFlyingHeight(CGUnit_C* unit) {
    this->m_flags2 &= ~0x80u;
    this->m_flyHeight = 0.0f;

    auto model = unit->GetObjectModel();
    auto mount = unit->m_mountModel;

    if (!unit->CanFly() || !unit->Unit()->mountDisplayID || !mount || !mount->IsLoaded(0, 0) || !model || !model->IsLoaded(0, 0)) {
        return;
    }

    if (mount->HasEvent(0x414D4324)) {
        this->m_flyHeightTime = Now();
        this->m_flyHeight = 0.0f;

        C3Vector event;
        mount->GetEventWorldPosition(event, 0x414D4324);
        C3Vector position = mount->GetPosition();
        float height = event.z - position.z;

        this->m_flags2 |= 0x80;

        if (0.05000000074505806f < std::fabs(height - this->m_heights[1])) {
            this->m_heights[1] = height;
            this->m_heights[0] = height;
            this->m_heights[2] = height;
        }

        return;
    }

    if (!(this->m_flags & 0x800000)) {
        if (mount->HasEvent(0x4D464324)) {
            C3Vector event;
            mount->GetEventPosition(event, 0x4D464324);
            this->m_flyHeight = event.z;
        }

        this->m_flags |= 0x800000;
    }
}

// ref: FUN_00604640
// Blend the camera height to the standing, mounted or swimming eye height (half speed for three
// seconds after a mount height was found), and the flying mount's seat height.
void CGCamera::CalcHeights(CGObject_C* target, int32_t time) {
    float rate = 1.0f;

    if (0 < this->m_flyHeightTime) {
        if (static_cast<uint32_t>(Now() - this->m_flyHeightTime) < 3000) {
            rate = 0.5f;
        } else {
            this->m_flyHeightTime = 0;
        }
    }

    if (target && target->IsA(TYPE_UNIT) && (static_cast<CGUnit_C*>(target)->m_localMove.GetMoveFlags() & 0x200000)) {
        this->SmoothHeight(this->m_heights[2] + s_heightOffset, 0.0f, rate, time);
        return;
    }

    if (this->m_distanceBlend.m_target < 1.8315001726150513f && !s_barberShopActive) {
        this->SmoothHeight(this->m_heights[0] + s_heightOffset, 0.0f, rate, time);
        this->SmoothMountHeight(s_heightOffset, 0.0f, 1.0f, time);
        return;
    }

    this->SmoothHeight(this->m_heights[1] + s_heightOffset, 0.0f, rate, time);

    if (target && target->IsA(TYPE_UNIT) && static_cast<CGUnit_C*>(target)->CanFly()) {
        this->SmoothMountHeight(this->m_flyHeight + s_heightOffset, 0.0f, rate, time);
        return;
    }

    this->SmoothMountHeight(s_heightOffset, 0.0f, rate, time);
}

// ref: FUN_006049c0
// How far the target is under water: sets 0x100000 (swimming) or 0x200000 (deep) by the depth
// against the unit's swim depth.
float CGCamera::CalcWaterDepth(CGObject_C* target) {
    this->m_flags &= 0xFFCFFFFF;

    if (!target) {
        return 0.0f;
    }

    uint32_t liquid;
    float height;
    uint32_t type;

    if (!CWorld::GetObjectFloor(target->m_worldObject, &liquid, &height, &type)) {
        return 0.0f;
    }

    C3Vector position;
    this->GetTargetPosition(position, target);

    float depth = height - position.z;
    float swimDepth;

    if (!target->IsA(TYPE_UNIT)) {
        swimDepth = ObjectGetScale(target) * ObjectGetModelHeight(target);
    } else {
        swimDepth = UnitGetCameraSwimDepth(static_cast<CGUnit_C*>(target));
    }

    if (depth <= swimDepth - 0.2222222238779068f) {
        this->m_flags |= 0x100000;
        return depth;
    }

    this->m_flags |= 0x200000;
    return depth;
}

// ------------------------------------------------------------------------------------------------
// Terrain tilt
// ------------------------------------------------------------------------------------------------

// The ground slope probe table: a slope up to `slope` tilts the view by `angle` (0x00a1e9c0).
static const float s_tiltTable[10][2] = {
    { 0.0f, 0.0f },
    { 0.09000000357627869f, 0.0872664600610733f },
    { 0.18000000715255737f, 0.1745329201221466f },
    { 0.27000001072883606f, 0.2617993950843811f },
    { 0.36000001430511475f, 0.3490658402442932f },
    { 0.4699999988079071f, 0.4363323152065277f },
    { 0.5799999833106995f, 0.5235987901687622f },
    { 0.699999988079071f, 0.6108652353286743f },
    { 0.8399999737739563f, 0.6981316804885864f },
    { 1.0f, 0.7853981852531433f },
};

// ref: FUN_00603a10
// The slope of the ground ahead of the target, from two segment probes at most every 100ms,
// turned into a tilt factor between -20 and 20 degrees.
void CGCamera::CalcTerrainTiltFactor(CGObject_C* target, int32_t time) {
    if (!target) {
        return;
    }

    if (!target->IsA(TYPE_UNIT) || !Int(s_terrainTilt)) {
        this->m_terrainTilt = 0.0f;
        return;
    }

    C3Vector position;
    this->GetTargetPosition(position, target);
    float facing = target->GetFacing();

    if (time - this->m_tiltTime - 100 < 0) {
        return;
    }

    this->m_tiltTime = time;

    C3Vector start = { position.x, position.y, position.z + 1.6666666269302368f };
    C3Vector direction = { std::cos(facing), std::sin(facing), 0.0f };
    C3Vector end = {
        direction.x * 0.0f + start.x,
        start.y + direction.y * 0.0f,
        0.0f * 0.0f + start.z
    };

    float t = 1.0f;
    C3Vector hit = end;
    WorldQuerySegment(start, end, &hit, &t, 0x151, nullptr);

    C3Vector ahead = {
        hit.x - direction.x * 0.2777777910232544f,
        hit.y - direction.y * 0.2777777910232544f,
        hit.z - direction.z * 0.2777777910232544f
    };
    C3Vector down = { ahead.x, ahead.y, ahead.z - 7.111111164093018f };
    C3Vector ground = down;
    t = 1.0f;
    WorldQuerySegment(ahead, down, &ground, &t, 0x151, nullptr);

    C2Vector run = { ground.x - start.x, ground.y - start.y };
    float slope = ((ground.z - ahead.z) + 1.6666666269302368f) / sqrtf(run.x * run.x + run.y * run.y);

    float sign = -1.0f;

    if (slope < 0.0f) {
        slope = -slope;
        sign = 1.0f;
    }

    float tilt = 0.0f;

    for (int32_t i = 9; i > 0; i--) {
        if (s_tiltTable[i][0] < slope || s_tiltTable[i][0] == slope) {
            float angle = s_tiltTable[i - 1][1];

            if (angle <= 0.0f) {
                this->m_terrainTilt = 0.0f;
                return;
            }

            tilt = sign * angle;

            if (-0.3490658402442932f <= tilt && tilt < 0.3490658402442932f) {
                this->m_terrainTilt = tilt;
                return;
            }

            tilt = tilt < -0.3490658402442932f ? -0.3490658402442932f : 0.3490658402442932f;
            break;
        }
    }

    this->m_terrainTilt = tilt;
}

// ref: FUN_00601b70
// The tilt pitch for what the target is doing (fall, fear, idle, jump, move, strafe, swim, taxi,
// track, turn) under the smooth style: absorb times the tilt factor, smoothed by the state's delay
// and factor, or set at once.
void CGCamera::UpdateTerrainTilt(CGObject_C* target, int32_t time, int32_t immediate) {
    if (!target || !target->IsA(TYPE_UNIT)) {
        return;
    }

    auto unit = static_cast<CGUnit_C*>(target);
    int32_t style = Int(s_smoothStyle);
    int32_t state;

    if (unit->Unit()->flags & 0x100000) {
        state = 7;
    } else {
        uint32_t moveFlags = unit->m_localMove.GetMoveFlags();

        if (moveFlags & 0x2200000) {
            state = 6;
        } else if (moveFlags & 0x1000) {
            state = 0;
        } else if (CGCamera::IsFalling(unit)) {
            state = 3;
        } else if (this->m_flags & 0x100) {
            state = 8;
        } else if (this->m_flags & 0x1000) {
            state = 1;
        } else if (moveFlags & 0x3) {
            state = 4;
        } else if (moveFlags & 0xC) {
            state = 5;
        } else {
            state = (moveFlags & 0x30) ? 9 : 2;
        }
    }

    float delay = Float(s_terrainTiltState[style][state][1]);
    float factor = Float(s_terrainTiltState[style][state][2]);
    float tilt = Float(s_terrainTiltState[style][state][0]) * this->m_terrainTilt;

    if (0.0f <= factor) {
        if (!immediate && !AreEqual(factor, 0.0f)) {
            if (!this->SmoothTilt(tilt, delay, factor, time)) {
                return;
            }

            this->m_tiltBlend.m_duration = ClampRange(this->m_tiltBlend.m_duration, factor * Float(s_terrainTiltTimeMin), Float(s_terrainTiltTimeMax) * factor);
            return;
        }

        this->m_tiltPitch = tilt;
    } else {
        tilt = this->m_tiltPitch;
    }

    this->m_flags &= ~0x10000000u;
    this->m_tiltBlend.m_target = tilt;
    this->m_tiltBlend.m_start = 0;
    this->m_tiltBlend.m_duration = 0.0f;
}

// ref: FUN_00603d30
// Every running blend advanced along its cosine ease, the bob settled, then the tilt refreshed.
void CGCamera::UpdateBlends(CGObject_C* target, int32_t time) {
    if (this->IsBobbing()) {
        float t = (Elapsed(time - this->m_bobStart) * 0.0010000000474974513f) / this->m_bobDuration;

        if (!(1.0f < t || t == 1.0f) || (t < 1.0f)) {
            this->m_bob.x = CosineInterp(this->m_bobFrom.x, 0.0f, t);
            this->m_bob.y = CosineInterp(this->m_bobFrom.y, 0.0f, t);
            this->m_bob.z = CosineInterp(this->m_bobFrom.z, 0.0f, t);
        } else {
            this->m_bob = { 0.0f, 0.0f, 0.0f };
        }
    }

    auto advance = [time](CameraBlend& blend, float& value, uint32_t& flags, uint32_t flag, bool settle) {
        if (std::fabs(blend.m_target - value) < 2.384185791015625e-07f) {
            if (settle) {
                flags &= ~flag;
                blend.m_target = value;
                blend.m_start = 0;
                blend.m_duration = 0.0f;
            }

            return;
        }

        if (!(flags & flag)) {
            return;
        }

        int32_t elapsed = time - blend.m_start;

        if (elapsed < 0) {
            return;
        }

        float t = (Elapsed(elapsed) * 0.0010000000474974513f) / blend.m_duration;

        if (t < 1.0f) {
            value = CosineInterp(blend.m_from, blend.m_target, t);
        } else {
            value = blend.m_target;
        }
    };

    advance(this->m_fovBlend, this->m_fovOffset, this->m_flags, 0x40000000, true);
    advance(this->m_distanceBlend, this->m_distance, this->m_flags, 0x4000000, true);
    advance(this->m_heightBlend, this->m_height, this->m_flags, 0x20000000, true);
    advance(this->m_tiltBlend, this->m_tiltPitch, this->m_flags, 0x10000000, true);
    // The tracking pitch settles only while the free-look pitch is not being dragged (0x80000000).
    advance(this->m_pitchOffsetBlend, this->m_pitchOffset, this->m_flags, 0x8000000, !(this->m_flags & 0x80000000));
    advance(this->m_pitchBlend, this->m_pitch, this->m_flags, 0x2000000, true);

    uint32_t flags = this->m_flags;

    if ((flags & 0x8) && (flags & 0x1000000)) {
        this->m_yawBlend.m_start = time;
    }

    if (!(flags & 0x1) && !(flags & 0x8)) {
        advance(this->m_yawBlend, this->m_yaw, this->m_flags, 0x1000000, true);
    }

    this->ClampAngles();

    advance(this->m_mountHeightBlend, this->m_mountHeight, this->m_flags, 0x400000, true);

    this->CalcTerrainTiltFactor(target, time);
    this->UpdateTerrainTilt(target, time, 0);
}

// ------------------------------------------------------------------------------------------------
// Placement and collision
// ------------------------------------------------------------------------------------------------

// ref: FUN_00601d60
// The camera `distance` back along the view from `target`, turned by blend A, offset sideways by
// the mount height scaled to the distance, plus `offset`.
C3Vector& CGCamera::CalcPosition(C3Vector& out, const C3Vector& target, float distance, const C3Vector& offset) const {
    out = target;

    C3Vector forward = this->Forward();
    this->ApplyBlendA(forward);

    out.x -= forward.x * distance;
    out.y -= forward.y * distance;
    out.z -= forward.z * distance;

    if (0.0f < distance && 2.384185791015625e-07f <= std::fabs(this->m_mountHeight)) {
        float reference = this->m_distanceBlend.m_target <= this->m_distance ? this->m_distance : this->m_distanceBlend.m_target;

        if (0.0f < reference) {
            float scale = (this->m_collisionScale * this->m_mountHeight * distance) / reference;
            C3Vector up = this->Up();

            out.x += up.x * scale;
            out.y += up.y * scale;
            out.z += up.z * scale;
        }
    }

    out.x += offset.x;
    out.y += offset.y;
    out.z += offset.z;

    return out;
}

// ref: FUN_006057b0
// The world triangles inside a view frustum, each taken into the camera's space; the furthest
// depth any of them reaches is the collision depth.
static int32_t CameraFrustumDepth(CWFrustum* frustum, const C3Vector& origin, float* depth) {
    static TSGrowableArray<CFacet> s_facets;

    if (!frustum) {
        return 0;
    }

    s_facets.SetCount(0);
    WorldQueryFrustumFacets(frustum, s_facets);

    if (!s_facets.Count()) {
        return 0;
    }

    int32_t hit = 0;

    for (uint32_t i = 0; i < s_facets.Count(); i++) {
        CFacet& facet = s_facets[i];

        for (int32_t v = 0; v < 3; v++) {
            facet.m_points[v].x -= origin.x;
            facet.m_points[v].y -= origin.y;
            facet.m_points[v].z -= origin.z;
        }

        uint32_t* clipped = nullptr;
        uint32_t clippedCount = 0;

        if (FrustumClipFacet(frustum, facet.m_points, 3, &clipped, &clippedCount)) {
            hit = 1;

            for (uint32_t c = 0; c < clippedCount; c++) {
                auto point = reinterpret_cast<const C3Vector*>(clipped[c]);

                if (*depth < point->z) {
                    *depth = point->z;
                }
            }
        }
    }

    return hit;
}

// ref: FUN_006059e0
// Whether the near-plane frustum swept from `from` toward `to` touches world geometry, and if so
// how much of the distance survives.
int32_t CGCamera::CollideFrustum(float* distance, const C3Vector& from, const C3Vector& to, uint32_t flags) {
    float reach = *distance - this->m_nearZ;

    if (reach < 0.0010000000474974513f) {
        *distance = 0.0f;
        return 1;
    }

    C3Vector direction = { to.x - from.x, to.y - from.y, to.z - from.z };
    float lengthSquared = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;

    if (lengthSquared < 0.0010000000474974513f) {
        return 0;
    }

    float scale = 1.0f / sqrtf(lengthSquared);
    direction = { direction.x * scale, direction.y * scale, direction.z * scale };

    C3Vector up = { direction.y * 0.0f - direction.z * 0.0f, direction.z - direction.x * 0.0f, direction.x * 0.0f - direction.y };

    if (up.x * up.x + up.z * up.z + up.y * up.y < 0.0010000000474974513f) {
        up = { direction.y * 0.0f - direction.z, direction.z * 0.0f - direction.x * 0.0f, direction.x - direction.y * 0.0f };
    }

    float upScale = 1.0f / sqrtf(up.x * up.x + up.y * up.y + up.z * up.z);
    up = { up.x * upScale, up.y * upScale, up.z * upScale };

    C3Vector target = { direction.x + from.x, from.y + direction.y, direction.z + from.z };

    C44Matrix view;
    MatrixLookAt(view, from, target, up);

    C44Matrix projection;
    float fov = this->FOV();
    GxuXformCreateProjection_Exact(fov, this->m_aspect, this->m_nearZ, this->m_farZ, projection);

    float depth = 0.0f;
    CWFrustum frustum;

    CameraSetFrustumCorners(frustum, view, projection, from, 1.0f);
    int32_t nearHit = CameraFrustumDepth(&frustum, from, &depth);
    CameraSetFrustumCorners(frustum, view, projection, from, 1.75f);
    int32_t wideHit = CameraFrustumDepth(&frustum, from, &depth);

    if (!wideHit && !nearHit) {
        return 0;
    }

    float remaining = *distance - depth * reach;
    *distance = remaining < 0.0f ? 0.0f : remaining;

    return 1;
}

// ref: FUN_00605d60
// The distance and height that keep the camera out of the world: the height up to the ceiling
// (with the water surface when swimming), then the distance back along the view, both by segment
// queries and the frustum sweep. Returns the 0x10000 / 0x20000 collision bits.
uint32_t CGCamera::CollideDistance(const C3Vector& target, float* distance, float* height, const C3Vector& offset, float waterDepth, float* scale) {
    *scale = 1.0f;
    uint32_t collided = 0;

    float wantedDistance = this->m_distanceBlend.m_target <= this->m_distance ? this->m_distance : this->m_distanceBlend.m_target;
    *distance = wantedDistance;

    float wantedHeight = this->m_heightBlend.m_target <= this->m_height ? this->m_height : this->m_heightBlend.m_target;
    *height = wantedHeight;

    uint32_t flags = this->m_flags;

    if (flags & 0x8) {
        *distance = this->m_distance;
        return 0;
    }

    float floor = 0.8333333134651184f;
    float ceiling = this->m_height;
    uint32_t queryFlags = CameraCollisionFlags();

    if (9.5367431640625e-07f < wantedHeight - this->m_nearZ) {
        if (queryFlags & 0x30000) {
            if (flags & 0x100000) {
                floor = waterDepth + 0.2222222238779068f;

                if (ceiling <= floor) {
                    ceiling = floor;
                }
            } else if ((flags & 0x200000) && (ceiling = waterDepth - 0.8333333134651184f) <= 0.8333333134651184f) {
                ceiling = 0.8333333134651184f;
            }
        }

        C3Vector bottom = { target.x, target.y, target.z + floor };

        float reach = (wantedHeight - floor) + this->m_mountHeight;
        *height = reach;

        float clearance = 0.1111111119389534f < reach ? reach : 0.1111111119389534f;
        *height = clearance;

        C3Vector top = { bottom.x, bottom.y, clearance + bottom.z };
        C3Vector hit = { 0.0f, 0.0f, 0.0f };
        float t = 1.0f;

        if (WorldQuerySegment(top, bottom, &hit, &t, queryFlags, nullptr)) {
            *scale = t;
            collided = 0x20000;
            *height = t * *height;
        }

        if (2.384185791015625e-07f <= std::fabs(clearance - *height)) {
            float lowered = *height - 0.1111111119389534f;
            *height = 0.1111111119389534f < lowered ? lowered : 0.1111111119389534f;
        }

        *height = (*height + floor) - this->m_mountHeight;

        auto targetObject = ClntObjMgrObjectPtr(this->m_target, TYPE_OBJECT, __FILE__, __LINE__);

        if (targetObject && targetObject->IsA(TYPE_UNIT)) {
            float swimDepth = UnitGetCameraSwimDepth(static_cast<CGUnit_C*>(targetObject));
            float minimum = swimDepth * 0.75f;

            if (targetObject->GetGUID() == ClntObjMgrGetActivePlayer() && s_barberShopActive) {
                minimum = swimDepth;
            }

            if (*height < minimum) {
                *height = minimum;
            }
        }
    }

    if (*height < floor) {
        *height = floor;
    }

    if (ceiling < *height) {
        *height = ceiling;
    }

    C3Vector eye = { target.x, target.y, *height + target.z };
    float before = *distance;

    if (9.5367431640625e-07f < *distance - this->m_nearZ) {
        C3Vector forward = this->Forward();
        C3Vector back = {
            eye.x - forward.x * *distance,
            eye.y - forward.y * *distance,
            eye.z - forward.z * *distance
        };

        if (2.384185791015625e-07f <= std::fabs(this->m_mountHeight)) {
            C3Vector up = this->Up();
            float lift = this->m_mountHeight * *scale;

            back.x += up.x * lift;
            back.y += up.y * lift;
            back.z += up.z * lift;
        }

        C3Vector hit = { 0.0f, 0.0f, 0.0f };
        float t = 1.0f;

        if (WorldQuerySegment(eye, back, &hit, &t, queryFlags, nullptr)) {
            collided |= 0x10000;
            *distance = *distance * t;
        }

        if (9.5367431640625e-07f < *distance) {
            C3Vector position;
            this->CalcPosition(position, eye, *distance, offset);

            if (this->CollideFrustum(distance, position, eye, queryFlags)) {
                collided |= 0x10000;
            }

            if (NotEqual(before, *distance)) {
                float pulled = *distance - 0.1111111119389534f;
                *distance = pulled <= 0.0f ? 0.0f : pulled;
            }
        }
    }

    if (this->m_distance < *distance) {
        *distance = this->m_distance;
    }

    return collided;
}

// ref: FUN_006061d0
// With the camera behind the target, the water surface between them keeps the camera on the
// target's side of it.
C3Vector& CGCamera::CollideWater(C3Vector& out, const C3Vector& target, float distance, const C3Vector& position) {
    out = position;

    if (!(0.0f < distance)) {
        return out;
    }

    C3Vector top = { position.x, position.y, position.z + 0.2222222238779068f };
    C3Vector bottom = { position.x, position.y, position.z - 0.2222222238779068f };
    C3Vector hit = { 0.0f, 0.0f, 0.0f };
    float t = 1.0f;

    if (WorldQuerySegment(top, bottom, &hit, &t, 0x20000, nullptr)) {
        out = hit;
        out.z = position.z <= out.z ? out.z - 0.2222222238779068f : out.z + 0.2222222238779068f;

        if (this->CollideFrustum(&distance, out, target, 0x100171)) {
            C3Vector forward = this->Forward();
            this->ApplyBlendA(forward);

            out.x = target.x - forward.x * distance;
            out.y = target.y - forward.y * distance;
            out.z = target.z - forward.z * distance;
        }
    }

    s_lastCollisionHeight = this->m_pitch;

    return out;
}

// ------------------------------------------------------------------------------------------------
// Shakes
// ------------------------------------------------------------------------------------------------

// ref: FUN_005fe6c0
// One shake's displacement: a sine at its frequency, decayed when asked, along the view, across it
// or vertically.
void CameraApplyShake(C3Vector* offset, float facing, float amplitude, float time, float frequency, int32_t decay, float coefficient, int32_t axis) {
    float value = std::sin(frequency * time * CMath::TWO_PI) * amplitude;

    if (decay == 1) {
        value = std::exp(-(time * coefficient)) * value;
    }

    if (axis == 0) {
        offset->x = std::cos(facing) * value + offset->x;
        offset->y = value * std::sin(facing) + offset->y;
        return;
    }

    if (axis == 1) {
        offset->x = std::cos(facing + 1.5707963705062866f) * value + offset->x;
        offset->y = value * std::sin(facing + 1.5707963705062866f) + offset->y;
        return;
    }

    if (axis == 2) {
        offset->z = value + offset->z;
    }
}

// ref: FUN_006004b0
// A shake's falloff with distance: none within `nearSquared`, none at all beyond `farSquared`,
// and a power falloff between.
static int32_t CameraShakeFalloff(const C3Vector& position, const C3Vector& origin, float* amplitude, float nearDistance, float nearSquared, float farDistance, float farSquared) {
    float distanceSquared = (position.y - origin.y) * (position.y - origin.y) + (position.z - origin.z) * (position.z - origin.z)
        + (position.x - origin.x) * (position.x - origin.x);

    if (farSquared < distanceSquared) {
        return 0;
    }

    if (nearSquared < distanceSquared) {
        *amplitude = std::pow(nearDistance, farDistance) * *amplitude;
    }

    return 1;
}

// ref: FUN_00606330
void CGCamera::AddShake(const C3Vector& position, int32_t decay, int32_t axis, float amplitude, float frequency, float duration, float phase, float coefficient) {
    auto shake = static_cast<CameraShake*>(SMemAlloc(sizeof(CameraShake), __FILE__, __LINE__, 0x8));

    if (!shake) {
        return;
    }

    new (shake) CameraShake();
    this->m_shakes.LinkToHead(shake);

    shake->m_amplitude = amplitude;
    shake->m_decay = decay;
    shake->m_frequency = frequency;
    shake->m_axis = axis;
    shake->m_duration = duration;
    shake->m_phase = phase;
    shake->m_coefficient = coefficient;
    shake->m_startTime = Now();
    shake->m_position = position;
}

// ref: FUN_00606410
// A CameraShakes.dbc record by id.
void CGCamera::AddShakeByID(int32_t shakeId, const C3Vector& position) {
    auto record = g_cameraShakesDB.GetRecord(shakeId);

    if (!record) {
        return;
    }

    this->AddShake(position, record->m_shakeType, record->m_direction, record->m_amplitude * 0.02777777798473835f,
                   record->m_frequency, record->m_duration, record->m_phase, record->m_coefficient);
}

// ref: FUN_00606480
void CGCamera::ClearShakes() {
    while (auto shake = this->m_shakes.Head()) {
        this->m_shakes.UnlinkNode(shake);
        SMemFree(shake, __FILE__, __LINE__, 0);
    }
}

// ref: FUN_006064f0
CameraShake* CGCamera::RemoveShake(CameraShake* shake) {
    auto next = this->m_shakes.Next(shake);
    this->m_shakes.UnlinkNode(shake);
    SMemFree(shake, __FILE__, __LINE__, 0);

    return next;
}

// ref: FUN_00606970
// The strongest live shake on each axis, by distance falloff from the camera, applied to `offset`.
void CGCamera::ApplyShakes(const C3Vector& position, C3Vector* offset) {
    if (!this->m_shakes.Head()) {
        return;
    }

    int32_t now = Now();
    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_target, TYPE_UNIT, __FILE__, __LINE__));

    if (!unit) {
        return;
    }

    float facing = UnitWorldSmoothFacing(unit);
    float strongest[3] = { 0.0f, 0.0f, 0.0f };
    float times[3] = { 0.0f, 0.0f, 0.0f };
    CameraShake* chosen[3] = { nullptr, nullptr, nullptr };

    for (auto shake = this->m_shakes.Head(); shake;) {
        float time = Elapsed(now - shake->m_startTime) * 0.0010000000474974513f + shake->m_phase;

        if (!(time < shake->m_duration)) {
            shake = this->RemoveShake(shake);
            continue;
        }

        float amplitude = shake->m_amplitude;

        if (CameraShakeFalloff(position, shake->m_position, &amplitude, 9.0f, 81.0f, 80.0f, 6400.0f)
            && strongest[shake->m_axis] < amplitude) {
            strongest[shake->m_axis] = amplitude;
            chosen[shake->m_axis] = shake;
            times[shake->m_axis] = time;
        }

        shake = this->m_shakes.Next(shake);
    }

    for (int32_t axis = 0; axis < 3; axis++) {
        auto shake = chosen[axis];

        if (shake) {
            CameraApplyShake(offset, facing, strongest[axis], times[axis], shake->m_frequency, shake->m_decay, shake->m_coefficient, shake->m_axis);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// The model camera
// ------------------------------------------------------------------------------------------------

// ref: FUN_005ff440
// Place the camera at `position` looking at `target`, rolled by `roll`.
void CGCamera::SetPositionAndLookAt(const C3Vector& position, const C3Vector& target, float roll) {
    C3Vector forward = { target.x - position.x, target.y - position.y, target.z - position.z };
    float lengthSquared = forward.x * forward.x + forward.y * forward.y + forward.z * forward.z;

    if (2.384185791015625e-07f < lengthSquared) {
        float scale = 1.0f / sqrtf(lengthSquared);
        forward = { scale * forward.x, forward.y * scale, forward.z * scale };
    }

    C3Vector up = { 0.0f, std::sin(roll), std::cos(roll) };

    this->m_position = position;
    this->SetFacing(forward, up);
}

// ref: FUN_006018c0
int32_t CGCamera::InitModelCamera() {
    this->m_flags |= 0x4;
    this->m_modelTime = static_cast<uint32_t>(OsGetAsyncTimeMs());

    this->GetScene()->SetTime(0);
    this->m_model->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 0, 1);
    this->m_model->SetAnimating(1);

    C3Vector origin = { 0.0f, 0.0f, 0.0f };
    this->GetScene()->Animate(origin);

    auto camera = this->m_model->GetCameraByIndex(0);

    if (!camera) {
        this->m_model->Release();
        this->m_model = nullptr;
        return 0;
    }

    this->m_modelCamera = static_cast<HCAMERA>(HandleDuplicate(camera));

    C3Vector position = { 0.0f, 0.0f, 0.0f };
    C3Vector target = { 0.0f, 0.0f, 0.0f };
    DataMgrGetCoord(this->m_modelCamera, 7, &position);
    DataMgrGetCoord(this->m_modelCamera, 8, &target);
    float roll = DataMgrGetFloat(this->m_modelCamera, 5);

    this->SetPositionAndLookAt(position, target, roll);

    return 1;
}

// ref: FUN_00601e90
void CGCamera::UpdateModelCamera() {
    if (!(this->m_flags & 0x4)) {
        if (!this->m_model->IsLoaded(0, 0) || !this->InitModelCamera()) {
            return;
        }
    }

    int32_t now = static_cast<int32_t>(OsGetAsyncTimeMs());
    this->m_model->SetAnimating(1);

    int32_t elapsed = now - static_cast<int32_t>(this->m_modelTime);
    this->GetScene()->AdvanceTime(elapsed);

    C3Vector origin = { 0.0f, 0.0f, 0.0f };
    this->GetScene()->Animate(origin);

    this->m_modelTime = static_cast<uint32_t>(now);

    C3Vector position = { 0.0f, 0.0f, 0.0f };
    C3Vector target = { 0.0f, 0.0f, 0.0f };
    DataMgrGetCoord(this->m_modelCamera, 7, &position);
    DataMgrGetCoord(this->m_modelCamera, 8, &target);
    float roll = DataMgrGetFloat(this->m_modelCamera, 5);

    this->SetPositionAndLookAt(position, target, roll);
}

// ref: FUN_00606570
// A cinematic: the model whose camera flies the view, placed at `position` turned by `facing`.
int32_t CGCamera::SetModel(const char* name, const C3Vector& position, float facing, void* doneCallback, WOWGUID doneOwner) {
    this->ReleaseModel();

    this->m_model = this->GetScene()->CreateModel(name, 0);

    if (!this->m_model) {
        return 0;
    }

    this->m_model->SetSequenceDoneCallback(reinterpret_cast<M2SequenceDoneCallback>(doneCallback), doneOwner);

    this->m_modelPlacement = C34Matrix();
    this->m_modelPlacement.d0 = position.x;
    this->m_modelPlacement.d1 = position.y;
    this->m_modelPlacement.d2 = position.z;
    this->m_modelPlacement.RotateAroundZ(facing);

    this->m_model->m_flag8000 = 1;
    this->m_model->matrixB4 = C44Matrix(this->m_modelPlacement.a0, this->m_modelPlacement.a1, this->m_modelPlacement.a2, 0.0f, this->m_modelPlacement.b0, this->m_modelPlacement.b1, this->m_modelPlacement.b2, 0.0f, this->m_modelPlacement.c0, this->m_modelPlacement.c1, this->m_modelPlacement.c2, 0.0f, this->m_modelPlacement.d0, this->m_modelPlacement.d1, this->m_modelPlacement.d2, 1.0f);

    if (!this->m_model->IsLoaded(1, 0)) {
        this->m_flags &= ~0x4u;
        return 1;
    }

    return this->InitModelCamera() ? 1 : 0;
}

// ------------------------------------------------------------------------------------------------
// The target
// ------------------------------------------------------------------------------------------------

// ref: FUN_00604e00
// The target's eye heights (standing, mounted, swimming): from its model's head attachment and
// the stand-state and mount sequences' bounds, or the model's size for anything else; clamped to
// [0.833, 15] and then blended to.
int32_t CGCamera::CalcTargetHeights(CGObject_C* target) {
    auto model = target->GetObjectModel();

    if (model && !model->IsLoaded(0, 0)) {
        return 0;
    }

    float maxHeight = 15.0f;
    this->m_flags |= 0x4;
    this->m_heights[0] = 0.0f;
    this->m_heights[1] = 0.0f;
    this->m_heights[2] = 0.0f;

    float scale = ObjectGetScale(target) * ObjectGetRenderScale(target);

    if (target->IsA(TYPE_UNIT) && model) {
        auto unit = static_cast<CGUnit_C*>(target);
        float mountScale = scale;

        if (unit->m_mountModel) {
            mountScale = scale * UnitGetMountScale(unit);
        }

        uint32_t standAnim;

        switch (UnitGetStandState(unit)) {
            case 1: standAnim = 97; break;
            case 3: standAnim = 100; break;
            case 4: standAnim = 102; break;
            case 5: standAnim = 103; break;
            case 6: standAnim = 104; break;
            case 7: standAnim = 6; break;
            case 8: standAnim = 115; break;
            default: standAnim = 0; break;
        }

        if (Int(s_heightIgnoreStandState)) {
            standAnim = 0;
        }

        int32_t mountAttachment = -1;

        if (unit->m_mountModel) {
            standAnim = 91;
            mountAttachment = 0;
        }

        M2SequenceInfo stand;
        M2SequenceInfo current;
        model->GetSequenceInfo(0, 0, stand);
        model->GetSequenceInfo(standAnim, 0, current);

        CAaBox standBox = stand.extent;
        CAaBox currentBox = current.extent;
        standBox.Scale(scale);
        currentBox.Scale(scale);

        float lowered = standBox.t.z - currentBox.t.z;
        float head;

        if (!model->HasAttachment(0x11)) {
            if (!model->m_shared->m_m2DataLoaded) {
                model->WaitForLoad(nullptr);
            }

            const CAaBox& bounds = model->m_shared->m_data->collisionBounds.extent;
            head = (bounds.t.z - bounds.b.z) * scale * 0.8999999761581421f;
        } else {
            C3Vector attachment;
            model->GetAttachmentPosition(&attachment, 0x11);

            if (!s_barberShopActive || !ObjectIsActivePlayer(target)) {
                head = (attachment.z + 0.0972222238779068f) * scale;
            } else {
                head = ((attachment.z + 0.0972222238779068f) - lowered) * scale;
            }
        }

        this->m_heights[0] += head;
        this->m_heights[1] += head;
        this->m_heights[2] += head;

        if (!model->HasSequence(standAnim)) {
            standAnim = 0;
        }

        if (model->HasSequence(0x2A)) {
            M2SequenceInfo swimIdle;
            M2SequenceInfo swim;
            model->GetSequenceInfo(0, 0, swimIdle);
            model->GetSequenceInfo(0x2A, 0, swim);

            CAaBox idleBox = swimIdle.extent;
            CAaBox swimBox = swim.extent;
            idleBox.Scale(scale);
            swimBox.Scale(scale);

            this->m_heights[2] -= idleBox.t.z - swimBox.t.z;
        }

        this->m_heights[0] -= lowered;

        if (standAnim == 91) {
            this->m_heights[1] -= lowered;
        }

        if (mountAttachment != -1 && unit->m_mountModel->HasAttachment(mountAttachment)) {
            C3Vector seat;
            unit->m_mountModel->GetAttachmentPosition(&seat, mountAttachment);
            float rider = seat.z * mountScale;

            if ((unit->Unit()->flags & 0x100000) && unit->m_mountModel->HasSequence(0x87)) {
                M2SequenceInfo idle;
                M2SequenceInfo flight;
                unit->m_mountModel->GetSequenceInfo(0, 0, idle);
                unit->m_mountModel->GetSequenceInfo(0x87, 0, flight);

                CAaBox idleBox = idle.extent;
                CAaBox flightBox = flight.extent;
                idleBox.Scale(mountScale);
                flightBox.Scale(mountScale);

                rider -= idleBox.t.z - flightBox.t.z;
            }

            this->m_heights[0] += rider;
            this->m_heights[1] += rider;
            this->m_heights[2] += rider;
        }

        maxHeight = UnitGetCameraMaxHeight(unit);

        if (maxHeight <= 0.0f) {
            maxHeight = 15.0f;
        }

        if (s_barberShopActive) {
            maxHeight *= 4.17232506322307e-08f;
        }
    } else {
        float height;

        if (!target->IsA(TYPE_GAMEOBJECT) || !model) {
            height = scale + scale;
        } else {
            CAaSphere sphere;
            model->GetBoundingSphere(sphere);

            if (sphere.r <= 0.009999999776482582f) {
                height = scale + scale;
            } else {
                height = sphere.r * 0.9900000095367432f * scale;
            }
        }

        this->m_heights[0] = height;
        this->m_heights[1] = height;
        this->m_heights[2] = height;
    }

    for (int32_t i = 0; i < 3; i++) {
        float height = this->m_heights[i];

        if (height < 0.8333333134651184f) {
            height = 0.8333333134651184f;
        } else if (maxHeight <= height) {
            height = maxHeight;
        }

        this->m_heights[i] = height;
    }

    this->CalcHeights(target, Now());
    this->CalcWaterDepth(target);

    return 1;
}

// ref: FUN_00603330
// Switch to a view: snap (blend 2), blend over the smooth times (blend 1), or just record it; mode
// 1 keeps the barber shop's distance, mode 2 zeroes the distance for first person.
void CGCamera::SetView(int32_t view, int32_t blend, int32_t mode) {
    if (this->m_flags & 0x8) {
        return;
    }

    if (view == 6) {
        auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

        if (!(player->Player()->flags & 0x80000)) {
            return;
        }
    }

    if (this->m_view == view && !(this->m_flags & 0x40) && blend) {
        blend = 2;
    }

    static const char* const s_viewNames[] = {
        "FIRST_PERSON", "THIRD_PERSON_A", "THIRD_PERSON_B", "THIRD_PERSON_C", "THIRD_PERSON_D",
        "THIRD_PERSON_E", "COMMENTATOR", "BARBER_SHOP"
    };

    SysMsgPrintf(SYSMSG_INFO, "Camera view %s", s_viewNames[view]);

    int32_t time = Now();

    if (this->m_view != view) {
        if (mode == 0) {
            char text[16];
            SStrPrintf(text, sizeof(text), "%d", view);
            s_view->Set(text, true, false, false, true);
        }

        this->m_view = view;
    }

    this->m_flags &= ~0x40u;
    uint32_t flags = this->m_flags;

    if (mode == 1) {
        this->m_savedBarberDistance = this->m_distanceBlend.m_target;
    }

    this->m_distanceBlend.m_target = this->m_distance;
    this->m_distanceBlend.m_duration = 0.0f;
    this->m_distanceBlend.m_start = 0;
    this->m_pitchBlend.m_start = 0;
    this->m_pitchBlend.m_target = this->m_pitch;
    this->m_flags = flags & 0xF9FFFFFF;
    this->m_pitchBlend.m_duration = 0.0f;

    if (mode != 1) {
        this->m_pitchOffsetBlend.m_target = this->m_pitchOffset;
        this->m_pitchOffsetBlend.m_start = 0;
        this->m_flags = flags & 0xF1FFFFFF;
        this->m_pitchOffsetBlend.m_duration = 0.0f;
    }

    if (mode != 2) {
        this->m_flags &= ~0x1000000u;
        this->m_yawBlend.m_target = this->m_yaw;
        this->m_yawBlend.m_start = 0;
        this->m_yawBlend.m_duration = 0.0f;
    }

    float yaw = this->m_views[this->m_view][2];

    if (0 < this->m_yawLock) {
        auto target = ClntObjMgrObjectPtr(this->m_target, TYPE_OBJECT, __FILE__, __LINE__);
        float facing;

        if (!target) {
            facing = 0.0f;
        } else if (!target->IsA(TYPE_UNIT)) {
            facing = target->GetFacing();
        } else {
            UnitUpdateSmoothFacing(static_cast<CGUnit_C*>(target), 0);
            facing = this->m_vehicleCamera ? this->m_vehicleCamera->m_smoothFacing : static_cast<CGUnit_C*>(target)->m_smoothFacing;
        }

        yaw = NormalizeAngle(facing + yaw);
    }

    if (blend == 1) {
        int32_t distanceBlend = this->SmoothDistance(this->m_views[view][0], 0.0f, 1.0f, time);
        int32_t pitchBlend = this->SmoothPitch(this->m_views[view][1], 0.0f, 1.0f, time);
        int32_t yawBlend = this->SmoothYaw(yaw, 0.0f, 1.0f, time);
        int32_t trackBlend = this->SmoothPitchOffset(0.0f, 0.0f, 1.0f, time);

        float duration = 0.0f;

        if (distanceBlend && 0.0f <= this->m_distanceBlend.m_duration) {
            duration = this->m_distanceBlend.m_duration;
        }

        if (pitchBlend && duration <= this->m_pitchBlend.m_duration) {
            duration = this->m_pitchBlend.m_duration;
        }

        if (yawBlend && duration <= this->m_yawBlend.m_duration) {
            duration = this->m_yawBlend.m_duration;
        }

        if (trackBlend && duration <= this->m_pitchOffsetBlend.m_duration) {
            duration = this->m_pitchOffsetBlend.m_duration;
        }

        duration = std::min(std::max(duration, Float(s_smoothTimeMin)), Float(s_smoothTimeMax));

        if (distanceBlend) {
            this->m_distanceBlend.m_duration = duration;
        }

        if (pitchBlend) {
            this->m_pitchBlend.m_duration = duration;
        }

        if (yawBlend) {
            this->m_yawBlend.m_duration = duration;
        }

        if (trackBlend) {
            this->m_pitchOffsetBlend.m_duration = duration;
        }

        if ((this->m_flags & 0x10000000) && (distanceBlend || pitchBlend || yawBlend || trackBlend)) {
            this->m_tiltBlend.m_duration = duration;
        }

        return;
    }

    if (blend != 2) {
        return;
    }

    bool changed;

    if (mode == 0) {
        changed = NotEqual(this->m_distance, this->m_views[view][0], 0.0010000000474974513f)
            || NotEqual(this->m_pitch, this->m_views[view][1], 0.0010000000474974513f)
            || NotEqual(this->m_yaw, yaw, 0.0010000000474974513f)
            || NotEqual(this->m_pitchOffset, 0.0f, 0.0010000000474974513f);

        this->m_distance = this->m_views[view][0];
        this->m_distanceBlend.m_target = this->m_views[view][0];
        this->m_pitch = this->m_views[view][1];
        this->m_pitchBlend.m_target = this->m_views[view][1];
        this->m_yaw = yaw;
        this->m_yawBlend.m_target = yaw;
        this->m_pitchOffset = 0.0f;
        this->m_pitchOffsetBlend.m_target = 0.0f;
    } else if (mode == 1) {
        this->m_savedDistance = this->m_distance;
        this->m_savedView = this->m_view;
        changed = true;
    } else if (mode == 2) {
        this->m_distanceBlend.m_target = 0.0f;
        this->m_distance = 0.0f;
        changed = true;
    } else {
        changed = false;
    }

    if ((this->m_flags & 0x10) && (this->m_flags & 0x10000000) && changed) {
        auto target = ClntObjMgrObjectPtr(this->m_target, TYPE_OBJECT, __FILE__, __LINE__);
        this->UpdateTerrainTilt(target, time, 1);
    }
}

// ref: FUN_006038a0
void CGCamera::SetViewCommentator(const C3Vector& position, float pitch, float yaw) {
    this->m_views[6][0] = 0.0f;
    this->m_views[6][2] = pitch;
    this->m_views[6][1] = yaw;
    this->m_position = position;

    this->SetView(6, 2, 0);
}

// ref: FUN_00603900
void CGCamera::SetViewBarberShop(float distance, float pitch, float yaw) {
    this->m_views[7][0] = distance;
    this->m_views[7][2] = pitch;
    this->m_views[7][1] = yaw;

    this->SetView(7, 1, 0);
}

// ref: FUN_006053d0
// Look out through a vehicle seat's eyes (`angle`, degrees, of pitch) and back: the seat view takes
// first person and a field-of-view swing; leaving restores the view and distance it held before.
void CGCamera::SetFirstPersonLook(float angle) {
    if (angle == 0.0f) {
        if (!(this->m_flags & 0x8)) {
            return;
        }

        float pitch = this->m_pitchOffsetBlend.m_target;
        int32_t time = Now();

        this->StartFovBlend(0.0f, 0.5f, time);
        this->m_flags = (this->m_flags & 0xFFFF7FF7) | 0x80000000;
        this->SetView(this->m_savedView, 2, 2);
        this->StartDistanceBlend(this->m_savedDistance, 0.5f, time);

        if (2.384185791015625e-07f <= std::fabs(this->m_firstPersonPitch)) {
            this->StartPitchOffsetBlend(pitch - this->m_firstPersonPitch, 0.5f, time);
            this->m_firstPersonPitch = 0.0f;
        }

        return;
    }

    int32_t time = Now();
    this->SetView(0, 2, 1);
    this->m_flags = (this->m_flags & 0x7FFF7FF7) | 0x8008;

    this->StartFovBlend(static_cast<float>(static_cast<int32_t>(angle)) * CMath::DEG2RAD - this->m_fov, 0.5f, time);
    this->StartDistanceBlend(0.0f, 0.5f, time);

    if (!(2.384185791015625e-07f <= std::fabs(this->m_pitchOffset))) {
        return;
    }

    auto target = ClntObjMgrObjectPtr(this->m_target, TYPE_OBJECT, __FILE__, __LINE__);

    if (!target) {
        return;
    }

    C3Vector saved[3] = {};
    memcpy(saved, &this->m_facing, sizeof(saved));

    C3Vector forward = this->Forward();
    C3Vector ahead = { forward.x * 100.0f + this->m_position.x, this->m_position.y + forward.y * 100.0f, this->m_position.z + forward.z * 100.0f };

    float yaw;
    float pitch;
    float roll;
    this->GetAngles(target, &yaw, &pitch, &roll);

    forward = this->Forward();
    C3Vector at = { forward.x * this->m_distance + this->m_position.x, this->m_position.y + forward.y * this->m_distance, this->m_position.z + forward.z * this->m_distance };

    memcpy(&this->m_facing, saved, sizeof(saved));

    C3Vector look = { ahead.x - at.x, ahead.y - at.y, ahead.z - at.z };
    look.Normalize();

    float offset = this->m_pitchOffsetBlend.m_target;
    this->m_pitchOffset = offset;

    float base = offset + this->m_pitch;

    if (this->m_pitchLock < 1) {
        base += this->m_tiltPitch;
    }

    float elevation = std::atan2(look.z, sqrtf(1.0f - look.z * look.z));
    float delta = CameraWrapAngleNear(0.0f, -elevation - base, -CMath::PI, CMath::PI);

    this->StartPitchOffsetBlend(delta + offset, 0.5f, time);
    this->m_firstPersonPitch = delta;
}

// ref: FUN_006066e0
// A new target: its heights, its frame, the free look it asks for, and the tilt it settles into.
void CGCamera::SetTargetObject(CGObject_C* target, int32_t freeLook) {
    if (this->m_flags & 0x10) {
        WorldFrameSetPlayerAlpha(0xFF);
    }

    WOWGUID previous = this->m_target;
    bool changed = target && target->GetGUID() != previous;

    bool moved = false;

    if (target) {
        C3Vector position;
        this->GetTargetPosition(position, target);

        moved = !(this->m_lastTargetPosition.x == position.x && this->m_lastTargetPosition.y == position.y
            && this->m_lastTargetPosition.z == position.z);
    }

    int32_t immediate = (this->m_flags & 0x10) && !changed ? 0 : 1;

    if (!(this->m_flags & 0x10) || changed || moved) {
        int32_t time = Now();
        this->CalcTerrainTiltFactor(target, time);
        this->UpdateTerrainTilt(target, time, immediate);
    }

    float fov = 0.0f;

    if (target && target->IsA(TYPE_PLAYER)) {
        fov = PlayerGetSwimDepth(static_cast<CGPlayer_C*>(target));
    }

    this->SetFovOffset(fov, immediate);
    this->m_unk1D0 = 0;

    if (!target) {
        this->m_target = 0;
        this->m_relativeTo = 0;
        this->ClearShakes();
        this->m_flags2 &= ~0x2u;
    } else {
        this->m_target = target->GetGUID();

        if (!target->IsA(TYPE_UNIT)) {
            this->m_targetFacing = target->GetFacing();
            this->m_relativeTo = 0;
        } else {
            this->m_targetFacing = target->GetRawFacing();

            WOWGUID transport = UnitGetCameraTransport(static_cast<CGUnit_C*>(target));

            if (ClntObjMgrObjectPtr(transport, TYPE_OBJECT, __FILE__, __LINE__)) {
                this->SetRelativeTo(transport);
            } else {
                this->SetRelativeTo(0);
            }
        }

        if (!freeLook) {
            if (this->m_flags2 & 0x2) {
                this->m_flags2 &= ~0x2u;
                this->EndFreeLook();
            }
        } else {
            this->m_flags2 |= 0x2;
            this->BeginFreeLook();
        }

        this->m_lookTarget = 0;

        if (!this->CalcTargetHeights(target)) {
            this->m_flags &= ~0x4u;
        }
    }

    this->m_flags &= ~0x800000u;

    if (previous == 0) {
        this->UpdateVehicle();
    }
}

// ref: FUN_00605700
// With nothing to follow, look at the look-at object from where the camera is.
void CGCamera::UpdateLookAtCamera(CGObject_C* target) {
    if (!target) {
        SErrSetLastError(0x57);
        return;
    }

    if (!(this->m_flags & 0x4) && !this->CalcTargetHeights(target)) {
        return;
    }

    C3Vector position;
    this->GetTargetPosition(position, target);

    C3Vector forward = {
        position.x - this->m_position.x,
        position.y - this->m_position.y,
        (this->m_heights[1] + position.z) - this->m_position.z
    };
    forward.Normalize();

    this->SetFacing(forward);
}

// ref: FUN_00606f90
// One frame of the follow camera.
void CGCamera::UpdateTargetCamera(CGObject_C* target, int32_t time) {
    if (!target) {
        SErrSetLastError(0x57);
        return;
    }

    if (!(this->m_flags & 0x4) && !this->CalcTargetHeights(target)) {
        return;
    }

    this->UpdateVehicleRelative(static_cast<int32_t>(CWorld::GetCurTimeMs()));

    float waterDepth = 0.0f;
    uint32_t onTaxi = 0;
    CGUnit_C* unit = nullptr;
    int32_t clickMoving = 0;
    int32_t forcedMotion = 0;
    uint32_t swimming = 0;

    if (target->IsA(TYPE_UNIT)) {
        unit = static_cast<CGUnit_C*>(target);
        onTaxi = (unit->Unit()->flags >> 20) & 1;
        clickMoving = UnitIsClickMoving(unit) & 0xFF;

        if (unit->m_localMove.IsSplineFlag2000() || (unit->m_localMove.GetMoveFlags() & 0x2000000)) {
            forcedMotion = 1;
        }

        auto spline = unit->m_localMove.GetSpline();
        swimming = unit->m_localMove.GetMoveFlags() & 0x200000;

        int32_t splineTaxi = spline && !(spline->flags & 0x400);
        waterDepth = UnitGetWaterDepth(unit);
        this->CheckFlyingHeight(unit);

        this->SetTaxiLook(splineTaxi && !onTaxi);
    } else {
        this->SetTaxiLook(0);
    }

    C3Vector position;
    this->GetTargetPosition(position, target);

    // Under the world: hold the camera at the last good spot, and play the warning once.
    float floor = -500.0f;
    WorldGetFloorHeight(position, &floor);

    if (floor <= position.z) {
        s_belowWorld = true;
    } else {
        position.x = this->m_lastTargetPosition.x;
        position.y = this->m_lastTargetPosition.y;
        position.z = floor;

        if (s_belowWorld) {
            SI2::PlaySoundKit("SpaceDeathUniversal", 0, nullptr, nullptr);
        }

        s_belowWorld = false;
    }

    if (onTaxi && this->m_unk2A4 == 0) {
        this->SmoothYaw(0.0f, 0.0f, 1.0f, Now());

        if (unit && unit->IsA(TYPE_PLAYER) && (static_cast<CGPlayer_C*>(unit)->Player()->flags & 0x20000)) {
            this->SmoothPitch(0.0f, 0.0f, 1.0f, Now());
        }
    }

    if (this->m_timedBits) {
        this->AdvanceTimedValues(time);
    }

    this->UpdateBlends(target, time);

    float yaw;
    float pitch;
    float roll;
    this->GetAngles(target, &yaw, &pitch, &roll);

    C3Vector shake = { 0.0f, 0.0f, 0.0f };

    if (!forcedMotion && !swimming) {
        C3Vector eye = { position.x, position.y, this->m_height + position.z };
        C3Vector forward = this->Forward();
        C3Vector camera = {
            eye.x - forward.x * this->m_distance,
            eye.y - forward.y * this->m_distance,
            eye.z - forward.z * this->m_distance
        };

        this->ApplyShakes(camera, &shake);
    }

    this->CalcBobbing(&this->m_bob);
    shake.x += this->m_bob.x;
    shake.y += this->m_bob.y;
    shake.z += this->m_bob.z;

    uint32_t deepWater = this->m_flags & 0x200000;
    float depth = this->CalcWaterDepth(target);
    auto input = InputControlGetActive();
    float viewPitch = pitch + this->m_pitchOffset;

    // Diving and surfacing tip the player's pitch toward the surface when cameraDive is on.
    if (swimming && (!unit || !unit->IsA(TYPE_PLAYER) || PlayerGetSwimDepth(static_cast<CGPlayer_C*>(unit)) == 0.0f)) {
        uint32_t flags = this->m_flags;
        uint32_t surfacing = flags & 0x100000;

        if ((surfacing || (flags & 0x200000)) && Int(s_dive) && NotEqual(waterDepth, 0.0f) && !clickMoving) {
            float surface = Float(s_surfacePitch) * CMath::DEG2RAD;
            float submerge = CMath::DEG2RAD * Float(s_submergePitch);

            if (((flags & 0x200000) && surface < 0.0f
                    && (!(InputControlGetFlags(input) & 0x2000001) || (viewPitch < 0.0f && surface < viewPitch)))
                || (surfacing && 0.0f < submerge
                    && (!(InputControlGetFlags(input) & 0x2000001) || (0.0f < viewPitch && viewPitch < submerge)))) {
                InputControlSetPitch(input, time, 0.0f);
            }
        }
    }

    // Crossing the surface eases the camera pitch to the surface or submerge final pitch.
    if (Int(s_waterCollision)) {
        float finalPitch = 0.0f;
        bool crossing = false;

        if (deepWater && (this->m_flags & 0x100000)) {
            finalPitch = Float(s_surfaceFinalPitch);
            crossing = true;
        } else if ((swimming || (this->m_flags & 0x100000)) && (this->m_flags & 0x200000) && !deepWater) {
            finalPitch = Float(s_submergeFinalPitch);
            crossing = true;
        }

        if (crossing) {
            finalPitch *= CMath::DEG2RAD;

            if (finalPitch != 0.0f) {
                if (!(this->m_flags & 0x1)) {
                    this->SmoothPitch(finalPitch, 0.0f, 1.0f, time);
                } else {
                    this->m_pitch = finalPitch;
                    this->m_pitchBlend.m_target = finalPitch;
                }
            }
        }
    }

    if (this->m_unk2D4 != this->m_unk2D8) {
        this->UpdateBlendA(time);
    }

    this->m_flags &= 0xFFFCFFFF;

    float distance;
    float height;
    this->m_flags |= this->CollideDistance(position, &distance, &height, shake, depth, &this->m_collisionScale);

    C3Vector eye = { position.x + shake.x, position.y + shake.y, shake.z + position.z + height };

    if (!(this->m_flags & 0x8) && static_cast<int32_t>(this->m_flags) >= 0) {
        if (this->CanTrackPitch()) {
            this->SmoothPitchOffset(0.0f, 0.0f, 1.0f, time);
        } else {
            this->StopPitchOffsetBlend();
        }
    }

    uint32_t flags = this->m_flags;

    if ((flags & 0x8) && !(flags & 0x40000000)) {
        distance = 0.0f;
        this->m_flags = flags & ~0x8000u;
    } else if (static_cast<int32_t>(flags) < 0 && !(flags & 0x40000000)) {
        this->m_flags = flags & 0x7FFFFFFF;
    }

    C3Vector none = { 0.0f, 0.0f, 0.0f };
    C3Vector placed;
    this->CalcPosition(placed, eye, distance, none);
    this->m_position = placed;

    C3Vector clear;
    this->CollideWater(clear, eye, distance, this->m_position);
    this->m_position = clear;

    if (this->m_relativeTo == 0 && !Int(s_waterCollision) && this->m_yawOffset == 0.0f && (!unit || !unit->CanFly())) {
        C3Vector look = { eye.x - this->m_position.x, eye.y - this->m_position.y, eye.z - this->m_position.z };

        if (0.10000000149011612f < look.z * look.z + look.y * look.y + look.x * look.x) {
            look.NormalizeUnchecked();
            this->SetFacing(look);
        }
    }

    if (this->m_unk2E8 != this->m_unk2EC) {
        this->UpdateBlendB(time);
    }

    float blendB;
    memcpy(&blendB, &this->m_unk2F8, sizeof(blendB));

    if (blendB != 0.0f) {
        CameraRollFacing(this->m_facing, -blendB);
    }

    if (0.0010000000474974513f <= std::fabs(this->m_pitchOffset)) {
        CameraRollFacing(this->m_facing, this->m_pitchOffset);
    }

    // A collision that pulled the camera in jumps the distance and height blends to it.
    if (0.1111111119389534f < this->m_distance - distance) {
        this->m_flags |= 0x4000000;
        float target = distance + 0.1111120656132698f;
        this->m_distanceBlend.m_start = time;
        this->m_distance = target;
        this->m_distanceBlend.m_from = target;
        this->m_distanceBlend.m_duration = 2.0f;
    }

    if (0.1111111119389534f < this->m_height - height) {
        this->m_flags |= 0x20000000;
        float target = 0.1111120656132698f + height;
        this->m_heightBlend.m_start = time;
        this->m_height = target;
        this->m_heightBlend.m_from = target;
        this->m_heightBlend.m_duration = 2.0f;
    }

    // The player model fades out as the camera closes in on it.
    uint8_t alpha = 0xFF;
    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));
    int32_t onVehicle = player && unit && PlayerIsOnVehicleWith(player, unit);
    CGUnit_C* fading = unit;
    float fadeDistance = 0.0f;

    for (;;) {
        float fadeFar = 1.8315001726150513f;
        float fadeNear = 0.0027777778450399637f;

        if (fading && UnitHasVehicleCamera(fading) && !onVehicle) {
            if (!UnitVehicleCameraActive(fading)) {
                fading = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(fading->GetTransportGUID(), TYPE_UNIT, __FILE__, __LINE__));
            }

            if (fading) {
                auto model = fading->GetObjectModel();

                // The seat flag 0x80000 and its fade scalars, times the model's bounding radius.
                if (model && model->IsLoaded(0, 0) && UnitGetVehicleFade(fading, &fadeNear, &fadeFar)) {
                    M2SequenceInfo info;
                    model->GetSequenceInfo(0, 0, info);

                    fadeNear *= info.radius;
                    fadeFar *= info.radius;

                    C3Vector modelPosition = model->GetPosition();
                    fadeDistance = CameraDistance(this->m_position, modelPosition);
                }
            }
        }

        if (s_barberShopActive) {
            fadeFar *= 0.3333333432674408f;
        }

        if (fadeDistance < this->m_height && this->m_pitch < -1.186823844909668f) {
            float lean = (-1.5707963705062866f - this->m_pitch) * -2.6043529510498047f;

            if (2.384185791015625e-07f <= std::fabs(lean)) {
                fadeFar = fadeFar / (lean * lean);
            }
        }

        float span = fadeDistance - this->m_nearZ;

        if (span < fadeFar) {
            if (span <= fadeNear) {
                alpha = 0;
                break;
            }

            float t = (span - fadeNear) / (fadeFar - fadeNear);
            uint8_t value = static_cast<uint8_t>(lrintf(CosineInterp(0.0f, 255.0f, t)));

            if (value < alpha) {
                alpha = value;
            }
        }

        if (!fading || !UnitHasTransport(fading) || onVehicle) {
            break;
        }

        fading = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(fading->GetTransportGUID(), TYPE_UNIT, __FILE__, __LINE__));

        if (!fading) {
            break;
        }
    }

    WorldFrameSetPlayerAlpha(alpha);

    // The field of view swing of a first-person seat fades the player out and back in.
    flags = this->m_flags;

    if ((flags & 0x40000000) && ((flags & 0x8000) || static_cast<int32_t>(flags) < 0)) {
        int32_t elapsed = time - this->m_fovBlend.m_start;

        if (elapsed >= 0) {
            float t = ((Elapsed(elapsed) * 0.0010000000474974513f) / this->m_fovBlend.m_duration) * 1.5f;

            if (1.0f < t) {
                t = 1.0f;
            }

            t = t * t;

            if (flags & 0x8000) {
                t = 1.0f - t;
            }

            uint8_t value = static_cast<uint8_t>(lrintf(t * 255.0f));

            if (value <= alpha) {
                alpha = value;
            }

            WorldFrameSetPlayerAlpha(alpha);
        }
    }

    this->CalcHeights(target, time);

    this->m_lastTargetPosition = target->GetPosition();
    this->m_unk2A4 = onTaxi;
}

// ref: FUN_005fe7b0
// The liquid the camera is in, from the world, handed to the sound system's underwater filter.
void CGCamera::UpdateLiquid() {
    int32_t liquid = WorldGetCameraLiquidType();
    SoundSetUnderwater(liquid);

    if (!(this->m_flags & 0x2) || this->m_liquidType != liquid) {
        this->m_flags |= 0x2;
        this->m_liquidType = liquid;
    }
}

// ref: FUN_00607b00
// The per-frame camera update: the model camera, else the follow camera, else the look-at camera,
// then the liquid it ends up in.
int32_t CGCamera::UpdateCallback(const void*, void* param) {
    auto camera = static_cast<CGCamera*>(param);

    if (!camera) {
        return 1;
    }

    int32_t time = Now();
    camera->m_nearZ = CWorld::GetNearClip();
    camera->m_farZ = CWorld::GetFarClip();

    if (camera->m_model) {
        camera->UpdateModelCamera();
        camera->UpdateLiquid();
        return 1;
    }

    auto target = ClntObjMgrObjectPtr(camera->m_target, TYPE_OBJECT, __FILE__, __LINE__);

    if (target) {
        camera->UpdateTargetCamera(target, time);
        camera->UpdateLiquid();
        return 1;
    }

    auto lookTarget = ClntObjMgrObjectPtr(camera->m_lookTarget, TYPE_OBJECT, __FILE__, __LINE__);

    if (lookTarget) {
        camera->UpdateLookAtCamera(lookTarget);
        camera->UpdateLiquid();
    }

    return 1;
}

// ------------------------------------------------------------------------------------------------
// Construction
// ------------------------------------------------------------------------------------------------

// ref: FUN_00606b30
CGCamera::CGCamera() : CSimpleCamera(CWorld::GetNearClip(), CWorld::GetFarClip(), 1.5707963705062866f) {
    this->m_modelTime = static_cast<uint32_t>(OsGetAsyncTimeMs());
    this->m_view = Int(s_view);

    this->m_distance = SStrToFloat(s_cameraViewDefaults[this->m_view * 3 + 0]);
    this->m_pitch = SStrToFloat(s_cameraViewDefaults[this->m_view * 3 + 1]) * CMath::DEG2RAD;
    this->m_distanceBlend.m_target = this->m_distance;
    this->m_pitchBlend.m_target = this->m_pitch;
    this->m_collisionScale = 1.0f;
    this->m_savedView = 0;

    this->LoadViewsFromCVars();
    this->SetTargetObject(nullptr, 0);

    ConsoleCommandRegister("pitchLimit", &CameraPitchLimitCommand, GAME, nullptr);

    this->m_flags |= 0x50;
    this->m_savedView = 2;
    this->m_savedDistance = 1.8315001726150513f;
    this->m_savedBarberDistance = 1.8315001726150513f;
    this->m_firstPersonPitch = 0.0f;

    // autoInteract (0x00bd08f4), the click-to-move cvar
    auto autoInteract = CVar::Lookup("autoInteract");

    if (autoInteract && autoInteract->m_intValue) {
        this->m_flags2 |= 0x1;
    } else {
        this->m_flags2 &= ~0x1u;
    }
}

// ref: FUN_00604d40
CGCamera::~CGCamera() {
    this->m_flags &= ~0x10u;
    this->ReleaseModel();

    ConsoleCommandUnregister("pitchLimit");

    this->ClearShakes();
}

// ------------------------------------------------------------------------------------------------
// The world frame's input hooks
// ------------------------------------------------------------------------------------------------

// Mouse-drag look from frozen's world frame: the reference reaches MouseLook through its own input
// handler with the cursor motion in device units.
// The angles are turned back into the device-unit motion MouseLook expects, so its NDC conversion
// and speed scaling land on the same angles the world frame asked for.
void CGCamera::Rotate(float deltaYaw, float deltaPitch) {
    float x = -deltaYaw / (0.0012499999720603228f * Float(s_yawMoveSpeed) * CMath::DEG2RAD);
    float y = deltaPitch / (0.0016666667070239782f * CMath::DEG2RAD * Float(s_pitchMoveSpeed));
    NDCToDDC(x, y, &x, &y);

    this->MouseLook(x, y, nullptr);
}

// The mouse wheel from frozen's world frame, as CameraZoomIn / CameraZoomOut.
void CGCamera::Zoom(float deltaDistance) {
    if (deltaDistance < 0.0f) {
        this->ZoomIn(-deltaDistance, EventTime(), 0.0f);
    } else {
        this->ZoomOut(deltaDistance, EventTime(), 0.0f);
    }
}

const WOWGUID& CGCamera::GetTarget() const {
    return this->m_target;
}

void CGCamera::SetTarget(const WOWGUID& target) {
    auto object = ClntObjMgrObjectPtr(target, TYPE_OBJECT, __FILE__, __LINE__);
    this->SetTargetObject(object, 0);
}

int32_t CGCamera::HasModel() const {
    return this->m_model != nullptr;
}

// ref: FUN_005fe880
void CGCamera::SetupWorldProjection(const CRect& projRect) {
    this->SetGxProjectionAndView(projRect);
}

C3Vector CGCamera::Target() const {
    return this->m_position + this->Forward();
}

float CGCamera::FOV() const {
    return std::min(std::max(this->m_fov + this->m_fovOffset, 0.0f), CMath::PI);
}

void CGCamera::SetTimedValue(int32_t index, int32_t startTime, int32_t duration, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    this->SetTimedValue(index, startTime, duration, bits);
}

// ------------------------------------------------------------------------------------------------
// Lua
// ------------------------------------------------------------------------------------------------

// ref: FUN_005ff000
// MoveView*Start: begin a timed move on `index` at the given speed (default 1).
static int32_t CameraScriptStartMove(lua_State* L, int32_t index) {
    int32_t time = EventTime();
    float speed = 1.0f;

    if (lua_isnumber(L, 1)) {
        speed = static_cast<float>(lua_tonumber(L, 1));
    }

    auto camera = CGWorldFrame::GetActiveCamera();

    if (!(camera->m_flags2 & 0x40)) {
        uint32_t bit = 1u << (index * 2);

        if (!(bit & camera->m_timedBits)) {
            camera->m_timedBits |= bit;
            camera->m_timedStart[index] = time;
        }

        camera->m_timedEnd[index] = 0;
        camera->m_timedValue[index] = speed;
    }

    return 0;
}

// MoveView*Stop (0x005ff0a0 and its siblings): stop the move at the event time.
static int32_t CameraScriptStopMove(int32_t index) {
    int32_t time = EventTime();
    auto camera = CGWorldFrame::GetActiveCamera();
    uint32_t bit = 1u << (index * 2);

    if (camera->m_timedBits & bit) {
        camera->m_timedStop[index] = time;
        camera->m_timedBits |= bit << 1;
    }

    return 0;
}

// ref: FUN_005ff080
static int32_t Script_MoveViewInStart(lua_State* L) { return CameraScriptStartMove(L, 0); }
static int32_t Script_MoveViewInStop(lua_State* L) { return CameraScriptStopMove(0); }
// ref: FUN_005ff0d0
static int32_t Script_MoveViewOutStart(lua_State* L) { return CameraScriptStartMove(L, 1); }
static int32_t Script_MoveViewOutStop(lua_State* L) { return CameraScriptStopMove(1); }
// ref: FUN_005ff120
static int32_t Script_MoveViewRightStart(lua_State* L) { return CameraScriptStartMove(L, 2); }
static int32_t Script_MoveViewRightStop(lua_State* L) { return CameraScriptStopMove(2); }
// ref: FUN_005ff170
static int32_t Script_MoveViewLeftStart(lua_State* L) { return CameraScriptStartMove(L, 3); }
static int32_t Script_MoveViewLeftStop(lua_State* L) { return CameraScriptStopMove(3); }
// ref: FUN_005ff1c0
static int32_t Script_MoveViewUpStart(lua_State* L) { return CameraScriptStartMove(L, 4); }
static int32_t Script_MoveViewUpStop(lua_State* L) { return CameraScriptStopMove(4); }
// ref: FUN_005ff210
static int32_t Script_MoveViewDownStart(lua_State* L) { return CameraScriptStartMove(L, 5); }
static int32_t Script_MoveViewDownStop(lua_State* L) { return CameraScriptStopMove(5); }

// ref: FUN_006017e0
static int32_t Script_CameraZoomIn(lua_State* L) {
    int32_t time = EventTime();
    float distance = lua_isnumber(L, 1) ? static_cast<float>(lua_tonumber(L, 1)) : 1.0f;

    if (s_barberShopActive) {
        distance *= 0.20000000298023224f;
    }

    CGWorldFrame::GetActiveCamera()->ZoomIn(distance, time, 0.0f);

    return 0;
}

// ref: FUN_00601840
static int32_t Script_CameraZoomOut(lua_State* L) {
    int32_t time = EventTime();
    float distance = lua_isnumber(L, 1) ? static_cast<float>(lua_tonumber(L, 1)) : 1.0f;

    if (s_barberShopActive) {
        distance *= 0.20000000298023224f;
    }

    CGWorldFrame::GetActiveCamera()->ZoomOut(distance, time, 0.0f);

    return 0;
}

// ref: FUN_006018a0
static int32_t Script_VehicleCameraZoomIn(lua_State* L) {
    return Script_CameraZoomIn(L);
}

// ref: FUN_006018b0
static int32_t Script_VehicleCameraZoomOut(lua_State* L) {
    return Script_CameraZoomOut(L);
}

// ref: FUN_006039b0
static int32_t Script_SetView(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: SetView(viewModeIndex)");
        return 0;
    }

    int32_t view = static_cast<int32_t>(lua_tonumber(L, 1));

    if (static_cast<uint32_t>(view - 1) < 5) {
        CGWorldFrame::GetActiveCamera()->SetView(view, Int(s_viewBlendStyle), 0);
    }

    return 0;
}

// ref: FUN_005ff260
static int32_t Script_SaveView(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: SaveView(viewModeIndex)");
        return 0;
    }

    int32_t view = static_cast<int32_t>(lua_tonumber(L, 1));

    if (static_cast<uint32_t>(view - 1) < 5 && view != 0) {
        CGWorldFrame::GetActiveCamera()->SaveView(view);
    }

    return 0;
}

// ref: FUN_00604c80
static int32_t Script_ResetView(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: ResetView(viewModeIndex)");
        return 0;
    }

    int32_t view = static_cast<int32_t>(lua_tonumber(L, 1));

    if (static_cast<uint32_t>(view - 1) < 5) {
        CGWorldFrame::GetActiveCamera()->ResetView(view);
    }

    return 0;
}

// NextView / PrevView (0x00604ce0, 0x00604d10): step through views 1..5.
static int32_t Script_NextView(lua_State* L) {
    auto camera = CGWorldFrame::GetActiveCamera();
    int32_t view = camera->m_view + 1;

    if (5 < view) {
        view = 1;
    }

    camera->SetView(view, Int(s_viewBlendStyle), 0);

    return 0;
}

static int32_t Script_PrevView(lua_State* L) {
    auto camera = CGWorldFrame::GetActiveCamera();
    int32_t view = camera->m_view - 1;

    if (view < 1) {
        view = 5;
    }

    camera->SetView(view, Int(s_viewBlendStyle), 0);

    return 0;
}

// ref: FUN_005ff2c0
static int32_t Script_FlipCameraYaw(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: FlipCameraYaw(degrees)");
        return 0;
    }

    float degrees = static_cast<float>(lua_tonumber(L, 1));
    auto camera = CGWorldFrame::GetActiveCamera();
    camera->m_yawOffset = degrees * CMath::DEG2RAD + camera->m_yawOffset;

    return 0;
}

// The camera's script functions (0x00ad2060), in the reference's order
static const struct {
    const char* name;
    int32_t (*function)(lua_State*);
} s_cameraScriptFunctions[] = {
    { "CameraZoomIn", &Script_CameraZoomIn },
    { "CameraZoomOut", &Script_CameraZoomOut },
    { "MoveViewInStart", &Script_MoveViewInStart },
    { "MoveViewInStop", &Script_MoveViewInStop },
    { "MoveViewOutStart", &Script_MoveViewOutStart },
    { "MoveViewOutStop", &Script_MoveViewOutStop },
    { "MoveViewLeftStart", &Script_MoveViewLeftStart },
    { "MoveViewLeftStop", &Script_MoveViewLeftStop },
    { "MoveViewRightStart", &Script_MoveViewRightStart },
    { "MoveViewRightStop", &Script_MoveViewRightStop },
    { "MoveViewUpStart", &Script_MoveViewUpStart },
    { "MoveViewUpStop", &Script_MoveViewUpStop },
    { "MoveViewDownStart", &Script_MoveViewDownStart },
    { "MoveViewDownStop", &Script_MoveViewDownStop },
    { "SetView", &Script_SetView },
    { "SaveView", &Script_SaveView },
    { "ResetView", &Script_ResetView },
    { "NextView", &Script_NextView },
    { "PrevView", &Script_PrevView },
    { "FlipCameraYaw", &Script_FlipCameraYaw },
    { "VehicleCameraZoomIn", &Script_VehicleCameraZoomIn },
    { "VehicleCameraZoomOut", &Script_VehicleCameraZoomOut },
};

// ref: FUN_005fe2c0
void CameraRegisterScriptFunctions() {
    for (auto& entry : s_cameraScriptFunctions) {
        FrameScript_RegisterFunction(entry.name, entry.function);
    }
}

// ref: FUN_005fe2f0
void CameraUnregisterScriptFunctions() {
    for (auto& entry : s_cameraScriptFunctions) {
        FrameScript_UnregisterFunction(entry.name);
    }
}
