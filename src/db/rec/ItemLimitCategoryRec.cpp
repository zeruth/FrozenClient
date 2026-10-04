#include "db/rec/ItemLimitCategoryRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* ItemLimitCategoryRec::GetFilename() {
    return "DBFilesClient\\ItemLimitCategory.dbc";
}

uint32_t ItemLimitCategoryRec::GetNumColumns() {
    return ItemLimitCategoryRec::COLUMN_COUNT;
}

uint32_t ItemLimitCategoryRec::GetRowSize() {
    return 80;
}

bool ItemLimitCategoryRec::NeedIDAssigned() {
    return false;
}

int32_t ItemLimitCategoryRec::GetID() {
    return this->m_ID;
}

void ItemLimitCategoryRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool ItemLimitCategoryRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t nameOfs[17];
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, nameOfs, sizeof(nameOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_quantity, sizeof(this->m_quantity), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_flags, sizeof(this->m_flags), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_name = stringBuffer ? &stringBuffer[nameOfs[CURRENT_LANGUAGE]] : "";

    return true;
}
