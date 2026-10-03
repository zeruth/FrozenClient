// TransportRotation.dbc -- a transport's turning: per game object entry, the time each key is
// reached and the rotation (a quaternion, x y z w) at it.
//
// Seven columns of 28 bytes, sorted by entry (FUN_0070c930); +0x08 the time, +0x0c the rotation.
#ifndef DB_REC_TRANSPORT_ROTATION_REC_HPP
#define DB_REC_TRANSPORT_ROTATION_REC_HPP

#include <cstdint>

class SFile;

class TransportRotationRec {
    public:
        int32_t m_ID;
        int32_t m_gameObjectsID;
        uint32_t m_timeIndex;
        float m_rot[4];

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
