#include "db/rec/SpellRangeRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* SpellRangeRec::GetFilename() {
    return "DBFilesClient\\SpellRange.dbc";
}

uint32_t SpellRangeRec::GetNumColumns() {
    return SpellRangeRec::COLUMN_COUNT;
}

uint32_t SpellRangeRec::GetRowSize() {
    return 160;
}

bool SpellRangeRec::NeedIDAssigned() {
    return false;
}

int32_t SpellRangeRec::GetID() {
    return this->m_ID;
}

void SpellRangeRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SpellRangeRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t displayNameOfs[17];
    uint32_t displayNameShortOfs[17];
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_rangeMin[0], sizeof(this->m_rangeMin[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_rangeMin[1], sizeof(this->m_rangeMin[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_rangeMax[0], sizeof(this->m_rangeMax[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_rangeMax[1], sizeof(this->m_rangeMax[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_flags, sizeof(this->m_flags), nullptr, nullptr, nullptr)
        || !SFile::Read(f, displayNameOfs, sizeof(displayNameOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, displayNameShortOfs, sizeof(displayNameShortOfs), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_displayName = stringBuffer ? &stringBuffer[displayNameOfs[CURRENT_LANGUAGE]] : "";
    this->m_displayNameShort = stringBuffer ? &stringBuffer[displayNameShortOfs[CURRENT_LANGUAGE]] : "";

    return true;
}
