#include "db/rec/SpellChainEffectsRec.hpp"
#include "util/SFile.hpp"

const char* SpellChainEffectsRec::GetFilename() {
    return "DBFilesClient\\SpellChainEffects.dbc";
}

uint32_t SpellChainEffectsRec::GetNumColumns() {
    return SpellChainEffectsRec::COLUMN_COUNT;
}

uint32_t SpellChainEffectsRec::GetRowSize() {
    return 177;
}

bool SpellChainEffectsRec::NeedIDAssigned() {
    return false;
}

int32_t SpellChainEffectsRec::GetID() {
    return this->m_ID;
}

void SpellChainEffectsRec::SetID(int32_t id) {
    this->m_ID = id;
}

bool SpellChainEffectsRec::Read(SFile* f, const char* stringBuffer) {
    uint32_t first[39];
    uint8_t packed[5];
    uint32_t last[4];

    if (!SFile::Read(f, first, sizeof(first), nullptr, nullptr, nullptr)
        || !SFile::Read(f, packed, sizeof(packed), nullptr, nullptr, nullptr)
        || !SFile::Read(f, last, sizeof(last), nullptr, nullptr, nullptr)) {
        return false;
    }

    auto asFloat = [](uint32_t v) { return *reinterpret_cast<const float*>(&v); };
    auto asString = [stringBuffer](uint32_t v) { return stringBuffer ? &stringBuffer[v] : ""; };

    this->m_ID = static_cast<int32_t>(first[0]);
    this->m_avgSegLen = asFloat(first[1]);
    this->m_width = asFloat(first[2]);
    this->m_noiseScale = asFloat(first[3]);
    this->m_texCoordScale = asFloat(first[4]);
    this->m_segDuration = static_cast<int32_t>(first[5]);
    this->m_segDelay = static_cast<int32_t>(first[6]);
    this->m_texture = asString(first[7]);
    this->m_flags = first[8];
    this->m_jointCount = static_cast<int32_t>(first[9]);
    this->m_jointOffsetRadius = asFloat(first[10]);
    this->m_jointsPerMinorJoint = static_cast<int32_t>(first[11]);
    this->m_minorJointsPerMajorJoint = static_cast<int32_t>(first[12]);
    this->m_minorJointScale = asFloat(first[13]);
    this->m_majorJointScale = asFloat(first[14]);
    this->m_jointMoveSpeed = asFloat(first[15]);
    this->m_jointSmoothness = asFloat(first[16]);
    this->m_minDurationBetweenJoints = asFloat(first[17]);
    this->m_maxDurationBetweenJoints = asFloat(first[18]);
    this->m_waveHeight = asFloat(first[19]);
    this->m_waveFreq = asFloat(first[20]);
    this->m_waveSpeed = asFloat(first[21]);
    this->m_minWaveAngle = asFloat(first[22]);
    this->m_maxWaveAngle = asFloat(first[23]);
    this->m_minWaveSpin = asFloat(first[24]);
    this->m_maxWaveSpin = asFloat(first[25]);
    this->m_arcHeight = asFloat(first[26]);
    this->m_minArcAngle = asFloat(first[27]);
    this->m_maxArcAngle = asFloat(first[28]);
    this->m_minArcSpin = asFloat(first[29]);
    this->m_maxArcSpin = asFloat(first[30]);
    this->m_delayBetweenEffects = asFloat(first[31]);
    this->m_minFlickerOnDuration = asFloat(first[32]);
    this->m_maxFlickerOnDuration = asFloat(first[33]);
    this->m_minFlickerOffDuration = asFloat(first[34]);
    this->m_maxFlickerOffDuration = asFloat(first[35]);
    this->m_pulseSpeed = asFloat(first[36]);
    this->m_pulseOnLength = asFloat(first[37]);
    this->m_pulseFadeLength = asFloat(first[38]);
    this->m_alpha = packed[0];
    this->m_red = packed[1];
    this->m_green = packed[2];
    this->m_blue = packed[3];
    this->m_blendMode = packed[4];
    this->m_combo = asString(last[0]);
    this->m_renderLayer = static_cast<int32_t>(last[1]);
    this->m_textureLength = asFloat(last[2]);
    this->m_wavePhase = asFloat(last[3]);

    return true;
}
