#include "object/client/CVehicleCamera_C.hpp"
#include "db/rec/VehicleSeatRec.hpp"
#include "object/Client.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/CVehiclePassenger_C.hpp"
#include "object/client/UnitVehicle_C.hpp"
#include "ui/InputControl.hpp"
#include "ui/game/CGCamera.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include <storm/Memory.hpp>
#include <tempest/Matrix.hpp>
#include <cmath>
#include <cstring>

int32_t WorldQuerySegment(const C3Vector& start, const C3Vector& end, C3Vector* hit, float* t, uint32_t flags, void* result);
void SpringToward(float* state, const float* target, float rate, float dt);
float ClampRange(float value, float low, float high);

namespace {

// DAT_00ca165c: the cameras alive.
int32_t s_cameraCount = 0;

bool Moving(int32_t state) {
    return state == 1 || state == 2 || state == 3;
}

CGUnit_C* UnitPtr(WOWGUID guid, int32_t line) {
    return static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\VehicleCamera_C.cpp", line));
}

CGObject_C* ObjectPtr(WOWGUID guid, int32_t line) {
    return static_cast<CGObject_C*>(ClntObjMgrObjectPtr(guid, TYPE_OBJECT, ".\\VehicleCamera_C.cpp", line));
}

int32_t RoundMs(float seconds) {
    return static_cast<int32_t>(std::nearbyint(seconds * 1000.0f));
}

// ref: FUN_006ff4c0
// The vector form of SpringToward: (position, velocity) pulled toward `target`.
void SpringVector(C3Vector& position, C3Vector& velocity, const C3Vector& target, float rate, float dt) {
    float x = rate * dt;
    float decay = 1.0f / (x * x * 0.48f + x * x * x * 0.235f + x + 1.0f);

    C3Vector pull = {
        ((position.x - target.x) * rate + velocity.x) * dt,
        (velocity.y + (position.y - target.y) * rate) * dt,
        (velocity.z + (position.z - target.z) * rate) * dt,
    };

    position = {
        target.x + (pull.x + (position.x - target.x)) * decay,
        (pull.y + (position.y - target.y)) * decay + target.y,
        (pull.z + (position.z - target.z)) * decay + target.z,
    };

    velocity = {
        (velocity.x - pull.x * rate) * decay,
        (velocity.y - pull.y * rate) * decay,
        decay * (velocity.z - pull.z * rate),
    };
}

// ref: FUN_00718080
// Where a unit's eyes are for the camera: its collision height less a sixth of a yard, capped for
// anything not a player.
float UnitCameraHeight(CGUnit_C* unit) {
    float height = unit->m_localMove.m_collisionHeight;

    if (!unit->IsA(TYPE_PLAYER) && 2.0277777f < height) {
        return 2.0277777f - 0.16666667f;
    }

    return height - 0.16666667f;
}

// ref: FUN_004f4500
// A world point into the passenger's transport's space.
C3Vector PassengerToTransport(const CPassenger& passenger, const C3Vector& world) {
    if (!passenger.m_transportGUID) {
        return world;
    }

    C44Matrix matrix;
    MovementGetTransportMatrixChecked(passenger.m_transportGUID, matrix, passenger.m_guid, ".\\Passenger.cpp", 0x4b);

    return world * matrix.AffineInverse();
}

} // namespace

// ref: FUN_00759160
void CVehicleCamera_C::Initialize() {
    s_cameraCount = 0;
}

// ref: FUN_007591d0
void CVehicleCamera_C::Shutdown() {
    s_cameraCount = 0;
}

// ref: FUN_00759d60
void CVehicleCamera_C::Free() {
    s_cameraCount--;
    this->~CVehicleCamera_C();
    STORM_FREE(this);
}

// ref: FUN_00759c60
int32_t CVehicleCamera_C::ConvertSmoothFacingFromRawToWorld(float& smoothFacing, CGObject_C* relativeTo) {
    if (!relativeTo) {
        return 0;
    }

    if (relativeTo->IsA(TYPE_UNIT)) {
        auto transport = ObjectPtr(relativeTo->GetTransportGUID(), 0x4a8);
        float facing = static_cast<CGUnit_C*>(relativeTo)->GetRawSmoothFacing();
        CVehicleCamera_C::ConvertSmoothFacingFromRawToWorld(facing, transport);
        smoothFacing = facing + smoothFacing;

        return 1;
    }

    smoothFacing = relativeTo->GetFacing() + smoothFacing;

    return 1;
}

// ref: FUN_00759ce0
int32_t CVehicleCamera_C::ConvertSmoothFacingFromWorldToRaw(float& smoothFacing, CGObject_C* relativeTo) {
    if (!relativeTo) {
        return 0;
    }

    if (relativeTo->IsA(TYPE_UNIT)) {
        auto transport = ObjectPtr(relativeTo->GetTransportGUID(), 0x4b9);
        float facing = static_cast<CGUnit_C*>(relativeTo)->GetRawSmoothFacing();
        CVehicleCamera_C::ConvertSmoothFacingFromRawToWorld(facing, transport);
        smoothFacing = smoothFacing - facing;

        return 1;
    }

    smoothFacing = smoothFacing - relativeTo->GetFacing();

    return 1;
}

