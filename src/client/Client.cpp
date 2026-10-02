#include "world/Shadow.hpp"
#include "world/MapWeather.hpp"
#include "ui/game/RaidTarget.hpp"
#include "ui/game/CGActionBar.hpp"
#include "ui/game/CGMinimapFrame.hpp"
#include "ui/game/CGPartyInfo.hpp"
#include "object/client/AuraCache.hpp"
#include "object/client/CastCache.hpp"
#include "object/client/QuestStatusCache.hpp"
#include "object/client/SpellBook.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/NameCache.hpp"
#include "object/client/ItemCache.hpp"
#include "client/Client.hpp"
#include <storm/Log.hpp>
#include "ui/InputControl.hpp"
#include "client/gui/OsGui.hpp"
#include "gx/Device.hpp"
#include "async/AsyncFile.hpp"
#include "client/Archive.hpp"
#include "client/ClientHandlers.hpp"
#include "client/ClientServices.hpp"
#include "component/CCharacterComponent.hpp"
#include "console/Command.hpp"
#include "console/Console.hpp"
#include "console/CVar.hpp"
#include "console/Device.hpp"
#include "console/Initialize.hpp"
#include "console/Screen.hpp"
#include "db/Db.hpp"
#include "glue/CGlueMgr.hpp"
#include "ui/Util.hpp"
#include "glue/GlueScriptEvents.hpp"
#include "gx/LoadingScreen.hpp"
#include "gx/Screen.hpp"
#include "gx/Texture.hpp"
#include "model/CM2Model.hpp"
#include "model/Model2.hpp"
#include "net/Poll.hpp"
#include "object/Client.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "sound/Interface.hpp"
#include "ui/FrameScript.hpp"
#include "ui/FrameXML.hpp"
#include "ui/Game.hpp"
#include "util/Filesystem.hpp"
#include "util/Random.hpp"
#include "util/SFile.hpp"
#include "world/World.hpp"
#include <cstdlib>
#include <storm/Array.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <bc/Debug.hpp>
#include <common/Prop.hpp>
#include <common/Time.hpp>
#include "util/OsSystem.hpp"
#include <storm/Error.hpp>

CVar* Client::g_accountNameVar;
CVar* Client::g_readTOSVar;
CVar* Client::g_readEULAVar;
CVar* Client::g_readTerminationWithoutNoticeVar;
CVar* Client::g_readScanningVar;
CVar* Client::g_readContestVar;
CVar* Client::g_accountListVar;
HEVENTCONTEXT Client::g_clientEventContext;

CGameTime g_clientGameTime;

static CVar* s_desktopGammaCvar;
static CVar* s_gammaCvar;
CVar* s_accountUsesTokenCvar;
static CVar* s_movieCvar;
static CVar* s_expansionMovieCvar;
static CVar* s_movieSubtitleCvar;
CVar* s_checkAddonVersionCvar;
static CVar* s_mouseSpeedCvar;
static CVar* s_errorsCvar;
static CVar* s_showErrorsCvar;
static CVar* s_errorLevelMinCvar;
static CVar* s_errorLevelMaxCvar;
static CVar* s_errorFilterCvar;
static CVar* s_lastCharacterIndexCvar;
static CVar* s_screenshotFormatCvar;
static CVar* s_screenshotQualityCvar;
static CVar* s_textureCacheSizeCvar;
static CVar* s_textureFilteringModeCvar;
static CVar* s_uiFasterCvar;

// ref: FUN_00401a80
// DesktopGamma on hands the device the desktop's own ramp; off, it goes back to the Gamma value.
bool DesktopGammaCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    if (SStrToInt(value) == 0) {
        float gamma = s_gammaCvar ? s_gammaCvar->m_floatValue : 1.0f;

        if (gamma < 0.001f) {
            gamma = 1.0f;
        }

        g_theGxDevicePtr->DeviceSetGamma(gamma);
        return true;
    }

    CGxGammaRamp ramp;
    g_theGxDevicePtr->DeviceDesktopGammaRamp(ramp);
    g_theGxDevicePtr->DeviceSetGammaRamp(ramp);
    return true;
}

// ref: FUN_00401a30
// Applied only while DesktopGamma is off.
bool GammaCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    float gamma = SStrToFloat(value);

    if (gamma < 0.001f) {
        gamma = 1.0f;
    }

    if (s_desktopGammaCvar && s_desktopGammaCvar->m_intValue == 0) {
        g_theGxDevicePtr->DeviceSetGamma(gamma);
    }

    return true;
}

