#include "db/rec/SpellVisualKitAreaModelRec.hpp"
#include "util/SFile.hpp"

const char* SpellVisualKitAreaModelRec::GetFilename() {
    return "DBFilesClient\\SpellVisualKitAreaModel.dbc";
}

uint32_t SpellVisualKitAreaModelRec::GetNumColumns() {
    return SpellVisualKitAreaModelRec::COLUMN_COUNT;
}

uint32_t SpellVisualKitAreaModelRec::GetRowSize() {
    return SpellVisualKitAreaModelRec::COLUMN_COUNT * 4;
}

bool SpellVisualKitAreaModelRec::NeedIDAssigned() {
    return false;
}

int32_t SpellVisualKitAreaModelRec::GetID() {
    return this->m_ID;
}

void SpellVisualKitAreaModelRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SpellVisualKitAreaModelRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t columns[SpellVisualKitAreaModelRec::COLUMN_COUNT];

    if (!SFile::Read(f, columns, sizeof(columns), nullptr, nullptr, nullptr)) {
        return false;
    }

    this->m_ID = static_cast<int32_t>(columns[0]);
    this->m_modelName = stringBuffer ? &stringBuffer[columns[1]] : "";
    this->m_flags = static_cast<int32_t>(columns[2]);

    return true;
}
