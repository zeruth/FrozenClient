#include "db/rec/HolidayNamesRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* HolidayNamesRec::GetFilename() {
    return "DBFilesClient\\HolidayNames.dbc";
}

uint32_t HolidayNamesRec::GetNumColumns() {
    return HolidayNamesRec::COLUMN_COUNT;
}

uint32_t HolidayNamesRec::GetRowSize() {
    return 72;
}

bool HolidayNamesRec::NeedIDAssigned() {
    return false;
}

int32_t HolidayNamesRec::GetID() {
    return this->m_ID;
}

void HolidayNamesRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool HolidayNamesRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t nameOfs[17];
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, nameOfs, sizeof(nameOfs), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_name = stringBuffer ? &stringBuffer[nameOfs[CURRENT_LANGUAGE]] : "";

    return true;
}
