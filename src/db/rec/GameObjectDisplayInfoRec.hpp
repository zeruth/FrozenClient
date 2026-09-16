#ifndef DB_REC_GAME_OBJECT_DISPLAY_INFO_REC_HPP
#define DB_REC_GAME_OBJECT_DISPLAY_INFO_REC_HPP

#include <cstdint>

class SFile;

// GameObjectDisplayInfo.dbc: a game object's display id resolves to the model it draws.
//
// Without this, CGGameObject_C had no way to name a model and every game object in the world was
// invisible -- 136 of them at the Ebon Hold spawn alone, which is most of what the zone is built
// from.
//
// 3790 rows in 3.3.5a. 94% of m_modelName are .mdx (M2 models), 4% .wmo, the rest empty.
class GameObjectDisplayInfoRec {
    public:
        int32_t m_ID;
        const char* m_modelName;
        int32_t m_sound[10];
        float m_geoBoxMin[3];
        float m_geoBoxMax[3];
        int32_t m_objectEffectPackageID;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
