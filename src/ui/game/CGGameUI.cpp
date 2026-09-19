#include "ui/game/CGGameUI.hpp"
#include <storm/String.hpp>
#include "ui/FrameScript.hpp"
#include "client/Client.hpp"
#include "console/CVar.hpp"
#include "object/Client.hpp"
#include "ui/CScriptObject.hpp"
#include "ui/FrameXML.hpp"
#include "gx/LoadingScreen.hpp"
#include "ui/Key.hpp"
#include "ui/game/ActionBarScript.hpp"
#include "ui/game/BattlefieldInfoScript.hpp"
#include "ui/game/BattlenetUI.hpp"
#include "ui/game/CGCamera.hpp"
#include "ui/game/CGCharacterModelBase.hpp"
#include "ui/game/CGCooldown.hpp"
#include "ui/game/CGDressUpModelFrame.hpp"
#include "ui/game/CGMinimapFrame.hpp"
#include "ui/game/CGQuestPOIFrame.hpp"
#include "ui/game/CGTabardModelFrame.hpp"
#include "ui/game/CGTooltip.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "ui/game/MiscScript.hpp"
#include "ui/game/MiscScriptStubs.hpp"
#include "ui/game/CharacterInfoScript.hpp"
#include "ui/game/GMTicketInfoScript.hpp"
#include "ui/game/GameScript.hpp"
#include "ui/game/PartyInfoScript.hpp"
#include "ui/game/RaidInfoScript.hpp"
#include "ui/game/ScriptEvents.hpp"
#include "ui/game/TradeInfoScript.hpp"
#include "ui/game/Types.hpp"
#include "ui/game/UIBindingsScript.hpp"
#include "ui/simple/CSimpleTop.hpp"
#include "util/CStatus.hpp"
#include "util/Filesystem.hpp"
#include "util/Log.hpp"
#include <common/MD5.hpp>

WOWGUID CGGameUI::s_currentObjectTrack;
uint32_t CGGameUI::s_cursorMoney;
CScriptObject* CGGameUI::s_gameTooltip;
bool CGGameUI::s_inWorld;
WOWGUID CGGameUI::s_lockedTarget;
bool CGGameUI::s_loggingIn;
CSimpleTop* CGGameUI::s_simpleTop;

void LoadScriptFunctions() {
    // TODO

    CGTooltip::CreateScriptMetaTable();
    CGCooldown::CreateScriptMetaTable();
    CGMinimapFrame::CreateScriptMetaTable();
    CGCharacterModelBase::CreateScriptMetaTable();
    CGDressUpModelFrame::CreateScriptMetaTable();
    CGTabardModelFrame::CreateScriptMetaTable();
    CGQuestPOIFrame::CreateScriptMetaTable();

    GameScriptRegisterFunctions();
    MiscScriptRegisterFunctions();
    MiscScriptRegisterStubs();
    UIBindingsRegisterScriptFunctions();

    // TODO

    ScriptEventsRegisterFunctions();

    // TODO

    ActionBarRegisterScriptFunctions();
    PartyInfoRegisterScriptFunctions();

    // TODO

    CharacterInfoRegisterScriptFunctions();

    // TODO

    TradeInfoRegisterScriptFunctions();

    // TODO

    BattlefieldInfoRegisterScriptFunctions();

    // TODO

    RaidInfoRegisterScriptFunctions();

    // TODO

    GMTicketInfoRegisterScriptFunctions();
    BattlenetUI_RegisterScriptFunctions();
}

