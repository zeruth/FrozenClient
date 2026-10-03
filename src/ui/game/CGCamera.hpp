#ifndef UI_GAME_C_G_CAMERA_HPP
#define UI_GAME_C_G_CAMERA_HPP

#include "gx/Camera.hpp"
#include "ui/simple/CSimpleCamera.hpp"
#include "util/GUID.hpp"
#include <storm/List.hpp>
#include <tempest/matrix/C34Matrix.hpp>
#include <cstdint>

class CGObject_C;
class CGUnit_C;
class CM2Model;
class CVehicleCamera_C;
class CWFrustum;
struct lua_State;


#include "ui/game/Types.hpp"

// One camera shake (reference CameraShake, 0x34 bytes, SMemAlloc'd into the camera's shake list).
struct CameraShake {
    TSLink<CameraShake> m_link;     // +0x00
    int32_t m_decay;                // +0x08  1 = exponential decay over the duration
    int32_t m_axis;                 // +0x0c  0 forward, 1 sideways, 2 vertical
    float m_amplitude;              // +0x10
    float m_frequency;              // +0x14
    float m_duration;               // +0x18  seconds
    float m_phase;                  // +0x1c  seconds added to the elapsed time
    float m_coefficient;            // +0x20  decay coefficient
    C3Vector m_position;            // +0x24  where it happened, for the distance falloff
    int32_t m_startTime;            // +0x30
};

// A value the camera moves toward over time: one 0x18-byte block per smoothed value from +0x1e0.
// The Start* functions set it up and UpdateBlends advances it with a cosine ease.
struct CameraBlend {
    int32_t m_start = 0;            // +0x00  when the move starts (ms)
    float m_duration = 0.0f;        // +0x04  how long it takes (s)
    float m_target = 0.0f;          // +0x08  where it is going
    float m_from = 0.0f;            // +0x0c  where it started
    float m_factor = 0.0f;          // +0x10  the factor it was requested with (re-request check)
    float m_delay = 0.0f;           // +0x14  the delay it was requested with (re-request check)
};

class CGCamera : public CSimpleCamera {
    public:
        // Types
        struct CameraViewData {
            const char* m_distance;
            const char* m_pitch;
            const char* m_yaw;
        };

        // Static variables
        static CameraViewData s_cameraViewDataDefault[MAX_CAMERA_VIEWS];

        // Static functions
        static int32_t UpdateCallback(const void*, void* param);
        static float SineEase(float t);
        static float CosineEase(float t);
        static float GetMaxDistance();
        static int32_t IsFalling(CGUnit_C* unit);

        // Virtual member functions
        virtual ~CGCamera();
        virtual float FOV() const;
        virtual C3Vector Forward() const;
        virtual C3Vector Right() const;
        virtual C3Vector Up() const;

        // Member functions
        CGCamera();

        // The frozen world frame's wheel hook; it feeds the reference ZoomIn / ZoomOut.

        const WOWGUID& GetTarget() const;
        void SetTarget(const WOWGUID& target);
        int32_t HasModel() const;
        int32_t IsFirstPerson() const;
        C33Matrix ParentToWorld() const;
        void SetupWorldProjection(const CRect& projRect);
        C3Vector Target() const;

        void SetTimedValue(int32_t index, int32_t startTime, int32_t duration, uint32_t value);
        void SetTimedValue(int32_t index, int32_t startTime, int32_t duration, float value);
        void SetBlendA(uint32_t value, int32_t start, int32_t end);
        void SetBlendB(uint32_t value, int32_t start, int32_t end);
        void SetUnk2FC(uint32_t value);
        void SetOverride2D0(float value);
        void ClearOverride2D0();

        // The model camera (cinematics, the login fly-throughs)
        int32_t HasModelCamera() const;
        void StartModelCamera();
        void ReleaseModel();
        int32_t InitModelCamera();
        void UpdateModelCamera();
        int32_t SetModel(const char* name, const C3Vector& position, float facing, void* doneCallback, WOWGUID doneOwner);
        void SetPositionAndLookAt(const C3Vector& position, const C3Vector& target, float roll);

