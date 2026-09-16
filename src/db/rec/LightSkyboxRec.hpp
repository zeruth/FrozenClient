#ifndef DB_REC_LIGHT_SKYBOX_REC_HPP
#define DB_REC_LIGHT_SKYBOX_REC_HPP

#include <cstdint>

class SFile;

// LightSkybox.dbc: maps a skybox id (referenced by LightParams) to the M2/MDX sky model a zone
// draws instead of a plain gradient (e.g. the Death Knight start's Environments\Stars\IceCrownSky).
class LightSkyboxRec {
    public:
        int32_t m_ID;
        const char* m_name = nullptr;
        int32_t m_flags;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