void CGGameUI::EnterWorld() {
    if (CGGameUI::s_inWorld) {
        return;
    }

    CGGameUI::s_inWorld = true;

    // TODO

    if (CGGameUI::s_loggingIn) {
        CGGameUI::s_loggingIn = false;

        FrameScript_SignalEvent(SCRIPT_PLAYER_LOGIN, nullptr);

        // TODO CGLCD::Login();
    }


    FrameScript_SignalEvent(SCRIPT_PLAYER_ENTERING_WORLD, nullptr);

    // FloatingChatFrame_Update applies a chat window's saved colour and alpha ONLY when it is
    // called from the update event -- `if ( onUpdateEvent ) then FCF_SetWindowColor(...)`. Nothing
    // signalled these, so every chat frame kept the opaque backdrop its XML template ships with,
    // and frame.oldAlpha was never set either, which is what raised
    // "bad argument #1 to 'max' (number expected, got nil)" in the fade path on every mouseover.
    //
    // 386 and 529 in g_scriptEvents; neither has a named constant.
    FrameScript_SignalEvent(386, nullptr); // UPDATE_CHAT_WINDOWS
    FrameScript_SignalEvent(529, nullptr); // UPDATE_FLOATING_CHAT_WINDOWS

    // Those two events are what finally give each chat window its name, and a tab is sized from the
    // width of its label. During FrameXML load ChatFrame1's tab has no text at all, so it measures
    // zero wide and FCFDock_UpdateTabs anchors every other tab on top of it -- the dock comes up
    // with all the tabs stacked in one place.
    //
    // FCF_SetWindowName only marks the dock dirty; the re-layout it needs afterwards rides on the
    // dock's OnUpdate, which is installed solely by FCFDock_SetPrimary's OnSizeChanged hook and so
    // never runs here. Clicking a tab goes through FCFDock_SelectWindow and fixes it, which is
    // exactly how the stacking looked: wrong until touched.
    //
    // FCF_DockUpdate is FrameXML's own forced re-layout (FCFDock_UpdateTabs with forceUpdate set),
    // and FloatingChatFrame.lua calls it for the same reason when leaving simple chat -- "we need
    // to update now". Guarded so a UI without it is not a Lua error.
    static const char* dockUpdate = "if FCF_DockUpdate then FCF_DockUpdate() end";
    FrameScript_ExecuteBuffer(dockUpdate, SStrLen(dockUpdate), "@FCF_DockUpdate", nullptr, nullptr);

    // TODO
}

WOWGUID& CGGameUI::GetCurrentObjectTrack() {
    return CGGameUI::s_currentObjectTrack;
}

uint32_t CGGameUI::GetCursorMoney() {
    return CGGameUI::s_cursorMoney;
}

WOWGUID& CGGameUI::GetLockedTarget() {
    return CGGameUI::s_lockedTarget;
}

static void GameUILoadProgress(float progress, void* param) {
    LoadingScreenSetProgress(progress);
}

void CGGameUI::Initialize() {
    // TODO

    CGGameUI::s_loggingIn = true;

    // TODO

    CGGameUI::s_simpleTop = STORM_NEW(CSimpleTop);

    // TODO

    LoadScriptFunctions();
    ScriptEventsRegisterEvents();
    CGGameUI::RegisterGameCVars();

    // TODO

    CGGameUI::RegisterFrameFactories();

    // TODO

    // The interface loader reports every failure -- a file it could not open, XML it could not
    // parse, a frame type it does not know -- into this collector. A plain CStatus throws all of
    // that away when it goes out of scope, which is why frozen loaded only part of FrameXML while
    // reporting nothing at all. The glue path already logs the same way to Logs\GlueXML.log.
    OsCreateDirectory("Logs", 0);

    CWOWClientStatus status;

    if (!SLogCreate("Logs\\FrameXML.log", 0, &status.m_logFile)) {
        SysMsgPrintf(SYSMSG_WARNING, "Cannot create WOWClient log file \"%s\"!", "Logs\\FrameXML.log");
    }

    // TODO

    uint8_t digest1[16];

    switch (FrameXML_CheckSignature("Interface\\FrameXML\\FrameXML.toc", "Interface\\FrameXML\\Bindings.xml", InterfaceKey, digest1)) {
        case 0: {
            status.Add(STATUS_WARNING, "FrameXML missing signature");
            ClientPostClose(10);

            break;
        }

        case 1: {
            status.Add(STATUS_WARNING, "FrameXML has corrupt signature");
            ClientPostClose(10);

            break;
        }

        case 2: {
            status.Add(STATUS_WARNING, "FrameXML is modified or corrupt");
            ClientPostClose(10);

            break;
        }

        case 3: {
            // Success
            break;
        }

        default: {
            ClientPostClose(10);

            break;
        }
    }

    MD5_CTX md5;
    MD5Init(&md5);

    // The loading bar advances with each interface file
    // TODO the original also counts the files of enabled addons
    auto numFiles = FrameXML_CountFiles("Interface\\FrameXML\\FrameXML.toc");
    FrameXML_SetProgressCallback(&GameUILoadProgress, nullptr, numFiles);

    FrameXML_FreeHashNodes();

    FrameXML_CreateFrames("Interface\\FrameXML\\FrameXML.toc", nullptr, &md5, &status);

    FrameXML_SetProgressCallback(nullptr, nullptr, 0);

    // TODO CGUIBindings::s_bindings->Load("Interface\\FrameXML\\Bindings.xml", &md5, &status);

    uint8_t digest2[16];
    MD5Final(digest2, &md5);

    // TODO digest validation

    // TODO

    CGGameUI::s_gameTooltip = CScriptObject::GetScriptObjectByName("GameTooltip", CGTooltip::GetObjectType());
    STORM_ASSERT(CGGameUI::s_gameTooltip);

    // TODO

    if (ClntObjMgrGetActivePlayer()) {
        CGGameUI::EnterWorld();
    }

    // TODO
}

