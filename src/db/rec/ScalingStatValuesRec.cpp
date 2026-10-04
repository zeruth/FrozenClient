#include "db/rec/ScalingStatValuesRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* ScalingStatValuesRec::GetFilename() {
    return "DBFilesClient\\ScalingStatValues.dbc";
}

uint32_t ScalingStatValuesRec::GetNumColumns() {
    return ScalingStatValuesRec::COLUMN_COUNT;
}

uint32_t ScalingStatValuesRec::GetRowSize() {
    return 96;
}

bool ScalingStatValuesRec::NeedIDAssigned() {
    return false;
}

int32_t ScalingStatValuesRec::GetID() {
    return this->m_ID;
}

void ScalingStatValuesRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool ScalingStatValuesRec::Read(SFile* f, const char* stringBuffer) {

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_charLevel, sizeof(this->m_charLevel), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_shoulderBudget, sizeof(this->m_shoulderBudget), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_trinketBudget, sizeof(this->m_trinketBudget), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponBudget1H, sizeof(this->m_weaponBudget1H), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_rangedBudget, sizeof(this->m_rangedBudget), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_clothShoulderArmor, sizeof(this->m_clothShoulderArmor), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_leatherShoulderArmor, sizeof(this->m_leatherShoulderArmor), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_mailShoulderArmor, sizeof(this->m_mailShoulderArmor), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_plateShoulderArmor, sizeof(this->m_plateShoulderArmor), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponDPS1H, sizeof(this->m_weaponDPS1H), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponDPS2H, sizeof(this->m_weaponDPS2H), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_spellcasterDPS1H, sizeof(this->m_spellcasterDPS1H), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_spellcasterDPS2H, sizeof(this->m_spellcasterDPS2H), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_rangedDPS, sizeof(this->m_rangedDPS), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_wandDPS, sizeof(this->m_wandDPS), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_spellPower, sizeof(this->m_spellPower), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_primaryBudget, sizeof(this->m_primaryBudget), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_tertiaryBudget, sizeof(this->m_tertiaryBudget), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_clothCloakArmor, sizeof(this->m_clothCloakArmor), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_clothChestArmor, sizeof(this->m_clothChestArmor), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_leatherChestArmor, sizeof(this->m_leatherChestArmor), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_mailChestArmor, sizeof(this->m_mailChestArmor), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_plateChestArmor, sizeof(this->m_plateChestArmor), nullptr, nullptr, nullptr)
    ) {
        return false;
    }



    return true;
}