        // Views
        void SaveViewToCVars(int32_t view, float distance, float pitch, float yaw);
        void SaveView(int32_t view);
        void LoadViewsFromCVars();
        int32_t IsViewDefault(int32_t view);
        void ResetView(int32_t view);
        void SetView(int32_t view, int32_t blend, int32_t mode);
        void SetViewCommentator(const C3Vector& position, float pitch, float yaw);
        void SetViewBarberShop(float distance, float pitch, float yaw);
        void SaveDistanceToCVars();
        void LoadSavedDistance();
        void SetVehicleDistanceMode(float mode, float time, int32_t smooth);
        void SetFirstPersonLook(float angle);

        // Rotation, zoom and their timed moves
        void AdjustYaw(float delta);
        void AdjustPitch(float delta);
        void ClampAngles();
        void SetMouseLookActive(int32_t active);
        void ZoomIn(float distance, int32_t time, float duration);
        void ZoomOut(float distance, int32_t time, float duration);
        void ZoomTo(float distance, int32_t time, float duration, int32_t lock);
        void AdvanceTimedValues(int32_t time);
        void MouseLook(float deltaX, float deltaY, float* pitchOut);
        void SyncPlayerFacing();
        void FaceYaw(float yaw);

        // Free look and the locks
        void BeginFreeLook();
        void EndFreeLook();
        void BeginFreeLookIfAllowed();
        void SetViewLocked(int32_t locked);
        void LockYaw();
        void UnlockYaw();
        void SetTracking(int32_t tracking);
        void SetTaxiLook(int32_t taxi);

        // The smoothed values
        int32_t StartDistanceBlend(float target, float duration, int32_t start);
        int32_t StartTiltBlend(float target, float duration, int32_t start);
        int32_t StartPitchBlend(float target, float duration, int32_t start);
        int32_t StartPitchOffsetBlend(float target, float duration, int32_t start);
        int32_t StartYawBlend(float target, float duration, int32_t start);
        int32_t StartMountHeightBlend(float target, float duration, int32_t start);
        int32_t StartFovBlend(float target, float duration, int32_t start);
        int32_t StartHeightBlend(float target, float duration, int32_t start);
        int32_t SmoothDistance(float target, float delay, float factor, int32_t time);
        int32_t SmoothTilt(float target, float delay, float factor, int32_t time);
        int32_t SmoothHeight(float target, float delay, float factor, int32_t time);
        int32_t SmoothPitch(float target, float delay, float factor, int32_t time);
        int32_t SmoothPitchOffset(float target, float delay, float factor, int32_t time);
        int32_t SmoothYaw(float target, float delay, float factor, int32_t time);
        int32_t SmoothMountHeight(float target, float delay, float factor, int32_t time);
        int32_t SmoothFov(float target, float delay, float factor, int32_t time);
        void StopPitchBlend();
        void StopPitchOffsetBlend();
        void StopYawBlend();
        void UpdateBlends(CGObject_C* target, int32_t time);
        void UpdateBlendA(int32_t time);
        void UpdateBlendB(int32_t time);
        void ApplyBlendA(C3Vector& vector) const;
        int32_t SetFovOffset(float degrees, int32_t immediate);

        // Distance limits
        void SetMinDistance(float distance);
        void SetMaxDistance(float distance);
        void ClearMinDistance();
        void ClearMaxDistance();

