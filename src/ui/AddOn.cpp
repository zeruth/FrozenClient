#include "ui/AddOn.hpp"

#include "client/Client.hpp"
#include "console/CVar.hpp"
#include "ui/FrameScript.hpp"
#include "ui/FrameXML.hpp"
#include "util/CStatus.hpp"
#include <common/MD5.hpp>
#include <cstdio>
#include <storm/Array.hpp>
#include <storm/String.hpp>

namespace {

// Add-ons are load-on-demand and few: the interface asks for one by name and never enumerates what
// has been loaded, so a flat list of names is all the bookkeeping this needs.
const int32_t ADDON_MAX = 64;
const int32_t ADDON_NAME_MAX = 64;

char s_loaded[ADDON_MAX][ADDON_NAME_MAX];
int32_t s_loadedCount;

// The reference's list of every add-on it found on disk. Nothing fills it: the enumeration that
// builds it is not ported, and the loader above does not need it.
TSFixedArray<UIADDON*> s_addOns;    // ref: DAT_00c24944

} // namespace

// ref: FUN_005f4ff0
uint32_t AddOnGetNumAddOns() {
    return s_addOns.Count();
}

// ref: FUN_005f5000
UIADDON* AddOnGetAddOn(uint32_t index) {
    if (s_addOns.Count() <= index) {
        return nullptr;
    }

    return s_addOns[index];
}

bool AddOnIsLoaded(const char* name) {
    if (!name || !*name) {
        return false;
    }

    for (int32_t i = 0; i < s_loadedCount; i++) {
        if (!SStrCmpI(s_loaded[i], name, STORM_MAX_STR)) {
            return true;
        }
    }

    return false;
}

int32_t AddOnLoad(const char* name, const char** reason) {
    *reason = nullptr;

    if (!name || !*name || SStrLen(name) >= ADDON_NAME_MAX) {
        *reason = "MISSING";

        return 0;
    }

    // Loading twice would run every script in it a second time, redefining frames that already
    // exist. The interface asks repeatedly, so this is the common case, not an error.
    if (AddOnIsLoaded(name)) {
        return 1;
    }

    if (s_loadedCount >= ADDON_MAX) {
        *reason = "MISSING";

        return 0;
    }

    // Both the folder and the table of contents inside it carry the add-on's name.
    char tocPath[260];
    SStrPrintf(tocPath, sizeof(tocPath), "Interface\\AddOns\\%s\\%s.toc", name, name);

    CStatus status;
    MD5_CTX md5;
    MD5Init(&md5);

    // Record the name before loading rather than after: the scripts run during the load may ask
    // whether the add-on is loaded, and the answer has to already be yes.
    SStrCopy(s_loaded[s_loadedCount], name, ADDON_NAME_MAX);
    s_loadedCount++;

    if (!FrameXML_CreateFrames(tocPath, nullptr, &md5, &status)) {
        s_loadedCount--;
        s_loaded[s_loadedCount][0] = 0;

        *reason = "MISSING";

        return 0;
    }

    // Never let a load fail quietly. A collector that nobody reads is exactly how the absence of
    // this whole subsystem stayed invisible.
    for (auto entry = status.m_entries.Head(); entry; entry = status.m_entries.Next(entry)) {
        fprintf(stderr, "AddOn %s: %s\n", name, entry->text);
    }

    // The game-side events have no enum yet; 423 is ADDON_LOADED in g_scriptEvents.
    FrameScript_SignalEvent(423, "%s", name);

    return 1;
}

// ref: FUN_005f4ca0
bool AddOnVersionCheckEnabled() {
    return s_checkAddonVersionCvar->m_intValue != 0;
}

// ref: FUN_005f4cb0
void AddOnSetVersionCheck(bool enabled) {
    if (enabled) {
        s_checkAddonVersionCvar->Set("1", true, false, false, true);

        return;
    }

    s_checkAddonVersionCvar->Set("0", true, false, false, true);
}
