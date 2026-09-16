#include "db/rec/SpellRec.hpp"
#include "util/SFile.hpp"

const char* SpellRec::GetFilename() {
    return "DBFilesClient\Spell.dbc";
}

uint32_t SpellRec::GetNumColumns() {
    return SpellRec::COLUMN_COUNT;
}

uint32_t SpellRec::GetRowSize() {
    return SpellRec::COLUMN_COUNT * 4;
}

bool SpellRec::NeedIDAssigned() {
    return false;
}

int32_t SpellRec::GetID() {
    return this->m_ID;
}

void SpellRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SpellRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t columns[SpellRec::COLUMN_COUNT];

    // One read for the whole row: the loader expects Read to consume exactly GetRowSize() bytes, and
    // 234 separate calls to skip past unused combat data would be the same bytes for more work.
    if (!SFile::Read(f, columns, sizeof(columns), nullptr, nullptr, nullptr)) {
        return false;
    }

    this->m_ID = static_cast<int32_t>(columns[0]);
    this->m_spellIconID = static_cast<int32_t>(columns[SpellRec::COLUMN_ICON]);
    this->m_name = stringBuffer ? &stringBuffer[columns[SpellRec::COLUMN_NAME]] : "";

    return true;
}