// ref: FUN_00402320
// The released-texture cache's budget, in megabytes, up to what this machine allows.
bool TextureCacheSizeCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    int32_t size = SStrToInt(value) << 20;
    int32_t maximum = TextureGetDefaultCacheSize();

    char msg[256];

    if (size < 0 || size > maximum) {
        SStrPrintf(msg, sizeof(msg), "Texture cache size (%d meg) greater than maximum allowed for your system (%d meg).", size >> 20, maximum >> 20);
        ConsoleWrite(msg, DEFAULT_COLOR);

        return false;
    }

    SStrPrintf(msg, sizeof(msg), "Texture cache size set to %d meg.", size >> 20);
    ConsoleWrite(msg, DEFAULT_COLOR);

    TextureSetCacheSize(size);

    return true;
}

// ref: FUN_004023c0
bool TextureFilteringCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    int32_t mode = SStrToInt(value);

    if (mode >= 0 && mode < 6) {
        return true;
    }

    char msg[256];
    SStrPrintf(msg, sizeof(msg), "Texture filtering mode must be in range 0 to %d.", 6);
    ConsoleWrite(msg, DEFAULT_COLOR);

    return false;
}

// Reference 0x00b47948: the UIFaster CVar's bit 1, read by the UI render batches.
int32_t g_uiFasterBatching;

// ref: FUN_00402250
// UIFaster: bit 0 lets small UI textures share atlas pages, bit 1 is read by the UI render batches.
//
// DIVERGED in one branch: the reference turns the atlas off ("Texture atlas disabled.") on the
// hardware its console detection recognised as an Intel adapter, or as one with a known atlas
// problem. Frozen does not port the console hardware detection (0x00769d00, DAT_00cabb38), so the
// hardware is always the reference's "not detected" case and the bit is taken as given.
bool UIFasterCalllback(CVar* var, const char* oldValue, const char* value, void* arg) {
    uint32_t mode = SStrToInt(value);

    if (mode > 3) {
        return false;
    }

    TextureSetAtlasEnable(mode & 0x1);

    g_uiFasterBatching = (mode >> 1) & 0x1;

    return true;
}

void AsyncFileInitialize() {
    // TODO
    AsyncFileReadInitialize(0, 100);
}

void BaseInitializeGlobal() {
    PropInitialize();
}

int32_t ClientGameTimeTickHandler(const void* data, void* param) {
    STORM_ASSERT(data);

    g_clientGameTime.GameTimeUpdate(static_cast<const EVENT_DATA_IDLE*>(data)->elapsedSec);

    return 1;
}

void ClientInitializeGameTime() {
    ClientServices::SetMessageHandler(SMSG_GAME_SPEED_SET, &ReceiveNewGameSpeed, nullptr);
    ClientServices::SetMessageHandler(SMSG_LOGIN_SET_TIME_SPEED, &ReceiveNewTimeSpeed, nullptr);
    ClientServices::SetMessageHandler(SMSG_GAME_TIME_UPDATE, &ReceiveGameTimeUpdate, nullptr);
    ClientServices::SetMessageHandler(SMSG_SERVERTIME, &ReceiveServerTime, nullptr);
    ClientServices::SetMessageHandler(SMSG_GAME_TIME_SET, &ReceiveNewGameTime, nullptr);
    ClientServices::SetMessageHandler(SMSG_WEATHER, &ReceiveWeather, nullptr);
    ClientServices::SetMessageHandler(SMSG_UPDATE_ACTION_BUTTONS, &ReceiveActionButtons, nullptr);
    NameCacheRegisterHandlers();
    ItemCacheRegisterHandlers();
    AuraCacheRegisterHandlers();
    CastCacheRegisterHandlers();
    QuestStatusRegisterHandlers();
    SpellBookRegisterHandlers();
    CGMinimapFrameRegisterHandlers();
    CGPartyInfo::RegisterHandlers();
    RaidTargetRegisterHandlers();
    ClientServices::SetMessageHandler(SMSG_EMOTE, &ReceiveEmote, nullptr);

    // TODO initialize s_forcedChangeCallbacks
}

int32_t ClientIdle(const void* data, void* param) {
    ClientGameTimeTickHandler(data, nullptr);

    // TODO Player_C_ZoneUpdateHandler(data, nullptr);

    return 1;
}

