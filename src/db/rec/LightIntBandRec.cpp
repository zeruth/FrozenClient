#include "db/rec/LightIntBandRec.hpp"
#include "util/SFile.hpp"

const char* LightIntBandRec::GetFilename() {
    return "DBFilesClient\\LightIntBand.dbc";
}

uint32_t LightIntBandRec::GetNumColumns() {
    return 34;
}

uint32_t LightIntBandRec::GetRowSize() {
    return 136;
}

bool LightIntBandRec::NeedIDAssigned() {
    return false;
}

int32_t LightIntBandRec::GetID() {
    return this->m_ID;
}

void LightIntBandRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool LightIntBandRec::Read(SFile* f, const char* stringBuffer) {
    return SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_num, sizeof(this->m_num), nullptr, nullptr, nullptr)
        && SFile::Read(f, this->m_times, sizeof(this->m_times), nullptr, nullptr, nullptr)
        && SFile::Read(f, this->m_values, sizeof(this->m_values), nullptr, nullptr, nullptr);
}
