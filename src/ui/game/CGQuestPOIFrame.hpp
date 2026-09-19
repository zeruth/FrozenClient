#ifndef UI_GAME_C_G_QUEST_POI_FRAME_HPP
#define UI_GAME_C_G_QUEST_POI_FRAME_HPP

#include "ui/simple/CSimpleFrame.hpp"

class CGQuestPOIFrame : public CSimpleFrame {
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
        // Two flags and a count the script surface reaches. The reference keeps the flags as
        // adjacent bytes at +0x2ac and +0x2ad and the count at +0x2bc.
        //
        // Nothing draws points of interest yet, so the flags record what was asked for and the
        // count stays zero -- truthfully, since no tooltips are built rather than as a placeholder.
        bool m_smoothing = false;
        bool m_merging = false;
        int32_t m_numTooltips = 0;

        // TODO the rest

        // Virtual member functions
        virtual int32_t GetScriptMetaTable();

        // Member functions
        CGQuestPOIFrame(CSimpleFrame* parent);
};

#endif
