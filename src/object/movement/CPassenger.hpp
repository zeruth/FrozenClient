#ifndef OBJECT_MOVEMENT_C_PASSENGER_HPP
#define OBJECT_MOVEMENT_C_PASSENGER_HPP

#include "util/GUID.hpp"
#include <storm/List.hpp>
#include <tempest/Matrix.hpp>
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
        // Tell the transport again that the passenger is aboard (mode 1, as joining is): the
        // transport re-threads it onto its list. The reference's name for it is lost; the
        // object's reenable calls it.
        void LeaveTransport();
        // ref: FUN_004f43b0
        // Store `rotation` packed, as the create block carries it: x scaled by 2^21, y and z by
        // 2^20, the sign of w folded in.
        void SetPackedRotation(const C4Quaternion& rotation);
        // ref: FUN_004f4930
        // Out of the transport's space into the world's: `matrix` gets the transport's matrix;
        // returns the transport's facing, which the passenger's facing took on.
        float TransformToWorld(C44Matrix& matrix);
        // ref: FUN_004f46e0
        float TransformFromTransport(const C44Matrix& matrix);
        // ref: FUN_004f47b0
        // Into `transport`'s space: `inverse` gets the inverse of its matrix (and `matrix`, when
        // given, the matrix); returns the facing change, which is minus the transport's facing.
        float TransformToTransport(WOWGUID transport, C44Matrix& inverse, C44Matrix* matrix);
        // ref: FUN_004f4970
        // Move to another transport (or none), keeping the passenger where it is in the world.
        void ChangeTransport(WOWGUID transport);

        // Public member variables. CGGameObject_C's own slots read these directly, as the
        // reference does.
        // +0x00: the passenger's link in its transport's list (CGGameObjectTransportBase).
        TSLink<CPassenger> m_transportLink;
        WOWGUID m_transportGUID;            // +0x08
        C3Vector m_position;                // +0x10
        // +0x20 in the reference (the constructor FUN_004f48e0 zeroes +0x20 and +0x24); for a
        // passenger with flag 0x2 the packed rotation below occupies the same eight bytes.
        float m_facing;
        // The rotation as the object's create block packs it (UPDATEFLAG_ROTATION); only game
        // objects carry one, and they set m_passengerFlags 0x2.
        uint64_t m_packedRotation = 0;
        // ref +0x24. PHASE4(MovementShared): its writers are the movement port's.
        float m_pitch = 0.0f;
        const WOWGUID& m_guid;              // +0x28
        // +0x2c: bit 0, the passenger is a CMovementShared (it moves on its own); set by its
        // constructor.
        uint32_t m_passengerFlags = 0;
};

#endif
