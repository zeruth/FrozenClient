#include "ui/game/RuneInfo.hpp"
#include "db/Db.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/SpellHistory.hpp"
#include "object/client/Spell_C.hpp"
#include "object/Types.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/CGActionBar.hpp"
#include "ui/game/Types.hpp"
#include "world/CWorld.hpp"
#include <common/DataStore.hpp>
#include <common/Time.hpp>
#include <cmath>

static uint32_t s_runeType[8];                      // ref: DAT_00c24304
static uint32_t s_runeBaseType[8];                  // ref: DAT_00c24324
static uint32_t s_runeCooldownStart[8];             // ref: DAT_00c24344 (when the rune will be ready)
static uint32_t s_runeRegenStart[8];                // ref: DAT_00c24364
static int32_t s_runeCount;                         // ref: DAT_00c24384
static uint32_t s_runesReady;                       // ref: DAT_00c24388

static const int32_t RUNE_TYPE_UPDATE = 599;
static const int32_t RUNE_POWER_UPDATE = 598;

static CGPlayer_C* RuneActivePlayer() {
    return static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, "d:\\BuildServer\\WoW\\1\\work\\WoW-code\\branches\\wow-patch-3_3_5_A-BNet\\WoW\\Source\\Object/ObjectClient/Player_C.h", 0xa0));
}

// ref: FUN_005edef0
// Six runes, two of each type, all ready.
void RuneReset() {
    for (int32_t rune = 0; rune < 8; rune++) {
        s_runeRegenStart[rune] = 0;
        s_runeCooldownStart[rune] = 0;
    }

    static const uint32_t types[6] = { 0, 0, 1, 1, 2, 2 };

    for (int32_t rune = 0; rune < 6; rune++) {
        s_runeType[rune] = types[rune];
        s_runeBaseType[rune] = types[rune];
    }

    s_runesReady = 0xFFFFFFFF;
    s_runeCount = 6;
}

// ref: FUN_005edfa0
void RuneSetRegenStart(int32_t rune, uint32_t start) {
    s_runeRegenStart[rune] = start;
}

// ref: FUN_005edfc0
bool RuneIsValid(uint32_t rune) {
    if (rune > 7) {
        return false;
    }

    if (s_runeCount != 0) {
        return static_cast<int32_t>(rune) < s_runeCount;
    }

    return static_cast<int32_t>(rune) < 8;
}

// ref: FUN_005edff0
uint32_t RuneGetType(int32_t rune, bool base) {
    return base ? s_runeBaseType[rune] : s_runeType[rune];
}

// ref: FUN_005ee020
void RuneConvert(int32_t rune, uint32_t type) {
    s_runeType[rune] = type;
    FrameScript_SignalEvent(RUNE_TYPE_UPDATE, "%d", rune + 1);
    SpellSignalUsableUpdate();
}

// ref: FUN_005ee110
uint32_t RuneGetRegenStart(int32_t rune) {
    if (!RuneActivePlayer()) {
        return 0;
    }

    return s_runeRegenStart[rune];
}

// ref: FUN_005ee140
// From how far the rune has regenerated and its type's regeneration rate, when it will be ready.
void RuneSetCooldown(int32_t rune, float progress) {
    auto player = RuneActivePlayer();

    if (!player) {
        return;
    }

    auto rate = player->Player()->runeRegen[s_runeType[rune]];

    if (rate < 0.01f) {
        s_runeCooldownStart[rune] = 0;

        return;
    }

    auto now = OsGetAsyncTimeMs();
    auto left = static_cast<int32_t>((1.0f - progress) * (1.0f / rate) * -1000.0f);

    s_runeCooldownStart[rune] = now - left;
}

// ref: FUN_005ee1f0
// When a rune that is not ready will be; 0 for a ready one or without a player.
uint32_t RuneGetCooldownStart(int32_t rune) {
    if (RuneActivePlayer() && !(s_runesReady & (1u << rune))) {
        return s_runeCooldownStart[rune];
    }

    return 0;
}

