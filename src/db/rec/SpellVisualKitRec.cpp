#include "db/rec/SpellVisualKitRec.hpp"
#include "util/SFile.hpp"

const char* SpellVisualKitRec::GetFilename() {
    return "DBFilesClient\\SpellVisualKit.dbc";
}

uint32_t SpellVisualKitRec::GetNumColumns() {
    return SpellVisualKitRec::COLUMN_COUNT;
}

uint32_t SpellVisualKitRec::GetRowSize() {
    return SpellVisualKitRec::COLUMN_COUNT * 4;
}

bool SpellVisualKitRec::NeedIDAssigned() {
    return false;
}

int32_t SpellVisualKitRec::GetID() {
    return this->m_ID;
}

void SpellVisualKitRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SpellVisualKitRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t columns[SpellVisualKitRec::COLUMN_COUNT];

    if (!SFile::Read(f, columns, sizeof(columns), nullptr, nullptr, nullptr)) {
        return false;
    }

    this->m_ID = static_cast<int32_t>(columns[0]);
    this->m_startAnimID = static_cast<int32_t>(columns[1]);
    this->m_animID = static_cast<int32_t>(columns[2]);
    this->m_headEffect = static_cast<int32_t>(columns[3]);
    this->m_chestEffect = static_cast<int32_t>(columns[4]);
    this->m_baseEffect = static_cast<int32_t>(columns[5]);
    this->m_leftHandEffect = static_cast<int32_t>(columns[6]);
    this->m_rightHandEffect = static_cast<int32_t>(columns[7]);
    this->m_breathEffect = static_cast<int32_t>(columns[8]);
    this->m_leftWeaponEffect = static_cast<int32_t>(columns[9]);
    this->m_rightWeaponEffect = static_cast<int32_t>(columns[10]);
    this->m_specialEffect[0] = static_cast<int32_t>(columns[11]);
    this->m_specialEffect[1] = static_cast<int32_t>(columns[12]);
    this->m_specialEffect[2] = static_cast<int32_t>(columns[13]);
    this->m_worldEffect = static_cast<int32_t>(columns[14]);
    this->m_soundID = static_cast<int32_t>(columns[15]);
    this->m_shakeID = static_cast<int32_t>(columns[16]);
    this->m_flags = static_cast<int32_t>(columns[37]);

    return true;
}