// The loading screen stays up until the player's model has loaded (0x409800 in the original)
static int32_t ClientPlayerModelReady() {
    auto player = ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__);

    if (!player || !player->m_model) {
        return 0;
    }

    return player->m_model->IsLoaded(0, 0);
}

void ClientInitializeGame(uint32_t mapId, C3Vector position) {
    // TODO

    if (CWorld::s_weather) {
        CWorld::s_weather->Clear();
    }

    WeatherSetAmbience(-1);

    // TODO

    ClntObjMgrInitializeShared();
    ClntObjMgrInitializeStd(mapId);

    // TODO

    CGGameUI::InitializeGame();

    LoadingScreenSetPlayerReadyCallback(&ClientPlayerModelReady);

    // TODO

    EventRegister(EVENT_ID_IDLE, ClientIdle);
    ClientInitializeGameTime();

    // TODO

    ClientServices::SetMessageHandler(SMSG_INVENTORY_CHANGE_FAILURE, InventoryChangeFailureHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_NOTIFICATION, NotifyHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_PLAYED_TIME, PlayedTimeHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_NEW_WORLD, NewWorldHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_TRANSFER_PENDING, TransferPendingHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_TRANSFER_ABORTED, TransferAbortedHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_LOGIN_VERIFY_WORLD, LoginVerifyWorldHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_KICK_REASON, CGlueMgr::OnKickReasonMsg, nullptr);

    // Load the map the character is on; the loading bar's second half tracks it
    auto mapRec = g_mapDB.GetRecord(mapId);

    if (!mapRec) {
        char message[64];
        SStrPrintf(message, sizeof(message), "Bad zone ID %i", mapId);
        ConsoleWrite(message, DEFAULT_COLOR);
    } else {
        CWorld::SetLoadProgressCallback(&LoadingScreenSetProgress3);
        CWorld::LoadMap(mapRec->m_directory, position, mapId);
        CWorld::SetLoadProgressCallback(nullptr);
    }

    // TODO
}

// ref: FUN_004015e0
bool MouseSpeedCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    OsGuiSetMouseSpeed(SStrToFloat(value));

    return true;
}

// ref: FUN_00401600
bool ErrorsCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    int32_t enabled = SStrToInt(value);
    const char* message;

    if (!enabled) {
        // TODO FUN_0040b3c0: disable the error display
        message = "Error display disabled";
    } else {
        // TODO FUN_0040b390: enable the error display
        message = "Error display enabled";
    }

    ConsoleWrite(message, DEFAULT_COLOR);
    // TODO FUN_004b4e50(enabled)

    return true;
}

// ref: FUN_00401650
bool ShowErrorsCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    int32_t shown = SStrToInt(value);
    const char* message;

    if (!shown) {
        // TODO FUN_0040b400: hide the error display
        message = "Error display hidden";
    } else {
        // TODO FUN_0040b3e0: show the error display
        message = "Error display shown";
    }

    ConsoleWrite(message, DEFAULT_COLOR);
    // TODO FUN_004b4e50(shown)

    return true;
}

// ref: FUN_004017c0
bool ErrorLevelMinCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    uint32_t level = SStrToInt(value);

    if (level < 4) {
        // TODO FUN_004b4e60(level); FUN_004016a0()

        return true;
    }

    ConsoleWriteA("%i is not valid, valid values are 0 - %i", DEFAULT_COLOR, level, 3);

    return false;
}

// ref: FUN_00401800
bool ErrorLevelMaxCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    uint32_t level = SStrToInt(value);

    if (level < 4) {
        // TODO FUN_004b4e80(level); FUN_004016a0()

        return true;
    }

    ConsoleWriteA("%i is not valid, valid values are 0 - %i", DEFAULT_COLOR, level, 3);

    return false;
}

// ref: FUN_00401a10
bool ErrorFilterCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO FUN_004018d0(value): parse the filter, refuse the value when it does not parse;
    // FUN_00401840(): apply it

    return true;
}

// ref: FUN_00401b20
bool ScreenshotFormatCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    ScreenshotSetFormat(value);

    return true;
}

// ref: FUN_00401b40
bool ScreenshotQualityCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    ScreenshotSetQuality(SStrToInt(value));

    return true;
}

