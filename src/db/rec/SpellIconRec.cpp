#include "db/rec/SpellIconRec.hpp"
#include "util/SFile.hpp"

const char* SpellIconRec::GetFilename() {
    return "DBFilesClient\\SpellIcon.dbc";
}

uint32_t SpellIconRec::GetNumColumns() {
    return 2;
}

uint32_t SpellIconRec::GetRowSize() {
    return 8;
}

bool SpellIconRec::NeedIDAssigned() {
    return false;
}

int32_t SpellIconRec::GetID() {
    return this->m_ID;
}

void SpellIconRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SpellIconRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t textureOfs;

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &textureOfs, sizeof(textureOfs), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    this->m_textureFilename = stringBuffer ? &stringBuffer[textureOfs] : "";

    return true;
}