// ref: FUN_00759b50
int32_t CVehicleCamera_C::ToWorld(C3Vector& point, CGObject_C* space) {
    if (!space) {
        return 0;
    }

    if (space->IsA(TYPE_UNIT)) {
        C3Vector position = space->GetPosition();
        point = { position.x + point.x, position.y + point.y, position.z + point.z };
        return 1;
    }

    C44Matrix world;
    space->GetWorldMatrix(world);
    C3Vector out;
    TransformPointInPlace(out, point, world);

    return 1;
}

// ref: FUN_00759bd0
int32_t CVehicleCamera_C::ToLocal(C3Vector& point, CGObject_C* space) {
    if (!space) {
        return 0;
    }

    if (space->IsA(TYPE_UNIT)) {
        C3Vector position = space->GetPosition();
        point = { point.x - position.x, point.y - position.y, point.z - position.z };
        return 1;
    }

    C44Matrix world;
    space->GetWorldMatrix(world);
    auto inverse = world.AffineInverse();
    C3Vector out;
    TransformPointInPlace(out, point, inverse);

    return 1;
}

// ref: FUN_0075b150
// ref: FUN_0075aa70
// ref: FUN_0075b1a0
// A rider's camera, from where the camera is now: its facing, its place, the target it follows.
CVehicleCamera_C* CVehicleCamera_C::Create(CGUnit_C* unit, uint32_t time) {
    if (!unit->m_vehiclePassenger || unit->m_vehiclePassenger->m_state == 0) {
        return nullptr;
    }

    auto camera = STORM_NEW(CVehicleCamera_C);
    s_cameraCount++;

    camera->m_lastTime = time - 1;
    camera->m_stateStart = time;
    camera->m_unit = unit;

    camera->SetTarget(unit->GetGUID(), unit->GetTransportGUID());
    camera->m_transportGUID = unit->GetTransportGUID();

    C44Matrix seat;
    UnitSeatMatrix(unit, seat);
    camera->m_startPosition = { seat.d0, seat.d1, seat.d2 };
    camera->m_seatPosition = camera->m_startPosition;
    camera->m_startLocal = unit->GetRawPosition();
    camera->m_springPosition = camera->m_startLocal;

    unit->UpdateSmoothFacing(0);
    float facing = unit->GetWorldSmoothFacing();
    camera->m_startFacing = facing;
    camera->m_smoothFacing = facing;
    camera->m_rawFacing = unit->m_smoothFacing;
    camera->m_targetFacing = facing;
    camera->m_collideScale = 1.0f;
    camera->m_collideVelocity = 0.0f;
    camera->m_facingSpring[0] = facing;
    camera->m_facingSpring[1] = 0.0f;
    camera->m_followFacing[0] = facing;
    camera->m_followFacing[1] = 0.0f;

    camera->BeginTransition(time);

    if (camera->m_state == 0) {
        camera->Free();
        return nullptr;
    }

    return camera;
}

// ref: FUN_00759200
// The seat's camera point: the vehicle's place (in its transport's space, or relative to the camera
// this one is relative to), then the seat's camera offset turned by the vehicle's facing.
C3Vector CVehicleCamera_C::ComputeSeatPosition(CGUnit_C* vehicle) {
    bool moving = Moving(this->m_state);
    auto seat = this->m_seat;

    if (moving && vehicle == this->m_unit && seat && (seat->m_flagsB & 0x200) && this->m_unit->m_vehiclePassenger) {
        if (auto next = this->m_unit->m_vehiclePassenger->GetVehicleUnit()) {
            vehicle = next;
        }
    }

    C3Vector out = { 0.0f, 0.0f, 0.0f };
    float facing;

    if (!vehicle) {
        vehicle = this->m_unit;
    }

    if (vehicle != this->m_unit && vehicle->m_vehicleCamera) {
        out = vehicle->m_vehicleCamera->m_seatPosition;
        facing = vehicle->m_vehicleCamera->m_smoothFacing;
    } else {
        C3Vector local;

        if (!moving || !seat || !(seat->m_flagsB & 0x100)) {
            local = vehicle->GetRawPosition();
        } else {
            C3Vector world = vehicle->m_vehiclePassenger ? vehicle->m_vehiclePassenger->GetPosition() : vehicle->GetModelWorldPosition();
            local = PassengerToTransport(vehicle->m_localMove, world);
        }

        float raw = vehicle->m_smoothFacing;
        bool relative = false;

        if (this->m_relativeGUID && vehicle->GetTransportGUID() == this->m_relativeGUID) {
            auto rel = UnitPtr(this->m_relativeGUID, 0xbe);

            if (rel && rel->m_vehicleCamera) {
                float f = rel->m_vehicleCamera->m_smoothFacing;
                C44Matrix matrix = C44Matrix::RotationAroundZ(f);
                matrix.d0 = rel->m_vehicleCamera->m_seatPosition.x;
                matrix.d1 = rel->m_vehicleCamera->m_seatPosition.y;
                matrix.d2 = rel->m_vehicleCamera->m_seatPosition.z;

                out = local;
                C3Vector tmp;
                TransformPointInPlace(tmp, out, matrix);
                facing = f + raw;
                relative = true;
            }
        }

        if (!relative) {
            out = vehicle->m_localMove.GetPosition(local);
            facing = vehicle->m_localMove.GetFacing(raw);
        }
    }

    if (moving && seat && (seat->m_cameraOffset[0] != 0.0f || seat->m_cameraOffset[1] != 0.0f || seat->m_cameraOffset[2] != 0.0f)) {
        C44Matrix turn = C44Matrix::RotationAroundZ(facing);
        C3Vector offset = { seat->m_cameraOffset[1], -seat->m_cameraOffset[0], seat->m_cameraOffset[2] };
        C3Vector tmp;
        TransformPointInPlace(tmp, offset, turn);
        out = { out.x + offset.x, out.y + offset.y, out.z + offset.z };
    }

    return out;
}

