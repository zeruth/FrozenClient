#ifndef DB_REC_VEHICLE_REC_HPP
#define DB_REC_VEHICLE_REC_HPP

#include <cstdint>

// Vehicle.dbc, PARTIAL and NOT LOADED. The reference keeps a row on every CVehicle_C and reads its
// flags and its eight seat ids; only those are declared here, at the reference's offsets, so the
// checks that consult them can be ported exactly. There is deliberately no Read/GetFilename and no
// entry in Db.cpp: frozen creates no CVehicle_C, so no row is ever needed, and the column count is
// not settled (the reference's Vehicle::Read at 0x008ba930 makes 30 field reads with the last
// landing at +0x94, which is consistent with several of them being arrays). Finish the row -- count
// the columns the way VehicleSeatRec's were counted -- before anything tries to load it.
class VehicleRec {
    public:
        int32_t m_ID;           // +0x00
        int32_t m_flags;        // +0x04
        float m_turnSpeed;      // +0x08
        float m_pitchSpeed;     // +0x0c
        float m_pitchMin;       // +0x10
        float m_pitchMax;       // +0x14
        int32_t m_seatID[8];    // +0x18
};

#endif
