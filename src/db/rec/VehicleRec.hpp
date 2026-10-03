#ifndef DB_REC_VEHICLE_REC_HPP
#define DB_REC_VEHICLE_REC_HPP

#include <cstdint>

class SFile;

// Vehicle.dbc: a vehicle's behaviour (flags), how fast it turns and pitches and between which
// pitches, its eight seats (VehicleSeat ids), how the camera follows it, the missile-target arc
// the UI draws for an aimed vehicle (textures, models, radii), and the seat indicator and power
// bars the vehicle UI shows. Forty columns of 160 bytes; the reference reads the indicator at
// +0x90.
class VehicleRec {
    public:
        int32_t m_ID;                           // +0x00
        int32_t m_flags;                        // +0x04
        float m_turnSpeed;                      // +0x08
        float m_pitchSpeed;                     // +0x0c
        float m_pitchMin;                       // +0x10
        float m_pitchMax;                       // +0x14
        int32_t m_seatID[8];                    // +0x18
        float m_mouseLookOffsetPitch;           // +0x38
        float m_cameraFadeDistScalarMin;        // +0x3c
        float m_cameraFadeDistScalarMax;        // +0x40
        float m_cameraPitchOffset;              // +0x44
        float m_facingLimitRight;               // +0x48
        float m_facingLimitLeft;                // +0x4c
        float m_msslTrgtTurnLingering;          // +0x50
        float m_msslTrgtPitchLingering;         // +0x54
        float m_msslTrgtMouseLingering;         // +0x58
        float m_msslTrgtEndOpacity;             // +0x5c
        float m_msslTrgtArcSpeed;               // +0x60
        float m_msslTrgtArcRepeat;              // +0x64
        float m_msslTrgtArcWidth;               // +0x68
        float m_msslTrgtImpactRadius[2];        // +0x6c
        const char* m_msslTrgtArcTexture;       // +0x74
        const char* m_msslTrgtImpactTexture;    // +0x78
        const char* m_msslTrgtImpactModel[2];   // +0x7c
        float m_cameraYawOffset;                // +0x84
        int32_t m_uiLocomotionType;             // +0x88
        float m_msslTrgtImpactTexRadius;        // +0x8c
        int32_t m_vehicleUIIndicatorID;         // +0x90
        int32_t m_powerDisplayID[3];            // +0x94

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
