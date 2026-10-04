#include "db/rec/ItemRandomSuffixRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* ItemRandomSuffixRec::GetFilename() {
    return "DBFilesClient\\ItemRandomSuffix.dbc";
}

uint32_t ItemRandomSuffixRec::GetNumColumns() {
    return ItemRandomSuffixRec::COLUMN_COUNT;
}

uint32_t ItemRandomSuffixRec::GetRowSize() {
    return 116;
}

bool ItemRandomSuffixRec::NeedIDAssigned() {
    return false;
}

int32_t ItemRandomSuffixRec::GetID() {
    return this->m_ID;
}

void ItemRandomSuffixRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool ItemRandomSuffixRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t nameOfs[17];
    uint32_t internalNameOfs;
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, nameOfs, sizeof(nameOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &internalNameOfs, sizeof(internalNameOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_enchantment[0], sizeof(this->m_enchantment[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_enchantment[1], sizeof(this->m_enchantment[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_enchantment[2], sizeof(this->m_enchantment[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_enchantment[3], sizeof(this->m_enchantment[3]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_enchantment[4], sizeof(this->m_enchantment[4]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_allocationPct[0], sizeof(this->m_allocationPct[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_allocationPct[1], sizeof(this->m_allocationPct[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_allocationPct[2], sizeof(this->m_allocationPct[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_allocationPct[3], sizeof(this->m_allocationPct[3]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_allocationPct[4], sizeof(this->m_allocationPct[4]), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_name = stringBuffer ? &stringBuffer[nameOfs[CURRENT_LANGUAGE]] : "";
    this->m_internalName = stringBuffer ? &stringBuffer[internalNameOfs] : "";

    return true;
}
