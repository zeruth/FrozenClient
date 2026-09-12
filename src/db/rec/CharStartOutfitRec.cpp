#include "db/rec/CharStartOutfitRec.hpp"
#include "util/SFile.hpp"

const char* CharStartOutfitRec::GetFilename() {
    return "DBFilesClient\\CharStartOutfit.dbc";
}

uint32_t CharStartOutfitRec::GetNumColumns() {
    return 77;
}

uint32_t CharStartOutfitRec::GetRowSize() {
    return 296;
}

bool CharStartOutfitRec::NeedIDAssigned() {
    return false;
}

int32_t CharStartOutfitRec::GetID() {
    return this->m_ID;
}

void CharStartOutfitRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool CharStartOutfitRec::Read(SFile* f, const char* stringBuffer) {
    if (
        !SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_raceID, sizeof(this->m_raceID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_classID, sizeof(this->m_classID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_sexID, sizeof(this->m_sexID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_outfitID, sizeof(this->m_outfitID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, this->m_itemID, sizeof(this->m_itemID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, this->m_displayItemID, sizeof(this->m_displayItemID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, this->m_inventoryType, sizeof(this->m_inventoryType), nullptr, nullptr, nullptr)
    ) {
        return false;
    }

    return true;
}
