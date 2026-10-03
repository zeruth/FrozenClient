#include "db/rec/TotemCategoryRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* TotemCategoryRec::GetFilename() {
    return "DBFilesClient\\TotemCategory.dbc";
}

uint32_t TotemCategoryRec::GetNumColumns() {
    return TotemCategoryRec::COLUMN_COUNT;
}

uint32_t TotemCategoryRec::GetRowSize() {
    return 80;
}

bool TotemCategoryRec::NeedIDAssigned() {
    return false;
}

int32_t TotemCategoryRec::GetID() {
    return this->m_ID;
}

void TotemCategoryRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool TotemCategoryRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t nameOfs[17];
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, nameOfs, sizeof(nameOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_totemCategoryType, sizeof(this->m_totemCategoryType), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_totemCategoryMask, sizeof(this->m_totemCategoryMask), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_name = stringBuffer ? &stringBuffer[nameOfs[CURRENT_LANGUAGE]] : "";

    return true;
}
