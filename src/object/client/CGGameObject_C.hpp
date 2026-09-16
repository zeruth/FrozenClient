#ifndef OBJECT_CLIENT_CG_GAME_OBJECT_C_HPP
#define OBJECT_CLIENT_CG_GAME_OBJECT_C_HPP

#include "object/client/CClientObjCreate.hpp"
#include "object/client/CGGameObject.hpp"
#include "object/client/CGObject_C.hpp"

class CGGameObject_C : public CGObject_C, public CGGameObject {
    public:
        // Virtual public member functions
        virtual ~CGGameObject_C();
        virtual int32_t GetModelFileName(const char*& name) const;

        // A game object does not move, so it has no CMovement to ask: its position arrives once, in
        // the create block's stationary-position field (UPDATEFLAG_STATIONARY_POSITION, 0x40), and
        // is kept here. Without these the base CGObject_C stubs answer (0,0,0) and facing 0, every
        // game object is culled as being at the map origin, and none of them ever draw.
        virtual C3Vector GetPosition() const;
        virtual float GetFacing() const;

        C3Vector m_position = { 0.0f, 0.0f, 0.0f };
        float m_facing = 0.0f;

        // Public member functions
        CGGameObject_C(uint32_t time, CClientObjCreate& objCreate);
        void PostInit(uint32_t time, const CClientObjCreate& init, bool a4);
        void SetStorage(uint32_t* storage, uint32_t* saved);
};

#endif
