#include "db/rec/GameTipsRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* GameTipsRec::GetFilename() {
    return "DBFilesClient\\GameTips.dbc";
}

uint32_t GameTipsRec::GetNumColumns() {
    return 18;
}

uint32_t GameTipsRec::GetRowSize() {
    return 72;
}

bool GameTipsRec::NeedIDAssigned() {
    return false;
}

int32_t GameTipsRec::GetID() {
    return this->m_ID;
}

void GameTipsRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool GameTipsRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t textOfs[16];
    uint32_t textMask;

    if (!SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)) {
        return false;
    }

    for (int32_t i = 0; i < 16; i++) {
        if (!SFile::Read(f, &textOfs[i], sizeof(uint32_t), nullptr, nullptr, nullptr)) {
            return false;
        }
    }

    if (!SFile::Read(f, &textMask, sizeof(textMask), nullptr, nullptr, nullptr)) {
        return false;
    }

    this->m_text = stringBuffer ? &stringBuffer[textOfs[CURRENT_LANGUAGE]] : "";

    return true;
}
