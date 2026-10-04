#include "db/rec/DurabilityCostsRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* DurabilityCostsRec::GetFilename() {
    return "DBFilesClient\\DurabilityCosts.dbc";
}

uint32_t DurabilityCostsRec::GetNumColumns() {
    return DurabilityCostsRec::COLUMN_COUNT;
}

uint32_t DurabilityCostsRec::GetRowSize() {
    return 120;
}

bool DurabilityCostsRec::NeedIDAssigned() {
    return false;
}

int32_t DurabilityCostsRec::GetID() {
    return this->m_ID;
}

void DurabilityCostsRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool DurabilityCostsRec::Read(SFile* f, const char* stringBuffer) {

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[0], sizeof(this->m_weaponSubClassCost[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[1], sizeof(this->m_weaponSubClassCost[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[2], sizeof(this->m_weaponSubClassCost[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[3], sizeof(this->m_weaponSubClassCost[3]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[4], sizeof(this->m_weaponSubClassCost[4]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[5], sizeof(this->m_weaponSubClassCost[5]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[6], sizeof(this->m_weaponSubClassCost[6]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[7], sizeof(this->m_weaponSubClassCost[7]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[8], sizeof(this->m_weaponSubClassCost[8]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[9], sizeof(this->m_weaponSubClassCost[9]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[10], sizeof(this->m_weaponSubClassCost[10]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[11], sizeof(this->m_weaponSubClassCost[11]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[12], sizeof(this->m_weaponSubClassCost[12]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[13], sizeof(this->m_weaponSubClassCost[13]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[14], sizeof(this->m_weaponSubClassCost[14]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[15], sizeof(this->m_weaponSubClassCost[15]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[16], sizeof(this->m_weaponSubClassCost[16]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[17], sizeof(this->m_weaponSubClassCost[17]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[18], sizeof(this->m_weaponSubClassCost[18]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[19], sizeof(this->m_weaponSubClassCost[19]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassCost[20], sizeof(this->m_weaponSubClassCost[20]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_armorSubClassCost[0], sizeof(this->m_armorSubClassCost[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_armorSubClassCost[1], sizeof(this->m_armorSubClassCost[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_armorSubClassCost[2], sizeof(this->m_armorSubClassCost[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_armorSubClassCost[3], sizeof(this->m_armorSubClassCost[3]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_armorSubClassCost[4], sizeof(this->m_armorSubClassCost[4]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_armorSubClassCost[5], sizeof(this->m_armorSubClassCost[5]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_armorSubClassCost[6], sizeof(this->m_armorSubClassCost[6]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_armorSubClassCost[7], sizeof(this->m_armorSubClassCost[7]), nullptr, nullptr, nullptr)
    ) {
        return false;
    }



    return true;
}
