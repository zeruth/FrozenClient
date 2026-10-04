#include "db/rec/SpellDescriptionVariablesRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* SpellDescriptionVariablesRec::GetFilename() {
    return "DBFilesClient\\SpellDescriptionVariables.dbc";
}

uint32_t SpellDescriptionVariablesRec::GetNumColumns() {
    return SpellDescriptionVariablesRec::COLUMN_COUNT;
}

uint32_t SpellDescriptionVariablesRec::GetRowSize() {
    return 8;
}

bool SpellDescriptionVariablesRec::NeedIDAssigned() {
    return false;
}

int32_t SpellDescriptionVariablesRec::GetID() {
    return this->m_ID;
}

void SpellDescriptionVariablesRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SpellDescriptionVariablesRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t variablesOfs;
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &variablesOfs, sizeof(variablesOfs), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_variables = stringBuffer ? &stringBuffer[variablesOfs] : "";

    return true;
}
