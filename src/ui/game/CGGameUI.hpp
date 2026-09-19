#ifndef UI_GAME_C_G_GAME_UI_HPP
#define UI_GAME_C_G_GAME_UI_HPP

#include "util/guid/Types.hpp"

class CScriptObject;
class CSimpleTop;

class CGGameUI {
    public:
        // What the mouse is carrying: a spell on its way to the hotbar, an item between bags, a
        // stack of money. The reference keeps this beside the rest of the game UI state -- its
        // cursor code names .\GameUI.cpp -- and s_cursorMoney below was already one member of it.
        // docs/ref/parity-cursor.md maps every address, kind and setter.
        //
        // Nothing picks anything up yet: the setters need the cursor image, the sound engine and
        // the action-bar workers. The kind is therefore always CURSOR_NONE today, which makes the
        // readers truthful rather than merely stubbed -- an empty cursor is the right answer, and
        // FrameXML's container and paper-doll updates ask for it on nearly every frame.
        //
        // Values are the reference's own, from the switch in GetCursorInfo (FUN_00515200) and the
        // matching one in the clear path (FUN_00519280).
        enum CURSOR_KIND {
            CURSOR_NONE             = 0,
            CURSOR_ITEM_OBJECT      = 1,    // an item that exists as an object, carries a guid
            CURSOR_MONEY            = 2,
            CURSOR_SPELL            = 3,    // also pet spells and companions, told apart at read time
            CURSOR_MERCHANT         = 5,
            CURSOR_ITEM_ENTRY       = 7,    // an item known only by its entry id
            CURSOR_MACRO            = 8,
            CURSOR_ITEM_ENTRY_ALT   = 9,    // also counts as "has item"
            CURSOR_GUILDBANK_ITEM   = 0x0b,
            CURSOR_GUILDBANK_MONEY  = 0x0c,
            CURSOR_EQUIPMENTSET     = 0x0d,
        };

        // Whether the cursor holds an item. Both of the reference's item-object kinds count and the
        // entry-only ones deliberately do not, so an item being dragged out of a merchant window
        // does not answer yes.
        static bool CursorHasItem();
        static uint32_t GetCursorKind();
        // Static variables
        static CScriptObject* s_gameTooltip;
        static CSimpleTop* s_simpleTop;

        // Static functions
        static void EnterWorld();
        static WOWGUID& GetCurrentObjectTrack();
        static uint32_t GetCursorMoney();
        static WOWGUID& GetLockedTarget();
        static void DisplayError(uint32_t errorCode, ...);
        static char s_lastError[3000];  // the last formatted error text (reference DAT_00bcfb90)
        static void Initialize();
        static void InitializeGame();
        static bool IsLoggingIn();
        static bool IsInWorld();
        static int32_t IsRaidMember(const WOWGUID& guid);
        static int32_t IsRaidMemberOrPet(const WOWGUID& guid);
        static void RegisterFrameFactories();
        static void RegisterGameCVars();

    private:
        static WOWGUID s_currentObjectTrack;
        static uint32_t s_cursorMoney;
        static uint32_t s_cursorKind;
        static uint32_t s_cursorHolding;    // set when something is picked up
        static uint32_t s_cursorIndex;      // merchant slot, guild bank slot
        static uint32_t s_cursorItemEntry;
        static WOWGUID s_cursorItemGUID;
        static uint32_t s_cursorSpell;
        static uint32_t s_cursorMacro;
        static bool s_inWorld;
        static WOWGUID s_lockedTarget;
        static bool s_loggingIn;
};

#endif
