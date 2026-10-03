// TransportAnimation.dbc -- a transport's (an elevator's) path: per game object entry, the time
// each key is reached, the offset from the object's place, and the sequence its model plays.
//
// Seven columns of 28 bytes, sorted by entry; the transport type binary-searches it
// (FUN_0070c8c0) and reads +0x08 (the time), +0x0c..+0x14 (the offset) and +0x18 (the sequence).
#ifndef DB_REC_TRANSPORT_ANIMATION_REC_HPP
#define DB_REC_TRANSPORT_ANIMATION_REC_HPP

#include <cstdint>

class SFile;

class TransportAnimationRec {
    public:
        int32_t m_ID;
        int32_t m_transportID;
        uint32_t m_timeIndex;
        float m_pos[3];
        int32_t m_sequenceID;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
