#include "db/rec/ItemSetRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* ItemSetRec::GetFilename() {
    return "DBFilesClient\\ItemSet.dbc";
}

uint32_t ItemSetRec::GetNumColumns() {
    return ItemSetRec::COLUMN_COUNT;
}

uint32_t ItemSetRec::GetRowSize() {
    return 212;
}

bool ItemSetRec::NeedIDAssigned() {
    return false;
}

int32_t ItemSetRec::GetID() {
    return this->m_ID;
}

void ItemSetRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool ItemSetRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t nameOfs[17];
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, nameOfs, sizeof(nameOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[0], sizeof(this->m_itemID[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[1], sizeof(this->m_itemID[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[2], sizeof(this->m_itemID[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[3], sizeof(this->m_itemID[3]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[4], sizeof(this->m_itemID[4]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[5], sizeof(this->m_itemID[5]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[6], sizeof(this->m_itemID[6]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[7], sizeof(this->m_itemID[7]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[8], sizeof(this->m_itemID[8]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[9], sizeof(this->m_itemID[9]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[10], sizeof(this->m_itemID[10]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[11], sizeof(this->m_itemID[11]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[12], sizeof(this->m_itemID[12]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[13], sizeof(this->m_itemID[13]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[14], sizeof(this->m_itemID[14]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[15], sizeof(this->m_itemID[15]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemID[16], sizeof(this->m_itemID[16]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_setSpellID[0], sizeof(this->m_setSpellID[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_setSpellID[1], sizeof(this->m_setSpellID[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_setSpellID[2], sizeof(this->m_setSpellID[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_setSpellID[3], sizeof(this->m_setSpellID[3]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_setSpellID[4], sizeof(this->m_setSpellID[4]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_setSpellID[5], sizeof(this->m_setSpellID[5]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_setSpellID[6], sizeof(this->m_setSpellID[6]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_setSpellID[7], sizeof(this->m_setSpellID[7]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_setThreshold[0], sizeof(this->m_setThreshold[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_setThreshold[1], sizeof(this->m_setThreshold[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_setThreshold[2], sizeof(this->m_setThreshold[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_setThreshold[3], sizeof(this->m_setThreshold[3]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_setThreshold[4], sizeof(this->m_setThreshold[4]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_setThreshold[5], sizeof(this->m_setThreshold[5]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_setThreshold[6], sizeof(this->m_setThreshold[6]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_setThreshold[7], sizeof(this->m_setThreshold[7]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_requiredSkill, sizeof(this->m_requiredSkill), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_requiredSkillRank, sizeof(this->m_requiredSkillRank), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_name = stringBuffer ? &stringBuffer[nameOfs[CURRENT_LANGUAGE]] : "";

    return true;
}
