#ifndef DB_REC_LIGHT_PARAMS_REC_HPP
#define DB_REC_LIGHT_PARAMS_REC_HPP

#include <cstdint>

class SFile;

// LightParams.dbc: a LightParams row (referenced by Light.dbc per weather) carries the sky's higher
// level settings, notably m_lightSkyboxID -> LightSkybox.dbc (the sky model to draw for this light).
class LightParamsRec {
    public:
        int32_t m_ID;
        int32_t m_highlightSky;
        int32_t m_lightSkyboxID;
        int32_t m_cloudTypeID;
        float m_glow;
        float m_waterShallowAlpha;
        float m_waterDeepAlpha;
        float m_oceanShallowAlpha;
        float m_oceanDeepAlpha;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
