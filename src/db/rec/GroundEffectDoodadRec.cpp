#include "db/rec/GroundEffectDoodadRec.hpp"
#include "util/SFile.hpp"

const char* GroundEffectDoodadRec::GetFilename() {
    return "DBFilesClient\\GroundEffectDoodad.dbc";
}

uint32_t GroundEffectDoodadRec::GetNumColumns() {
    return 3;
}

uint32_t GroundEffectDoodadRec::GetRowSize() {
    return 12;
}

bool GroundEffectDoodadRec::NeedIDAssigned() {
    return false;
}

int32_t GroundEffectDoodadRec::GetID() {
    return this->m_ID;
}

void GroundEffectDoodadRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool GroundEffectDoodadRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t pathOfs;

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &pathOfs, sizeof(uint32_t), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_flags, sizeof(this->m_flags), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    if (stringBuffer) {
        this->m_doodadPath = &stringBuffer[pathOfs];
    }

    return true;
}
