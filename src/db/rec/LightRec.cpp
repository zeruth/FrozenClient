#include "db/rec/LightRec.hpp"
#include "util/SFile.hpp"

const char* LightRec::GetFilename() {
    return "DBFilesClient\\Light.dbc";
}

uint32_t LightRec::GetNumColumns() {
    return 15;
}

uint32_t LightRec::GetRowSize() {
    return 60;
}

bool LightRec::NeedIDAssigned() {
    return false;
}

int32_t LightRec::GetID() {
    return this->m_ID;
}

void LightRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool LightRec::Read(SFile* f, const char* stringBuffer) {
    return SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_mapID, sizeof(this->m_mapID), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_x, sizeof(this->m_x), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_y, sizeof(this->m_y), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_z, sizeof(this->m_z), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_falloffStart, sizeof(this->m_falloffStart), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_falloffEnd, sizeof(this->m_falloffEnd), nullptr, nullptr, nullptr)
        && SFile::Read(f, this->m_params, sizeof(this->m_params), nullptr, nullptr, nullptr);
}
