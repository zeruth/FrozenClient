#ifndef DB_REC_WEATHER_REC_HPP
#define DB_REC_WEATHER_REC_HPP

#include <cstdint>

class SFile;

// Weather.dbc (3.3.5a, 8 columns): the weather states SMSG_WEATHER names. effectType selects the
// particle system (1 rain, 2 snow, 3 sand/mist); effectTexture overrides its default sprite.
class WeatherRec {
    public:
        int32_t m_ID;
        int32_t m_ambienceID;
        int32_t m_effectType;
        float m_transitionSkybox;
        float m_effectColor[3];
        const char* m_effectTexture = nullptr;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
