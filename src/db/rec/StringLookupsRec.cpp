#include "db/rec/StringLookupsRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* StringLookupsRec::GetFilename() {
    return "DBFilesClient\\StringLookups.dbc";
}

uint32_t StringLookupsRec::GetNumColumns() {
    return StringLookupsRec::COLUMN_COUNT;
}

uint32_t StringLookupsRec::GetRowSize() {
    return 8;
}

bool StringLookupsRec::NeedIDAssigned() {
    return false;
}

int32_t StringLookupsRec::GetID() {
    return this->m_ID;
}

void StringLookupsRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool StringLookupsRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t stringOfs;
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &stringOfs, sizeof(stringOfs), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_string = stringBuffer ? &stringBuffer[stringOfs] : "";

    return true;
}
