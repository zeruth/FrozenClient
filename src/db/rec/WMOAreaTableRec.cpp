#include "db/rec/WMOAreaTableRec.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* WMOAreaTableRec::GetFilename() {
    return "DBFilesClient\\WMOAreaTable.dbc";
}

uint32_t WMOAreaTableRec::GetNumColumns() {
    return 28;
}

uint32_t WMOAreaTableRec::GetRowSize() {
    return 112;
}

bool WMOAreaTableRec::NeedIDAssigned() {
    return false;
}

int32_t WMOAreaTableRec::GetID() {
    return this->m_ID;
}

void WMOAreaTableRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool WMOAreaTableRec::Read(SFile* f, const char* stringBuffer) {
    int32_t* fields[11] = {
        &this->m_ID, &this->m_wmoID, &this->m_nameSetID, &this->m_wmoGroupID,
        &this->m_soundProviderPref, &this->m_soundProviderPrefUnderwater, &this->m_ambienceID,
        &this->m_zoneMusic, &this->m_introSound, &this->m_flags, &this->m_areaTableID
    };

    for (auto field : fields) {
        if (!SFile::Read(f, field, sizeof(int32_t), nullptr, nullptr, nullptr)) {
            return false;
        }
    }

    uint32_t areaNameOfs[16];
    uint32_t areaNameMask;

    for (auto& ofs : areaNameOfs) {
        if (!SFile::Read(f, &ofs, sizeof(uint32_t), nullptr, nullptr, nullptr)) {
            return false;
        }
    }

    if (!SFile::Read(f, &areaNameMask, sizeof(uint32_t), nullptr, nullptr, nullptr)) {
        return false;
    }

    this->m_areaName = stringBuffer ? &stringBuffer[areaNameOfs[CURRENT_LANGUAGE]] : "";

    return true;
}