// ref: FUN_00759580
void CVehicleCamera_C::UpdatePosition() {
    this->m_flags |= 0x40;
    this->m_position = this->m_seatPosition;

    auto unit = UnitPtr(this->m_unitGUID, 0xfa);

    if (!unit) {
        return;
    }

    auto root = unit->m_vehiclePassenger ? unit->m_vehiclePassenger->GetRootVehicleUnit() : UnitGetVehicleRoot(unit, nullptr);

    if (!root) {
        return;
    }

    C3Vector from = root->GetPosition();
    from.z += UnitCameraHeight(root);

    auto camera = CGWorldFrame::GetActiveCamera();

    if (!camera) {
        return;
    }

    C3Vector dir = {
        this->m_seatPosition.x - from.x,
        this->m_seatPosition.y - from.y,
        (camera->m_height + this->m_seatPosition.z) - from.z,
    };

    float length = std::sqrt(dir.z * dir.z + dir.y * dir.y + dir.x * dir.x);

    if (length <= 1e-05f) {
        return;
    }

    float inv = 1.0f / length;
    dir = { dir.x * inv, dir.y * inv, dir.z * inv };
    length += 1.0f;

    if (200.0f < length) {
        length = 200.0f;
    }

    C3Vector to = { dir.x * length + from.x, dir.y * length + from.y, length * dir.z + from.z };
    C3Vector hit = { 0.0f, 0.0f, 0.0f };
    float t = 1.0f;
    float reach;

    if (!WorldQuerySegment(from, to, &hit, &t, CameraCollisionFlags(), nullptr)) {
        reach = this->m_collideScale * length - 1.0f;
    } else {
        if (this->m_collideScale <= t) {
            t = this->m_collideScale;
        } else {
            this->m_collideScale = t;
            this->m_collideVelocity = 0.0f;
        }

        reach = length * t - 1.0f;

        if (reach <= 1e-05f) {
            reach = 0.0f;
        }
    }

    if (reach != 0.0f) {
        from = { dir.x * reach + from.x, dir.y * reach + from.y, dir.z * reach + from.z };
    }

    this->m_position = from;
    this->m_position.z -= camera->m_height;
}

// ref: FUN_007597d0
// The seat (or one the camera is relative to) turns the camera with it.
int32_t CVehicleCamera_C::IsControllingFacing() {
    auto camera = this;

    while (true) {
        auto seat = camera->m_seat;

        if (seat) {
            if (Moving(camera->m_state)) {
                if (0.0001f < seat->m_cameraFacingChaseRate) {
                    return 1;
                }

                if (camera->m_state == 3 && (seat->m_flagsB & 0x40) && (seat->m_flagsB & 0x20000)) {
                    return 1;
                }
            } else if ((seat->m_flagsB & 0x80) && (seat->m_flagsB & 0x40000)) {
                return 1;
            }
        }

        auto rel = UnitPtr(camera->m_relativeGUID, 0x143);

        if (!rel || !rel->m_vehicleCamera) {
            return 0;
        }

        camera = rel->m_vehicleCamera;
    }
}

// ref: FUN_00759880
void CVehicleCamera_C::UpdateFlags() {
    bool moving = Moving(this->m_state);

    if (moving && this->m_seat && (this->m_seat->m_flagsB & 0x1)) {
        this->m_flags |= 0x2;
    } else {
        this->m_flags &= ~0x2u;
    }

    if (moving && this->m_seat && (this->m_seat->m_flagsB & 0x80001)) {
        this->m_flags |= 0x10;
    } else {
        this->m_flags &= ~0x10u;
    }
}

// ref: FUN_007598f0
float CVehicleCamera_C::GetBlend(uint32_t time) {
    if (this->m_state == 3) {
        return 1.0f;
    }

    auto ride = this->m_unit->m_vehiclePassenger;
    bool rideMoving;
    uint32_t mask;

    if (this->m_state == 1 || this->m_state == 2) {
        rideMoving = ride && ride->m_state == 2;
        mask = 0x40;
    } else {
        rideMoving = ride && ride->m_state == 5;
        mask = 0x80;
    }

    if (rideMoving && (!this->m_seat || !(this->m_seat->m_flagsB & mask))) {
        return ride->m_progress;
    }

    if (this->m_stateDuration == 0) {
        return 1.0f;
    }

    float t = static_cast<float>(static_cast<int32_t>(time - this->m_stateStart)) / static_cast<float>(this->m_stateDuration);

    return t < 0.0f ? 0.0f : (t < 1.0f ? t : 1.0f);
}

