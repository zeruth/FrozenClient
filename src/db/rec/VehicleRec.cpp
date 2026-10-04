#include "db/rec/VehicleRec.hpp"
#include "util/SFile.hpp"

const char* VehicleRec::GetFilename() {
    return "DBFilesClient\\Vehicle.dbc";
}

uint32_t VehicleRec::GetNumColumns() {
    return 40;
}

uint32_t VehicleRec::GetRowSize() {
    return 160;
}

bool VehicleRec::NeedIDAssigned() {
    return false;
}

int32_t VehicleRec::GetID() {
    return this->m_ID;
}

void VehicleRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool VehicleRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t arcTextureOfs;
    uint32_t impactTextureOfs;
    uint32_t impactModelOfs[2];

    if (!SFile::Read(f, &this->m_ID, sizeof(int32_t) * 29, nullptr, nullptr, nullptr)
        || !SFile::Read(f, &arcTextureOfs, sizeof(arcTextureOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &impactTextureOfs, sizeof(impactTextureOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, impactModelOfs, sizeof(impactModelOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_cameraYawOffset, sizeof(int32_t) * 7, nullptr, nullptr, nullptr)) {
        return false;
    }

    if (stringBuffer) {
        this->m_msslTrgtArcTexture = &stringBuffer[arcTextureOfs];
        this->m_msslTrgtImpactTexture = &stringBuffer[impactTextureOfs];
        this->m_msslTrgtImpactModel[0] = &stringBuffer[impactModelOfs[0]];
        this->m_msslTrgtImpactModel[1] = &stringBuffer[impactModelOfs[1]];
    } else {
        this->m_msslTrgtArcTexture = "";
        this->m_msslTrgtImpactTexture = "";
        this->m_msslTrgtImpactModel[0] = "";
        this->m_msslTrgtImpactModel[1] = "";
    }

    return true;
}
