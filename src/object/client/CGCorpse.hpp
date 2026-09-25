#ifndef OBJECT_CLIENT_CG_CORPSE_HPP
#define OBJECT_CLIENT_CG_CORPSE_HPP

#include "util/guid/Types.hpp"
#include <cstdint>

// 3.3.5a CORPSE_* update fields, in order from CORPSE_FIELD_OWNER; thirty dwords, the count
// TotalFields gives.
struct CGCorpseData {
    WOWGUID owner;
    WOWGUID party;
    int32_t displayID;
    int32_t items[19];
    uint32_t bytes1;
    uint32_t bytes2;
    uint32_t guild;
    uint32_t flags;
    uint32_t dynamicFlags;
    uint32_t pad;
};

class CGCorpse {
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
        CGCorpseData* m_corpse;
        uint32_t* m_corpseSaved;

        // Protected member functions
        CGCorpseData* Corpse() const;
};

#endif
