#ifndef DB_REC_VEHICLE_SEAT_REC_HPP
#define DB_REC_VEHICLE_SEAT_REC_HPP

#include <cstdint>

class SFile;

// VehicleSeat.dbc: one seat on a vehicle. The animation columns are why this is here: a rider's pose
// is the seat's to choose, with a start/loop pair for each phase of the ride (entering, aboard,
// leaving) and a second pair for the upper body while aboard. CVehiclePassenger_C picks from them.
//
// 58 columns, counted from the reference's own VehicleSeatRec::Read (0x008bad60 makes 58 field
// reads, the last landing at +0xe4). Columns 29 and up -- passenger orientation, the vehicle's own
// animations, the enter/exit sounds and the camera block -- are not named here; nothing ported
// reads them.
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

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
