#include "db/rec/GroundEffectTextureRec.hpp"
#include "util/SFile.hpp"

const char* GroundEffectTextureRec::GetFilename() {
    return "DBFilesClient\\GroundEffectTexture.dbc";
}

uint32_t GroundEffectTextureRec::GetNumColumns() {
    return 11;
}

uint32_t GroundEffectTextureRec::GetRowSize() {
    return 44;
}

bool GroundEffectTextureRec::NeedIDAssigned() {
    return false;
}

int32_t GroundEffectTextureRec::GetID() {
    return this->m_ID;
}

void GroundEffectTextureRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool GroundEffectTextureRec::Read(SFile* f, const char* stringBuffer) {
    return SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        && SFile::Read(f, this->m_doodadID, sizeof(this->m_doodadID), nullptr, nullptr, nullptr)
        && SFile::Read(f, this->m_doodadWeight, sizeof(this->m_doodadWeight), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_amount, sizeof(this->m_amount), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_terrainType, sizeof(this->m_terrainType), nullptr, nullptr, nullptr);
}
