#include "db/rec/ResistancesRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* ResistancesRec::GetFilename() {
    return "DBFilesClient\\Resistances.dbc";
}

uint32_t ResistancesRec::GetNumColumns() {
    return ResistancesRec::COLUMN_COUNT;
}

uint32_t ResistancesRec::GetRowSize() {
    return 80;
}

bool ResistancesRec::NeedIDAssigned() {
    return false;
}

int32_t ResistancesRec::GetID() {
    return this->m_ID;
}

void ResistancesRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool ResistancesRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t nameOfs[17];
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_flags, sizeof(this->m_flags), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_fizzleSoundID, sizeof(this->m_fizzleSoundID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, nameOfs, sizeof(nameOfs), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_name = stringBuffer ? &stringBuffer[nameOfs[CURRENT_LANGUAGE]] : "";

    return true;
}
