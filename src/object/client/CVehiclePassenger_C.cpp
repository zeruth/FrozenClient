#include "object/client/CVehiclePassenger_C.hpp"
#include "db/rec/VehicleSeatRec.hpp"
#include "object/client/CGUnit_C.hpp"

// ref: FUN_007487e0
bool CVehiclePassenger_C::IsRidingLiveVehicle() const {
    if (!(this->m_flags & 0x800)) {
        return false;
    }

    return this->m_vehicle && this->m_vehicle->Unit()->health > 0;
}

// ref: FUN_00747b20
int32_t CVehiclePassenger_C::GetSeatAnimation(const VehicleSeatRec* seat) const {
    if (!seat) {
        return 0x1FA;
    }

    // m_flags 0x2 means the start animation has already played, so the loop is what is wanted.
    switch (this->m_state) {
        case 1:
        case 2:
            if (seat->m_flags & 0x1) {
                if (!(this->m_flags & 0x2) && seat->m_enterAnimStart != -1) {
                    return seat->m_enterAnimStart;
                }

                if (seat->m_enterAnimLoop != -1) {
                    return seat->m_enterAnimLoop;
                }
            }

            break;

        case 3:
            if (seat->m_flags & 0x2) {
                if (!(this->m_flags & 0x2) && seat->m_rideAnimStart != -1) {
                    return seat->m_rideAnimStart;
                }

                if (seat->m_rideAnimLoop != -1) {
                    return seat->m_rideAnimLoop;
                }
            }

            break;

        case 4:
        case 5:
            if (this->m_vehicle && this->m_vehicle->SeatAllowsExitAnimation(seat)) {
                if (!(this->m_flags & 0x2) && seat->m_exitAnimStart != -1) {
                    return seat->m_exitAnimStart;
                }

                if (seat->m_exitAnimLoop != -1) {
                    return seat->m_exitAnimLoop;
                }
            }

            break;

        default:
            break;
    }

    return 0x1FA;
}

// ref: FUN_00747bd0
int32_t CVehiclePassenger_C::GetSeatUpperAnimation(const VehicleSeatRec* seat) const {
    if (!seat || this->m_state != 3 || !(seat->m_flags & 0x4)) {
        return 0x1FA;
    }

    // m_flags 0x4 here, not 0x2: the upper body has its own "start has played" bit.
    if (!(this->m_flags & 0x4) && seat->m_rideUpperAnimStart != -1) {
        return seat->m_rideUpperAnimStart;
    }

    if (seat->m_rideUpperAnimLoop != -1) {
        return seat->m_rideUpperAnimLoop;
    }

    return 0x1FA;
}

// ref: FUN_007484e0
// A sequence ended on the rider's model: while the seat drives an upper-body animation, the bone
// it ended on says which start has now played (0x4 the upper body, 0x2 the body); otherwise both
// are marked played.
void CVehiclePassenger_C::OnRiderSequenceDone(uint32_t boneId) {
    auto seat = this->m_seat;

    if (this->GetSeatAnimation(seat) != 0x1FA && seat && this->m_state == 3 && (seat->m_flags & 0x4)) {
        int32_t upper = -1;

        if (!(this->m_flags & 0x4)) {
            upper = seat->m_rideUpperAnimStart;
        }

        if (upper == -1) {
            upper = seat->m_rideUpperAnimLoop;
        }

        if (upper != -1 && upper != 0x1FA) {
            if (boneId != 0xFFFFFFFF && boneId != 0x1A) {
                this->m_flags |= 0x4;
            } else {
                this->m_flags |= 0x2;
            }

            return;
        }
    }

    this->m_flags |= 0x6;
}

// ref: FUN_00748560
bool CVehiclePassenger_C::GetRideAnimation(uint32_t allow, int32_t* out) const {
    if (((this->m_state != 0 && this->m_state != 3)
         || (this->m_vehicle && this->m_vehicle->Unit()->health < 1))
        && this->m_seat) {
        int32_t anim = this->GetSeatAnimation(this->m_seat);

        if (anim != 0x1FA) {
            *out = anim;

            return true;
        }
    }

    return false;
}

// ref: FUN_007485b0
bool CVehiclePassenger_C::GetRideUpperAnimation(uint32_t allow, int32_t* out) const {
    if (this->m_state != 3 && (!this->m_vehicle || this->m_vehicle->Unit()->health >= 1)) {
        return false;
    }

    auto seat = this->m_seat;

    if (!seat || this->m_state != 3 || !(seat->m_flags & 0x4)) {
        return false;
    }

    int32_t anim;

    if (!(this->m_flags & 0x4) && seat->m_rideUpperAnimStart != -1) {
        anim = seat->m_rideUpperAnimStart;
    } else {
        anim = seat->m_rideUpperAnimLoop;

        if (anim == -1) {
            return false;
        }
    }

    if (anim == 0x1FA) {
        return false;
    }

    *out = anim;

    return true;
}
