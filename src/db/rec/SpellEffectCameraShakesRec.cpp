#include "db/rec/SpellEffectCameraShakesRec.hpp"
#include "util/SFile.hpp"

const char* SpellEffectCameraShakesRec::GetFilename() {
    return "DBFilesClient\\SpellEffectCameraShakes.dbc";
}

uint32_t SpellEffectCameraShakesRec::GetNumColumns() {
    return SpellEffectCameraShakesRec::COLUMN_COUNT;
}

uint32_t SpellEffectCameraShakesRec::GetRowSize() {
    return SpellEffectCameraShakesRec::COLUMN_COUNT * 4;
}

bool SpellEffectCameraShakesRec::NeedIDAssigned() {
    return false;
}

int32_t SpellEffectCameraShakesRec::GetID() {
    return this->m_ID;
}

void SpellEffectCameraShakesRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SpellEffectCameraShakesRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t columns[SpellEffectCameraShakesRec::COLUMN_COUNT];

    if (!SFile::Read(f, columns, sizeof(columns), nullptr, nullptr, nullptr)) {
        return false;
    }

    this->m_ID = static_cast<int32_t>(columns[0]);
    this->m_cameraShake[0] = static_cast<int32_t>(columns[1]);
    this->m_cameraShake[1] = static_cast<int32_t>(columns[2]);
    this->m_cameraShake[2] = static_cast<int32_t>(columns[3]);

    return true;
}
