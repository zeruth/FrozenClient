#include "db/rec/WeaponImpactSoundsRec.hpp"
#include "util/SFile.hpp"

const char* WeaponImpactSoundsRec::GetFilename() {
    return "DBFilesClient\\WeaponImpactSounds.dbc";
}

uint32_t WeaponImpactSoundsRec::GetNumColumns() {
    return 23;
}

uint32_t WeaponImpactSoundsRec::GetRowSize() {
    return 92;
}

bool WeaponImpactSoundsRec::NeedIDAssigned() {
    return false;
}

int32_t WeaponImpactSoundsRec::GetID() {
    return this->m_ID;
}

void WeaponImpactSoundsRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool WeaponImpactSoundsRec::Read(SFile* f, const char* stringBuffer) {
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_weaponSubClassID, sizeof(this->m_weaponSubClassID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_parrySoundType, sizeof(this->m_parrySoundType), nullptr, nullptr, nullptr)
        || !SFile::Read(f, this->m_impactSoundID, sizeof(this->m_impactSoundID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, this->m_critImpactSoundID, sizeof(this->m_critImpactSoundID), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    return true;
}
