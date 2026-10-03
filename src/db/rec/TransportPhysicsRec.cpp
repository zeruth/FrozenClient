#include "db/rec/TransportPhysicsRec.hpp"
#include "util/SFile.hpp"

const char* TransportPhysicsRec::GetFilename() {
    return "DBFilesClient\\TransportPhysics.dbc";
}

uint32_t TransportPhysicsRec::GetNumColumns() {
    return 11;
}

uint32_t TransportPhysicsRec::GetRowSize() {
    return 44;
}

bool TransportPhysicsRec::NeedIDAssigned() {
    return false;
}

int32_t TransportPhysicsRec::GetID() {
    return this->m_ID;
}

void TransportPhysicsRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool TransportPhysicsRec::Read(SFile* f, const char* stringBuffer) {
    (void)stringBuffer;

    return SFile::Read(f, this, sizeof(*this), nullptr, nullptr, nullptr);
}
