#include "db/rec/GameObjectArtKitRec.hpp"
#include "util/SFile.hpp"

const char* GameObjectArtKitRec::GetFilename() {
    return "DBFilesClient\\GameObjectArtKit.dbc";
}

uint32_t GameObjectArtKitRec::GetNumColumns() {
    return 8;
}

uint32_t GameObjectArtKitRec::GetRowSize() {
    return 32;
}

bool GameObjectArtKitRec::NeedIDAssigned() {
    return false;
}

int32_t GameObjectArtKitRec::GetID() {
    return this->m_ID;
}

void GameObjectArtKitRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool GameObjectArtKitRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t ofs[7];

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, ofs, sizeof(ofs), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    for (int32_t i = 0; i < 3; i++) {
        this->m_textureVariation[i] = stringBuffer ? &stringBuffer[ofs[i]] : "";
    }

    for (int32_t i = 0; i < 4; i++) {
        this->m_attachModel[i] = stringBuffer ? &stringBuffer[ofs[3 + i]] : "";
    }

    return true;
}
