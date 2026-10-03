#ifndef OBJECT_CLIENT_CG_GAME_OBJECT_HPP
#define OBJECT_CLIENT_CG_GAME_OBJECT_HPP

#include "util/guid/Types.hpp"
#include <cstdint>

// 3.3.5a GAMEOBJECT_* update fields, in order from GAMEOBJECT_CREATED_BY. Only the leading fields
// are needed to name the model; the rest are listed so the offsets stay honest.
struct CGGameObjectData {
    WOWGUID createdBy;              // +0x00
    int32_t displayID;              // +0x08
    uint32_t flags;                 // +0x0c
    float parentRotation[4];        // +0x10
    // +0x20, GAMEOBJECT_DYNAMIC: the dynamic flags, and how far through its animation the object
    // is (0..65535, 0xffff for none), which the client spends once used.
    uint16_t dynamicFlags;          // +0x20
    uint16_t animProgress;          // +0x22
    int32_t faction;                // +0x24
    int32_t level;                  // +0x28
    // +0x2c, GAMEOBJECT_BYTES_1.
    int8_t state;                   // +0x2c
    uint8_t type;                   // +0x2d
    uint8_t artKit;                 // +0x2e
    uint8_t animProgressByte;       // +0x2f
};

class CGGameObject {
    public:
        // Public member functions
        int32_t GetDisplayID() const;

        // The gameobject fields. Public for the same reason CGUnit::Unit() is: code outside the
        // class reads them, as the reference does inline (CGUnit_C::CanShowLootAnimation wants the
        // gameobject's type byte).
        CGGameObjectData* GameObject() const;

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

};

#endif
