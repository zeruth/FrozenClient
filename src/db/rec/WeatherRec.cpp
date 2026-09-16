#include "db/rec/WeatherRec.hpp"
#include "util/SFile.hpp"

const char* WeatherRec::GetFilename() {
    return "DBFilesClient\\Weather.dbc";
}

uint32_t WeatherRec::GetNumColumns() {
    return 8;
}

uint32_t WeatherRec::GetRowSize() {
    return 32;
}

bool WeatherRec::NeedIDAssigned() {
    return false;
}

int32_t WeatherRec::GetID() {
    return this->m_ID;
}

void WeatherRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool WeatherRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t textureOfs;

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_ambienceID, sizeof(this->m_ambienceID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_effectType, sizeof(this->m_effectType), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_transitionSkybox, sizeof(this->m_transitionSkybox), nullptr, nullptr, nullptr)
        || !SFile::Read(f, this->m_effectColor, sizeof(this->m_effectColor), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &textureOfs, sizeof(uint32_t), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    if (stringBuffer) {
        this->m_effectTexture = &stringBuffer[textureOfs];
    }

    return true;
}
