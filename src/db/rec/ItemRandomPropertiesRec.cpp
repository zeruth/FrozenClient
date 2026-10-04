#include "db/rec/ItemRandomPropertiesRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* ItemRandomPropertiesRec::GetFilename() {
    return "DBFilesClient\\ItemRandomProperties.dbc";
}

uint32_t ItemRandomPropertiesRec::GetNumColumns() {
    return ItemRandomPropertiesRec::COLUMN_COUNT;
}

uint32_t ItemRandomPropertiesRec::GetRowSize() {
    return 96;
}

bool ItemRandomPropertiesRec::NeedIDAssigned() {
    return false;
}

int32_t ItemRandomPropertiesRec::GetID() {
    return this->m_ID;
}

void ItemRandomPropertiesRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool ItemRandomPropertiesRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t internalNameOfs;
    uint32_t nameOfs[17];
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &internalNameOfs, sizeof(internalNameOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_enchantment[0], sizeof(this->m_enchantment[0]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_enchantment[1], sizeof(this->m_enchantment[1]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_enchantment[2], sizeof(this->m_enchantment[2]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_enchantment[3], sizeof(this->m_enchantment[3]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_enchantment[4], sizeof(this->m_enchantment[4]), nullptr, nullptr, nullptr)
        || !SFile::Read(f, nameOfs, sizeof(nameOfs), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_internalName = stringBuffer ? &stringBuffer[internalNameOfs] : "";
    this->m_name = stringBuffer ? &stringBuffer[nameOfs[CURRENT_LANGUAGE]] : "";

    return true;
}
