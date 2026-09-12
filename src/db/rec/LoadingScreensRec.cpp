#include "db/rec/LoadingScreensRec.hpp"
#include "util/SFile.hpp"

const char* LoadingScreensRec::GetFilename() {
    return "DBFilesClient\\LoadingScreens.dbc";
}

uint32_t LoadingScreensRec::GetNumColumns() {
    return 4;
}

uint32_t LoadingScreensRec::GetRowSize() {
    return 16;
}

bool LoadingScreensRec::NeedIDAssigned() {
    return false;
}

int32_t LoadingScreensRec::GetID() {
    return this->m_ID;
}

void LoadingScreensRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool LoadingScreensRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t nameOfs;
    uint32_t fileNameOfs;

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &nameOfs, sizeof(uint32_t), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &fileNameOfs, sizeof(uint32_t), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_hasWideScreen, sizeof(this->m_hasWideScreen), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_name = nameOfs ? &stringBuffer[nameOfs] : "";
    this->m_fileName = fileNameOfs ? &stringBuffer[fileNameOfs] : "";

    return true;
}