        // The target, the vehicle and the relative frame
        void SetTargetObject(CGObject_C* target, int32_t freeLook);
        void SetRelativeTo(WOWGUID relativeTo);
        void UpdateVehicle();
        void ClearVehicle();
        void UpdateVehicleRelative(int32_t worldTime);
        float GetVehicleFacing(CGObject_C* target) const;
        C3Vector& GetTargetPosition(C3Vector& out, CGObject_C* target) const;
        void GetAngles(CGObject_C* target, float* yaw, float* pitch, float* roll);
        float WrapTargetYaw(float facing, int32_t moved);
        void CalcHeights(CGObject_C* target, int32_t time);
        int32_t CalcTargetHeights(CGObject_C* target);
        float CalcWaterDepth(CGObject_C* target);
        void CheckFlyingHeight(CGUnit_C* unit);
        void CalcTerrainTiltFactor(CGObject_C* target, int32_t time);
        void UpdateTerrainTilt(CGObject_C* target, int32_t time, int32_t immediate);
        void CalcSmoothing(void* input, float strafe);
        int32_t CanPitchSmooth(float minPitch, float maxPitch) const;
        int32_t CanTrackPitch() const;
        int32_t CanYawSmooth(float minYaw, float maxYaw) const;
        int32_t IsSmoothing() const;
        int32_t CanBob(CGObject_C* target) const;
        int32_t CanPivot(CGObject_C* target) const;
        int32_t IsBobbing() const;
        void SetBobbing(int32_t moving);
        void StopBobbing();
        void CalcBobbing(C3Vector* offset);

        // Placement and collision
        C3Vector& CalcPosition(C3Vector& out, const C3Vector& target, float distance, const C3Vector& offset) const;
        uint32_t CollideDistance(const C3Vector& target, float* distance, float* height, const C3Vector& offset, float waterDepth, float* scale);
        int32_t CollideFrustum(float* distance, const C3Vector& from, const C3Vector& to, uint32_t flags);
        C3Vector& CollideWater(C3Vector& out, const C3Vector& target, float distance, const C3Vector& position);
        void ApplyShakes(const C3Vector& position, C3Vector* offset);
        void AddShake(const C3Vector& position, int32_t decay, int32_t axis, float amplitude, float frequency, float duration, float phase, float coefficient);
        void AddShakeByID(int32_t shakeId, const C3Vector& position);
        void ClearShakes();
        CameraShake* RemoveShake(CameraShake* shake);

        // The per-frame updates
        void UpdateLiquid();
        void UpdateTargetCamera(CGObject_C* target, int32_t time);
        void UpdateLookAtCamera(CGObject_C* target);