// ref: FUN_007599d0
WOWGUID CVehicleCamera_C::GetRelativeGUID() {
    auto camera = this;
    WOWGUID target = camera->m_unitGUID;

    while (target != camera->m_unit->GetGUID()) {
        auto unit = UnitPtr(target, 0x39a);

        if (!unit) {
            return 0;
        }

        camera = unit->m_vehicleCamera;

        if (!camera || camera->m_state == 0) {
            return unit->GetTransportGUID();
        }

        target = camera->m_unitGUID;
    }

    return camera->m_transportGUID;
}

// ref: FUN_00759a60
// The active camera's chain lets go of this camera's rider.
void CVehicleCamera_C::Detach() {
    auto active = CGWorldFrame::GetActiveCamera();

    if (!active || !active->m_vehicleCamera) {
        return;
    }

    for (auto camera = active->m_vehicleCamera; camera->m_relativeGUID;) {
        if (camera->m_relativeGUID == this->m_unit->GetGUID()) {
            camera->m_relativeGUID = 0;
            return;
        }

        auto unit = UnitPtr(camera->m_relativeGUID, 0x3dc);

        if (!unit || !unit->m_vehicleCamera) {
            return;
        }

        camera = unit->m_vehicleCamera;
    }
}

// ref: FUN_00759ae0
int32_t CVehicleCamera_C::IsInChain(CVehicleCamera_C* other) {
    if (!other->m_relativeGUID) {
        return 0;
    }

    if (other->m_relativeGUID == this->m_unit->GetGUID()) {
        return 1;
    }

    auto unit = UnitPtr(other->m_relativeGUID, 0x461);

    return unit && unit->m_vehicleCamera && this->IsInChain(unit->m_vehicleCamera) ? 1 : 0;
}

// ref: FUN_00759d80
// The camera's place for this frame: the seat point (chased by the seat's position rate), and
// while it is not yet aboard, between where it started and there (sprung by the seat's choice).
void CVehicleCamera_C::UpdatePositionBlend(CGUnit_C* vehicle, uint32_t time, float blend) {
    this->m_flags &= ~0x40u;

    C3Vector target = this->ComputeSeatPosition(vehicle);
    float dt = static_cast<float>(static_cast<int32_t>(time - this->m_lastTime)) * 0.001f;

    // The collision scale eases back to 1.
    {
        float x = dt * 2.5f;
        float decay = 1.0f / (x * x * 0.48f + x * x * x * 0.235f + x + 1.0f);
        float offset = this->m_collideScale - 1.0f;
        float pull = (offset * 2.5f + this->m_collideVelocity) * dt;

        this->m_collideScale = (offset + pull) * decay + 1.0f;
        this->m_collideVelocity = decay * (this->m_collideVelocity - 2.5f * pull);
    }

    if (!vehicle) {
        vehicle = this->m_unit;
    }

    auto space = ObjectPtr(vehicle->GetTransportGUID(), 0x2ae);
    CVehicleCamera_C* relCamera = nullptr;

    if (space && vehicle->GetTransportGUID() == this->m_relativeGUID && space->IsA(TYPE_UNIT)) {
        relCamera = static_cast<CGUnit_C*>(space)->m_vehicleCamera;
    }

    if (this->m_seat && 0.0001f < this->m_seat->m_cameraPosChaseRate && Moving(this->m_state)) {
        if (relCamera) {
            target = { target.x - relCamera->m_seatPosition.x, target.y - relCamera->m_seatPosition.y, target.z - relCamera->m_seatPosition.z };
        } else {
            CVehicleCamera_C::ToLocal(target, space);
        }

        if (!(this->m_flags & 0x20)) {
            SpringVector(this->m_followPosition, this->m_followVelocity, target, 20.0f / this->m_seat->m_cameraPosChaseRate, dt);
            target = this->m_followPosition;
        } else {
            this->m_followPosition = target;
            this->m_followVelocity = { 0.0f, 0.0f, 0.0f };
        }

        if (relCamera) {
            target = { relCamera->m_seatPosition.x + target.x, relCamera->m_seatPosition.y + target.y, relCamera->m_seatPosition.z + target.z };
        } else {
            CVehicleCamera_C::ToWorld(target, space);
        }
    }

    if (this->m_state == 3) {
        this->m_seatPosition = target;
        return;
    }

    if (space) {
        this->m_startPosition = this->m_startLocal;

        if (relCamera) {
            this->m_startPosition = {
                this->m_startPosition.x + relCamera->m_seatPosition.x,
                relCamera->m_seatPosition.y + this->m_startPosition.y,
                relCamera->m_seatPosition.z + this->m_startPosition.z,
            };
        } else {
            CVehicleCamera_C::ToWorld(this->m_startPosition, space);
        }
    }

    if (this->m_state == 1 || this->m_state == 4) {
        this->m_seatPosition = this->m_startPosition;
        return;
    }

    uint32_t mask = (this->m_state == 2 || this->m_state == 3) ? 0x20000 : 0x40000;

    if (this->m_seat && (this->m_seat->m_flagsB & mask)) {
        C3Vector local = target;
        CVehicleCamera_C::ToLocal(local, space);

        if (0.999999f <= blend) {
            // FUN_006feca0
            this->m_springPosition = local;
            this->m_springVelocity = { 0.0f, 0.0f, 0.0f };
            this->m_seatPosition = target;
            return;
        }

        float duration = static_cast<float>(this->m_stateDuration);
        float rate = (1.0f / (0.001f + duration * 0.001f)) * 4.0f + 1.0f / ((1.0f - blend) * (1.0f - blend));

        SpringVector(this->m_springPosition, this->m_springVelocity, local, rate, dt);
        this->m_seatPosition = this->m_springPosition;
        CVehicleCamera_C::ToWorld(this->m_seatPosition, space);
        return;
    }

    this->m_seatPosition = {
        (target.x - this->m_startPosition.x) * blend + this->m_startPosition.x,
        (target.y - this->m_startPosition.y) * blend + this->m_startPosition.y,
        blend * (target.z - this->m_startPosition.z) + this->m_startPosition.z,
    };
}