void CGGameUI::InitializeGame() {
    // TODO

    CGGameUI::Initialize();

    // TODO
}

bool CGGameUI::IsLoggingIn() {
    return CGGameUI::s_loggingIn;
}

bool CGGameUI::IsInWorld() {
    return CGGameUI::s_inWorld;
}

int32_t CGGameUI::IsRaidMember(const WOWGUID& guid) {
    // TODO

    return false;
}

int32_t CGGameUI::IsRaidMemberOrPet(const WOWGUID& guid) {
    // TODO

    return false;
}

void CGGameUI::RegisterFrameFactories() {
    FrameXML_RegisterFactory("WorldFrame", &CGWorldFrame::Create, true);
    FrameXML_RegisterFactory("GameTooltip", &CGTooltip::Create, false);
    FrameXML_RegisterFactory("Cooldown", &CGCooldown::Create, false);
    FrameXML_RegisterFactory("Minimap", &CGMinimapFrame::Create, false);
    FrameXML_RegisterFactory("PlayerModel", &CGCharacterModelBase::Create, false);
    FrameXML_RegisterFactory("DressUpModel", &CGDressUpModelFrame::Create, false);
    FrameXML_RegisterFactory("TabardModel", &CGTabardModelFrame::Create, false);
    FrameXML_RegisterFactory("QuestPOIFrame", &CGQuestPOIFrame::Create, false);
}

