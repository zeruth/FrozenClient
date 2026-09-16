#include "db/rec/LightFloatBandRec.hpp"
#include "util/SFile.hpp"

const char* LightFloatBandRec::GetFilename() {
        // The separator must be an ESCAPED backslash. Written as a single one, "\L" is not a
    // valid escape, the compiler drops the backslash, and the name becomes
    // "DBFilesClientLightFloatBand.dbc" -- a file that does not exist. The load then failed
    // silently and every LightFloatBand lookup returned 0, which is why fog distance and cloud
    // density were always zero while the colour bands (LightIntBand) worked fine.
    return "DBFilesClient\\LightFloatBand.dbc";
}

uint32_t LightFloatBandRec::GetNumColumns() {
    return 34;
}

uint32_t LightFloatBandRec::GetRowSize() {
    return 136;
}

bool LightFloatBandRec::NeedIDAssigned() {
    return false;
}

int32_t LightFloatBandRec::GetID() {
    return this->m_ID;
}

void LightFloatBandRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool LightFloatBandRec::Read(SFile* f, const char* stringBuffer) {
    return SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_num, sizeof(this->m_num), nullptr, nullptr, nullptr)
        && SFile::Read(f, this->m_times, sizeof(this->m_times), nullptr, nullptr, nullptr)
        && SFile::Read(f, this->m_values, sizeof(this->m_values), nullptr, nullptr, nullptr);
}
