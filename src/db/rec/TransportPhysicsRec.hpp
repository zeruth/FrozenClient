// TransportPhysics.dbc -- how a ship rocks (ShipPath::Bob): the swell's amplitude and time scale, the roll's and the
// pitch's, the bank into a turn and the turn rate it is reached at, and the speed below which it
// is damped and by how much.
//
// Eleven columns of 44 bytes.
#ifndef DB_REC_TRANSPORT_PHYSICS_REC_HPP
#define DB_REC_TRANSPORT_PHYSICS_REC_HPP

#include <cstdint>

class SFile;

class TransportPhysicsRec {
    public:
        int32_t m_ID;
        float m_waveAmp;
        float m_waveTimeScale;
        float m_rollAmp;
        float m_rollTimeScale;
        float m_pitchAmp;
        float m_pitchTimeScale;
        float m_maxBank;
        float m_maxBankTurnSpeed;
        float m_speedDampThresh;
        float m_speedDamp;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
