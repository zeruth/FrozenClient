#ifndef OBJECT_CLIENT_CG_GAME_OBJECT_HPP
#define OBJECT_CLIENT_CG_GAME_OBJECT_HPP

#include "util/guid/Types.hpp"
#include <cstdint>

// 3.3.5a GAMEOBJECT_* update fields, in order from GAMEOBJECT_CREATED_BY. Only the leading fields
// are needed to name the model; the rest are listed so the offsets stay honest.
struct CGGameObjectData {
    WOWGUID createdBy;
    int32_t displayID;
    int32_t flags;
    float parentRotation[4];
    int32_t dynamic;
    int32_t faction;
    int32_t level;
    int32_t bytes1;
};

class CGGameObject {
    public:
        // Public member functions
        int32_t GetDisplayID() const;

        // Public static functions
        static uint32_t GetBaseOffset();
        static uint32_t GetBaseOffsetSaved();
        static uint32_t GetDataSize();
        static uint32_t GetDataSizeSaved();
        static uint32_t TotalFields();
        static uint32_t TotalFieldsSaved();

    protected:
        // Protected member variables
        CGGameObjectData* m_gameObj;
        uint32_t* m_gameObjSaved;

        // Protected member functions
        CGGameObjectData* GameObject() const;
};

#endif
