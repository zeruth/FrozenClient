#ifndef OBJECT_CLIENT_CG_OBJECT_HPP
#define OBJECT_CLIENT_CG_OBJECT_HPP

#include "object/Types.hpp"
#include "util/GUID.hpp"
#include <cstdint>

struct CGObjectData {
    WOWGUID m_guid;
    OBJECT_TYPE m_type;
    int32_t m_entryID;
    float m_scale;
    uint32_t pad;
};

class CGObject {
    public:
        // Public static functions
        static uint32_t GetBaseOffset();
        static uint32_t GetBaseOffsetSaved();
        static uint32_t GetDataSize();
        static uint32_t GetDataSizeSaved();
        static uint32_t TotalFields();
        static uint32_t TotalFieldsSaved();

        // Public member variables
        uint32_t uint0; // TODO what is this?
        uint32_t m_memHandle;

        // Public member functions
        WOWGUID GetGUID() const;
        OBJECT_TYPE GetType() const;
        OBJECT_TYPE_ID GetTypeID() const;
        int32_t IsA(OBJECT_TYPE type) const;
        int32_t IsExactlyA(OBJECT_TYPE_ID typeID) const;
        float GetScale() const;
        int32_t GetEntryID() const;

    // The storage and its saved copy are read directly by the mirror (Mirror.cpp, the reference's
    // ObjectMgrClient), as the reference reads them; it has no access control.
    public:
        CGObjectData* m_obj;
        uint32_t* m_objSaved;
        OBJECT_TYPE_ID m_typeID;
        // The watchers registered on this object alone (the reference's lists at +0x44 onwards,
        // FUN_004d3d40). Owned by Mirror.cpp, which frees them when the object goes.
        void* m_mirrorHandlers = nullptr;

    protected:
        // Protected member functions
        CGObjectData* Obj() const;
};

#endif
