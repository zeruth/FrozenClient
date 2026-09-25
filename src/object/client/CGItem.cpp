#include "object/client/CGItem.hpp"
#include "object/client/CGObject.hpp"

uint32_t CGItem::GetBaseOffset() {
    return CGObject::TotalFields();
}

uint32_t CGItem::GetBaseOffsetSaved() {
    return CGObject::TotalFieldsSaved();
}

uint32_t CGItem::GetDataSize() {
    return CGItem::TotalFields() * sizeof(uint32_t);
}

uint32_t CGItem::GetDataSizeSaved() {
    return CGItem::TotalFieldsSaved() * sizeof(uint32_t);
}

uint32_t CGItem::TotalFields() {
    return CGItem::GetBaseOffset() + 58;
}

uint32_t CGItem::TotalFieldsSaved() {
    return CGItem::GetBaseOffsetSaved() + 47;
}

CGItemData* CGItem::Item() const {
    return this->m_item;
}

// ref: FUN_00584ae0
int32_t CGItem::GetMaxDurability() const {
    if (this->m_item->flags >> 3 & 1) {
        return 0;
    }

    return this->m_item->maxDurability;
}
