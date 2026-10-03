#ifndef DB_REC_SPELL_CHAIN_EFFECTS_REC_HPP
#define DB_REC_SPELL_CHAIN_EFFECTS_REC_HPP

#include <cstdint>

class SFile;

// SpellChainEffects.dbc: how a chain lightning bolt looks and moves. 48 columns in 177 bytes --
// the colour and blend mode are five single bytes between the dwords.
class SpellChainEffectsRec {
    public:
        static const int32_t COLUMN_COUNT = 48;

        int32_t m_ID;
        float m_avgSegLen;
        float m_width;
        float m_noiseScale;
        float m_texCoordScale;
        int32_t m_segDuration;
        int32_t m_segDelay;
        const char* m_texture;
        uint32_t m_flags;
        int32_t m_jointCount;
        float m_jointOffsetRadius;
        int32_t m_jointsPerMinorJoint;
        int32_t m_minorJointsPerMajorJoint;
        float m_minorJointScale;
        float m_majorJointScale;
        float m_jointMoveSpeed;
        float m_jointSmoothness;
        float m_minDurationBetweenJoints;
        float m_maxDurationBetweenJoints;
        float m_waveHeight;
        float m_waveFreq;
        float m_waveSpeed;
        float m_minWaveAngle;
        float m_maxWaveAngle;
        float m_minWaveSpin;
        float m_maxWaveSpin;
        float m_arcHeight;
        float m_minArcAngle;
        float m_maxArcAngle;
        float m_minArcSpin;
        float m_maxArcSpin;
        float m_delayBetweenEffects;
        float m_minFlickerOnDuration;
        float m_maxFlickerOnDuration;
        float m_minFlickerOffDuration;
        float m_maxFlickerOffDuration;
        float m_pulseSpeed;
        float m_pulseOnLength;
        float m_pulseFadeLength;
        uint8_t m_alpha;
        uint8_t m_red;
        uint8_t m_green;
        uint8_t m_blue;
        uint8_t m_blendMode;
        const char* m_combo;
        int32_t m_renderLayer;
        float m_textureLength;
        float m_wavePhase;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