void CGGameUI::RegisterGameCVars() {
    // TODO

    // Interface option cvars read by the options panels
    CVar::Register("UnitNameOwn", "Show your own name", 0x10, "0", nullptr, GAME);
    CVar::Register("assistAttack", "Attack on assist", 0x10, "0", nullptr, GAME);
    CVar::Register("buffDurations", "Show buff durations", 0x10, "1", nullptr, GAME);
    CVar::Register("cameraSmoothStyle", "Camera smoothing style", 0x10, "4", nullptr, GAME);
    CVar::Register("cameraSmoothTrackingStyle", "Camera tracking style", 0x10, "3", nullptr, GAME);
    CVar::Register("cameraYawSmoothSpeed", "Camera yaw smoothing speed", 0x10, "180", nullptr, GAME);
    CVar::Register("deselectOnClick", "Deselect on click", 0x10, "1", nullptr, GAME);
    CVar::Register("displayWorldPVPObjectives", "Display world PvP objectives", 0x10, "1", nullptr, GAME);
    CVar::Register("equipmentManager", "Equipment manager", 0x10, "0", nullptr, GAME);
    CVar::Register("lockActionBars", "Lock action bars", 0x10, "0", nullptr, GAME);
    CVar::Register("mouseInvertPitch", "Invert mouse pitch", 0x10, "0", nullptr, GAME);
    CVar::Register("profanityFilter", "Profanity filter", 0x10, "1", nullptr, GAME);
    CVar::Register("questFadingDisable", "Disable quest text fading", 0x10, "0", nullptr, GAME);
    CVar::Register("rotateMinimap", "Rotate the minimap", 0x10, "0", nullptr, GAME);
    CVar::Register("showTutorials", "Show tutorials", 0x10, "1", nullptr, GAME);
    CVar::Register("targetOfTargetMode", "Target of target mode", 0x10, "5", nullptr, GAME);
    CVar::Register("threatWarning", "Threat warning mode", 0x10, "3", nullptr, GAME);
    CVar::Register("useUiScale", "Use the UI scale", 0x10, "0", nullptr, GAME);
    CVar::Register("uiScale", "UI scale", 0x10, "1", nullptr, GAME);
    CVar::Register("UnitNameNPC", "Show NPC names", 0x10, "1", nullptr, GAME);
    CVar::Register("UnitNamePlayerGuild", "Show player guild names", 0x10, "1", nullptr, GAME);
    CVar::Register("UnitNamePlayerPVPTitle", "Show player PvP titles", 0x10, "1", nullptr, GAME);
    CVar::Register("UnitNameFriendlyPlayerName", "Show friendly player names", 0x10, "1", nullptr, GAME);
    CVar::Register("UnitNameFriendlyPetName", "Show friendly pet names", 0x10, "1", nullptr, GAME);
    CVar::Register("UnitNameFriendlyGuardianName", "Show friendly guardian names", 0x10, "1", nullptr, GAME);
    CVar::Register("UnitNameFriendlyTotemName", "Show friendly totem names", 0x10, "1", nullptr, GAME);
    CVar::Register("UnitNameEnemyPlayerName", "Show enemy player names", 0x10, "1", nullptr, GAME);
    CVar::Register("UnitNameEnemyPetName", "Show enemy pet names", 0x10, "1", nullptr, GAME);
    CVar::Register("UnitNameEnemyGuardianName", "Show enemy guardian names", 0x10, "1", nullptr, GAME);
    CVar::Register("UnitNameEnemyTotemName", "Show enemy totem names", 0x10, "1", nullptr, GAME);
    CVar::Register("UnitNameNonCombatCreatureName", "Show non-combat creature names", 0x10, "0", nullptr, GAME);
    CVar::Register("showDispelDebuffs", "Show dispellable debuffs", 0x10, "1", nullptr, GAME);
    CVar::Register("screenEdgeFlash", "Flash the screen edge in combat", 0x10, "1", nullptr, GAME);
    CVar::Register("previewTalents", "Preview talent changes", 0x10, "0", nullptr, GAME);
    CVar::Register("chatBubbles", "Show chat bubbles", 0x10, "1", nullptr, GAME);
    CVar::Register("chatBubblesParty", "Show party chat bubbles", 0x10, "0", nullptr, GAME);
    CVar::Register("cameraDistanceMaxFactor", "Camera distance factor", 0x10, "1", nullptr, GAME);
    CVar::Register("autointeract", "Click to move", 0x10, "0", nullptr, GAME);
    CVar::Register("autoRangedCombat", "Auto ranged combat", 0x10, "0", nullptr, GAME);
    CVar::Register("autoQuestWatch", "Auto quest watch", 0x10, "1", nullptr, GAME);
    CVar::Register("autoDismountFlying", "Auto dismount when flying", 0x10, "0", nullptr, GAME);
    CVar::Register("alwaysShowActionBars", "Always show action bars", 0x10, "0", nullptr, GAME);
    CVar::Register("showTargetOfTarget", "Show target of target", 0x10, "0", nullptr, GAME);
    CVar::Register("showPartyPets", "Show party pets", 0x10, "1", nullptr, GAME);
    CVar::Register("showPartyBackground", "Show party background", 0x10, "0", nullptr, GAME);
    CVar::Register("autoSelfCast", "Auto self cast", 0x10, "0", nullptr, GAME);
    CVar::Register("scriptErrors", "Show script errors", 0x10, "0", nullptr, GAME);
    CVar::Register("secureAbilityToggle", "Secure ability toggle", 0x10, "1", nullptr, GAME);
    CVar::Register("lootUnderMouse", "Open loot under the mouse", 0x10, "0", nullptr, GAME);
    CVar::Register("autoLootDefault", "Auto loot", 0x10, "0", nullptr, GAME);
    CVar::Register("showToastOnline", "Show online toasts", 0x10, "1", nullptr, GAME);
    CVar::Register("showToastOffline", "Show offline toasts", 0x10, "1", nullptr, GAME);
    CVar::Register("showToastBroadcast", "Show broadcast toasts", 0x10, "1", nullptr, GAME);
    CVar::Register("showToastFriendRequest", "Show friend request toasts", 0x10, "1", nullptr, GAME);
    CVar::Register("showToastWindow", "Show toast window", 0x10, "0", nullptr, GAME);
    CVar::Register("toastDuration", "Toast duration", 0x10, "5", nullptr, GAME);

    CVar::Register("enableCombatText", "Whether to show floating combat text", 0x10, "1", nullptr, GAME);
    CVar::Register("combatTextFloatMode", "The combat text float mode", 0x10, "1", nullptr, GAME);
    CVar::Register("fctCombatState", nullptr, 0x10, "0", nullptr, GAME);
    CVar::Register("fctDodgeParryMiss", nullptr, 0x10, "0", nullptr, GAME);
    CVar::Register("fctDamageReduction", nullptr, 0x10, "0", nullptr, GAME);
    CVar::Register("fctRepChanges", nullptr, 0x10, "0", nullptr, GAME);
    CVar::Register("fctReactives", nullptr, 0x10, "0", nullptr, GAME);
    CVar::Register("fctFriendlyHealers", nullptr, 0x10, "0", nullptr, GAME);
    CVar::Register("fctComboPoints", nullptr, 0x10, "0", nullptr, GAME);
    CVar::Register("fctLowManaHealth", nullptr, 0x10, "1", nullptr, GAME);
    CVar::Register("fctEnergyGains", nullptr, 0x10, "0", nullptr, GAME);
    CVar::Register("fctPeriodicEnergyGains", nullptr, 0x10, "0", nullptr, GAME);
    CVar::Register("fctHonorGains", nullptr, 0x10, "0", nullptr, GAME);
    CVar::Register("fctAuras", nullptr, 0x10, "0", nullptr, GAME);
    CVar::Register("fctAllSpellMechanics", nullptr, 0x10, "0", nullptr, GAME);
    CVar::Register("fctSpellMechanics", nullptr, 0x10, "0", nullptr, GAME);
    CVar::Register("fctSpellMechanicsOther", nullptr, 0x10, "0", nullptr, GAME);

    CVar::Register("xpBarText", "Whether the XP bar shows the numeric experience value", 0x10, "0", nullptr, GAME);

    CVar::Register("playerStatusText", "Whether the player portrait shows numeric health/mana values", 0x10, "0", nullptr, GAME);
    CVar::Register("petStatusText", "Whether the pet portrait shows numeric health/mana values", 0x10, "0", nullptr, GAME);
    CVar::Register("partyStatusText", "Whether the party portraits shows numeric health/mana values", 0x10, "0", nullptr, GAME);
    CVar::Register("targetStatusText", "Whether the target portrait shows numeric health/mana values", 0x10, "0", nullptr, GAME);
    CVar::Register("statusTextPercentage", "Whether numeric health/mana values are shown as raw values or percentages", 0x10, "0", nullptr, GAME);

    CVar::Register("showPartyBackground", "Show a background behind party members", 0x10, "0", nullptr, GAME);
    CVar::Register("partyBackgroundOpacity", "The opacity of the party background", 0x10, "0.5", nullptr, GAME);
    CVar::Register("hidePartyInRaid", "Whether to hide the party UI while in a raid", 0x10, "0", nullptr, GAME);
    CVar::Register("showPartyPets", "Whether to show pets in the party UI", 0x20, "1", nullptr, GAME);
    CVar::Register("showRaidRange", "Show range indicator in raid UI", 0x20, "0", nullptr, GAME);

    CVar::Register("showArenaEnemyFrames", "Show arena enemy frames while in an Arena", 0x20, "1", nullptr, GAME);
    CVar::Register("showArenaEnemyCastbar", "Show the spell enemies are casting on the Arena Enemy frames", 0x20, "1", nullptr, GAME);
    CVar::Register("showArenaEnemyPets", "Show the enemy team's pets on the ArenaEnemy frames", 0x20, "1", nullptr, GAME);

    CVar::Register("fullSizeFocusFrame", "Increases the size of the focus frame to that of the target frame", 0x20, "0", nullptr, GAME);

    // TODO

    CameraRegisterCVars();

    // TODO

    // The rest of the reference's RegisterGameCVars (FUN_0051d9b0), in its order. Callbacks the
    // reference installs are noted by address; frozen has no handler for them yet.
    const char* militaryTime = "1"; // TODO the reference picks "0" for locales 2, 5 and 10 (FUN_00635d90)
    CVar::Register("autoStand", "Automatically stand when needed", 0x10, "1", nullptr, GAME);
    CVar::Register("autoDismount", "Automatically dismount when needed", 0x10, "1", nullptr, GAME);
    CVar::Register("autoUnshift", "Automatically leave shapeshift form when needed", 0x10, "1", nullptr, GAME);
    CVar::Register("autoClearAFK", "Automatically clear AFK when moving or chatting", 0x10, "1", nullptr, GAME);
    CVar::Register("blockTrades", "Whether to automatically block trade requests", 0x20, "0", nullptr, GAME);
    CVar::Register("alwaysCompareItems", "Always show item comparison tooltips", 0x10, "0", nullptr, GAME);
    CVar::Register("stopAutoAttackOnTargetChange", "Whether to stop attacking when changing targets", 0x20, "0", nullptr, GAME);
    CVar::Register("showTargetCastbar", "Show the spell your current target is casting", 0x10, "1", nullptr, GAME);
    CVar::Register("showVKeyCastbar", "If the V key display is up for your current target, show the enemy cast bar with the target's health bar in the game field", 0x10, "1", nullptr, GAME);
    CVar::Register("minimapZoom", "The current outdoor minimap zoom level", 0x20, "3", nullptr, GAME);
    CVar::Register("minimapInsideZoom", "The current indoor minimap zoom level", 0x20, "3", nullptr, GAME);
    CVar::Register("minimapPortalMax", "Max Number of Portals to traverse for minimap", 0x20, "99", nullptr, GAME);
    CVar::Register("showLootSpam", "Whether to show verbose loot rolls", 0x10, "1", nullptr, GAME);
    CVar::Register("displayFreeBagSlots", "Whether or not the backpack button should indicate how many inventory slots you've got free", 0x10, "0", nullptr, GAME);
    CVar::Register("showClock", "Whether to display the time manager's clock button", 0x10, "1", nullptr, GAME);
    CVar::Register("colorblindMode", "Enables colorblind accessibility features in the game", 0x10, "0", nullptr, GAME);
    CVar::Register("autoQuestProgress", "Whether to automatically watch all quests when they are updated", 0x10, "1", nullptr, GAME);
    CVar::Register("showQuestTrackingTooltips", "Displays quest tracking information in unit and object tooltips", 0x20, "1", nullptr, GAME);
    CVar::Register("mapQuestDifficulty", "Whether to color quest titles by difficulty in the World Map", 0x20, "0", nullptr, GAME);
    CVar::Register("questLogCollapseFilter", "bit filed for saving off the state of the headers in Quest Log", 0x20, "0", nullptr, GAME);
    CVar::Register("advancedWatchFrame", "Enables advanced Objectives tracking features", 0x10, "0", nullptr, GAME);
    CVar::Register("watchFrameIgnoreCursor", "Disables Objectives frame mouseover and title dropdown.", 0x10, "0", nullptr, GAME);
    CVar::Register("watchFrameBaseAlpha", "Objectives frame opacity.", 0x10, "0", nullptr, GAME);
    CVar::Register("watchFrameState", "Stores Objectives frame locked and collapsed states", 0x10, "0", nullptr, GAME);
    CVar::Register("showQuestObjectivesOnMap", "Shows quest POIs on the main map.", 0x20, "1", nullptr, GAME);
    CVar::Register("trackedQuests", "Internal cvar for saving tracked quests in order", 0x120, "", nullptr, GAME);
    CVar::Register("trackedAchievements", "Internal cvar for saving tracked achievements in order", 0x120, "", nullptr, GAME);
    CVar::Register("flaggedTutorials", "Internal cvar for saving compleated tutorials in order", 0x110, "", nullptr, GAME);
    CVar::Register("advancedWorldMap", "Enables advanced World Map features", 0x20, "0", nullptr, GAME);
    CVar::Register("worldMapOpacity", "Opacity for the world map when sized down", 0x20, "0", nullptr, GAME);
    CVar::Register("watchFrameWidth", "Controls objectives frame width", 0, "0", nullptr, GAME);
    CVar::Register("trackerSorting", "sorting option for the objectives tracker", 0x20, "0", nullptr, GAME);
    CVar::Register("trackerFilter", "filter option for the objectives tracker", 0x20, "7", nullptr, GAME);
    CVar::Register("spamFilter", "Whether to enable spam filtering", 0x10, "1", nullptr, GAME);
    CVar::Register("removeChatDelay", "Remove Chat Hover Delay", 0x10, "0", nullptr, GAME);
    CVar::Register("guildShowOffline", "Show offline guild members in the guild UI", 0x10, "1", nullptr, GAME); // TODO callback FUN_00512730
    CVar::Register("guildMemberNotify", "Receive notification when guild members log on/off", 0x10, "0", nullptr, GAME);
    CVar::Register("guildRecruitmentChannel", "Whether to automatically join the guild recruitment channel when not in a guild", 0x10, "1", nullptr, GAME); // TODO callback FUN_00512750
    CVar::Register("lfgAutoFill", "Whether to automatically add party members while looking for a group", 0x10, "0", nullptr, GAME);
    CVar::Register("lfgAutoJoin", "Whether to automatically join a party while looking for a group", 0x10, "0", nullptr, GAME);
    CVar::Register("friendsViewButtons", "Whether to show the friends list view buttons", 0x20, "0", nullptr, GAME);
    CVar::Register("friendsSmallView", "Whether to use smaller buttons in the friends list", 0x20, "0", nullptr, GAME);
    CVar::Register("wholeChatWindowClickable", "Whether the user may click anywhere on a chat window to change EditBox focus (only works in IM style)", 0x10, "1", nullptr, GAME);
    CVar::Register("chatMouseScroll", "Whether the user can use the mouse wheel to scroll through chat", 0x10, "1", nullptr, GAME);
    CVar::Register("CombatDamage", "Display damage numbers over hostile creatures when damaged", 0x10, "1", nullptr, GAME);
    CVar::Register("CombatLogPeriodicSpells", "Display damage caused by periodic effects", 0x10, "1", nullptr, GAME);
    CVar::Register("PetMeleeDamage", "Display pet melee damage in the world", 0x10, "1", nullptr, GAME);
    CVar::Register("PetSpellDamage", "Display pet spell damage in the world", 0x10, "1", nullptr, GAME);
    CVar::Register("CombatHealing", "Display amount of healing you did to the target", 0x10, "1", nullptr, GAME);
    CVar::Register("showCastableBuffs", "Show only Buffs the player can cast.  Only applies to raids.", 0x20, "0", nullptr, GAME);
    CVar::Register("consolidateBuffs", "Consolidates buffs displayed for the player.", 0x20, "0", nullptr, GAME);
    CVar::Register("showCastableDebuffs", "Show only debuffs the player can apply.", 0x20, "0", nullptr, GAME);
    CVar::Register("showToastConversation", "Whether to show Battle.net message for conversations", 0x10, "1", nullptr, GAME);
    CVar::Register("showNewbieTips", "Show beginner tooltips", 0x10, "1", nullptr, GAME);
    CVar::Register("UberTooltips", "Show verbose tooltips", 0x10, "1", nullptr, GAME);
    CVar::Register("showItemLevel", "Show item level in the tooltip", 0x10, "0", nullptr, GAME);
    CVar::Register("calendarShowWeeklyHolidays", "Whether weekly holidays should appear in the calendar", 0x20, "1", nullptr, GAME); // TODO callback FUN_005127a0
    CVar::Register("calendarShowDarkmoon", "Whether Darkmoon Faire holidays should appear in the calendar", 0x20, "1", nullptr, GAME); // TODO callback FUN_005127a0
    CVar::Register("calendarShowBattlegrounds", "Whether Battleground holidays should appear in the calendar", 0x20, "0", nullptr, GAME); // TODO callback FUN_005127a0
    CVar::Register("calendarShowLockouts", "Whether raid lockouts should appear in the calendar", 0x20, "1", nullptr, GAME); // TODO callback FUN_005127a0
    CVar::Register("calendarShowResets", "Whether raid resets should appear in the calendar", 0x20, "0", nullptr, GAME); // TODO callback FUN_005127a0
    CVar::Register("nameplateShowEnemies", "", 0x20, "0", nullptr, GAME);
    CVar::Register("nameplateShowEnemyPets", "", 0x20, "1", nullptr, GAME);
    CVar::Register("nameplateShowEnemyGuardians", "", 0x20, "1", nullptr, GAME);
    CVar::Register("nameplateShowEnemyTotems", "", 0x20, "1", nullptr, GAME);
    CVar::Register("nameplateShowFriends", "", 0x20, "0", nullptr, GAME);
    CVar::Register("nameplateShowFriendlyPets", "", 0x20, "1", nullptr, GAME);
    CVar::Register("nameplateShowFriendlyGuardians", "", 0x20, "1", nullptr, GAME);
    CVar::Register("nameplateShowFriendlyTotems", "", 0x20, "1", nullptr, GAME);
    CVar::Register("nameplateAllowOverlap", "switches between overlapping nameplates or the (old) never overlapping version", 0x20, "1", nullptr, GAME);
    CVar::Register("unitHighlights", "Whether the highlight circle around units should be displayed", 0x10, "1", nullptr, GAME); // TODO callback LAB_00518bd0
    CVar::Register("enablePVPNotifyAFK", "The ability to shutdown the AFK notification system", 0x10, "1", nullptr, GAME);
    CVar::Register("serviceTypeFilter", "Which trainer services to show", 0x10, "3", nullptr, GAME);
    CVar::Register("autojoinPartyVoice", "Automatically join the voice session in party/raid chat", 0x10, "1", nullptr, GAME);
    CVar::Register("autojoinBGVoice", "Automatically join the voice session in battleground chat", 0x10, "0", nullptr, GAME);
    CVar::Register("PushToTalkSound", "Play a sound when voice recording activates and deactivates", 0x10, "0", nullptr, GAME);
    CVar::Register("combatLogOn", "Whether or not the combat log is shown", 0x20, "1", nullptr, GAME);
    CVar::Register("showKeyring", "Whether or not the keyring is shown", 0x20, "0", nullptr, GAME);
    CVar::Register("showBattlefieldMinimap", "Whether or not the battlefield minimap is shown", 0x20, "0", nullptr, GAME);
    CVar::Register("playerStatLeftDropdown", "The player stat selected in the left dropdown", 0x20, "", nullptr, GAME);
    CVar::Register("playerStatRightDropdown", "The player stat selected in the right dropdown", 0x20, "", nullptr, GAME);
    CVar::Register("talentFrameShown", "The talent UI has been shown", 0x10, "0", nullptr, GAME);
    CVar::Register("auctionDisplayOnCharacter", "Show auction items on the dress-up paperdoll", 0x10, "0", nullptr, GAME);
    CVar::Register("addFriendInfoShown", "The info for Add Friend has been shown", 0x10, "0", nullptr, GAME);
    CVar::Register("pendingInviteInfoShown", "The info for pending invites has been shown", 0x10, "0", nullptr, GAME);
    CVar::Register("timeMgrUseMilitaryTime", "Toggles the display of either 12 or 24 hour time", 0x10, militaryTime, nullptr, GAME);
    CVar::Register("timeMgrUseLocalTime", "Toggles the use of either the realm time or your system time", 0x10, "0", nullptr, GAME);
    CVar::Register("timeMgrAlarmTime", "The time manager's alarm time in minutes", 0x10, "0", nullptr, GAME);
    CVar::Register("timeMgrAlarmMessage", "The time manager's alarm message", 0x10, "", nullptr, GAME);
    CVar::Register("timeMgrAlarmEnabled", "Toggles whether or not the time manager's alarm will go off", 0x10, "0", nullptr, GAME);
    CVar::Register("combatLogRetentionTime", "The maximum duration in seconds to retain combat log entries", 0x10, "300", nullptr, GAME);
    CVar::Register("currencyTokensUnused1", "Currency token types marked as unused.", 0x20, "0", nullptr, GAME);
    CVar::Register("currencyTokensUnused2", "Currency token types marked as unused.", 0x20, "0", nullptr, GAME);
    CVar::Register("currencyTokensBackpack1", "Currency token types shown on backpack.", 0x20, "0", nullptr, GAME);
    CVar::Register("currencyTokensBackpack2", "Currency token types shown on backpack.", 0x20, "0", nullptr, GAME);
    CVar::Register("showTokenFrame", "The token UI has been shown", 0x20, "0", nullptr, GAME);
    CVar::Register("showTokenFrameHonor", "The token UI has shown Honor", 0x20, "0", nullptr, GAME);
    CVar::Register("predictedHealth", "Whether or not to use predicted health values in the UI", 0x10, "1", nullptr, GAME);
    CVar::Register("predictedPower", "Whether or not to use predicted power values in the UI", 0x10, "1", nullptr, GAME);
    CVar::Register("threatWorldText", "Whether or not to show threat floaters in combat", 0x10, "1", nullptr, GAME);
    CVar::Register("threatShowNumeric", "Whether or not to show numeric threat on the target and focus frames", 0x10, "0", nullptr, GAME);
    CVar::Register("threatPlaySounds", "Whether or not to sounds when certain threat transitions occur", 0x10, "1", nullptr, GAME);
    CVar::Register("ShowAllSpellRanks", "show either all spell ranks, or only the highest rank", 0x10, "1", nullptr, GAME);
    CVar::Register("ShowClassColorInNameplate", "use this to display the class color in the nameplate health bar", 0x20, "0", nullptr, GAME); // TODO callback FUN_00512770
    CVar::Register("lfgSelectedRoles", "Stores what roles the player is willing to take on.", 0x120, "0", nullptr, GAME);
    CVar::Register("lfdCollapsedHeaders", "Stores which LFD headers are collapsed.", 0x120, "", nullptr, GAME);
    CVar::Register("lfdSelectedDungeons", "Stores which LFD dungeons are selected.", 0x120, "", nullptr, GAME);
    CVar::Register("lastTalkedToGM", "Stores the last GM someone was talking to in case they reload the UI while the GM chat window is open.", 0x10, "", nullptr, GAME);
    CVar::Register("autoCompleteResortNamesOnRecency", "Shows people you recently spoke with higher up on the AutoComplete list.", 0x10, "1", nullptr, GAME); // TODO callback FUN_00512830
    CVar::Register("autoCompleteWhenEditingFromCenter", "If you edit a name by inserting characters into the center, a smarter auto-complete will occur.", 0x10, "1", nullptr, GAME); // TODO callback FUN_00512850
    CVar::Register("autoCompleteUseContext", "The system will, for example, only show people in your guild when you are typing /gpromote. Names will also never be removed.", 0x10, "1", nullptr, GAME); // TODO callback FUN_00512870
    CVar::Register("colorChatNamesByClass", "If enabled, the name of a player speaking in chat will be colored according to his class.", 0x10, "0", nullptr, GAME);
    CVar::Register("autoFilledMultiCastSlots", "Bitfield that saves whether multi-cast slots have been automatically filled.", 0x20, "0", nullptr, GAME);
    CVar::Register("minimapTrackedInfo", "Stores the minimap tracking that was active last session.", 0x20, "", nullptr, GAME);
    CVar::Register("questPOI", "If enabled, the quest POI system will be used.", 0x20, "1", nullptr, GAME);
    CVar::Register("miniWorldMap", "Whether or not the world map has been toggled to smaller size", 0x20, "0", nullptr, GAME);
    CVar::Register("dontShowEquipmentSetsOnItems", "Don't show which equipment sets an item is associated with", 0x10, "0", nullptr, GAME);
}
