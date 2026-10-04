#ifndef OBJECT_CLIENT_CG_DYNAMIC_OBJECT_HPP
#define OBJECT_CLIENT_CG_DYNAMIC_OBJECT_HPP

#include "util/guid/Types.hpp"
#include <cstdint>

// 3.3.5a DYNAMICOBJECT_* update fields, in order from DYNAMICOBJECT_CASTER; six dwords.
struct CGDynamicObjectData {
    WOWGUID caster;
    // DYNAMICOBJECT_BYTES: the low byte is the kind (1 a portal, 2 a farsight area).
    uint32_t bytes;
    int32_t spellID;
    float radius;
    uint32_t castTime;
};

class CGDynamicObject {
    public:
        // Public static functions
        static uint32_t GetBaseOffset();
        static uint32_t GetBaseOffsetSaved();
        static uint32_t GetDataSize();
        static uint32_t GetDataSizeSaved();
        static uint32_t TotalFields();
        static uint32_t TotalFieldsSaved();

    protected:
        // Protected member variables
        CGDynamicObjectData* m_dynamicObj;
        uint32_t* m_dynamicObjSaved;

        // Protected member functions
        CGDynamicObjectData* DynamicObject() const;
};

#endif