// ref: FUN_00401b60
void ClientRegisterConsoleCommands() {
    // TODO ConsoleCommandRegister("reloadUI", <tail jump at 00401b00>, GRAPHICS, nullptr);
    // TODO ConsoleCommandRegister("perf", <FUN_008c8de0>, DEBUG, nullptr);

    Client::g_accountNameVar = CVar::Register("accountName", "Saved account name", 0x40, "", nullptr, GAME, false, nullptr, false);
    Client::g_accountListVar = CVar::Register("accountList", "List of wow accounts for saved Blizzard account", 0x0, "", nullptr, GAME, false, nullptr, false);
    s_accountUsesTokenCvar = CVar::Register("g_accountUsesToken", "Saved whether uses authenticator", 0x0, "0", nullptr, GAME, false, nullptr, false);
    s_movieCvar = CVar::Register("movie", "Show movie on startup", 0x0, "1", nullptr, GAME, false, nullptr, false);

    // DEBUG category and flag 0x2, which is what the reference gives it -- not GAME like its
    // neighbours here. Its own help text says a restart is needed, and frozen never reads it, so
    // nothing acts on a change either way.
    CVar::Register(
        "processAffinityMask",
        "Sets which core(s) WoW may execute on - changes require restart to take effect",
        0x2,
        "0",
        nullptr,
        DEBUG,
        false,
        nullptr,
        false
    );
    s_expansionMovieCvar = CVar::Register("expansionMovie", "Show expansion movie on startup", 0x0, "1", nullptr, GAME, false, nullptr, false);
    s_movieSubtitleCvar = CVar::Register("movieSubtitle", "Show movie subtitles", 0x0, "0", nullptr, GAME, false, nullptr, false);
    s_checkAddonVersionCvar = CVar::Register("checkAddonVersion", "Check interface addon version number", 0x0, "1", nullptr, GAME, false, nullptr, false);

    char mouseSpeed[32];
    SStrPrintf(mouseSpeed, sizeof(mouseSpeed), "%1.1f", OsGuiGetMouseSpeed());
    s_mouseSpeedCvar = CVar::Register("mouseSpeed", nullptr, 0x0, mouseSpeed, &MouseSpeedCallback, GAME, false, nullptr, false);

    s_errorsCvar = CVar::Register("Errors", nullptr, 0x0, "0", &ErrorsCallback, DEBUG, false, nullptr, false);
    s_showErrorsCvar = CVar::Register("ShowErrors", nullptr, 0x0, "1", &ShowErrorsCallback, DEBUG, false, nullptr, false);
    s_errorLevelMinCvar = CVar::Register("ErrorLevelMin", nullptr, 0x0, "2", &ErrorLevelMinCallback, DEBUG, false, nullptr, false);
    s_errorLevelMaxCvar = CVar::Register("ErrorLevelMax", nullptr, 0x0, "3", &ErrorLevelMaxCallback, DEBUG, false, nullptr, false);
    s_errorFilterCvar = CVar::Register("ErrorFilter", nullptr, 0x0, "all", &ErrorFilterCallback, DEBUG, false, nullptr, false);
    s_desktopGammaCvar = CVar::Register("DesktopGamma", nullptr, 0x0, "0", &DesktopGammaCallback, GRAPHICS, false, nullptr, false);
    s_gammaCvar = CVar::Register("Gamma", nullptr, 0x0, "1.0", &GammaCallback, GRAPHICS, false, nullptr, false);
    s_lastCharacterIndexCvar = CVar::Register("lastCharacterIndex", "Last character selected", 0x0, "0", nullptr, GAME, false, nullptr, false);
    Client::g_readTOSVar = CVar::Register("readTOS", "Status of the TOS", 0x0, "0", nullptr, GAME, false, nullptr, false);
    Client::g_readEULAVar = CVar::Register("readEULA", "Status of the EULA", 0x0, "0", nullptr, GAME, false, nullptr, false);
    Client::g_readTerminationWithoutNoticeVar = CVar::Register("readTerminationWithoutNotice", "Status of the Termination without Notice notice", 0x0, "0", nullptr, GAME, false, nullptr, false);
    Client::g_readScanningVar = CVar::Register("readScanning", "Status of the Scanning notice", 0x0, "0", nullptr, GAME, false, nullptr, false);
    Client::g_readContestVar = CVar::Register("readContest", "Status of the Contest notice", 0x0, "0", nullptr, GAME, false, nullptr, false);
    s_screenshotFormatCvar = CVar::Register("screenshotFormat", "Set the format of screenshots", 0x1, "jpeg", &ScreenshotFormatCallback, GRAPHICS, false, nullptr, false);
    s_screenshotQualityCvar = CVar::Register("screenshotQuality", "Set the quality of screenshots (1 - 10)", 0x1, "3", &ScreenshotQualityCallback, GRAPHICS, false, nullptr, false);

    auto showToolsVar = CVar::Register("showToolsUI", "Display the launcher when starting the game", 0x0, "-1", nullptr, GAME, false, nullptr, false);

    if (showToolsVar->GetInt() < 0 || showToolsVar->GetInt() > 1) {
        showToolsVar->Set("1", true, false, false, true);
    }

    // TODO FUN_00422140 (trial account?): when set, register "converted" ("Trial to Retail", "0")
    // and pass its value to FUN_004209b0

    CVar::Register("accounttype", "Account Type", 0x0, "", nullptr, GAME, false, nullptr, false);
}

