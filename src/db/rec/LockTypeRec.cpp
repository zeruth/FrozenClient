#include "db/rec/LockTypeRec.hpp"
#include "util/SFile.hpp"

const char* LockTypeRec::GetFilename() {
    return "DBFilesClient\\LockType.dbc";
}

uint32_t LockTypeRec::GetNumColumns() {
    return LockTypeRec::COLUMN_COUNT;
}

uint32_t LockTypeRec::GetRowSize() {
    return LockTypeRec::COLUMN_COUNT * 4;
}

bool LockTypeRec::NeedIDAssigned() {
    return false;
}

int32_t LockTypeRec::GetID() {
    return this->m_ID;
}

void LockTypeRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool LockTypeRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t columns[LockTypeRec::COLUMN_COUNT];

    if (!SFile::Read(f, columns, sizeof(columns), nullptr, nullptr, nullptr)) {
        return false;
    }

    this->m_ID = static_cast<int32_t>(columns[0]);
    this->m_name = stringBuffer ? &stringBuffer[columns[LockTypeRec::COLUMN_NAME]] : "";
    this->m_resourceName = stringBuffer ? &stringBuffer[columns[LockTypeRec::COLUMN_RESOURCE_NAME]] : "";
    this->m_verb = stringBuffer ? &stringBuffer[columns[LockTypeRec::COLUMN_VERB]] : "";
    this->m_cursorName = stringBuffer ? &stringBuffer[columns[LockTypeRec::COLUMN_CURSOR_NAME]] : "";

    return true;
}
