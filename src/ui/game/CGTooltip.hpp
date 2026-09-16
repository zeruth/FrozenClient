#ifndef UI_GAME_C_G_TOOLTIP_HPP
#define UI_GAME_C_G_TOOLTIP_HPP

#include "ui/simple/CSimpleFrame.hpp"
#include "util/guid/Types.hpp"

class CGTooltip : public CSimpleFrame {
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
        CSimpleFrame* m_owner = nullptr;
        TOOLTIP_ANCHORPOINT m_anchorPoint;
        // TODO
        C2Vector m_offset;
        // TODO

        // How many lines are currently in use. The line font strings themselves are not owned here:
        // GameTooltipTemplate.xml declares <name>TextLeft1..8 / TextRight1..8 as children, so a line
        // is found by name rather than created. The reference makes more on demand when a tooltip
        // needs more than the template declares; that is not done yet, so lines past the last
        // declared one are dropped instead of silently overwriting line 8.
        int32_t m_lineCount = 0;
        float m_minimumWidth = 0.0f;
        float m_padding = 0.0f;
        WOWGUID m_unitGUID = 0;

        // The tooltip's own script elements. FrameXML declares all of these on GameTooltip,
        // WorldMapTooltip and ItemRefTooltip, and without them the interface load reports five
        // "Unknown script element" warnings per tooltip and the handlers never run -- so nothing
        // that hooks tooltip construction (money lines, unit and item decoration, the default
        // anchor) fires at all.
        ScriptIx m_onTooltipSetDefaultAnchor;
        ScriptIx m_onTooltipAddMoney;
        ScriptIx m_onTooltipCleared;
        ScriptIx m_onTooltipSetUnit;
        ScriptIx m_onTooltipSetItem;
        ScriptIx m_onTooltipSetSpell;
        ScriptIx m_onTooltipSetQuest;
        ScriptIx m_onTooltipSetAchievement;

        // Virtual member functions
        virtual bool IsA(int32_t type);
        virtual int32_t GetScriptMetaTable();
        virtual ScriptIx* GetScriptByName(const char* name, ScriptData& data);

        // Member functions
        void RunOnTooltipSetDefaultAnchorScript();
        void RunOnTooltipAddMoneyScript(int32_t money);
        void RunOnTooltipClearedScript();
        void RunOnTooltipSetUnitScript();
        void RunOnTooltipSetItemScript();

        // Member functions
        CGTooltip(CSimpleFrame* parent);
};

#endif
