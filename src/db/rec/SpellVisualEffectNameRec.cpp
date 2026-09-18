#include "db/rec/SpellVisualEffectNameRec.hpp"
#include "util/SFile.hpp"

const char* SpellVisualEffectNameRec::GetFilename() {
    return "DBFilesClient\\SpellVisualEffectName.dbc";
}

uint32_t SpellVisualEffectNameRec::GetNumColumns() {
    return SpellVisualEffectNameRec::COLUMN_COUNT;
}

uint32_t SpellVisualEffectNameRec::GetRowSize() {
    return SpellVisualEffectNameRec::COLUMN_COUNT * 4;
}

bool SpellVisualEffectNameRec::NeedIDAssigned() {
    return false;
}

int32_t SpellVisualEffectNameRec::GetID() {
    return this->m_ID;
}

void SpellVisualEffectNameRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SpellVisualEffectNameRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t columns[SpellVisualEffectNameRec::COLUMN_COUNT];

    if (!SFile::Read(f, columns, sizeof(columns), nullptr, nullptr, nullptr)) {
        return false;
    }

    this->m_ID = static_cast<int32_t>(columns[0]);
    this->m_name = stringBuffer ? &stringBuffer[columns[1]] : "";
    this->m_fileName = stringBuffer ? &stringBuffer[columns[2]] : "";
    this->m_areaEffectSize = *reinterpret_cast<const float*>(&columns[3]);
    this->m_scale = *reinterpret_cast<const float*>(&columns[4]);
    this->m_minAllowedScale = *reinterpret_cast<const float*>(&columns[5]);
    this->m_maxAllowedScale = *reinterpret_cast<const float*>(&columns[6]);

    return true;
}
