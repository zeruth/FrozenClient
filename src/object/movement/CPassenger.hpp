#ifndef OBJECT_MOVEMENT_C_PASSENGER_HPP
#define OBJECT_MOVEMENT_C_PASSENGER_HPP

#include "util/GUID.hpp"
#include <tempest/Vector.hpp>

class CPassenger {
    public:
        // Public member functions
        CPassenger(const WOWGUID& transportGUID, const C3Vector& position, const WOWGUID& guid)
            : m_transportGUID(transportGUID)
            , m_position(position)
            , m_facing(0.0f)
            , m_guid(guid) {};
        float GetFacing() const;
        float GetFacing(float facing) const;
        C3Vector GetPosition() const;
        C3Vector GetPosition(const C3Vector& position) const;
        float GetRawFacing() const;
        float GetPitch() const { return this->m_pitch; }
        WOWGUID GetTransportGUID() const;

    protected:
        // Protected member variables
        // TODO
        WOWGUID m_transportGUID;
        C3Vector m_position;
        // TODO
        float m_facing;
        // ref +0x24. PHASE4(MovementShared): its writers are the movement port's.
        float m_pitch = 0.0f;
        // TODO
        const WOWGUID& m_guid;
        // TODO
};

#endif