// ref: FUN_005ee240
// A new ready mask: a rune that went down starts regenerating (from `progress`, in 255ths, when
// given); one that came up is ready.
void RuneUpdateReady(uint32_t oldReady, uint32_t newReady, const uint8_t* progress) {
    if (!RuneActivePlayer()) {
        return;
    }

    s_runesReady = newReady;

    for (int32_t rune = 0; rune < 8; rune++) {
        uint32_t bit = 1u << rune;

        if ((bit & oldReady) && !(newReady & bit)) {
            if (!RuneActivePlayer() || s_runeRegenStart[rune] == 0) {
                float done = 0.0f;

                if (progress) {
                    done = static_cast<float>(progress[rune]) * (1.0f / 255.0f);

                    if (done > 1.0f) {
                        done = 1.0f;
                    }
                }

                s_runeRegenStart[rune] = OsGetAsyncTimeMs() - static_cast<int32_t>(10000.0f * done);
                RuneSetCooldown(rune, done);
                FrameScript_SignalEvent(RUNE_POWER_UPDATE, "%d%b", rune + 1, 0);
            }
        } else if (!(bit & oldReady) && (newReady & bit)) {
            s_runeRegenStart[rune] = 0;
            s_runeCooldownStart[rune] = 0;
            FrameScript_SignalEvent(RUNE_POWER_UPDATE, "%d%b", rune + 1, 1);
        } else if ((s_runesReady & bit) && RuneActivePlayer() && s_runeRegenStart[rune] != 0) {
            s_runeRegenStart[rune] = 0;
            s_runeCooldownStart[rune] = 0;
            FrameScript_SignalEvent(RUNE_POWER_UPDATE, "%d%b", rune + 1, 1);
        }
    }

    SpellSignalUsableUpdate();
}

// ref: FUN_005ee3d0
int32_t RuneCountReady(int32_t type) {
    if (!RuneActivePlayer()) {
        return 0;
    }

    int32_t count = 0;

    for (int32_t rune = 0; rune < s_runeCount; rune++) {
        if (static_cast<int32_t>(s_runeType[rune]) == type && (s_runesReady & (1u << rune))) {
            count++;
        }
    }

    return count;
}

uint32_t RuneGetReadyMask() {
    return s_runesReady;
}

int32_t RuneGetCount() {
    return s_runeCount;
}

// ref: FUN_005ee5b0
// The cost is the SpellRuneCost row scaled by the power cost modifiers (above 100%); death runes
// make up any shortfall of the other three types.
bool SpellHasRunes(int32_t spellID, int32_t* missing) {
    auto player = RuneActivePlayer();

    if (!player) {
        return false;
    }

    auto spell = g_spellDB.GetRecord(spellID);

    if (!spell) {
        return false;
    }

    int32_t percent = player->GetPowerCostModifier(spell->m_schoolMask) + 100;
    percent = static_cast<int32_t>(std::nearbyint(player->GetPowerCostMultiplier(spell->m_schoolMask) * static_cast<float>(percent)));
    SpellApplyModifier(spell, &percent, 0xe);

    if (percent == 0 || spell->m_powerType != 5) {
        return true;
    }

    auto cost = g_spellRuneCostDB.GetRecord(spell->m_runeCostID);

    if (!cost) {
        return true;
    }

    int32_t shortfall = 0;
    bool lacking = false;
    const int32_t costs[3] = { cost->m_blood, cost->m_unholy, cost->m_frost };

    for (int32_t type = 0; type < 3; type++) {
        if (!costs[type]) {
            continue;
        }

        auto ready = RuneCountReady(type);
        auto needed = costs[type];

        if (percent > 100) {
            needed = needed * percent / 100;
        }

        if (ready < needed) {
            shortfall += needed - ready;
            lacking = true;

            if (missing) {
                missing[type] = needed - ready;
            }
        }
    }

    if (!lacking) {
        return true;
    }

    if (RuneCountReady(3) < shortfall) {
        if (missing) {
            missing[3] = shortfall - RuneCountReady(3);
        }

        return false;
    }

    return true;
}

