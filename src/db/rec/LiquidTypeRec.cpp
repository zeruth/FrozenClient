#include "db/rec/LiquidTypeRec.hpp"
#include "util/SFile.hpp"

const char* LiquidTypeRec::GetFilename() {
    return "DBFilesClient\\LiquidType.dbc";
}

uint32_t LiquidTypeRec::GetNumColumns() {
    return 45;
}

uint32_t LiquidTypeRec::GetRowSize() {
    return 180;
}

bool LiquidTypeRec::NeedIDAssigned() {
    return false;
}

int32_t LiquidTypeRec::GetID() {
    return this->m_ID;
}

void LiquidTypeRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool LiquidTypeRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t nameOfs;
    uint32_t textureOfs[6];

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &nameOfs, sizeof(uint32_t), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_flags, sizeof(this->m_flags), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_type, sizeof(this->m_type), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_soundID, sizeof(this->m_soundID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_spellID, sizeof(this->m_spellID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_maxDarkenDepth, sizeof(this->m_maxDarkenDepth), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_fogDarkenIntensity, sizeof(this->m_fogDarkenIntensity), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_ambDarkenIntensity, sizeof(this->m_ambDarkenIntensity), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_dirDarkenIntensity, sizeof(this->m_dirDarkenIntensity), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_lightID, sizeof(this->m_lightID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_particleScale, sizeof(this->m_particleScale), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_particleMovement, sizeof(this->m_particleMovement), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_particleTexSlots, sizeof(this->m_particleTexSlots), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_materialID, sizeof(this->m_materialID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, textureOfs, sizeof(textureOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, this->m_color, sizeof(this->m_color), nullptr, nullptr, nullptr)
        || !SFile::Read(f, this->m_float, sizeof(this->m_float), nullptr, nullptr, nullptr)
        || !SFile::Read(f, this->m_int, sizeof(this->m_int), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    if (stringBuffer) {
        this->m_name = &stringBuffer[nameOfs];

        for (int32_t i = 0; i < 6; i++) {
            this->m_texture[i] = &stringBuffer[textureOfs[i]];
        }
    }

    return true;
}