void ClientPostClose(int32_t a1) {
    // TODO s_finalDialog = a1;
    EventPostCloseEx(nullptr);
}

int32_t DestroyEngineCallback(const void* a1, void* a2) {
    // TODO

    WowClientDestroy();

    // TODO the reference's shutdown between the two (0x004066e5..0x00406715)

    TextureDestroy();

    // TODO

    return 1;
}

int32_t InitializeEngineCallback(const void* a1, void* a2) {
    // TODO
    // sub_4D2A30();

    AsyncFileInitialize();
    TextureInitialize();

    ModelBlobLoad("world\\model.blob");

    if (SFile::IsStreamingMode()) {
        TextureLoadBlob("world\\liquid.tex");
    }

    ScrnInitialize(0);
    ConsoleScreenInitialize(nullptr); // TODO argument

    s_textureFilteringModeCvar = CVar::Register(
        "textureFilteringMode",
        "Texture filtering mode",
        0x1,
        "1",
        &TextureFilteringCallback,
        GRAPHICS
    );

    s_uiFasterCvar = CVar::Register(
        "UIFaster",
        "UI acceleration option",
        0x0,
        "3",
        &UIFasterCalllback,
        GRAPHICS
    );

    s_textureCacheSizeCvar = CVar::Register(
        "textureCacheSize",
        "Texture cache size in bytes",
        0x1,
        "32",
        &TextureCacheSizeCallback,
        GRAPHICS
    );

    TextureSetCacheSize(s_textureCacheSizeCvar->m_intValue << 20);

    // AddConsoleDeviceDefaultCallback(SetDefaults);

    // if (ConsoleDeviceHardwareChanged()) {
    //     v3 = 0;

    //     do {
    //         SetDefaults(v3++);
    //     } while (v3 < 3);
    // }

    auto m2Flags = M2RegisterCVars();
    M2Initialize(m2Flags, 0);

    // The texture filtering mode picks a filter and an anisotropy from the reference's two tables
    // (0x00ab6128 and 0x00ab6140): bilinear, trilinear, then anisotropic at 2, 4, 8 and 16.
    static int32_t s_filterModes[6] = { 3, 4, 5, 5, 5, 5 };
    static uint32_t s_maxAnisotropy[6] = { 1, 1, 2, 4, 8, 16 };

    int32_t filtering = s_textureFilteringModeCvar->m_intValue;

    TextureSetFilterMode(s_filterModes[filtering]);
    TextureSetMaxAnisotropy(s_maxAnisotropy[filtering]);


    WowClientInit();

    return 1;
}

static int32_t AddRunOnceFile(const OSFILEENTRY* entry, void* param) {
    auto files = static_cast<TSGrowableArray<char*>*>(param);
    *files->New() = SStrDupA(entry->name, __FILE__, __LINE__);
    return 0;
}

static int CompareRunOnceFiles(const void* a, const void* b) {
    return SStrCmp(*static_cast<const char* const*>(a), *static_cast<const char* const*>(b), STORM_MAX_STR);
}

// Applies every WTF\RunOnce*.wtf in name order, then discards the list (0x406740 in the original)
static void ProcessRunOnceFiles(int32_t (*callback)(const char*)) {
    char basePath[STORM_MAX_PATH];
    SFile::GetBasePath(basePath, sizeof(basePath));

    char dir[STORM_MAX_PATH];
    SStrPrintf(dir, sizeof(dir), "%sWTF\\", basePath);

    TSGrowableArray<char*> files;
    OsFileEnumerate(dir, "RunOnce*.wtf", &AddRunOnceFile, &files, 1);

    if (files.Count() > 1) {
        qsort(files.m_data, files.Count(), sizeof(char*), &CompareRunOnceFiles);
    }

    for (uint32_t i = 0; i < files.Count(); ++i) {
        callback(files[i]);
        SMemFree(files[i], __FILE__, __LINE__, 0);
    }
}

