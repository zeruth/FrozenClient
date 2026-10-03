#ifndef DB_REC_SCREEN_EFFECT_REC_HPP
#define DB_REC_SCREEN_EFFECT_REC_HPP

#include <cstdint>

class SFile;

// A full-screen effect an aura or the player's state can put up: which effect (0 glow, 1 death,
// 2 nether, 3 the special effect), the special effect's three parameters, and the light
// parameters, ambience and music it forces while it runs.
class ScreenEffectRec {
    public:
        int32_t m_ID;                   // +0x00
        const char* m_name;             // +0x04
        int32_t m_effect;               // +0x08
        uint32_t m_params[3];           // +0x0c: colour, edge size, grey scale
        int32_t m_field18;              // +0x18
        int32_t m_lightParamsID;        // +0x1c
        int32_t m_soundAmbienceID;      // +0x20
        int32_t m_zoneMusicID;          // +0x24

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
