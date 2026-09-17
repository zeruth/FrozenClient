#include "db/rec/SkillLineRec.hpp"
#include "util/SFile.hpp"

const char* SkillLineRec::GetFilename() {
    return "DBFilesClient\\SkillLine.dbc";
}

uint32_t SkillLineRec::GetNumColumns() {
    return SkillLineRec::COLUMN_COUNT;
}

uint32_t SkillLineRec::GetRowSize() {
    return SkillLineRec::COLUMN_COUNT * 4;
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
    uint32_t columns[SkillLineRec::COLUMN_COUNT];

    if (!SFile::Read(f, columns, sizeof(columns), nullptr, nullptr, nullptr)) {
        return false;
    }

    this->m_ID = static_cast<int32_t>(columns[0]);
    this->m_categoryID = static_cast<int32_t>(columns[SkillLineRec::COLUMN_CATEGORY]);
    this->m_displayName = stringBuffer ? &stringBuffer[columns[SkillLineRec::COLUMN_DISPLAY_NAME]] : "";
    this->m_spellIconID = static_cast<int32_t>(columns[SkillLineRec::COLUMN_ICON]);

    return true;
}
