#include "db/rec/ScreenEffectRec.hpp"
#include "util/SFile.hpp"

const char* ScreenEffectRec::GetFilename() {
    return "DBFilesClient\\ScreenEffect.dbc";
}

uint32_t ScreenEffectRec::GetNumColumns() {
    return 10;
}

uint32_t ScreenEffectRec::GetRowSize() {
    return 40;
}

bool ScreenEffectRec::NeedIDAssigned() {
    return false;
}

int32_t ScreenEffectRec::GetID() {
    return this->m_ID;
}

void ScreenEffectRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool ScreenEffectRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t nameOfs;

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &nameOfs, sizeof(uint32_t), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_effect, sizeof(this->m_effect), nullptr, nullptr, nullptr)
        || !SFile::Read(f, this->m_params, sizeof(this->m_params), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_field18, sizeof(this->m_field18), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_lightParamsID, sizeof(this->m_lightParamsID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_soundAmbienceID, sizeof(this->m_soundAmbienceID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_zoneMusicID, sizeof(this->m_zoneMusicID), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_name = nameOfs ? &stringBuffer[nameOfs] : "";

    return true;
}
