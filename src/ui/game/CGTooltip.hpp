#ifndef UI_GAME_C_G_TOOLTIP_HPP
#define UI_GAME_C_G_TOOLTIP_HPP

#include "ui/simple/CSimpleFrame.hpp"
#include "ui/Types.hpp"
#include "util/guid/Types.hpp"
#include <storm/Array.hpp>
#include <tempest/Vector.hpp>

class CImVector;
class CRect;
class CSimpleFontString;
class CSimpleStatusBar;
class CSimpleTexture;

// GameTooltip (Tooltip.cpp). The members are in the reference's order; the offsets beside them are
// the reference's, from the constructor (FUN_0061dee0), the destructor (FUN_0061e160) and the
// functions that read them. Gaps not yet recovered are noted where they fall.
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
        CSimpleFrame* m_owner = nullptr;                    // +0x29c
        TOOLTIP_ANCHORPOINT m_anchorType = TOOLTIP_ANCHOR_LEFT; // +0x2a0
        // The lines in use, then every line there is: the template's TextLeft/TextRight pairs that
        // PostLoadXML found, and the ones AddLine and AddFontStrings have added since.
        uint32_t m_numLines = 0;                            // +0x2a4
        uint32_t m_maxLines = 0;                            // +0x2a8
        TSFixedArray<CSimpleFontString*> m_leftStrings;     // +0x2ac
        TSFixedArray<CSimpleFontString*> m_rightStrings;    // +0x2b8
        // Whether each line may wrap; a line with right-hand text never does.
        TSFixedArray<int32_t> m_wrapLine;                   // +0x2c4
        CSimpleStatusBar* m_statusBar = nullptr;            // +0x2d0 the template's <name>StatusBar
        // The template's <name>Texture1..10, handed out by AddTexture in order, and the line each
        // one sits on.
        CSimpleTexture* m_textures[10] = {};                // +0x2d4
        uint32_t m_textureLine[10] = {};                    // +0x2fc
        uint32_t m_numTextures = 0;                         // +0x324
        // What the tooltip was filled from. The refresh callbacks (FUN_0061dce0..FUN_0061de50) fill
        // it again from these when the data they wait on arrives.
        WOWGUID m_unitGUID = 0;                             // +0x328
        WOWGUID m_guid330 = 0;                              // +0x330 refreshed by FUN_00626720
        WOWGUID m_guid338 = 0;                              // +0x338
        WOWGUID m_itemGUID = 0;                             // +0x340 kept across a clear
        WOWGUID m_guid348 = 0;                              // +0x348 refreshed by FUN_00622410
        WOWGUID m_guid350 = 0;                              // +0x350
        // The unit whose health the status bar follows, through an object update callback.
        WOWGUID m_statusUnitGUID = 0;                       // +0x358
        int32_t m_itemID = 0;                               // +0x360
        int32_t m_spellID = 0;                              // +0x364
        int32_t m_questID = 0;                              // +0x368
        int32_t m_achievementID = 0;                        // +0x36c
        int32_t m_spellID2 = 0;                             // +0x370
        // +0x374 .. +0x3c4: the achievement criteria and the rest, recovered with their fillers.
        // A spell tooltip waiting on data counts down here before it is filled again (FUN_0061dd60).
        int32_t m_spellRefresh = 0;                         // +0x3c8
        // FadeOut starts this: the alpha falls over m_fadeTime seconds and the tooltip hides.
        int32_t m_fading = 0;                               // +0x3cc
        float m_fadeTime = 0.0f;                            // +0x3d0
        float m_padding = 0.0f;                             // +0x3d4
        float m_minimumWidth = 0.0f;                        // +0x3d8
        int32_t m_minimumWidthForced = 0;                   // +0x3dc
        C2Vector m_anchorOffset;                            // +0x3e0
        // +0x3e8 .. +0x4d0: the item link the tooltip was filled from, recovered with the item fillers.
        ScriptIx m_onTooltipSetDefaultAnchor;               // +0x4d4
        ScriptIx m_onTooltipCleared;                        // +0x4dc
        ScriptIx m_onTooltipAddMoney;                       // +0x4e4
        ScriptIx m_onTooltipSetUnit;                        // +0x4ec
        ScriptIx m_onTooltipSetItem;                        // +0x4f4
        ScriptIx m_onTooltipSetSpell;                       // +0x4fc
        ScriptIx m_onTooltipSetQuest;                       // +0x504
        ScriptIx m_onTooltipSetAchievement;                 // +0x50c
        ScriptIx m_onTooltipSetEquipmentSet;                // +0x514
        ScriptIx m_onTooltipSetFrameStack;                  // +0x51c

        // Virtual member functions
        virtual ~CGTooltip();
        virtual ScriptIx* GetScriptByName(const char* name, ScriptData& data);
        virtual bool IsA(int32_t type);
        virtual bool IsA(const char* typeName);
        virtual const char* GetObjectTypeName();
        virtual int32_t GetScriptMetaTable();
        virtual void OnLayerShow();
        virtual void OnLayerUpdate(float elapsedSec);
        virtual void PostLoadXML(const XMLNode* node, CStatus* status);
        virtual int32_t HideThis();

        // Member functions
        CGTooltip(CSimpleFrame* parent);
        void AddFontStrings(CSimpleFontString* left, CSimpleFontString* right);
        void AddLine(const char* left, const char* right, const CImVector& leftColor, const CImVector& rightColor, int32_t wrap);
        void AddLine(const char* left, const char* right, int32_t wrap);
        void AddTexture(const char* fileName, const CRect& texCoords, const CImVector& color);
        void AppendText(const char* text);
        void CalculateSize();
        void ClearTooltip();
        void FadeOut();
        void RunOnTooltipAddMoneyScript(int32_t cost, int32_t maxCost);
        void SetAnchor(int32_t force);
        void SetOwner(CSimpleFrame* owner, TOOLTIP_ANCHORPOINT anchorType, float offsetX, float offsetY);
};

class ItemStats_C;

// The per-item stat totals the tooltip and GetItemStats build, 0x12c bytes in the reference.
// stats[] is indexed as the reference indexes it: 0..6 armor then the six resistances, 8 attack
// power, 9 ranged attack power, and 11 + an item stat type for the item's own stats; 69..72 count
// the sockets carrying colour bit 1, 2, 4 and 8. flags records which sources have been added.
struct ItemStatTotals {
    float dps;
    int32_t stats[73];
    uint32_t flags;

    void Clear();
    void AddArmorAndResistances(const ItemStats_C* info);
    void AddSockets(const ItemStats_C* info);
    void AddItemStats(const ItemStats_C* info);
    void FoldStats(int32_t index, const int32_t* into, uint32_t count, int32_t collapse);
};

void FormatTimeInterval(char* dest, uint32_t destSize, uint64_t time, const char* prefix, int32_t displayValue, int32_t roundUp, bool inSeconds);

#endif
