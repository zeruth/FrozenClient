#ifndef DB_REC_LIGHT_INT_BAND_REC_HPP
#define DB_REC_LIGHT_INT_BAND_REC_HPP

#include <cstdint>

class SFile;

// LightIntBand.dbc: time-of-day colour bands. Each LightParams owns 18 consecutive rows (band 0 =
// direct/diffuse sun, 1 = ambient, 7 = fog); each row holds up to 16 time-keyed BGRA colours.
class LightIntBandRec {
    public:
        int32_t m_ID;
        uint32_t m_num;
        uint32_t m_times[16];
        uint32_t m_values[16];

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