// ref: FUN_005ee7e0
// The latest of the earliest ready time among the runes of each type the spell lacks.
bool SpellGetRuneCooldown(const SpellRec* spell, uint32_t* ready) {
    if (!RuneActivePlayer()) {
        return false;
    }

    if (!g_spellRuneCostDB.GetRecord(spell->m_runeCostID)) {
        return false;
    }

    int32_t missing[4] = {};

    if (SpellHasRunes(spell->m_ID, missing)) {
        return false;
    }

    uint32_t times[8] = {};
    uint32_t latest = 0;
    int32_t latestRune = 0;

    for (int32_t rune = 0; rune < 8; rune++) {
        if (s_runeType[rune] != 3 && missing[s_runeType[rune]] != 0) {
            auto start = RuneGetCooldownStart(rune);
            times[rune] = start;

            if (latest == 0 || static_cast<int32_t>(start - latest) >= 0) {
                latestRune = rune;
                latest = start;
            }
        }
    }

    for (int32_t rune = 0; rune < 8; rune++) {
        if (s_runeType[rune] != 3 || !RuneActivePlayer() || (s_runesReady & (1u << rune)) || s_runeCooldownStart[rune] == 0) {
            continue;
        }

        auto start = RuneGetCooldownStart(rune);

        if (latest == 0) {
            times[rune] = start;
            latestRune = rune;
            latest = start;
        } else if (start < latest) {
            times[latestRune] = RuneGetCooldownStart(rune);
            latestRune = 0;
            latest = 0;

            for (int32_t other = 0; other < 8; other++) {
                if (s_runeType[other] != 3 && (latest == 0 || static_cast<int32_t>(times[other] - latest) >= 0)) {
                    latest = times[other];
                    latestRune = other;
                }
            }
        }
    }

    // Per type pair, the sooner of its two runes (or the later when both are needed).
    auto pick = [&](int32_t needed, uint32_t first, uint32_t second) -> uint32_t {
        if (needed == 1) {
            if (first != 0 && (second == 0 || first < second)) {
                return first;
            }

            return second;
        }

        if (needed == 2) {
            return second < first ? first : second;
        }

        return 0;
    };

    auto blood = pick(missing[0], times[0], times[1]);
    auto unholy = pick(missing[1], times[2], times[3]);
    auto frost = pick(missing[2], times[4], times[5]);

    auto later = unholy > frost ? unholy : frost;

    if (later < blood) {
        *ready = blood;

        return true;
    }

    *ready = later;

    return true;
}

// SMSG_CONVERT_RUNE (the reference's Player_C dispatcher, FUN_006e2e90's sibling at 0x006e3f6f):
// the rune and its new type.
int32_t RuneConvertHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint8_t rune = 0;
    uint8_t type = 0;
    msg->Get(rune);
    msg->Get(type);

    RuneConvert(rune, type);

    return 1;
}

// ref: FUN_005ee440
// SMSG_RESYNC_RUNES: the rune count, then each rune's type and how far it has regenerated.
int32_t RuneResyncHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint32_t count = 0;
    msg->Get(count);
    s_runeCount = static_cast<int32_t>(count);

    for (int32_t rune = 0; rune < s_runeCount; rune++) {
        uint8_t type = 0;
        uint8_t progress = 0;
        msg->Get(type);
        s_runeType[rune] = type;
        s_runeBaseType[rune] = type;
        msg->Get(progress);

        auto done = static_cast<float>(progress) * (1.0f / 255.0f);

        if (done > 1.0f) {
            done = 1.0f;
        }

        s_runeRegenStart[rune] = OsGetAsyncTimeMs() - static_cast<int32_t>(10000.0f * done);
        RuneSetCooldown(rune, done);
        FrameScript_SignalEvent(RUNE_POWER_UPDATE, "%d%b", rune + 1, 0);
    }

    return 1;
}

// ref: FUN_005ee520
// SMSG_ADD_RUNE_POWER: runes not ready that the mask names restart their regeneration from now.
int32_t RuneAddPowerHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint32_t mask = 0;
    msg->Get(mask);

    for (int32_t rune = 0; rune < s_runeCount; rune++) {
        uint32_t bit = 1u << rune;

        if ((mask & bit) && !(s_runesReady & bit)) {
            s_runeRegenStart[rune] = OsGetAsyncTimeMs() - 10000;
            RuneSetCooldown(rune, 1.0f);
            FrameScript_SignalEvent(RUNE_POWER_UPDATE, "%d%b", rune + 1, 0);
        }
    }

    FrameScript_SignalEvent(SCRIPT_ACTIONBAR_UPDATE_COOLDOWN, nullptr);

    return 1;
}
