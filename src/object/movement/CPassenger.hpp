#ifndef OBJECT_MOVEMENT_C_PASSENGER_HPP
#define OBJECT_MOVEMENT_C_PASSENGER_HPP

#include "util/GUID.hpp"
#include <tempest/Quaternion.hpp>
#include <tempest/Vector.hpp>

class CPassenger {
    public:
        // Public member functions
        // ref: FUN_004f48e0
        // `guid` is the owner's guid where the owner keeps it (its descriptor), not a copy: the
        // reference holds a pointer to it.
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

        // The packed rotation, turned with the transport's when there is one.
        C4Quaternion GetRotation() const;
        // The heading the rotation gives, about Z.
        float GetRotationFacing() const;
        // Join the transport; when it is not there the passenger stays where it is in the world,
        // at `worldPosition`, and on no transport.
        void JoinTransport(const C3Vector& worldPosition);
        // Leave the transport, which keeps the passenger's place in its list.
        void LeaveTransport();

        // Public member variables. CGGameObject_C's own slots read these directly, as the
        // reference does.
        // TODO +0x00: the passenger's link in its transport's list.
        WOWGUID m_transportGUID;            // +0x08
        C3Vector m_position;                // +0x10
        float m_facing;                     // +0x1c
        // +0x20: the rotation as the object's create block packs it (UPDATEFLAG_ROTATION);
        // only game objects carry one.
        uint64_t m_packedRotation = 0;
        // ref +0x24. PHASE4(MovementShared): its writers are the movement port's.
        float m_pitch = 0.0f;
        const WOWGUID& m_guid;              // +0x28
        // TODO +0x2c
};

#endif
