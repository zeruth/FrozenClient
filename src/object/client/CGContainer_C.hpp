#ifndef OBJECT_CLIENT_CG_CONTAINER_C_HPP
#define OBJECT_CLIENT_CG_CONTAINER_C_HPP

#include "object/client/CClientObjCreate.hpp"
#include "object/client/CGContainer.hpp"
#include "object/client/CGItem_C.hpp"
#include "object/client/CGBag_C.hpp"

class CGContainer_C : public CGItem_C, public CGContainer {
    public:
        // Virtual public member functions
        virtual ~CGContainer_C();
        void* Virtual024() override;                // 0x024, the same bag as GetBag
        CGBag_C* GetBag() override;                 // 0x028

        // +0x760: the container's own slots.
        CGBag_C m_bag;

        // Public member functions
        CGContainer_C(uint32_t time, CClientObjCreate& objCreate);
        void SetStorage(uint32_t* storage, uint32_t* saved);
};

#endif
