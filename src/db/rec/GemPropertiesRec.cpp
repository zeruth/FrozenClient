#include "db/rec/GemPropertiesRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* GemPropertiesRec::GetFilename() {
    return "DBFilesClient\\GemProperties.dbc";
}

uint32_t GemPropertiesRec::GetNumColumns() {
    return GemPropertiesRec::COLUMN_COUNT;
}

uint32_t GemPropertiesRec::GetRowSize() {
    return 20;
}

bool GemPropertiesRec::NeedIDAssigned() {
    return false;
}

int32_t GemPropertiesRec::GetID() {
    return this->m_ID;
}

void GemPropertiesRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool GemPropertiesRec::Read(SFile* f, const char* stringBuffer) {

    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_enchantID, sizeof(this->m_enchantID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_maxCountInv, sizeof(this->m_maxCountInv), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_maxCountItem, sizeof(this->m_maxCountItem), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_type, sizeof(this->m_type), nullptr, nullptr, nullptr)
    ) {
        return false;
    }



    return true;
}
