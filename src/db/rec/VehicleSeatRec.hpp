#ifndef DB_REC_VEHICLE_SEAT_REC_HPP
#define DB_REC_VEHICLE_SEAT_REC_HPP

#include <cstdint>

class SFile;

// VehicleSeat.dbc: one seat on a vehicle. The animation columns are why this is here: a rider's pose
// is the seat's to choose, with a start/loop pair for each phase of the ride (entering, aboard,
// leaving) and a second pair for the upper body while aboard. CVehiclePassenger_C picks from them.
//
// 58 columns, counted from the reference's own VehicleSeatRec::Read (0x008bad60 makes 58 field
// reads, the last landing at +0xe4).
class VehicleSeatRec {
    public:
        static const int32_t COLUMN_COUNT = 58;

        int32_t m_ID;
        // 0x1 the seat animates its rider while entering, 0x2 while aboard, 0x8 and 0x8000 the two
        // ways it may animate them while leaving (CGUnit_C::SeatAllowsExitAnimation picks which of
        // the two applies from the vehicle's own movement state).
        int32_t m_flags;
        int32_t m_attachmentID;
        float m_attachmentOffset[3];
        float m_enterPreDelay;
        float m_enterSpeed;
        float m_enterGravity;
        int32_t m_enterMinDuration;
        int32_t m_enterMaxDuration;
        float m_enterMinArcHeight;
        float m_enterMaxArcHeight;
        int32_t m_enterAnimStart;
        int32_t m_enterAnimLoop;
        int32_t m_rideAnimStart;
        int32_t m_rideAnimLoop;
        int32_t m_rideUpperAnimStart;
        int32_t m_rideUpperAnimLoop;
        float m_exitPreDelay;
        float m_exitSpeed;
        float m_exitGravity;
        int32_t m_exitMinDuration;
        int32_t m_exitMaxDuration;
        float m_exitMinArcHeight;
        float m_exitMaxArcHeight;
        int32_t m_exitAnimStart;
        int32_t m_exitAnimLoop;
        int32_t m_exitAnimEnd;
        float m_passengerYaw;                   // +0x74
        float m_passengerPitch;                 // +0x78
        float m_passengerRoll;                  // +0x7c
        int32_t m_passengerAttachmentID;        // +0x80
        int32_t m_vehicleEnterAnim;             // +0x84
        int32_t m_vehicleExitAnim;              // +0x88
        int32_t m_vehicleRideAnimLoop;          // +0x8c
        int32_t m_vehicleEnterAnimBone;         // +0x90
        int32_t m_vehicleExitAnimBone;          // +0x94
        int32_t m_vehicleRideAnimLoopBone;      // +0x98
        float m_vehicleEnterAnimDelay;          // +0x9c
        float m_vehicleExitAnimDelay;           // +0xa0
        int32_t m_vehicleAbilityDisplay;        // +0xa4
        int32_t m_enterUISoundID;               // +0xa8
        int32_t m_exitUISoundID;                // +0xac
        int32_t m_uiSkin;                       // +0xb0
        int32_t m_flagsB;                       // +0xb4
        float m_cameraEnteringDelay;            // +0xb8
        float m_cameraEnteringDuration;         // +0xbc
        float m_cameraExitingDelay;             // +0xc0
        float m_cameraExitingDuration;          // +0xc4
        float m_cameraOffset[3];                // +0xc8
        float m_cameraPosChaseRate;             // +0xd4
        float m_cameraFacingChaseRate;          // +0xd8
        float m_cameraEnteringZoom;             // +0xdc
        float m_cameraSeatZoomMin;              // +0xe0
        float m_cameraSeatZoomMax;              // +0xe4

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
