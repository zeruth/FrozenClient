#include "db/rec/SpellVisualRec.hpp"
#include "util/SFile.hpp"

const char* SpellVisualRec::GetFilename() {
    return "DBFilesClient\\SpellVisual.dbc";
}

uint32_t SpellVisualRec::GetNumColumns() {
    return SpellVisualRec::COLUMN_COUNT;
}

uint32_t SpellVisualRec::GetRowSize() {
    return SpellVisualRec::COLUMN_COUNT * 4;
}

bool SpellVisualRec::NeedIDAssigned() {
    return false;
}

int32_t SpellVisualRec::GetID() {
    return this->m_ID;
}

void SpellVisualRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SpellVisualRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t columns[SpellVisualRec::COLUMN_COUNT];

    if (!SFile::Read(f, columns, sizeof(columns), nullptr, nullptr, nullptr)) {
        return false;
    }

    this->m_ID = static_cast<int32_t>(columns[0]);
    this->m_precastKit = static_cast<int32_t>(columns[1]);
    this->m_castKit = static_cast<int32_t>(columns[2]);
    this->m_impactKit = static_cast<int32_t>(columns[3]);
    this->m_stateKit = static_cast<int32_t>(columns[4]);
    this->m_stateDoneKit = static_cast<int32_t>(columns[5]);
    this->m_channelKit = static_cast<int32_t>(columns[6]);
    this->m_hasMissile = static_cast<int32_t>(columns[7]);
    this->m_missileModel = static_cast<int32_t>(columns[8]);
    this->m_missilePathType = static_cast<int32_t>(columns[9]);
    this->m_missileDestinationAttachment = static_cast<int32_t>(columns[10]);
    this->m_missileSound = static_cast<int32_t>(columns[11]);
    this->m_animEventSoundID = static_cast<int32_t>(columns[12]);
    this->m_flags = static_cast<int32_t>(columns[13]);
    this->m_casterImpactKit = static_cast<int32_t>(columns[14]);
    this->m_targetImpactKit = static_cast<int32_t>(columns[15]);
    this->m_missileAttachment = static_cast<int32_t>(columns[16]);
    this->m_missileFollowGroundHeight = static_cast<int32_t>(columns[17]);
    this->m_missileFollowGroundDropSpeed = static_cast<int32_t>(columns[18]);
    this->m_missileFollowGroundApproach = static_cast<int32_t>(columns[19]);
    this->m_missileFollowGroundFlags = static_cast<int32_t>(columns[20]);
    this->m_missileMotion = static_cast<int32_t>(columns[21]);
    this->m_missileTargetingKit = static_cast<int32_t>(columns[22]);
    this->m_instantAreaKit = static_cast<int32_t>(columns[23]);
    this->m_impactAreaKit = static_cast<int32_t>(columns[24]);
    this->m_persistentAreaKit = static_cast<int32_t>(columns[25]);

    for (int32_t i = 0; i < 3; i++) {
        this->m_missileCastOffset[i] = *reinterpret_cast<const float*>(&columns[26 + i]);
        this->m_missileImpactOffset[i] = *reinterpret_cast<const float*>(&columns[29 + i]);
    }

    return true;
}
