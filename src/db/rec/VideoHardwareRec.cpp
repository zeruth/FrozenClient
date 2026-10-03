#include "db/rec/VideoHardwareRec.hpp"
#include "util/SFile.hpp"

const char* VideoHardwareRec::GetFilename() {
    return "DBFilesClient\\VideoHardware.dbc";
}

uint32_t VideoHardwareRec::GetNumColumns() {
    return 23;
}

uint32_t VideoHardwareRec::GetRowSize() {
    return 92;
}

bool VideoHardwareRec::NeedIDAssigned() {
    return false;
}

int32_t VideoHardwareRec::GetID() {
    return this->m_ID;
}

void VideoHardwareRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool VideoHardwareRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t oglOverridesOfs;
    uint32_t d3dOverridesOfs;

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_vendorID, sizeof(this->m_vendorID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_deviceID, sizeof(this->m_deviceID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_farclipIdx, sizeof(this->m_farclipIdx), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_terrainLODDistIdx, sizeof(this->m_terrainLODDistIdx), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_terrainShadowLOD, sizeof(this->m_terrainShadowLOD), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_detailDoodadDensityIdx, sizeof(this->m_detailDoodadDensityIdx), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_detailDoodadAlpha, sizeof(this->m_detailDoodadAlpha), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_animatingDoodadIdx, sizeof(this->m_animatingDoodadIdx), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_trilinear, sizeof(this->m_trilinear), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_numLights, sizeof(this->m_numLights), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_specularity, sizeof(this->m_specularity), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_waterLODIdx, sizeof(this->m_waterLODIdx), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_particleDensityIdx, sizeof(this->m_particleDensityIdx), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_unitDrawDistIdx, sizeof(this->m_unitDrawDistIdx), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_smallCullDistIdx, sizeof(this->m_smallCullDistIdx), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_resolutionIdx, sizeof(this->m_resolutionIdx), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_baseMipLevel, sizeof(this->m_baseMipLevel), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &oglOverridesOfs, sizeof(oglOverridesOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &d3dOverridesOfs, sizeof(d3dOverridesOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_fixLag, sizeof(this->m_fixLag), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_multisample, sizeof(this->m_multisample), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_atlasdisable, sizeof(this->m_atlasdisable), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    if (stringBuffer) {
        this->m_oglOverrides = &stringBuffer[oglOverridesOfs];
        this->m_d3dOverrides = &stringBuffer[d3dOverridesOfs];
    } else {
        this->m_oglOverrides = "";
        this->m_d3dOverrides = "";
    }

    return true;
}
