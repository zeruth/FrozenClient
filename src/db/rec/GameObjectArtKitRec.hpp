#ifndef DB_REC_GAME_OBJECT_ART_KIT_REC_HPP
#define DB_REC_GAME_OBJECT_ART_KIT_REC_HPP

#include <cstdint>

class SFile;

// GameObjectArtKit.dbc (0x00ad3924): the textures and models a game object's GAMEOBJECT_BYTES_1
// art kit swaps onto its model -- the banner a capture point flies, the emblem on a siege door.
// The three textures replace the model's texture slots 11..13 and the four models hang at
// attachments 0..3 (FUN_00713da0).
class GameObjectArtKitRec {
    public:
        int32_t m_ID;
        const char* m_textureVariation[3];
        const char* m_attachModel[4];

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
