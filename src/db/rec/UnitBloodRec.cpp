#include "db/rec/UnitBloodRec.hpp"
#include "util/SFile.hpp"

const char* UnitBloodRec::GetFilename() {
    return "DBFilesClient\\UnitBlood.dbc";
}

uint32_t UnitBloodRec::GetNumColumns() {
    return 10;
}

uint32_t UnitBloodRec::GetRowSize() {
    return 40;
}

bool UnitBloodRec::NeedIDAssigned() {
    return false;
}

int32_t UnitBloodRec::GetID() {
    return this->m_ID;
}

void UnitBloodRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool UnitBloodRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t groundBloodOfs[5];

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, this->m_combatBloodSpurtFront, sizeof(this->m_combatBloodSpurtFront), nullptr, nullptr, nullptr)
        || !SFile::Read(f, this->m_combatBloodSpurtBack, sizeof(this->m_combatBloodSpurtBack), nullptr, nullptr, nullptr)
        || !SFile::Read(f, groundBloodOfs, sizeof(groundBloodOfs), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    for (uint32_t i = 0; i < 5; i++) {
        this->m_groundBlood[i] = stringBuffer ? &stringBuffer[groundBloodOfs[i]] : "";
    }

    return true;
}
