#include "db/rec/ItemSubClassRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

namespace {

// One localized string block: sixteen offsets and the mask behind them.
bool ReadLocalized(SFile* f, const char* stringBuffer, const char*& out) {
    uint32_t offsets[16];
    uint32_t mask;

    for (int32_t i = 0; i < 16; i++) {
        if (!SFile::Read(f, &offsets[i], sizeof(uint32_t), nullptr, nullptr, nullptr)) {
            return false;
        }
    }

    if (!SFile::Read(f, &mask, sizeof(uint32_t), nullptr, nullptr, nullptr)) {
        return false;
    }

    out = stringBuffer ? &stringBuffer[offsets[CURRENT_LANGUAGE]] : "";

    return true;
}

} // namespace

const char* ItemSubClassRec::GetFilename() {
    return "DBFilesClient\\ItemSubClass.dbc";
}

uint32_t ItemSubClassRec::GetNumColumns() {
    return 44;
}

uint32_t ItemSubClassRec::GetRowSize() {
    return 176;
}

bool ItemSubClassRec::NeedIDAssigned() {
    return true;
}

int32_t ItemSubClassRec::GetID() {
    return this->m_ID;
}

void ItemSubClassRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool ItemSubClassRec::Read(SFile* f, const char* stringBuffer) {
    int32_t* fields[] = {
        &this->m_classID,
        &this->m_subClassID,
        &this->m_prerequisiteProficiency,
        &this->m_postrequisiteProficiency,
        &this->m_flags,
        &this->m_displayFlags,
        &this->m_weaponParrySeq,
        &this->m_weaponReadySeq,
        &this->m_weaponAttackSeq,
        &this->m_weaponSwingSize,
    };

    for (auto field : fields) {
        if (!SFile::Read(f, field, sizeof(int32_t), nullptr, nullptr, nullptr)) {
            return false;
        }
    }

    return ReadLocalized(f, stringBuffer, this->m_displayName)
        && ReadLocalized(f, stringBuffer, this->m_verboseName);
}
