#include "db/rec/SpellItemEnchantmentRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* SpellItemEnchantmentRec::GetFilename() {
    return "DBFilesClient\\SpellItemEnchantment.dbc";
}

uint32_t SpellItemEnchantmentRec::GetNumColumns() {
    return SpellItemEnchantmentRec::COLUMN_COUNT;
}

uint32_t SpellItemEnchantmentRec::GetRowSize() {
    return 152;
}

bool SpellItemEnchantmentRec::NeedIDAssigned() {
    return false;
}

int32_t SpellItemEnchantmentRec::GetID() {
    return this->m_ID;
}

void SpellItemEnchantmentRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SpellItemEnchantmentRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t nameOfs[17];
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_charges, sizeof(this->m_charges), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_effect[0], sizeof(this->m_effect[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_effect[1], sizeof(this->m_effect[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_effect[2], sizeof(this->m_effect[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_effectPointsMin[0], sizeof(this->m_effectPointsMin[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_effectPointsMin[1], sizeof(this->m_effectPointsMin[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_effectPointsMin[2], sizeof(this->m_effectPointsMin[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_effectPointsMax[0], sizeof(this->m_effectPointsMax[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_effectPointsMax[1], sizeof(this->m_effectPointsMax[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_effectPointsMax[2], sizeof(this->m_effectPointsMax[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_effectArg[0], sizeof(this->m_effectArg[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_effectArg[1], sizeof(this->m_effectArg[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_effectArg[2], sizeof(this->m_effectArg[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, nameOfs, sizeof(nameOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_itemVisual, sizeof(this->m_itemVisual), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_flags, sizeof(this->m_flags), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_srcItemID, sizeof(this->m_srcItemID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_conditionID, sizeof(this->m_conditionID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_requiredSkillID, sizeof(this->m_requiredSkillID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_requiredSkillRank, sizeof(this->m_requiredSkillRank), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_minLevel, sizeof(this->m_minLevel), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_name = stringBuffer ? &stringBuffer[nameOfs[CURRENT_LANGUAGE]] : "";

    return true;
}
