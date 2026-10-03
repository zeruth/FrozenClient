#include "object/client/CMovement_C.hpp"
#include "object/movement/CPassenger.hpp"
#include <tempest/Math.hpp>
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
        MovementNotifyTransport(this, this->m_transportGUID, 1);
    }
}

// ref: FUN_004f43b0
void CPassenger::SetPackedRotation(const C4Quaternion& rotation) {
    int32_t sign = 0.0f <= rotation.w ? 1 : -1;
    int32_t y = CMath::fint(rotation.y * 1048576.0f) * sign;
    int32_t x = CMath::fint(rotation.x * 2097152.0f) * sign;
    int32_t z = CMath::fint(rotation.z * 1048576.0f) * sign;

    uint32_t high = ((static_cast<uint32_t>(x) >> 11) << 21) | ((static_cast<uint32_t>(y) & 0x1fffffu) | (static_cast<uint32_t>(x) << 21)) >> 11;
    uint32_t low = (static_cast<uint32_t>(y) << 21) | (static_cast<uint32_t>(z) & 0x1fffffu);

    this->m_packedRotation = (static_cast<uint64_t>(high) << 32) | low;
}

// ref: FUN_004f4930
float CPassenger::TransformToWorld(C44Matrix& matrix) {
    MovementGetTransportMatrixChecked(this->m_transportGUID, matrix, this->m_guid, ".\\Passenger.cpp", 0x8c);

    return this->TransformFromTransport(matrix);
}

// ref: FUN_004f46e0
float CPassenger::TransformFromTransport(const C44Matrix& matrix) {
    float facing = MovementGetTransportFacing(this->m_transportGUID);

    C3Vector out;
    TransformPointInPlace(out, this->m_position, matrix);

    if (this->m_passengerFlags & 0x2) {
        C4Quaternion rotation(this->m_packedRotation);
        this->SetPackedRotation(rotation * MovementGetTransportRotation(this->m_transportGUID));

        return facing;
    }

    this->m_facing = NormalizeAngle(this->m_facing + facing);

    return facing;
}

// ref: FUN_004f47b0
float CPassenger::TransformToTransport(WOWGUID transport, C44Matrix& inverse, C44Matrix* matrix) {
    float facing = -MovementGetTransportFacing(transport);

    if (matrix) {
        MovementGetTransportMatrixChecked(transport, *matrix, this->m_guid, ".\\Passenger.cpp", 0xa5);
        inverse = matrix->AffineInverse();
    } else {
        MovementGetTransportMatrixChecked(transport, inverse, this->m_guid, ".\\Passenger.cpp", 0xa9);
        inverse = inverse.AffineInverse();
    }

    C3Vector out;
    TransformPointInPlace(out, this->m_position, inverse);

    if (this->m_passengerFlags & 0x2) {
        C4Quaternion turn(inverse);
        C4Quaternion rotation(this->m_packedRotation);
        this->SetPackedRotation(rotation * turn);

        return facing;
    }

    this->m_facing = NormalizeAngle(this->m_facing + facing);

    return facing;
}

// ref: FUN_004f4970
void CPassenger::ChangeTransport(WOWGUID transport) {
    if (transport == this->m_transportGUID) {
        return;
    }

    if (transport && !MovementTransportIsValid(transport)) {
        return;
    }

    C44Matrix matrix;

    if (this->m_transportGUID) {
        this->TransformToWorld(matrix);
    }

    if (transport) {
        this->TransformToTransport(transport, matrix, nullptr);
    }

    this->m_transportGUID = transport;
}
