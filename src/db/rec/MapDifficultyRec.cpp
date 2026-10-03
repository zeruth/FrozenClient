#include "db/rec/MapDifficultyRec.hpp"
#include "db/Db.hpp"
#include "util/Locale.hpp"
#include "util/SFile.hpp"

const char* MapDifficultyRec::GetFilename() {
    return "DBFilesClient\\MapDifficulty.dbc";
}

uint32_t MapDifficultyRec::GetNumColumns() {
    return 23;
}

uint32_t MapDifficultyRec::GetRowSize() {
    return 92;
}

bool MapDifficultyRec::NeedIDAssigned() {
    return false;
}

int32_t MapDifficultyRec::GetID() {
    return this->m_ID;
}

void MapDifficultyRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool MapDifficultyRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t messageOfs[16];
    uint32_t messageMask;
    uint32_t difficultyStringOfs;

    if (!SFile::Read(f, &this->m_ID, sizeof(this->m_ID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_mapID, sizeof(this->m_mapID), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_difficulty, sizeof(this->m_difficulty), nullptr, nullptr, nullptr)
        || !SFile::Read(f, messageOfs, sizeof(messageOfs), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &messageMask, sizeof(messageMask), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_raidDuration, sizeof(this->m_raidDuration), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &this->m_maxPlayers, sizeof(this->m_maxPlayers), nullptr, nullptr, nullptr)
        || !SFile::Read(f, &difficultyStringOfs, sizeof(difficultyStringOfs), nullptr, nullptr, nullptr)) {
        return false;
    }

    if (stringBuffer) {
        this->m_message = &stringBuffer[messageOfs[CURRENT_LANGUAGE]];
        this->m_difficultyString = &stringBuffer[difficultyStringOfs];
    } else {
        this->m_message = "";
        this->m_difficultyString = "";
    }

    return true;
}

// ref: FUN_00634950
const MapDifficultyRec* MapDifficultyFind(int32_t mapID, int32_t difficulty, int32_t* index) {
    int32_t count = g_mapDifficultyDB.GetNumRecords();

    for (int32_t i = 0; i < count; i++) {
        if (g_mapDifficultyDB.GetRecordByIndex(i)->m_mapID != mapID) {
            continue;
        }

        for (int32_t j = i; j < count; j++) {
            auto row = g_mapDifficultyDB.GetRecordByIndex(j);

            if (row->m_mapID != mapID) {
                return nullptr;
            }

            if (row->m_difficulty == difficulty) {
                if (index) {
                    *index = j;
                }

                return row;
            }
        }
    }

    return nullptr;
}
