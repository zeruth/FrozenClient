#include "db/rec/ItemPetFoodRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* ItemPetFoodRec::GetFilename() {
    return "DBFilesClient\\ItemPetFood.dbc";
}

uint32_t ItemPetFoodRec::GetNumColumns() {
    return ItemPetFoodRec::COLUMN_COUNT;
}

uint32_t ItemPetFoodRec::GetRowSize() {
    return 72;
}

bool ItemPetFoodRec::NeedIDAssigned() {
    return false;
}

int32_t ItemPetFoodRec::GetID() {
    return this->m_ID;
}

void ItemPetFoodRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool ItemPetFoodRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t nameOfs[17];
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, nameOfs, sizeof(nameOfs), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_name = stringBuffer ? &stringBuffer[nameOfs[CURRENT_LANGUAGE]] : "";

    return true;
}
