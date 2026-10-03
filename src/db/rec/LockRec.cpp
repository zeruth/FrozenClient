#include "db/rec/LockRec.hpp"
#include "util/SFile.hpp"

const char* LockRec::GetFilename() {
    return "DBFilesClient\\Lock.dbc";
}

uint32_t LockRec::GetNumColumns() {
    return 33;
}

uint32_t LockRec::GetRowSize() {
    return 132;
}

bool LockRec::NeedIDAssigned() {
    return false;
}

int32_t LockRec::GetID() {
    return this->m_ID;
}

void LockRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool LockRec::Read(SFile* f, const char* stringBuffer) {
    return SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        && SFile::Read(f, this->m_type, sizeof(this->m_type), nullptr, nullptr, nullptr)
        && SFile::Read(f, this->m_index, sizeof(this->m_index), nullptr, nullptr, nullptr)
        && SFile::Read(f, this->m_skill, sizeof(this->m_skill), nullptr, nullptr, nullptr)
        && SFile::Read(f, this->m_action, sizeof(this->m_action), nullptr, nullptr, nullptr);
}
