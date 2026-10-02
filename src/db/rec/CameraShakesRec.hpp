#ifndef DB_REC_CAMERA_SHAKES_REC_HPP
#define DB_REC_CAMERA_SHAKES_REC_HPP

#include <cstdint>

class SFile;

// CameraShakes.dbc: one shake a spell or event plays on the camera (CGCamera::AddShakeByID).
class CameraShakesRec {
    public:
        int32_t m_ID;
        int32_t m_shakeType;    // 1 = decays exponentially
        int32_t m_direction;    // 0 forward, 1 sideways, 2 vertical
        float m_amplitude;
        float m_frequency;
        float m_duration;
        float m_phase;
        float m_coefficient;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
