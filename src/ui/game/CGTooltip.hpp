#ifndef UI_GAME_C_G_TOOLTIP_HPP
#define UI_GAME_C_G_TOOLTIP_HPP

#include "ui/simple/CSimpleFrame.hpp"
#include <vector>
#include "util/guid/Types.hpp"
#include <storm/Array.hpp>

class CSimpleFontString;

class CGTooltip : public CSimpleFrame {
    public:
        // Structs

        // The pair of font strings that make up one tooltip line. The template declares
        // TextLeft1..8 / TextRight1..8 as named children, so those are found by name; a pair handed
        // over by AddFontStrings extends the tooltip past them.
        struct TOOLTIPLINE {
            CSimpleFontString* left = nullptr;
            CSimpleFontString* right = nullptr;
        };

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
        // needs more than the template declares; that is not done yet, so a line past the last
        // declared one exists only if AddFontStrings was handed a pair for it, and is otherwise
        // dropped rather than silently overwriting line 8.
        int32_t m_lineCount = 0;

        // Lines beyond the ones the template declares, in the order AddFontStrings registered them.
        TSGrowableArray<TOOLTIPLINE> m_extraLines;

        // Whether each line may be broken across several lines, indexed the same way m_lineCount
        // counts. The reference keeps exactly this: a per-line array (its +0x2cc) written by the
        // add-a-line helper and read by the layout pass, which is what decides a line's width.
        // A line carrying right-hand text never wraps -- the reference clears the flag rather than
        // trying to wrap around a second column.
        std::vector<uint8_t> m_lineWrap;

        // The spell the tooltip was last filled from, which is what GetSpell reports. The reference
        // keeps two spell slots (+0x364 and +0x370) and returns name/rank/id for each; only the
        // first is tracked here, so GetSpell returns three values rather than up to six.
        int32_t m_spellID = 0;
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
        void AddFontStrings(CSimpleFontString* left, CSimpleFontString* right);
        void RunOnTooltipSetDefaultAnchorScript();
        void RunOnTooltipAddMoneyScript(int32_t money);
        void RunOnTooltipClearedScript();
        void RunOnTooltipSetUnitScript();
        void RunOnTooltipSetItemScript();

        // Member functions
        CGTooltip(CSimpleFrame* parent);
};

struct ItemInfo;

// The per-item stat totals the tooltip and GetItemStats build, 0x12c bytes in the reference.
// stats[] is indexed as the reference indexes it: 0..6 armor then the six resistances, 8 attack
// power, 9 ranged attack power, and 11 + an item stat type for the item's own stats; 69..72 count
// the sockets carrying colour bit 1, 2, 4 and 8. flags records which sources have been added.
struct ItemStatTotals {
    float dps;
    int32_t stats[73];
    uint32_t flags;

    void Clear();
    void AddArmorAndResistances(const ItemInfo* info);
    void AddSockets(const ItemInfo* info);
    void AddItemStats(const ItemInfo* info);
    void FoldStats(int32_t index, const int32_t* into, uint32_t count, int32_t collapse);
};

void FormatTimeInterval(char* dest, uint32_t destSize, uint64_t time, const char* prefix, int32_t displayValue, int32_t roundUp, bool inSeconds);

#endif
