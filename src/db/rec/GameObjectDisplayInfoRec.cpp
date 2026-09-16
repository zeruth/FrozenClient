#include "db/rec/GameObjectDisplayInfoRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* GameObjectDisplayInfoRec::GetFilename() {
    return "DBFilesClient\\GameObjectDisplayInfo.dbc";
}

uint32_t GameObjectDisplayInfoRec::GetNumColumns() {
    return 19;
}

uint32_t GameObjectDisplayInfoRec::GetRowSize() {
    return 76;
}

bool GameObjectDisplayInfoRec::NeedIDAssigned() {
    return false;
}

int32_t GameObjectDisplayInfoRec::GetID() {
    return this->m_ID;
}

void GameObjectDisplayInfoRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool GameObjectDisplayInfoRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t modelNameOfs;

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &modelNameOfs, sizeof(modelNameOfs), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    for (int32_t i = 0; i < 10; i++) {
        if (!SFile::Read(f, &this->m_sound[i], sizeof(this->m_sound[i]), nullptr, nullptr, nullptr)) {
            return false;
        }
    }

    for (int32_t i = 0; i < 3; i++) {
        if (!SFile::Read(f, &this->m_geoBoxMin[i], sizeof(this->m_geoBoxMin[i]), nullptr, nullptr, nullptr)) {
            return false;
        }
    }

    for (int32_t i = 0; i < 3; i++) {
        if (!SFile::Read(f, &this->m_geoBoxMax[i], sizeof(this->m_geoBoxMax[i]), nullptr, nullptr, nullptr)) {
            return false;
        }
    }

    if (!SFile::Read(f, &this->m_objectEffectPackageID, sizeof(this->m_objectEffectPackageID), nullptr, nullptr, nullptr)) {
        return false;
    }

    this->m_modelName = stringBuffer ? &stringBuffer[modelNameOfs] : "";

    return true;
}