        // Member variables, at the reference's offsets (the CSimpleCamera base ends at +0x48)
        CM2Model* m_model = nullptr;                // +0x048 the model camera's model
        uint32_t m_modelTime = 0;                   // +0x04c the model camera's clock
        HCAMERA m_modelCamera = nullptr;            // +0x050 the model's camera
        C34Matrix m_modelPlacement;                 // +0x054 the model camera's placement
        WOWGUID m_target = 0;                       // +0x088 who the camera follows
        WOWGUID m_lookTarget = 0;                   // +0x090 who the camera looks at, with no target
        uint32_t m_flags = 0;                       // +0x098
        uint32_t m_flags2 = 0;                      // +0x09c
        WOWGUID m_relativeTo = 0;                   // +0x0a0 the transport the angles are relative to
        float m_terrainTilt = 0.0f;                 // +0x0a8 the terrain tilt factor
        int32_t m_yawLock = 0;                      // +0x0ac the yaw follows the target while > 0
        int32_t m_pitchLock = 0;                    // +0x0b0 the tilt is applied while > 0
        int32_t m_view = 0;                         // +0x0b4
        float m_views[MAX_CAMERA_VIEWS][3] = {};    // +0x0b8 distance, pitch, yaw per view
        float m_distance = 0.0f;                    // +0x118
        float m_yaw = 0.0f;                         // +0x11c
        float m_pitch = 0.0f;                       // +0x120
        float m_roll = 0.0f;                        // +0x124
        float m_height = 0.0f;                      // +0x128
        float m_yawOffset = 0.0f;                   // +0x12c FlipCameraYaw
        float m_pitchOffset = 0.0f;                 // +0x130 the tracking pitch
        float m_tiltPitch = 0.0f;                   // +0x134 the terrain tilt pitch
        float m_fovOffset = 0.0f;                   // +0x138
        float m_mountHeight = 0.0f;                 // +0x13c
        C3Vector m_bob = { 0.0f, 0.0f, 0.0f };      // +0x140 the bobbing offset
        float m_heights[3] = {};                    // +0x14c standing, mounted, swimming
        float m_flyHeight = 0.0f;                   // +0x158
        int32_t m_flyHeightTime = 0;                // +0x15c
        uint32_t m_timedBits = 0;                   // +0x160 two bits per timed value
        int32_t m_timedStart[6] = {};               // +0x164
        int32_t m_timedStop[6] = {};                // +0x17c
        int32_t m_timedEnd[6] = {};                 // +0x194
        float m_timedValue[6] = {};                 // +0x1ac
        C3Vector m_lastTargetPosition = { 0.0f, 0.0f, 0.0f };  // +0x1c4
        uint32_t m_unk1D0 = 0;                      // +0x1d0
        float m_targetFacing = 0.0f;                // +0x1d4
        int32_t m_tiltTime = 0;                     // +0x1d8
        float m_lockedYaw = 0.0f;                   // +0x1dc
        CameraBlend m_distanceBlend;                // +0x1e0
        CameraBlend m_tiltBlend;                    // +0x1f8
        CameraBlend m_heightBlend;                  // +0x210
        CameraBlend m_pitchBlend;                   // +0x228
        CameraBlend m_pitchOffsetBlend;             // +0x240
        CameraBlend m_yawBlend;                     // +0x258
        CameraBlend m_fovBlend;                     // +0x270
        int32_t m_bobStart = 0;                     // +0x288
        float m_bobDuration = 0.0f;                 // +0x28c
        C3Vector m_bobFrom = { 0.0f, 0.0f, 0.0f };  // +0x290
        uint32_t m_unk29C = 0;                      // +0x29c
        int32_t m_liquidType = 0;                   // +0x2a0
        uint32_t m_unk2A4 = 0;                      // +0x2a4 the target was on a taxi last frame
        CameraBlend m_mountHeightBlend;             // +0x2a8
        float m_collisionScale = 1.0f;              // +0x2c0
        int32_t m_vehicleDistanceMode = 0;          // +0x2c4
        float m_minDistance = 0.0f;                 // +0x2c8
        float m_maxDistance = 0.0f;                 // +0x2cc
        float m_unk2D0 = 0.0f;                      // +0x2d0 the distance saved while overridden
        int32_t m_unk2D4 = 0;                       // +0x2d4 blend A start
        int32_t m_unk2D8 = 0;                       // +0x2d8 blend A end
        uint32_t m_unk2DC = 0;                      // +0x2dc blend A source
        uint32_t m_unk2E0 = 0;                      // +0x2e0 blend A target
        uint32_t m_unk2E4 = 0;                      // +0x2e4 blend A value
        int32_t m_unk2E8 = 0;                       // +0x2e8 blend B start
        int32_t m_unk2EC = 0;                       // +0x2ec blend B end
        uint32_t m_unk2F0 = 0;                      // +0x2f0 blend B source
        uint32_t m_unk2F4 = 0;                      // +0x2f4 blend B target
        uint32_t m_unk2F8 = 0;                      // +0x2f8 blend B value
        uint32_t m_unk2FC = 0;                      // +0x2fc the tracking style override
        STORM_EXPLICIT_LIST(CameraShake, m_link) m_shakes;  // +0x300
        int32_t m_savedView = 2;                    // +0x30c
        float m_savedDistance = 0.0f;               // +0x310
        float m_savedBarberDistance = 0.0f;         // +0x314
        float m_firstPersonPitch = 0.0f;            // +0x318
        CVehicleCamera_C* m_vehicleCamera = nullptr; // +0x31c the vehicle seat's camera
};

void CameraRegisterCVars();
float CameraWrapAngleOnce(float angle);
void CameraSplitFloor(float value, float* fraction, int32_t* whole);
void CameraApplyShake(C3Vector* offset, float facing, float amplitude, float time, float frequency, int32_t decay, float coefficient, int32_t axis);
void CameraRegisterScriptFunctions();
void CameraUnregisterScriptFunctions();

#endif
