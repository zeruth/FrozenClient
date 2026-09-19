#ifndef UI_GAME_C_G_COOLDOWN_HPP
#define UI_GAME_C_G_COOLDOWN_HPP

#include "ui/simple/CSimpleFrame.hpp"

class CGCooldown : public CSimpleFrame {
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
        // The two flags the script surface can set. The reference keeps them at +0x2ac and +0x3d8.
        //
        // Nothing draws the swirl yet, so these record what the interface asked for rather than
        // changing anything on screen. Their initial values are not recovered -- the reference sets
        // them in a constructor that has not been decompiled -- so both start false here, which is
        // the neutral reading and not a claim about the original.
        int32_t m_reverse = 0;
        int32_t m_drawEdge = 0;

        // TODO the rest

        // Virtual member functions
        virtual int32_t GetScriptMetaTable();

        // Member functions
        CGCooldown(CSimpleFrame* parent);
};

#endif