// ref: FUN_0075a1c0
// The camera's facing for this frame, the same way: the vehicle's (chased by the seat's facing
// rate), blended from where it started; a seat that holds the yaw turns the camera's own yaw with it.
void CVehicleCamera_C::UpdateFacing(CGUnit_C* vehicle, uint32_t time, float blend, CGObject_C* relative) {
    float facing;
    WOWGUID spaceGUID;

    if (vehicle && vehicle != this->m_unit && vehicle->m_vehicleCamera) {
        facing = vehicle->m_vehicleCamera->m_rawFacing;
        spaceGUID = vehicle->m_vehicleCamera->GetRelativeGUID();
    } else {
        if (!vehicle) {
            vehicle = this->m_unit;
        }

        vehicle->UpdateSmoothFacing(0);
        facing = vehicle->m_smoothFacing;
        spaceGUID = vehicle->GetTransportGUID();
    }

    float previous = this->m_targetFacing;
    this->m_targetFacing = facing;

    auto space = ObjectPtr(spaceGUID, 0x31c);
    CVehicleCamera_C* relCamera = nullptr;

    if (space && spaceGUID == this->m_relativeGUID && space->IsA(TYPE_UNIT)) {
        relCamera = static_cast<CGUnit_C*>(space)->m_vehicleCamera;
    }

    if (relCamera) {
        this->m_targetFacing = relCamera->m_smoothFacing + this->m_targetFacing;
    } else {
        CVehicleCamera_C::ConvertSmoothFacingFromRawToWorld(this->m_targetFacing, space);
    }

    const float pi = 3.1415927f;
    const float twoPi = 6.2831855f;

    while (this->m_targetFacing + pi < previous) {
        this->m_targetFacing += twoPi;
    }

    while (previous < this->m_targetFacing - pi) {
        this->m_targetFacing -= twoPi;
    }

    float dt = static_cast<float>(static_cast<int32_t>(time - this->m_lastTime)) * 0.001f;
    auto seat = this->m_seat;

    if (seat && 0.0001f < seat->m_cameraFacingChaseRate && Moving(this->m_state)) {
        if (!(this->m_flags & 0x20)) {
            SpringToward(this->m_followFacing, &this->m_targetFacing, 20.0f / seat->m_cameraFacingChaseRate, dt);
            this->m_targetFacing = this->m_followFacing[0];
        } else {
            this->m_followFacing[0] = this->m_targetFacing;
            this->m_followFacing[1] = 0.0f;
        }
    }

    float old = this->m_smoothFacing;
    float next;

    if (this->m_state == 3) {
        next = this->m_targetFacing;
    } else if (this->m_state == 1 || this->m_state == 4) {
        next = this->m_startFacing;
    } else if (!seat || !(seat->m_flagsB & (this->m_state != 2 ? 0x40000u : 0x20000u))) {
        next = (this->m_targetFacing - this->m_startFacing) * CGCamera::CosineEase(blend) + this->m_startFacing;
    } else if (0.999999f <= blend) {
        this->m_facingSpring[0] = this->m_targetFacing;
        this->m_facingSpring[1] = 0.0f;
        next = this->m_facingSpring[0];
    } else {
        float toward = this->m_targetFacing;

        while (toward + pi < this->m_facingSpring[0]) {
            toward += twoPi;
        }

        while (this->m_facingSpring[0] < toward - pi) {
            toward -= twoPi;
        }

        float duration = static_cast<float>(this->m_stateDuration);
        SpringToward(this->m_facingSpring, &toward, 1.0f / ((1.0f - blend) * (1.0f - blend)) + (1.0f / (duration * 0.001f + 0.001f)) * 4.0f, dt);
        next = this->m_facingSpring[0];
    }

    this->m_smoothFacing = next;

    float oldRaw = this->m_rawFacing;
    this->m_rawFacing = this->m_smoothFacing;
    CVehicleCamera_C::ConvertSmoothFacingFromWorldToRaw(this->m_rawFacing, relative);

    auto camera = CGWorldFrame::GetActiveCamera();
    auto input = InputControlGetActive();

    if ((this->m_flags & 0x2) && Moving(this->m_state) && camera && camera->m_target == this->m_unit->GetGUID()) {
        if ((this->m_flags & 0x4) && camera->m_yawLock < 1) {
            if (this->m_state == 3) {
                camera->AdjustYaw(old - this->m_smoothFacing);
            } else {
                this->m_rawFacing = oldRaw;
                this->m_smoothFacing = old;
                this->m_facingSpring[0] = old;
                this->m_targetFacing = previous;
            }
        }

        bool held = !input || !(input->m_unk04 & 0x300) || (input->m_unk04 & 0x2000001);

        if (held) {
            this->m_flags |= 0x4;
        } else {
            this->m_flags &= ~0x4u;
        }

        return;
    }

    this->m_flags &= ~0x4u;
}

