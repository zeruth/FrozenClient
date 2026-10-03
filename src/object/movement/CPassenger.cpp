#include "object/client/CMovement_C.hpp"
#include "object/movement/CPassenger.hpp"
#include <tempest/Matrix.hpp>
#include <cmath>

float NormalizeAngle(float angle);

float CPassenger::GetFacing() const {
    return this->GetFacing(this->m_facing);
}

// ref: FUN_004f42a0
float CPassenger::GetFacing(float facing) const {
    if (!this->m_transportGUID) {
        return facing;
    }

    return NormalizeAngle(MovementGetTransportFacing(this->m_transportGUID) + facing);
}

C3Vector CPassenger::GetPosition() const {
    return this->GetPosition(this->m_position);
}

// ref: FUN_004f4460
C3Vector CPassenger::GetPosition(const C3Vector& position) const {
    if (!this->m_transportGUID) {
        return position;
    }

    C44Matrix transportMatrix;
    MovementGetTransportMatrixChecked(this->m_transportGUID, transportMatrix, this->m_guid, ".\\Passenger.cpp", 0);

    return position * transportMatrix;
}

float CPassenger::GetRawFacing() const {
    return this->m_facing;
}

WOWGUID CPassenger::GetTransportGUID() const {
    return this->m_transportGUID;
}

// ref: FUN_004f45b0
C4Quaternion CPassenger::GetRotation() const {
    C4Quaternion rotation(this->m_packedRotation);

    if (!this->m_transportGUID) {
        return rotation;
    }

    return rotation * MovementGetTransportRotation(this->m_transportGUID);
}

// ref: FUN_004f4630
float CPassenger::GetRotationFacing() const {
    C4Quaternion q = this->GetRotation();

    float a = 1.0f - (q.y * q.y + q.z * q.z) * 2.0f;
    float b = (q.y * q.x + q.w * q.z) * 2.0f;

    if (std::fabs(a) >= 2.384185791015625e-07f) {
        if (std::fabs(b) >= 2.384185791015625e-07f) {
            return std::atan2(b, a);
        }

        return a <= 0.0f ? 3.1415927f : 0.0f;
    }

    return b < 0.0f ? 1.5f * 3.1415927f : 0.5f * 3.1415927f;
}

// ref: FUN_004f4230
void CPassenger::JoinTransport(const C3Vector& worldPosition) {
    if (!this->m_transportGUID) {
        return;
    }

    if (!MovementNotifyTransport(this, this->m_transportGUID, 1)) {
        this->m_transportGUID = 0;
        this->m_position = worldPosition;
    }
}

// ref: FUN_004f4280
void CPassenger::LeaveTransport() {
    if (this->m_transportGUID) {
        MovementNotifyTransport(this, this->m_transportGUID, 2);
    }
}
