#include "db/rec/SpellCastTimesRec.hpp"
#include "util/SFile.hpp"

const char* SpellCastTimesRec::GetFilename() {
    return "DBFilesClient\\SpellCastTimes.dbc";
}

uint32_t SpellCastTimesRec::GetNumColumns() {
    return SpellCastTimesRec::COLUMN_COUNT;
}

uint32_t SpellCastTimesRec::GetRowSize() {
    return SpellCastTimesRec::COLUMN_COUNT * 4;
}

bool SpellCastTimesRec::NeedIDAssigned() {
    return false;
}

int32_t SpellCastTimesRec::GetID() {
    return this->m_ID;
}

void SpellCastTimesRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SpellCastTimesRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t columns[SpellCastTimesRec::COLUMN_COUNT];

    if (!SFile::Read(f, columns, sizeof(columns), nullptr, nullptr, nullptr)) {
        return false;
    }

    this->m_ID = static_cast<int32_t>(columns[0]);
    this->m_base = static_cast<int32_t>(columns[1]);
    this->m_perLevel = static_cast<int32_t>(columns[2]);
    this->m_minimum = static_cast<int32_t>(columns[3]);

    return true;
}
