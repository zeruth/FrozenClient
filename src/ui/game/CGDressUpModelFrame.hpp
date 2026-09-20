#ifndef UI_GAME_C_G_DRESS_UP_MODEL_FRAME_HPP
#define UI_GAME_C_G_DRESS_UP_MODEL_FRAME_HPP

#include "ui/game/CGCharacterModelBase.hpp"

class CGDressUpModelFrame : public CGCharacterModelBase {
    public:
        // Static variables
        static int32_t s_metatable;
        static int32_t s_objectType;

        // Static functions
        static CSimpleFrame* Create(CSimpleFrame* parent);
        static void CreateScriptMetaTable();
        static int32_t GetObjectType();

        // Without this the frame answers only to its base's types, and every one of its own
        // script methods fails the This() check with "Wrong object type for member function" --
        // the methods are registered and reachable, the object just denies being what it is.
        virtual bool IsA(int32_t type);
        static void RegisterScriptMethods(lua_State* L);

        // Member variables
        // TODO

        // Virtual member functions
        virtual int32_t GetScriptMetaTable();

        // Member functions
        CGDressUpModelFrame(CSimpleFrame* parent);
};

#endif