// Client shutdown (0x406B70 in the original)
static void Sub406B70() {
    // TODO
    // - misc shutdown

    ProcessRunOnceFiles(&CVar::RemoveFile);

    ConsoleDestroyClientCVar();

    // TODO
    // - misc shutdown
    // - display any pending fatal error
}

int32_t g_localeIndex;

// TODO name this (maybe something like InitializeLocale?)
void Sub405DD0() {
    // TODO

    ClientOpenArchives();

    // TODO
    // - Wow.ini handling

    // TODO get this from the soupy mess of locale checks above
    auto locale = "enUS";

    // The reference stores the locale as its index in the table at 0x00ad2fe0.
    static const char* s_locales[9] = { "enUS", "koKR", "frFR", "deDE", "zhCN", "zhTW", "esES", "esMX", "ruRU" };

    for (int32_t i = 0; i < 9; i++) {
        if (!SStrCmpI(locale, s_locales[i], STORM_MAX_STR)) {
            g_localeIndex = i;
            break;
        }
    }

    ClientServices::InitLoginServerCVars(1, locale);

    // TODO
}

bool LocaleChangedCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO

    return true;
}

// Reference 0x00b2f9d8: the timing test's result, as InitializeGlobal read it.
static int32_t s_timingTestError;

// ref: FUN_00403200
// Only 0..2 are methods. Picking a different one clears a recorded test error, which the next
// start-up will measure again.
static bool TimingMethodCallback(CVar* var, const char* oldValue, const char* newValue, void* arg) {
    uint32_t method = atol(newValue);

    if (method < 3) {
        auto timingTestError = static_cast<CVar*>(arg);

        if (oldValue && method != static_cast<uint32_t>(atol(oldValue)) && timingTestError->m_intValue) {
            timingTestError->SetReadOnly(false);
            timingTestError->Set("0", true, false, false, true);
            timingTestError->SetReadOnly(true);
        }

        return true;
    }

    ConsolePrintf("'%s' is not a valid timing method. Valid methods are:", newValue);

    for (int32_t i = 0; i < 3; i++) {
        ConsolePrintf("  %d - %s", i, OsTimeGetTimingMethodName(i));
    }

    return false;
}

// ref: FUN_004032a0
static int32_t TimingInfoCommand(const char* command, const char* arguments) {
    int32_t desired = CVar::LookupRegistered("timingMethod")->m_intValue;
    int32_t selected = OsTimeGetTimingMethod();
    int32_t error = CVar::LookupRegistered("timingTestError")->m_intValue;

    ConsolePrintf("Timing method desired: %d - %s", desired, OsTimeGetTimingMethodName(desired));
    ConsolePrintf("Timing method selected: %d - %s", selected, OsTimeGetTimingMethodName(selected));
    ConsolePrintf("Timing test error: %d", error);

    return 1;
}

