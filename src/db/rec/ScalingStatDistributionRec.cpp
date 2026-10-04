#include "db/rec/ScalingStatDistributionRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* ScalingStatDistributionRec::GetFilename() {
    return "DBFilesClient\\ScalingStatDistribution.dbc";
}

uint32_t ScalingStatDistributionRec::GetNumColumns() {
    return ScalingStatDistributionRec::COLUMN_COUNT;
}

uint32_t ScalingStatDistributionRec::GetRowSize() {
    return 88;
}

bool ScalingStatDistributionRec::NeedIDAssigned() {
    return false;
}

int32_t ScalingStatDistributionRec::GetID() {
    return this->m_ID;
}

void ScalingStatDistributionRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool ScalingStatDistributionRec::Read(SFile* f, const char* stringBuffer) {

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_statID[0], sizeof(this->m_statID[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_statID[1], sizeof(this->m_statID[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_statID[2], sizeof(this->m_statID[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_statID[3], sizeof(this->m_statID[3]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_statID[4], sizeof(this->m_statID[4]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_statID[5], sizeof(this->m_statID[5]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_statID[6], sizeof(this->m_statID[6]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_statID[7], sizeof(this->m_statID[7]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_statID[8], sizeof(this->m_statID[8]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_statID[9], sizeof(this->m_statID[9]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_bonus[0], sizeof(this->m_bonus[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_bonus[1], sizeof(this->m_bonus[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_bonus[2], sizeof(this->m_bonus[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_bonus[3], sizeof(this->m_bonus[3]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_bonus[4], sizeof(this->m_bonus[4]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_bonus[5], sizeof(this->m_bonus[5]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_bonus[6], sizeof(this->m_bonus[6]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_bonus[7], sizeof(this->m_bonus[7]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_bonus[8], sizeof(this->m_bonus[8]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_bonus[9], sizeof(this->m_bonus[9]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_maxLevel, sizeof(this->m_maxLevel), nullptr, nullptr, nullptr)
    ) {
        return false;
    }



    return true;
}
