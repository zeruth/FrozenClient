#ifndef DB_REC_LIGHT_REC_HPP
#define DB_REC_LIGHT_REC_HPP

#include <cstdint>

class SFile;

// Light.dbc: places outdoor light volumes on a map. Each references LightParams rows (one per
// weather condition); the map's default light sits at position (0,0,0).
class LightRec {
    public:
        int32_t m_ID;
        int32_t m_mapID;
        float m_x;
        float m_y;
        float m_z;
        float m_falloffStart;
        float m_falloffEnd;
        int32_t m_params[8];

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