// ref: FUN_0075a630
// The camera's distance, pitch and yaw blends take the seat's settings over `duration` -- once, and
// only for the camera following this rider (or what it rides).
void CVehicleCamera_C::ApplyCameraBlend(uint32_t time, int32_t duration) {
    if ((this->m_flags & 0x8) || (duration != 0 && !(this->m_flags & 0x1))) {
        return;
    }

    auto camera = CGWorldFrame::GetActiveCamera();

    if (!camera) {
        return;
    }

    if (camera->m_target != this->m_unit->GetGUID() && camera->m_target != this->m_unitGUID) {
        return;
    }

    auto player = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, ".\\Player_C.h", 0xa0));

    if (!player) {
        return;
    }

    WOWGUID next = player->m_vehiclePassenger ? player->m_vehiclePassenger->m_nextVehicleGUID : 0;
    auto vehicle = UnitPtr(next, 0x38d);

    if (!vehicle || player == this->m_unit || UnitGetVehicleRoot(player, this->m_unit) != this->m_unit) {
        VehicleSetCameraBlend(this->m_unit, this->m_vehicleRec, this->m_seat, time, time + duration);
        this->m_flags |= 0x8;
    }
}

// ref: FUN_0075a720
int32_t CVehicleCamera_C::AttachToActiveCamera() {
    auto active = CGWorldFrame::GetActiveCamera();

    if (!active || !active->m_vehicleCamera) {
        return 0;
    }

    auto camera = active->m_vehicleCamera;

    while (camera->m_relativeGUID) {
        auto unit = UnitPtr(camera->m_relativeGUID, 0x3b2);

        if (!unit || !unit->m_vehicleCamera) {
            return 0;
        }

        camera = unit->m_vehicleCamera;
    }

    auto ride = camera->m_unit->m_vehiclePassenger;
    WOWGUID next = ride ? ride->m_nextVehicleGUID : 0;

    if (next == this->m_unit->GetGUID() && !camera->IsInChain(this)) {
        camera->m_relativeGUID = this->m_unit->GetGUID();
        return 1;
    }

    return 0;
}

// ref: FUN_0075a7d0
// The camera follows `target`, relative to the camera of the vehicle it rides (made for the moment
// if it has none) unless that would close a loop.
void CVehicleCamera_C::SetTarget(WOWGUID target, WOWGUID fallback) {
    this->m_flags |= 0x80;
    this->m_unitGUID = target;

    WOWGUID self = this->m_unit->GetGUID();

    if (target == self) {
        target = fallback;
    }

    if (this->m_relativeGUID == target) {
        this->m_flags &= ~0x80u;
        return;
    }

    bool created = false;

    if (target && target != self) {
        auto unit = UnitPtr(target, 0x401);

        if (unit && unit->m_vehiclePassenger && unit->m_vehiclePassenger->m_state != 0) {
            auto other = unit->m_vehicleCamera;
            created = other == nullptr;

            if (created) {
                other = UnitCreateVehicleCamera(unit);
            }

            if (other && !this->IsInChain(other)) {
                goto keep;
            }

            if (created) {
                UnitDestroyVehicleCamera(unit);
            }
        }

        target = 0;
    }

keep:
    WOWGUID old = this->m_relativeGUID;
    this->m_relativeGUID = target;

    if (!created) {
        if (auto camera = CGWorldFrame::GetActiveCamera()) {
            camera->UpdateVehicle();
        }
    }

    if (old) {
        if (auto unit = UnitPtr(old, 0x428)) {
            UnitReleaseVehicleCamera(unit);
        }
    }

    this->m_flags &= ~0x80u;
}

