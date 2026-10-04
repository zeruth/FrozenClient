#include "db/rec/ObjectEffectModifierRec.hpp"
#include "util/SFile.hpp"

const char* ObjectEffectModifierRec::GetFilename() {
    return "DBFilesClient\\ObjectEffectModifier.dbc";
}

uint32_t ObjectEffectModifierRec::GetNumColumns() {
    return 8;
}

uint32_t ObjectEffectModifierRec::GetRowSize() {
    return 32;
}

bool ObjectEffectModifierRec::NeedIDAssigned() {
    return false;
}

int32_t ObjectEffectModifierRec::GetID() {
    return this->m_ID;
}

void ObjectEffectModifierRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool ObjectEffectModifierRec::Read(SFile* f, const char* stringBuffer) {
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_inputType, sizeof(this->m_inputType), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_mapType, sizeof(this->m_mapType), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_outputType, sizeof(this->m_outputType), nullptr, nullptr, nullptr)
        || !SFile::Read(f, this->m_param, sizeof(this->m_param), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    return true;
}
