#include "db/rec/ItemPurchaseGroupRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* ItemPurchaseGroupRec::GetFilename() {
    return "DBFilesClient\\ItemPurchaseGroup.dbc";
}

uint32_t ItemPurchaseGroupRec::GetNumColumns() {
    return ItemPurchaseGroupRec::COLUMN_COUNT;
}

uint32_t ItemPurchaseGroupRec::GetRowSize() {
    return 104;
}

bool ItemPurchaseGroupRec::NeedIDAssigned() {
    return false;
}

int32_t ItemPurchaseGroupRec::GetID() {
    return this->m_ID;
}

void ItemPurchaseGroupRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool ItemPurchaseGroupRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t nameOfs[17];
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[0], sizeof(this->m_itemID[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[1], sizeof(this->m_itemID[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[2], sizeof(this->m_itemID[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[3], sizeof(this->m_itemID[3]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[4], sizeof(this->m_itemID[4]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[5], sizeof(this->m_itemID[5]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[6], sizeof(this->m_itemID[6]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[7], sizeof(this->m_itemID[7]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, nameOfs, sizeof(nameOfs), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_name = stringBuffer ? &stringBuffer[nameOfs[CURRENT_LANGUAGE]] : "";

    return true;
}
