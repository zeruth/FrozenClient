#include "object/client/CMovement_C.hpp"
#include "object/movement/CPassenger.hpp"
#include <tempest/Matrix.hpp>

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
