#include "db/rec/SkillCostsDataRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* SkillCostsDataRec::GetFilename() {
    return "DBFilesClient\\SkillCostsData.dbc";
}

uint32_t SkillCostsDataRec::GetNumColumns() {
    return SkillCostsDataRec::COLUMN_COUNT;
}

uint32_t SkillCostsDataRec::GetRowSize() {
    return 20;
}

bool SkillCostsDataRec::NeedIDAssigned() {
    return false;
}

int32_t SkillCostsDataRec::GetID() {
    return this->m_ID;
}

void SkillCostsDataRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SkillCostsDataRec::Read(SFile* f, const char* stringBuffer) {

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_skillCostsID, sizeof(this->m_skillCostsID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_cost[0], sizeof(this->m_cost[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_cost[1], sizeof(this->m_cost[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_cost[2], sizeof(this->m_cost[2]), nullptr, nullptr, nullptr)
    ) {
        return false;
    }



    return true;
}
