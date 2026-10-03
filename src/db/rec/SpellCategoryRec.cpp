#include "db/rec/SpellCategoryRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* SpellCategoryRec::GetFilename() {
    return "DBFilesClient\\SpellCategory.dbc";
}

uint32_t SpellCategoryRec::GetNumColumns() {
    return SpellCategoryRec::COLUMN_COUNT;
}

uint32_t SpellCategoryRec::GetRowSize() {
    return 8;
}

bool SpellCategoryRec::NeedIDAssigned() {
    return false;
}

int32_t SpellCategoryRec::GetID() {
    return this->m_ID;
}

void SpellCategoryRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SpellCategoryRec::Read(SFile* f, const char* stringBuffer) {

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_flags, sizeof(this->m_flags), nullptr, nullptr, nullptr)
    ) {
        return false;
    }



    return true;
}
