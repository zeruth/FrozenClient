#ifndef OBJECT_CLIENT_CG_ITEM_C_HPP
#define OBJECT_CLIENT_CG_ITEM_C_HPP

#include "object/client/CClientObjCreate.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/CGItem.hpp"

class CGItem_C : public CGObject_C, public CGItem {
    public:
        // Virtual public member functions
        virtual ~CGItem_C();

        // Public member functions
        CGItem_C(uint32_t time, CClientObjCreate& objCreate);
        void PostInit(uint32_t time, const CClientObjCreate& init, bool a4);
        void SetStorage(uint32_t* storage, uint32_t* saved);

        // Fields of this item's Item.dbc record, keyed by its entry id; 0 when there is none.
        int32_t GetClassID() const;
        int32_t GetSubclassID() const;
        int32_t GetDisplayInfoID() const;
        int32_t GetInventoryType() const;
};

#endif
