#include "db/rec/SkillLineAbilityRec.hpp"
#include "util/SFile.hpp"

const char* SkillLineAbilityRec::GetFilename() {
    return "DBFilesClient\\SkillLineAbility.dbc";
}

uint32_t SkillLineAbilityRec::GetNumColumns() {
    return SkillLineAbilityRec::COLUMN_COUNT;
}

uint32_t SkillLineAbilityRec::GetRowSize() {
    return SkillLineAbilityRec::COLUMN_COUNT * 4;
}

bool SkillLineAbilityRec::NeedIDAssigned() {
    return false;
}

int32_t SkillLineAbilityRec::GetID() {
    return this->m_ID;
}

void SkillLineAbilityRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SkillLineAbilityRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t columns[SkillLineAbilityRec::COLUMN_COUNT];

    if (!SFile::Read(f, columns, sizeof(columns), nullptr, nullptr, nullptr)) {
        return false;
    }

    this->m_ID = static_cast<int32_t>(columns[0]);
    this->m_skillLine = static_cast<int32_t>(columns[1]);
    this->m_spell = static_cast<int32_t>(columns[2]);
    this->m_raceMask = static_cast<int32_t>(columns[3]);
    this->m_classMask = static_cast<int32_t>(columns[4]);

    return true;
}