int32_t InitializeGlobal() {
    // TODO

    // SCmdRegisterArgList(&ProcessCommandLine(void)::s_wowArgList, 17u);

    // CmdLineProcess();

    // sub_403600("WoW.mfil");

    // if (dword_B2FA10 != 2) {
    //     sub_403560();
    // }

    // LOBYTE(v24) = 0;

    // if (sub_422140()) {
    //     LOBYTE(v24) = OsDirectoryExists((int)"WTF/Account") == 0;
    // }

    // ClientServices::LoadCDKey();

    ConsoleInitializeClientCommand();

    ConsoleInitializeClientCVar("Config.wtf");

    // sub_7663F0();

    ProcessRunOnceFiles(&CVar::Load);

    // CVar::Register("dbCompress", "Database compression", 0, "-1", 0, 5, 0, 0, 0);

    auto localeVar = CVar::Register(
        "locale",
        "Set the game locale",
        0x0,
        "****",
        &LocaleChangedCallback,
        DEFAULT,
        false,
        nullptr,
        false
    );

    if (SStrCmp(localeVar->GetString(), "****") == 0) {
        localeVar->Set("enUS", true, false, false, true);
    }

    CVar::Register(
        "useEnglishAudio",
        "override the locale and use English audio",
        0x0,
        "0",
        nullptr,
        DEFAULT,
        false,
        nullptr,
        false
    );

    // if (sub_422140()) {
    //     sub_4036B0(v24, 0, a2, (int)v2, (char)v24);
    // }

    // SStrCopy(&a1a, v2->m_stringValue.m_str, 5);

    // sub_402D50(&a1a);

    // CVar::Set(v2, &a1a, 1, 0, 0, 1);

    char localePath[STORM_MAX_PATH];
    SStrPrintf(localePath, sizeof(localePath), "%s%s", "Data\\", localeVar->GetString());
    SFile::SetLocalePath(localePath);

    // sub_423D70();

    Sub405DD0();

    // CVar* v3 = CVar::Register(
    //     "processAffinityMask",
    //     "Sets which core(s) WoW may execute on - changes require restart to take effect",
    //     2,
    //     "0",
    //     &sub_4022E0,
    //     0,
    //     0,
    //     0,
    //     0
    // );

    // CVar* v4 = CVar::Lookup("videoOptionsVersion");

    // if (!v4 || v4->m_intValue < 3) {
    //     SStrPrintf(v23, 8, "%u", 0);
    //     CVar::Set(v3, v23, 1, 0, 0, 1);
    //     CVar::Update((int)v3);
    // }

    // v5 = v3->m_intValue;

    // if (v5) {
    //     SSetCurrentProcessAffinityMask(v5);
    // }

    BaseInitializeGlobal();

    EventInitialize(1, 0);

    CVar* timingTestError = CVar::Register(
        "timingTestError",
        "Error reported by the timing validation system",
        0x6,
        "0",
        nullptr,
        DEFAULT,
        false,
        nullptr,
        false
    );

    CVar* timingMethod = CVar::Register(
        "timingMethod",
        "Desired method for game timing",
        0x2,
        "0",
        TimingMethodCallback,
        DEFAULT,
        false,
        timingTestError,
        false
    );

    OsTimeStartup(timingMethod->m_intValue);

    ConsoleCommandRegister("timingInfo", TimingInfoCommand, DEBUG, nullptr);

    s_timingTestError = OsTimeGetTestError();

    if (s_timingTestError != timingTestError->m_intValue) {
        char value[48];
        SStrPrintf(value, sizeof(value), "%d", s_timingTestError);

        timingTestError->SetReadOnly(false);
        timingTestError->Set(value, true, false, false, true);
        timingTestError->Update();
        timingTestError->SetReadOnly(true);

        // The reference hands the CVar itself to %d, not its value.
        ConsolePrintf("Timing test error: %d", static_cast<int32_t>(reinterpret_cast<intptr_t>(timingTestError)));
    }

    // WowClientDB<Startup_StringsRec>::Load(&g_Startup_StringsDB, 0, ".\\Client.cpp", 0x12E3u);
    // Startup_StringsRec* v11 = g_Startup_StringsDB.GetRecordByIndex(1);
    // const char* v12;

    // if (v11) {
    //     v12 = v11->m_text;
    // } else {
    //     v12 = "World of Warcraft";
    // }

    // TODO
    // - replace with above logic for loading from Startup_Strings.dbc
    const char* v12 = "World of Warcraft";

    char v15[260];

    SStrCopy(v15, v12, 0x7FFFFFFF);

    ConsoleDeviceInitialize(v15);

    // OsIMEInitialize();

    uint32_t seed = OsGetAsyncTimeMs();
    g_rndSeed.SetSeed(seed);

    Client::g_clientEventContext = EventCreateContextEx(
        1,
        &InitializeEngineCallback,
        &DestroyEngineCallback,
        0,
        0
    );

    return 1;
}

void CommonMain() {
    StormInitialize();

    // TODO
    // - error log setup
    // - misc other setup

    if (InitializeGlobal()) {
        EventDoMessageLoop();

        Sub406B70();
    }

    // TODO
    // - misc cleanup
}

void BlizzardAssertCallback(const char* a1, const char* a2, const char* a3, uint32_t a4) {
    if (*a2) {
        SErrDisplayError(0, a3, a4, a2, 0, 1, 0x11111111);
    } else {
        SErrDisplayError(0, a3, a4, a1, 0, 1, 0x11111111);
    }
}

void StormInitialize() {
    // TODO
    // SStrInitialize();
    // SErrInitialize();
    SLogInitialize();
    // SFile::Initialize();

    OsEnableCpuLog();

    Blizzard::Debug::SetAssertHandler(BlizzardAssertCallback);
}

