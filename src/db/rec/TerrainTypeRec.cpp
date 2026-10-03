#include "db/rec/TerrainTypeRec.hpp"
#include "util/SFile.hpp"

const char* TerrainTypeRec::GetFilename() {
    return "DBFilesClient\\TerrainType.dbc";
}

uint32_t TerrainTypeRec::GetNumColumns() {
    return 6;
}

uint32_t TerrainTypeRec::GetRowSize() {
    return 24;
}

bool TerrainTypeRec::NeedIDAssigned() {
    return false;
}

int32_t TerrainTypeRec::GetID() {
    return this->m_ID;
}

void TerrainTypeRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool TerrainTypeRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t descOfs;

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &descOfs, sizeof(descOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_footstepSprayRun, sizeof(this->m_footstepSprayRun), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_footstepSprayWalk, sizeof(this->m_footstepSprayWalk), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_soundID, sizeof(this->m_soundID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_flags, sizeof(this->m_flags), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_terrainDesc = stringBuffer ? &stringBuffer[descOfs] : "";

    return true;
}
