// TaxiPathNode.dbc -- a taxi path's points in order, sorted by path (FUN_007f7ad0 searches it): the map, the place,
// flags (0x1 teleports to the next node, 0x2 stops), the stop's delay in seconds, and the events
// sounded on arriving and leaving.
//
// Eleven columns of 44 bytes.
#ifndef DB_REC_TAXI_PATH_NODE_REC_HPP
#define DB_REC_TAXI_PATH_NODE_REC_HPP

#include <cstdint>

class SFile;

class TaxiPathNodeRec {
    public:
        int32_t m_ID;
        int32_t m_pathID;
        int32_t m_nodeIndex;
        int32_t m_mapID;
        float m_loc[3];
        uint32_t m_flags;
        uint32_t m_delay;
        uint32_t m_arrivalEventID;
        uint32_t m_departureEventID;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