void WowClientDestroy() {
    // TODO

    ShadowDestroy();

    CGlueMgr::Shutdown();

    // TODO
}

void WowClientInit() {
    EventRegister(EVENT_ID_TICK, reinterpret_cast<EVENTHANDLERFUNC>(&CWorld::OnTick));

    // TODO
    // _cfltcvt_init_0();

    ClientRegisterConsoleCommands();
    ClientDBInitialize();

    LoadingScreenInitialize();

    // Both DEFAULT category and unflagged, which is how the reference registers them alongside
    // the rest of its FrameXML setup. scriptProfile has no callback there either -- its value is
    // read once, here, and passed into the Lua state; changing it later does nothing until the
    // state is rebuilt. taintLog does have a callback in the reference; frozen has no taint
    // machinery for it to switch on, so the value is stored and nothing acts on it.
    auto scriptProfile = CVar::Register(
        "scriptProfile",
        "Whether or not script profiling is enabled",
        0x0,
        "0",
        nullptr,
        DEFAULT,
        false,
        nullptr,
        false
    );

    CVar::Register(
        "taintLog",
        "Whether taint logging is enabled",
        0x0,
        "0",
        nullptr,
        DEFAULT,
        false,
        nullptr,
        false
    );

    FrameScript_Initialize(scriptProfile ? scriptProfile->m_intValue : 0);
    SI2::Init(0);

    // TODO
    // sub_6F66B0();

    FrameXML_RegisterDefault();
    GlueScriptEventsInitialize();
    ScriptEventsInitialize();

    // TODO
    // sub_6F75E0();

    CCharacterComponent::Initialize();

    ClientServices::Initialize();
    // TODO ClientServices::SetMessageHandler(SMSG_TUTORIAL_FLAGS, (int)sub_530920, 0);

    // TODO
    // v2 = CVar::Lookup("EnableVoiceChat");
    // if (v2 && *(_DWORD *)(v2 + 48)) {
    //     ComSatClient_Init();
    // }

    // TODO
    // DBCache_RegisterHandlers();
    // DBCache_Initialize(a1);

    CWorldParam::Initialize();
    CWorld::Initialize();

    // The blob shadow module, where the reference initializes it: right after CWorld::Initialize,
    // which is also what registers the shadowLOD CVar whose callback generates the two fade ramps.
    ShadowInit();

    // TODO
    // GxuLightInitialize();
    // GxuLightBucketSizeSet(16.665001);
    InputControlInitialize();

    CGlueMgr::Initialize();

    // TODO
    // if (GetConsoleMessage()) {
    //     v3 = (const char *)GetConsoleMessage();
    //     CGlueMgr::AddChangedOptionWarning(v3);
    //     SetConsoleMessage(0);
    // }

    // TODO
    // if (sub_422140()) {
    //     sub_421630();
    // }

    // ref: the tail of FUN_00404130
    //
    // The cinematics play on the FIRST start and then never again, which is why they come before
    // the licence screens rather than after: this runs while the glue is being brought up, before
    // anything is shown. The movie screen returns to "login" itself when it finishes or is
    // skipped (MovieFrame_OnHide), so nothing here has to wait for it.
    //
    // The cvar is the memory. It ships as "1", is cleared to "0" the moment the movie screen is
    // chosen, and the write is what stops it playing on every launch -- set movie back to 1 in
    // Config.wtf to see them again.
    //
    // PARTIAL: the reference gates on a flag (its 00b2f9e1) that selects WHICH cvar decides. When
    // that flag is set it reads expansionMovie instead and clears both, which is how a client
    // upgraded to a new expansion plays that expansion's intro once more. Frozen does not track
    // an expansion upgrade, so only the ordinary branch is ported; expansionMovie is left alone
    // rather than cleared on a condition that cannot be evaluated.
    bool playIntro = s_movieCvar && StringToBOOL(s_movieCvar->GetString());

    if (playIntro) {
        s_movieCvar->Set("0", true, false, false, true);

        CGlueMgr::SetScreen("movie");
    } else {
        CGlueMgr::SetScreen("login");
    }

    // TODO
    // CGlueMgr::m_pendingTimerAlert = dword_B2F9D8;
    // sub_7FC5A0();

    EventRegister(EVENT_ID_POLL, &PollNet);
}
