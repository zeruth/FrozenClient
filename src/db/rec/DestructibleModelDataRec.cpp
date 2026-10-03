#include "db/rec/DestructibleModelDataRec.hpp"
#include "util/SFile.hpp"

const char* DestructibleModelDataRec::GetFilename() {
    return "DBFilesClient\\DestructibleModelData.dbc";
}

uint32_t DestructibleModelDataRec::GetNumColumns() {
    return 19;
}

uint32_t DestructibleModelDataRec::GetRowSize() {
    return 76;
}

bool DestructibleModelDataRec::NeedIDAssigned() {
    return false;
}

int32_t DestructibleModelDataRec::GetID() {
    return this->m_ID;
}

void DestructibleModelDataRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool DestructibleModelDataRec::Read(SFile* f, const char* stringBuffer) {
    (void)stringBuffer;

    return SFile::Read(f, this, sizeof(*this), nullptr, nullptr, nullptr);
}
