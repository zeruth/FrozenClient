#ifndef UI_GAME_C_G_TABARD_MODEL_FRAME_HPP
#define UI_GAME_C_G_TABARD_MODEL_FRAME_HPP

#include "ui/game/CGCharacterModelBase.hpp"

class CGTabardModelFrame : public CGCharacterModelBase {
    public:
        // Static variables
        static int32_t s_metatable;
        static int32_t s_objectType;

        // Static functions
        static CSimpleFrame* Create(CSimpleFrame* parent);
        static void CreateScriptMetaTable();
        static int32_t GetObjectType();
        static void RegisterScriptMethods(lua_State* L);

        // Member variables

        // The five tabard variations the frame edits, laid out the way the reference lays them out
        // at +0x380..+0x390 and indexed in that order by CycleVariation(1..5). The pairing is fixed
        // by the file-name builders: the emblem name takes the first two, the border name the next
        // two, and the background name the last one. Within each pair the style comes before the
        // colour, matching the order the guild tabard fields arrive in.
        int32_t m_emblemStyle = 0;
        int32_t m_emblemColor = 0;
        int32_t m_borderStyle = 0;
        int32_t m_borderColor = 0;
        int32_t m_backgroundColor = 0;

        // Virtual member functions
        virtual int32_t GetScriptMetaTable();
        virtual bool IsA(int32_t type);

        // Member functions
        CGTabardModelFrame(CSimpleFrame* parent);
};

#endif
