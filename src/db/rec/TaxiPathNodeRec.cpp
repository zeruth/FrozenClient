#include "db/rec/TaxiPathNodeRec.hpp"
#include "util/SFile.hpp"

const char* TaxiPathNodeRec::GetFilename() {
    return "DBFilesClient\\TaxiPathNode.dbc";
}

uint32_t TaxiPathNodeRec::GetNumColumns() {
    return 11;
}

uint32_t TaxiPathNodeRec::GetRowSize() {
    return 44;
}

bool TaxiPathNodeRec::NeedIDAssigned() {
    return false;
}

int32_t TaxiPathNodeRec::GetID() {
    return this->m_ID;
}

void TaxiPathNodeRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool TaxiPathNodeRec::Read(SFile* f, const char* stringBuffer) {
    (void)stringBuffer;

    return SFile::Read(f, this, sizeof(*this), nullptr, nullptr, nullptr);
}