// ref: FUN_0075a930
void CVehicleCamera_C::ChooseTarget(CGUnit_C* vehicle, const VehicleSeatRec* seat, int32_t exiting, WOWGUID* out) {
    WOWGUID self = this->m_unit->GetGUID();

    if (!exiting && vehicle && seat && (seat->m_flags & 0x800)) {
        *out = vehicle->GetTransportGUID();
        this->m_transportGUID = 0;
        this->SetTarget(self, *out);
        return;
    }

    auto ride = this->m_unit->m_vehiclePassenger;

    if (!ride) {
        *out = this->m_unit->GetTransportGUID();
    } else if (!exiting) {
        *out = ride->m_nextVehicleGUID;
    } else {
        *out = ride->m_spaceGUID;
    }

    if (!vehicle) {
        this->m_transportGUID = this->m_unit->GetTransportGUID();
    } else if (!exiting && (!seat || !(seat->m_flagsB & 0x80001))) {
        this->m_transportGUID = vehicle->GetGUID();
    } else {
        auto root = vehicle->m_vehiclePassenger ? vehicle->m_vehiclePassenger->GetRootVehicleUnit() : UnitGetVehicleRoot(vehicle, nullptr);
        this->m_transportGUID = (root ? root : vehicle)->GetTransportGUID();
    }

    this->SetTarget(self, *out);
}

// ref: FUN_0075aac0
// The ride changed: the camera's transition is planned from the seat's camera timings (or a clamp
// of its move's duration, or half a second), its state chosen, its target taken, and its springs
// started from where it is.
void CVehicleCamera_C::BeginTransition(uint32_t time) {
    auto ride = this->m_unit->m_vehiclePassenger;

    if (!ride) {
        return;
    }

    this->m_flags &= ~0x8u;
    this->m_vehicleRec = ride->m_vehicleRec;
    this->m_seat = ride->m_seat;

    auto seat = this->m_seat;
    int32_t rideState = ride->m_state;
    int32_t exiting;
    float delay = 0.0f;

    switch (rideState) {
        case 0:
        case 5:
            exiting = 1;
            break;

        case 1:
            exiting = 0;
            delay = seat ? seat->m_enterPreDelay : 0.0f;
            break;

        case 2:
        case 3:
            exiting = 0;
            break;

        case 4:
            exiting = 1;
            delay = seat ? seat->m_exitPreDelay : 0.0f;
            break;

        default:
            return;
    }

    if (!UnitAllowsSeatAnimation(this->m_unit, seat, exiting)) {
        this->m_transitionDuration = 0;
        this->m_flags |= 0x1;
    } else if (exiting && seat && (seat->m_flagsB & 0x80)) {
        delay = seat->m_cameraExitingDelay;
        this->m_transitionDuration = RoundMs(seat->m_cameraExitingDuration);
        this->m_flags |= 0x1;
    } else if (!exiting && seat && (seat->m_flagsB & 0x40)) {
        delay = seat->m_cameraEnteringDelay;
        this->m_transitionDuration = RoundMs(seat->m_cameraEnteringDuration);
        this->m_flags |= 0x1;
    } else if (seat) {
        float length = exiting ? seat->m_exitMaxDuration : seat->m_enterMaxDuration;
        this->m_transitionDuration = RoundMs(ClampRange(length, 0.5f, 3.0f));
        this->m_flags &= ~0x1u;
    } else {
        this->m_transitionDuration = 500;
        this->m_flags &= ~0x1u;
    }

    bool timed = (this->m_flags & 0x1) != 0;

    if (exiting) {
        if (0.0f < delay) {
            this->m_state = 4;
            this->m_stateDuration = RoundMs(delay);
        } else if (rideState == 0 && (this->m_transitionDuration == 0 || !timed)) {
            this->m_state = 0;
            this->m_stateDuration = 0;
        } else {
            this->m_state = 5;
            this->m_stateDuration = this->m_transitionDuration;
        }
    } else if (0.0f < delay) {
        this->m_state = 1;
        this->m_stateDuration = RoundMs(delay);
    } else if (rideState == 3 && (this->m_transitionDuration == 0 || !timed)) {
        this->m_state = 3;
        this->m_stateDuration = 0;
    } else {
        this->m_state = 2;
        this->m_stateDuration = this->m_transitionDuration;
    }

    WOWGUID out = 0;
    this->ChooseTarget(ride->GetVehicleUnit(), seat, exiting, &out);

    if (delay < 1e-05f) {
        int32_t duration;

        if (exiting) {
            duration = (rideState == 0 && !timed) ? 0 : this->m_transitionDuration;
        } else {
            duration = (rideState == 3 && !timed) ? 0 : this->m_transitionDuration;
        }

        this->ApplyCameraBlend(time, duration);
    }

    this->m_startFacing = this->m_smoothFacing;
    this->m_targetFacing = this->m_smoothFacing;
    this->m_stateStart = time;
    this->m_followFacing[0] = this->m_smoothFacing;
    this->m_startPosition = this->m_seatPosition;
    this->m_startLocal = this->m_seatPosition;

    auto space = ObjectPtr(out, 0x1f6);

    if (space && out == this->m_relativeGUID && space->IsA(TYPE_UNIT) && static_cast<CGUnit_C*>(space)->m_vehicleCamera) {
        auto rel = static_cast<CGUnit_C*>(space)->m_vehicleCamera;
        this->m_startLocal = {
            this->m_startLocal.x - rel->m_seatPosition.x,
            this->m_startLocal.y - rel->m_seatPosition.y,
            this->m_startLocal.z - rel->m_seatPosition.z,
        };
    } else {
        CVehicleCamera_C::ToLocal(this->m_startLocal, space);
    }

    this->m_rawFacing = this->m_smoothFacing;
    CVehicleCamera_C::ConvertSmoothFacingFromWorldToRaw(this->m_rawFacing, ObjectPtr(this->GetRelativeGUID(), 0x1ff));

    this->m_springPosition = this->m_startLocal;
    this->m_springVelocity = { 0.0f, 0.0f, 0.0f };
    this->m_facingSpring[0] = this->m_smoothFacing;
    this->m_facingSpring[1] = 0.0f;

    this->UpdateFlags();

    if ((this->m_flags & 0x2) && Moving(this->m_state)) {
        this->m_flags |= 0x4;
    }

    this->m_flags |= 0x20;
}

