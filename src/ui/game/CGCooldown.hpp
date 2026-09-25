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

        // Without this the frame answers only to its base's types, and every one of its own
        // script methods fails the This() check with "Wrong object type for member function" --
        // the methods are registered and reachable, the object just denies being what it is.
        virtual bool IsA(int32_t type);
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
        // +0x2a8: selects between the two colour sets UpdateColors writes. Its meaning and writer are
        // not recovered.
        int32_t m_unk2a8 = 0;
        // The three vertex colours UpdateColors writes, at the reference's +0x37c, +0x3d0 and +0x42c.
        // Nothing draws with them yet.
        CImVector m_color37c = {};
        CImVector m_color3d0 = {};
        CImVector m_color42c = {};

        // TODO the rest

        // Virtual member functions
        virtual int32_t GetScriptMetaTable();

        // Member functions
        CGCooldown(CSimpleFrame* parent);

        // ref: FUN_005ec790
        // Recolour from the frame's effective alpha.
        void UpdateColors();
};

#endif
