#include "db/rec/SpellVisualKitModelAttachRec.hpp"
#include "util/SFile.hpp"

const char* SpellVisualKitModelAttachRec::GetFilename() {
    return "DBFilesClient\\SpellVisualKitModelAttach.dbc";
}

uint32_t SpellVisualKitModelAttachRec::GetNumColumns() {
    return SpellVisualKitModelAttachRec::COLUMN_COUNT;
}

uint32_t SpellVisualKitModelAttachRec::GetRowSize() {
    return SpellVisualKitModelAttachRec::COLUMN_COUNT * 4;
}

bool SpellVisualKitModelAttachRec::NeedIDAssigned() {
    return false;
}

int32_t SpellVisualKitModelAttachRec::GetID() {
    return this->m_ID;
}

void SpellVisualKitModelAttachRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SpellVisualKitModelAttachRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t columns[SpellVisualKitModelAttachRec::COLUMN_COUNT];

    if (!SFile::Read(f, columns, sizeof(columns), nullptr, nullptr, nullptr)) {
        return false;
    }

    auto asFloat = [](uint32_t v) { return *reinterpret_cast<const float*>(&v); };

    this->m_ID = static_cast<int32_t>(columns[0]);
    this->m_parentSpellVisualKitID = static_cast<int32_t>(columns[1]);
    this->m_spellVisualEffectNameID = static_cast<int32_t>(columns[2]);
    this->m_attachmentID = static_cast<int32_t>(columns[3]);
    this->m_offset[0] = asFloat(columns[4]);
    this->m_offset[1] = asFloat(columns[5]);
    this->m_offset[2] = asFloat(columns[6]);
    this->m_yaw = asFloat(columns[7]);
    this->m_pitch = asFloat(columns[8]);
    this->m_roll = asFloat(columns[9]);

    return true;
}