// ref: FUN_0075af00
void CVehicleCamera_C::SetTransitionTime(uint32_t time, int32_t duration) {
    if (this->m_flags & 0x1) {
        return;
    }

    this->m_flags |= 0x1;
    this->m_transitionDuration = duration;

    if (this->m_state == 2 || this->m_state == 5) {
        this->m_stateDuration = duration;
        this->ApplyCameraBlend(time, duration);
    }
}

// ref: FUN_0075af40
// One frame: the free look follows the player while the seat owns it; the camera this one is relative
// to updates first; a wait gives way to its move and a move to its end; then the place and facing.
void CVehicleCamera_C::Update(uint32_t time) {
    if (this->m_state == 0 || time == this->m_lastTime) {
        return;
    }

    if ((this->m_flags & 0x10) && this->m_unit->GetGUID() == ClntObjMgrGetActivePlayer()) {
        auto camera = CGWorldFrame::GetActiveCamera();
        auto input = InputControlGetActive();

        if (camera && input && (input->m_unk04 & 0x2000001)) {
            camera->MouseLook(0.0f, 0.0f, nullptr);

            if (input->CanMouseSteer(this->m_unit)) {
                camera->SyncPlayerFacing();
            }
        }
    }

    auto relative = UnitPtr(this->m_relativeGUID, 0x23a);

    if (relative && relative->m_vehicleCamera) {
        relative->m_vehicleCamera->Update(time);
    }

    int32_t state = this->m_state;

    if (state != 0 && state != 3) {
        int32_t over = static_cast<int32_t>(time - this->m_stateDuration - this->m_stateStart);

        if (over >= 0) {
            this->m_stateStart = time;
            int32_t duration = (this->m_flags & 0x1) ? this->m_transitionDuration : 1000;

            switch (state) {
                case 1:
                case 4:
                    duration -= over;
                    this->m_state = 2;
                    this->m_stateDuration = duration;
                    this->ApplyCameraBlend(time, duration);
                    break;

                case 2:
                    this->m_state = 3;
                    this->m_stateDuration = 0;
                    this->ApplyCameraBlend(time, 0);
                    break;

                case 5:
                    this->m_state = 0;
                    this->ApplyCameraBlend(time, 0);
                    this->SetTarget(0, 0);
                    this->Detach();
                    UnitDestroyVehicleCamera(this->m_unit);
                    return;

                default:
                    break;
            }
        }
    }

    float blend = this->GetBlend(time);
    auto relativeObject = ObjectPtr(this->GetRelativeGUID(), 0x26c);
    auto target = UnitPtr(this->m_unitGUID, 0x26f);

    if (target) {
        this->UpdatePositionBlend(target, time, blend);
        this->UpdateFacing(target, time, blend, relativeObject);
    }

    this->m_flags &= ~0x20u;
    this->m_lastTime = time;
}

// ref: FUN_0075b480
void CVehicleCamera_C::SaveState(State& out) const {
    out.springPosition = this->m_springPosition;
    out.springVelocity = this->m_springVelocity;
    out.smoothFacing = this->m_smoothFacing;
    out.followPosition = this->m_followPosition;
    out.followVelocity = this->m_followVelocity;
    out.collideScale = this->m_collideScale;
    out.facingSpring = this->m_facingSpring[0];
}

// ref: FUN_0075b4f0
void CVehicleCamera_C::RestoreState(const State& state) {
    this->m_springPosition = state.springPosition;
    this->m_springVelocity = state.springVelocity;
    this->m_smoothFacing = state.smoothFacing;
    this->m_followPosition = state.followPosition;
    this->m_followVelocity = state.followVelocity;
    this->m_collideScale = state.collideScale;
    this->m_facingSpring[0] = state.facingSpring;
}

// ref: FUN_0075b610
CVehicleCamera_C::PointSet::PointSet(const PointSet& source) {
    this->count = source.count;
    memcpy(this->points, source.points, source.count * sizeof(C3Vector));
    memcpy(this->values, source.values, source.count * sizeof(float));
}

// ref: FUN_0075c6f0
int32_t CVehicleCamera_C::PointSet::AllPointsNear(const C3Vector& p) const {
    for (uint32_t i = 0; i < this->count; i++) {
        const C3Vector& point = this->points[i];

        // 1/1296 (0x00aa2cec)
        if (0.0007716049440205097f < (p.y - point.y) * (p.y - point.y) + (p.z - point.z) * (p.z - point.z) + (p.x - point.x) * (p.x - point.x)) {
            return 0;
        }
    }

    return 1;
}
