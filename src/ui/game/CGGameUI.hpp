#ifndef UI_GAME_C_G_GAME_UI_HPP
#define UI_GAME_C_G_GAME_UI_HPP

#include "event/CEvent.hpp"
#include <tempest/Vector.hpp>
#include "util/guid/Types.hpp"

class CScriptObject;
class CVar;
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
        static void GetCursorItem(WOWGUID* item, WOWGUID* bag, uint32_t* slot);
        static WOWGUID GetCursorItemGUID();
        static uint32_t GetCursorItemEntry();
        static void GetCursorItemEntryAndIndex(uint32_t* entry, uint32_t* index);
        static uint32_t GetCursorKind();
        static uint32_t GetCursorSpell();
        static char* GetLastError();
        // Static variables
        static CVar* s_currencyTokensUnused1Cvar;   // ref: DAT_00bd09f4
        static CVar* s_currencyTokensUnused2Cvar;   // ref: DAT_00bd09f8
        static CVar* s_currencyTokensBackpack1Cvar; // ref: DAT_00bd09fc
        static CVar* s_currencyTokensBackpack2Cvar; // ref: DAT_00bd0a00
        static CVar* s_predictedHealthCvar;         // ref: DAT_00bd0a04
        static CVar* s_predictedPowerCvar;          // ref: DAT_00bd0a08
        static CVar* s_threatWarningCvar;           // ref: DAT_00bd0a0c
        static CVar* s_threatWorldTextCvar;         // ref: DAT_00bd0a10
        static CVar* s_threatShowNumericCvar;       // ref: DAT_00bd0a14
        static CVar* s_threatPlaySoundsCvar;        // ref: DAT_00bd0a18
        static CVar* s_combatDamageCvar;            // ref: DAT_00bd0980
        static CVar* s_combatLogPeriodicSpellsCvar; // ref: DAT_00bd0984
        static CVar* s_petMeleeDamageCvar;          // ref: DAT_00bd0988
        static CVar* s_petSpellDamageCvar;          // ref: DAT_00bd098c
        static CVar* s_combatHealingCvar;           // ref: DAT_00bd0990
        static CScriptObject* s_gameTooltip;
        static CSimpleTop* s_simpleTop;

        // Static functions
        static void EnterWorld();
        static WOWGUID& GetCurrentObjectTrack();
        static uint32_t GetCursorMoney();
        static WOWGUID& GetLockedTarget();
        static void OnObjectDisabled(WOWGUID guid);
        static WOWGUID GetInteractTarget();
        static uint32_t GetCursorHolding();
        static void SetPreviousTarget(WOWGUID guid);
        static void ClearTarget(WOWGUID guid, int32_t notify);
        static void SetTarget(WOWGUID guid);
        // ref: FUN_005124d0
        // Whether an opening or in-game cinematic is playing.
        static int32_t InCinematic();
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
        static WOWGUID s_cursorItemBagGUID;     // ref: DAT_00bd0760
        static uint32_t s_cursorItemSlot;       // ref: DAT_00bd075c
        static uint32_t s_cursorSpell;
        static uint32_t s_cursorMacro;
        static bool s_inWorld;
        static WOWGUID s_lockedTarget;
        // ref: DAT_00bd07b8. The target before the current one (TargetLastTarget's).
        static WOWGUID s_previousTarget;
        // ref: DAT_00bd07a8. The NPC or object the player has a gossip, vendor, trainer or other
        // interaction window open with; FUN_00512e60 closes whichever window it was. Nothing
        // opens one yet, so it stays zero.
        static WOWGUID s_interactTarget;
        // Set while a cinematic plays (DAT_00bd07fc): FUN_00528af0 raises it, FUN_00528c30 drops it.
        static int32_t s_inCinematic;
        static bool s_loggingIn;
};

class CMouseEvent;

int32_t GameUIMouseButtonCallback(CMouseEvent* evt);
int32_t GameUIMouseRelativeCallback(CMouseEvent* evt);
void GameUIEnterMouseLook();
void GameUILeaveMouseLook();

// A short click in the world, as the world frame hands it to GameUI: the object or surface's
// owner, the point, the ray (for a click on nothing), and the button (1 left, 4 right).
struct WORLDCLICK {
    WOWGUID guid = 0;
    C3Vector position = {};
    C3Vector start = {};
    C3Vector end = {};
    int32_t button = 0;
};

void GameUIClearCursor(int32_t restore, int32_t signal);

// Threat (GameUI.cpp).
void GameUIUpdateThreatWarning(int32_t mode);
int32_t GameUIThreatWarningActive();
void GameUIAddThreatUnit(const WOWGUID& unit, const WOWGUID& target);
void GameUIRemoveThreatUnit(const WOWGUID& unit, const WOWGUID& target);
uint8_t GameUIGetThreatStatus(const WOWGUID& guid);

// Whether the player controls its character (GameUI.cpp).
void GameUISetPlayerControl(int32_t hasControl);
int32_t GameUIPlayerHasControl();
void GameUIInitPlayerControl(WOWGUID mover);

// The player's own corpse, which the minimap points to (GameUI.cpp).
void GameUISetCorpseGUID(WOWGUID guid);

// The colour a unit's name (or bar, with `forName` 0) takes (CGUnit_CName.cpp).
class CImVector;
void GameUIGetUnitColor(WOWGUID guid, CImVector* out, int32_t forName);
WOWGUID GameUIGetCorpseGUID();
int32_t GameUIWorldRightPress(const CMouseEvent& evt);
int32_t GameUITargetAndInteract(WOWGUID guid);
int32_t GameUISelectObject(WOWGUID guid);
int32_t GameUIClickObject(WOWGUID guid, int32_t button);
int32_t GameUIClickSurface(const WORLDCLICK& click);
int32_t GameUIClickNothing(const WORLDCLICK& click);

#endif
