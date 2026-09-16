#include "db/rec/EmotesRec.hpp"
#include "util/SFile.hpp"

const char* EmotesRec::GetFilename() {
    return "DBFilesClient\\Emotes.dbc";
}

uint32_t EmotesRec::GetNumColumns() {
    return 7;
}

uint32_t EmotesRec::GetRowSize() {
    return 28;
}

bool EmotesRec::NeedIDAssigned() {
    return false;
}

int32_t EmotesRec::GetID() {
    return this->m_ID;
}

void EmotesRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool EmotesRec::Read(SFile* f, const char* stringBuffer) {
    return SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_nameOffset, sizeof(this->m_nameOffset), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_animID, sizeof(this->m_animID), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_flags, sizeof(this->m_flags), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_specProc, sizeof(this->m_specProc), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_specParam, sizeof(this->m_specParam), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_soundID, sizeof(this->m_soundID), nullptr, nullptr, nullptr);
}
