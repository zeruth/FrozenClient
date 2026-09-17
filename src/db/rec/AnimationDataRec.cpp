#include "db/rec/AnimationDataRec.hpp"
#include "util/SFile.hpp"

const char* AnimationDataRec::GetFilename() {
    return "DBFilesClient\\AnimationData.dbc";
}

uint32_t AnimationDataRec::GetNumColumns() {
    return 8;
}

uint32_t AnimationDataRec::GetRowSize() {
    return 32;
}

bool AnimationDataRec::NeedIDAssigned() {
    return false;
}

int32_t AnimationDataRec::GetID() {
    return this->m_ID;
}

void AnimationDataRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool AnimationDataRec::Read(SFile* f, const char* stringBuffer) {
    return SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_nameOffset, sizeof(this->m_nameOffset), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_weaponFlags, sizeof(this->m_weaponFlags), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_bodyFlags, sizeof(this->m_bodyFlags), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_flags, sizeof(this->m_flags), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_fallback, sizeof(this->m_fallback), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_behaviorID, sizeof(this->m_behaviorID), nullptr, nullptr, nullptr)
        && SFile::Read(f, &this->m_behaviorTier, sizeof(this->m_behaviorTier), nullptr, nullptr, nullptr);
}
