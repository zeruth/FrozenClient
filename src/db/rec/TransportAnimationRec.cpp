#include "db/rec/TransportAnimationRec.hpp"
#include "util/SFile.hpp"

const char* TransportAnimationRec::GetFilename() {
    return "DBFilesClient\\TransportAnimation.dbc";
}

uint32_t TransportAnimationRec::GetNumColumns() {
    return 7;
}

uint32_t TransportAnimationRec::GetRowSize() {
    return 28;
}

bool TransportAnimationRec::NeedIDAssigned() {
    return false;
}

int32_t TransportAnimationRec::GetID() {
    return this->m_ID;
}

void TransportAnimationRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool TransportAnimationRec::Read(SFile* f, const char* stringBuffer) {
    (void)stringBuffer;

    return SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_transportID, sizeof(this->m_transportID), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_timeIndex, sizeof(this->m_timeIndex), nullptr, nullptr, nullptr)
        && SFile::Read(f, this->m_pos, sizeof(this->m_pos), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_sequenceID, sizeof(this->m_sequenceID), nullptr, nullptr, nullptr);
}
