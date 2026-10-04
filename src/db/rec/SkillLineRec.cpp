#include "db/rec/SkillLineRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* SkillLineRec::GetFilename() {
    return "DBFilesClient\\SkillLine.dbc";
}

uint32_t SkillLineRec::GetNumColumns() {
    return SkillLineRec::COLUMN_COUNT;
}

uint32_t SkillLineRec::GetRowSize() {
    return 224;
}

bool SkillLineRec::NeedIDAssigned() {
    return false;
}

int32_t SkillLineRec::GetID() {
    return this->m_ID;
}

void SkillLineRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SkillLineRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t displayNameOfs[17];
    uint32_t descriptionOfs[17];
    uint32_t alternateVerbOfs[17];
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_categoryID, sizeof(this->m_categoryID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_skillCostsID, sizeof(this->m_skillCostsID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, displayNameOfs, sizeof(displayNameOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, descriptionOfs, sizeof(descriptionOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_spellIconID, sizeof(this->m_spellIconID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, alternateVerbOfs, sizeof(alternateVerbOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_canLink, sizeof(this->m_canLink), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_displayName = stringBuffer ? &stringBuffer[displayNameOfs[CURRENT_LANGUAGE]] : "";
    this->m_description = stringBuffer ? &stringBuffer[descriptionOfs[CURRENT_LANGUAGE]] : "";
    this->m_alternateVerb = stringBuffer ? &stringBuffer[alternateVerbOfs[CURRENT_LANGUAGE]] : "";

    return true;
}
