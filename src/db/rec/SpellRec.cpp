#include "db/rec/SpellRec.hpp"
#include "util/SFile.hpp"

const char* SpellRec::GetFilename() {
    return "DBFilesClient\\Spell.dbc";
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
    this->m_activeIconID = static_cast<int32_t>(columns[SpellRec::COLUMN_ACTIVE_ICON]);
    this->m_effectAura[0] = static_cast<int32_t>(columns[SpellRec::COLUMN_EFFECT_AURA]);
    this->m_effectAura[1] = static_cast<int32_t>(columns[SpellRec::COLUMN_EFFECT_AURA + 1]);
    this->m_effectAura[2] = static_cast<int32_t>(columns[SpellRec::COLUMN_EFFECT_AURA + 2]);
    this->m_effect[0] = static_cast<int32_t>(columns[SpellRec::COLUMN_EFFECT]);
    this->m_effect[1] = static_cast<int32_t>(columns[SpellRec::COLUMN_EFFECT + 1]);
    this->m_effect[2] = static_cast<int32_t>(columns[SpellRec::COLUMN_EFFECT + 2]);
    this->m_name = stringBuffer ? &stringBuffer[columns[SpellRec::COLUMN_NAME]] : "";
    this->m_rank = stringBuffer ? &stringBuffer[columns[SpellRec::COLUMN_RANK]] : "";
    this->m_attributes = columns[SpellRec::COLUMN_ATTRIBUTES];
    this->m_spellVisualID[0] = static_cast<int32_t>(columns[SpellRec::COLUMN_VISUAL]);
    this->m_spellVisualID[1] = static_cast<int32_t>(columns[SpellRec::COLUMN_VISUAL + 1]);

    return true;
}
