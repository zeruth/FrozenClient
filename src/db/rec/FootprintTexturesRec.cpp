#include "db/rec/FootprintTexturesRec.hpp"
#include "util/SFile.hpp"

const char* FootprintTexturesRec::GetFilename() {
    return "DBFilesClient\\FootprintTextures.dbc";
}

uint32_t FootprintTexturesRec::GetNumColumns() {
    return 2;
}

uint32_t FootprintTexturesRec::GetRowSize() {
    return 8;
}

bool FootprintTexturesRec::NeedIDAssigned() {
    return false;
}

int32_t FootprintTexturesRec::GetID() {
    return this->m_ID;
}

void FootprintTexturesRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool FootprintTexturesRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t pathOfs;

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &pathOfs, sizeof(uint32_t), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_footstepFilePath = stringBuffer ? &stringBuffer[pathOfs] : "";

    return true;
}
