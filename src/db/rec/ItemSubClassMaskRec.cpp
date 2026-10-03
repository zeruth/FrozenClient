#include "db/rec/ItemSubClassMaskRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* ItemSubClassMaskRec::GetFilename() {
    return "DBFilesClient\\ItemSubClassMask.dbc";
}

uint32_t ItemSubClassMaskRec::GetNumColumns() {
    return ItemSubClassMaskRec::COLUMN_COUNT;
}

uint32_t ItemSubClassMaskRec::GetRowSize() {
    return 76;
}

bool ItemSubClassMaskRec::NeedIDAssigned() {
    return true;
}

int32_t ItemSubClassMaskRec::GetID() {
    return this->m_ID;
}

void ItemSubClassMaskRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool ItemSubClassMaskRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t nameOfs[17];
    if (
        !SFile::Read(f, &this->m_classID, sizeof(this->m_classID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_mask, sizeof(this->m_mask), nullptr, nullptr, nullptr)
        || !SFile::Read(f, nameOfs, sizeof(nameOfs), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_name = stringBuffer ? &stringBuffer[nameOfs[CURRENT_LANGUAGE]] : "";

    return true;
}
