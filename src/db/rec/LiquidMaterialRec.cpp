#include "db/rec/LiquidMaterialRec.hpp"
#include "util/SFile.hpp"

const char* LiquidMaterialRec::GetFilename() {
    // The separator is TWO backslashes in the source. Written with one, "\L" is not an
    // escape, the backslash is dropped and the path becomes "DBFilesClientLiquidMaterial.dbc"
    // -- a file that does not exist, so the DBC loads zero rows and every liquid material
    // lookup fails. That is what happened here; see the same note in LightFloatBandRec.
    return "DBFilesClient\\LiquidMaterial.dbc";
}

uint32_t LiquidMaterialRec::GetNumColumns() {
    return 3;
}

uint32_t LiquidMaterialRec::GetRowSize() {
    return 12;
}

bool LiquidMaterialRec::NeedIDAssigned() {
    return false;
}

int32_t LiquidMaterialRec::GetID() {
    return this->m_ID;
}

void LiquidMaterialRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool LiquidMaterialRec::Read(SFile* f, const char* stringBuffer) {
    (void)stringBuffer;

    return SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_LVF, sizeof(this->m_LVF), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_flags, sizeof(this->m_flags), nullptr, nullptr, nullptr);
}
