// Unit_C.cpp, continued: how units stand toward each other (UnitReaction, CanAttack, CanAssist,
// CanCooperate)
// and the name a unit shows over its head -- the text, whether it shows, and its colour.
#include "object/client/CGUnit_C.hpp"
#include "db/Db.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/CVehicle_C.hpp"
#include "object/client/CVehiclePassenger_C.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/UnitVehicle_C.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/CGGameUI.hpp"
#include "ui/game/CGPartyInfo.hpp"
#include "ui/game/CGRaidInfo.hpp"
#include "ui/game/ReputationInfo.hpp"
#include <storm/String.hpp>
#include <tempest/vector/CImVector.hpp>
#include <cstring>

namespace {

bool IsPlayerObject(const CGUnit_C* unit) {
    return unit->IsA(TYPE_PLAYER);
}

const CGPlayerData* PlayerData(const CGUnit_C* unit) {
    return static_cast<const CGPlayer_C*>(unit)->Player();
}

bool PlayerControlled(const CGUnit_C* unit) {
    return (unit->Unit()->flags >> 3) & 1;
}

uint8_t PvPFlags(const CGUnit_C* unit) {
    return static_cast<uint8_t>(unit->Unit()->bytes2 >> 8);
}

WOWGUID CharmerOrCreator(const CGUnit_C* unit) {
    auto data = unit->Unit();
    return data->charmedBy ? data->charmedBy : data->createdBy;
}

// DuelInfo.cpp's duel opponent (DAT_00c22b60).
// TODO(DuelInfo): frozen does not port the duel messages, so no opponent is ever known.
WOWGUID DuelInfoGetOpponent() {
    return 0;
}

// FUN_00512a00: in the player's party or raid.
bool InPartyOrRaid(WOWGUID guid) {
    return CGPartyInfo::IsPlayerOrMember(guid) || CGRaidInfo::IndexOf(guid) != 0;
}

// FriendList::GetFriend. TODO(FriendList): frozen keeps no friend list instance (the friend
// messages are not ported), so nobody is a friend.
bool IsFriend(WOWGUID guid) {
    (void)guid;
    return false;
}

// UNIT_FIELD_BYTES_1 byte 2 bit 0x2 (descriptor +0x112).
bool IsUntrackable(const CGUnit_C* unit) {
    return (unit->Unit()->bytes1 >> 16) & 0x2;
}

} // namespace

// ref: FUN_0071f5c0
// Whether the two are dueling each other: players (or what they control) of one duel on
// opposite teams, or the active player and what its duel opponent controls.
bool CGUnit_C::IsDuelingWith(const CGUnit_C* other) const {
    if (!other || !other->IsA(TYPE_UNIT)) {
        return false;
    }

    if (!PlayerControlled(this) || !PlayerControlled(other)) {
        return false;
    }

    auto self = const_cast<CGUnit_C*>(this);
    auto a = IsPlayerObject(this) ? self : self->GetControllingPlayer();
    auto b = IsPlayerObject(other) ? const_cast<CGUnit_C*>(other) : const_cast<CGUnit_C*>(other)->GetControllingPlayer();

    if (!a || !b) {
        WOWGUID opponent = DuelInfoGetOpponent();

        if (this->GetGUID() == ClntObjMgrGetActivePlayer() && !b) {
            WOWGUID owner = CharmerOrCreator(other);
            return owner && owner == opponent;
        }

        if (other->GetGUID() == ClntObjMgrGetActivePlayer() && !a) {
            WOWGUID owner = CharmerOrCreator(this);
            return owner && owner == opponent;
        }

        return false;
    }

    auto pa = PlayerData(a);
    auto pb = PlayerData(b);

    return pa->duelTeam && pb->duelTeam && pa->duelArbiter == pb->duelArbiter && pa->duelTeam != pb->duelTeam;
}

// ref: FUN_007251c0
// How this unit regards `other`, 0 hated .. 7 exalted: itself is friendly; players (and what they
// control) of one duel are hostile on opposite teams; the active player's group stands by the
// templates; free-for-all PvP on both is hostile; the active player's standing with a creature's
// faction (forced, at war, or the reputation of a faction it has) decides for creatures; the
// faction templates for the rest.
int32_t CGUnit_C::UnitReaction(const CGUnit_C* other) const {
    if (other == this) {
        return 4;
    }

    auto self = const_cast<CGUnit_C*>(this);

    if (PlayerControlled(this) && PlayerControlled(other)) {
        auto a = self->GetControllingPlayer();
        auto b = const_cast<CGUnit_C*>(other)->GetControllingPlayer();
        bool decided = false;

        if (!a || !b) {
            if (this->GetGUID() == ClntObjMgrGetActivePlayer() && !b) {
                if (this->IsDuelingWith(other)) {
                    return 1;
                }
            } else if (other->GetGUID() == ClntObjMgrGetActivePlayer() && !a) {
                if (other->IsDuelingWith(this)) {
                    return 1;
                }
            }
        } else {
            auto pa = PlayerData(a);
            auto pb = PlayerData(b);

            if (pa->duelTeam && pb->duelTeam && pa->duelArbiter == pb->duelArbiter) {
                return pa->duelTeam != pb->duelTeam ? 1 : 4;
            }

            if (a == b) {
                return 4;
            }

            if (a->GetGUID() == ClntObjMgrGetActivePlayer() && InPartyOrRaid(b->GetGUID())) {
                decided = true;
            } else if (b->GetGUID() == ClntObjMgrGetActivePlayer() && InPartyOrRaid(a->GetGUID())) {
                decided = true;
            }

            if (decided) {
                return this->GetReaction(other);
            }
        }

        if ((PvPFlags(this) & 0x4) && (PvPFlags(other) & 0x4)) {
            return 1;
        }
    }

    if (PlayerControlled(this)) {
        if (auto controller = self->GetControllingPlayer()) {
            auto otherFaction = g_factionTemplateDB.GetRecord(other->Unit()->factionTemplate);

            if (controller->GetGUID() == ClntObjMgrGetActivePlayer() && otherFaction) {
                int32_t forced;

                if (ReputationGetForcedReaction(otherFaction->m_faction, &forced)) {
                    return forced;
                }

                if (!(controller->Unit()->flags2 & 0x4) && FactionHasReputation(otherFaction->m_faction)) {
                    if ((otherFaction->m_flags & 0x1000) && (PlayerData(controller)->flags & 0x100)) {
                        return 1;
                    }

                    return ReputationIsAtWar(otherFaction->m_faction) ? 1 : 4;
                }
            }
        }
    }

    int32_t reaction = this->GetReaction(other);

    return 6 < reaction ? 7 : reaction;
}

// ref: FUN_00729740
// Whether this unit may attack `other`: never someone untouchable (the unit flags 0x2, 0x100000,
// 0x80, 0x10000, 0x2000000), not across the PvP-only and creature-only flags, not a vehicle's own
// driver, not in a sanctuary; between creatures only when either is hostile to the other; between
// players by duel, group FFA or PvP; otherwise when not friendly.
bool CGUnit_C::CanAttack(const CGUnit_C* other) const {
    if (IsPlayerObject(this) && ((PlayerData(this)->flags >> 0x13) & 1)) {
        return false;
    }

    if (IsPlayerObject(other) && ((PlayerData(other)->flags >> 4) & 1)
        && (!this->m_creatureStats || !(this->m_creatureStats->m_typeFlags & 0x2))) {
        return false;
    }

    uint32_t otherFlags = other->Unit()->flags;

    if ((otherFlags & 0x2) || (otherFlags & 0x100000) || (otherFlags & 0x80) || (otherFlags & 0x10000)
        || (otherFlags & 0x2000000)) {
        return false;
    }

    uint32_t flags = this->Unit()->flags;

    if ((flags & 0x8) && (otherFlags & 0x100)) {
        return false;
    }

    if (!(flags & 0x8) && (otherFlags & 0x200)) {
        return false;
    }

    if ((otherFlags & 0x8) && (flags & 0x100)) {
        return false;
    }

    if (!(otherFlags & 0x8) && (flags & 0x200)) {
        return false;
    }

    // UNIT_FIELD_FLAGS_2 bit 0x10000 (descriptor +0xda): a vehicle may not hit its own driver.
    if ((this->Unit()->flags2 & 0x10000) || (other->Unit()->flags2 & 0x10000)) {
        auto selfRec = this->m_vehicle ? this->m_vehicle->m_rec : nullptr;
        auto otherRec = other->m_vehicle ? other->m_vehicle->m_rec : nullptr;

        if (selfRec && (selfRec->m_flags & 0x20000000)
            && UnitGetVehicleRoot(const_cast<CGUnit_C*>(this), nullptr) == this) {
            return false;
        }

        if (otherRec && (otherRec->m_flags & 0x20000000)
            && UnitGetVehicleRoot(const_cast<CGUnit_C*>(other), nullptr) == other) {
            return false;
        }
    }

    bool selfPlayer = (this->Unit()->flags >> 3) & 1;
    bool otherPlayer = (other->Unit()->flags >> 3) & 1;

    if (!selfPlayer && !otherPlayer) {
        // FUN_00514080, both ways.
        return other->UnitReaction(this) < 2 || this->UnitReaction(other) < 2;
    }

    if (selfPlayer && otherPlayer) {
        // FUN_00514050
        if (this->IsFriendlyTo(other)) {
            return false;
        }

        auto a = const_cast<CGUnit_C*>(this)->GetControllingPlayer();
        auto b = const_cast<CGUnit_C*>(other)->GetControllingPlayer();

        if (!a || !b) {
            if (PvPFlags(this) & 0x8) {
                return false;
            }

            return !(PvPFlags(other) & 0x8);
        }

        auto pa = PlayerData(a);
        auto pb = PlayerData(b);

        if (pa->duelTeam && pb->duelTeam && pa->duelArbiter == pb->duelArbiter) {
            return true;
        }

        uint8_t theirs = PvPFlags(other);

        if (!(theirs & 0x1)) {
            uint8_t ours = PvPFlags(this);

            if ((ours & 0x4) && (theirs & 0x4)) {
                return true;
            }

            if (!(ours & 0x2) && !(theirs & 0x2)) {
                return false;
            }

            if (ours & 0x8) {
                return false;
            }

            return !(theirs & 0x8);
        }

        if (PvPFlags(this) & 0x8) {
            return false;
        }

        return !(theirs & 0x8);
    }

    if (selfPlayer && (PvPFlags(other) & 0x8)) {
        return false;
    }

    if (otherPlayer && (PvPFlags(this) & 0x8)) {
        return false;
    }

    return !(this->IsFriendlyTo(other));
}

// ref: FUN_00514050
// Friendly toward `other`, or a creature that treats everyone as a friend (type flag 26).
bool CGUnit_C::IsFriendlyTo(const CGUnit_C* other) const {
    return this->UnitReaction(other) >= 4 || this->GetCreatureTypeFlag26();
}

// ref: FUN_007293d0
// Whether this unit may help `other` (heal it, buff it). Never one that cannot be helped (flag
// 0x2000000); unless `ignorePvP`, never across the PvP line the 0x100/0x200 flags draw; only toward
// a friend (or as a creature that helps everyone). Between players' units, never into a duel on the
// other team, into free-for-all from outside it, or out of a sanctuary into a flagged unit.
bool CGUnit_C::CanAssist(const CGUnit_C* other, bool ignorePvP) const {
    uint32_t otherFlags = other->Unit()->flags;

    if ((otherFlags >> 25) & 1) {
        return false;
    }

    bool selfPvP = (this->Unit()->flags >> 3) & 1;

    if (!ignorePvP) {
        if ((selfPvP && ((otherFlags >> 8) & 1)) || (!selfPvP && ((otherFlags >> 9) & 1))) {
            return false;
        }
    }

    if (this->UnitReaction(other) < 4
        && (!this->m_creatureStats || !((this->m_creatureStats->m_typeFlags >> 26) & 1))) {
        return false;
    }

    if (!(otherFlags & 0x8)) {
        if (selfPvP) {
            if (!ignorePvP && !(PvPFlags(other) & 0x1) && !other->GetCreatureTypeFlag12()
                && !other->GetCreatureTypeFlag26()) {
                return false;
            }

            return true;
        }

        return true;
    }

    auto self = const_cast<CGUnit_C*>(this)->GetControllingPlayer();
    auto them = const_cast<CGUnit_C*>(other)->GetControllingPlayer();

    if (self && them) {
        auto mine = PlayerData(self);
        auto theirs = PlayerData(them);

        if (theirs->duelTeam != 0 && (mine->duelArbiter != theirs->duelArbiter || mine->duelTeam != theirs->duelTeam)) {
            return false;
        }
    }

    uint8_t theirFlags = PvPFlags(other);
    uint8_t myFlags = PvPFlags(this);

    if (((theirFlags & 0x4) && !(myFlags & 0x4)) || ((myFlags & 0x8) && !(theirFlags & 0x8) && (theirFlags & 0x1))) {
        return false;
    }

    return true;
}

// ref: FUN_00729b30
// Whether this unit may help `other`: never itself; between uncharmed units of factions of one
// team (or where either has none) whenever it may not attack it.
bool CGUnit_C::CanCooperate(const CGUnit_C* other) const {
    if (other == this) {
        return false;
    }

    if (this->Unit()->charm || other->Unit()->charm) {
        return false;
    }

    auto a = g_factionTemplateDB.GetRecord(this->Unit()->factionTemplate);
    auto b = g_factionTemplateDB.GetRecord(other->Unit()->factionTemplate);

    if (a && b && a->m_factionGroup != b->m_factionGroup) {
        return false;
    }

    return !this->CanAttack(other);
}

// ref: FUN_0072a290
// The unit's name with the player's chosen title around it (CharTitles, by the player's sex),
// and the PvP medal after it; `lines` counts the lines (two with a medal).
const char* CGUnit_C::GetNameWithTitle(char* buffer, uint32_t size, bool withTitle, int32_t* lines) {
    if (lines) {
        *lines = 1;
    }

    if (!IsPlayerObject(this) || !withTitle || PlayerData(this)->chosenTitle < 1) {
        SStrCopy(buffer, this->GetUnitName(nullptr, 1), size);
        return buffer;
    }

    // FUN_00719900: the title by its bit index.
    const CharTitlesRec* title = nullptr;

    for (int32_t i = 0; i < g_charTitlesDB.GetNumRecords(); i++) {
        auto rec = g_charTitlesDB.GetRecordByIndex(i);

        if (rec && rec->m_maskID == PlayerData(this)->chosenTitle) {
            title = rec;
            break;
        }
    }

    if (!title) {
        return buffer;
    }

    uint8_t sex = this->GetGUID() == ClntObjMgrGetActivePlayer() ? static_cast<uint8_t>(PlayerData(this)->bytes_3_1)
                                                                 : static_cast<uint8_t>(this->Unit()->bytes0 >> 16);

    const char* format;

    if (sex == 1) {
        format = title->m_name1 && title->m_name1[0] ? title->m_name1 : title->m_name;
    } else {
        format = title->m_name && title->m_name[0] ? title->m_name : title->m_name1;
    }

    char pattern[400];
    SStrCopy(pattern, format ? format : "%s", sizeof(pattern));
    SStrPrintf(buffer, size, pattern, this->GetUnitName(nullptr, 1));

    // TODO(Player_C): the PvP medal (player block +0x1e, "PVP_MEDAL%d") is not a field frozen
    // names; nothing shows it.

    return buffer;
}

// ref: FUN_0072d4f0
// The lines over the unit's head: the name (with the title when `flags` 0x8), then for a creature
// its template's subname and a summon's owner line, or for a player its guild (0x4) and the
// foreign-realm label. A unit whose aura says to show another's name (effect 0x117) shows that
// one's. Returns the line count.
//
// PARTIAL: the aura redirect (FUN_004f8850 over +0xdd0 / +0xc50) is not followed; the summon
// title (FUN_0061e830, the tooltip's) and the guild name (FUN_006db6a0, the guild cache) are not
// ported; the four prefixes the unit's slots 0x108..0x114 add are empty in frozen.
int32_t CGUnit_C::GetNameText(int32_t flags, char* buffer, uint32_t size) {
    char name[1024] = {};
    char subname[1024] = {};

    int32_t lines = 0;
    this->GetNameWithTitle(name, sizeof(name), (flags >> 3) & 1, &lines);

    if (!IsPlayerObject(this)) {
        auto stats = this->m_creatureStats;
        const char* sub = stats ? stats->m_subName : nullptr;

        if (sub && sub[0] && this->Unit()->petNumber == 0) {
            SStrPrintf(subname, sizeof(subname), "\n<%s>", sub);
            lines++;
        }
    }

    SStrPrintf(buffer, size, "%s%s", name, subname);

    return lines;
}

CImVector CGUnit_C::s_attackBlinkColor = { 0x00, 0x00, 0xff, 0xff };
int32_t CGUnit_C::s_attackBlinkOn = 0;
uint32_t CGUnit_C::s_attackBlinkTime = 0;

// ref: FUN_00718ac0
// The name's colour: the frame's highlight colour while targeted (state 0x10), else the game UI's
// colour for the unit.
int32_t CGUnit_C::Virtual078(int32_t* out) {
    if (this->m_stateFlags & 0x10) {
        *out = static_cast<int32_t>(CGUnit_C::s_attackBlinkColor.value);
        return 1;
    }

    CImVector color;
    GameUIGetUnitColor(this->GetGUID(), &color, 1);
    *out = static_cast<int32_t>(color.value);

    return 1;
}

// ref: FUN_00729c70
// Which of the UnitName cvar's categories (the mask in `mask`) the unit's name falls in: its own,
// an NPC's, a player's friendly or enemy, a pet's, a guardian's, a totem's, a critter's.
//
// PARTIAL: the reference also hides it while DAT_00ac80a8 is set and the unit's +0xc38 pair is;
// neither is identified.
int32_t CGUnit_C::Virtual0D0(uint32_t mask) {
    auto player = CGPlayer_C::GetActivePtr();
    auto data = this->Unit();
    WOWGUID ownerGUID = CharmerOrCreator(this);
    auto owner = ownerGUID ? static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(ownerGUID, TYPE_UNIT, ".\\Unit_C.cpp", 0x2d39)) : nullptr;

    if (!this->CanBeTargetted()) {
        return 0;
    }

    // FUN_0071ffc0: the player, its mover, or one it possesses (0x1000) is "own".
    if (this->GetGUID() == ClntObjMgrGetActivePlayer() || this->GetGUID() == CGUnit_C::s_activeMover
        || (this->m_stateFlags & 0x1000)) {
        return mask & 0x1;
    }

    if (this->GetGUID() == CGGameUI::GetLockedTarget()) {
        return 1;
    }

    if (!player) {
        return 1;
    }

    if (IsPlayerObject(this)) {
        if (player->CanCooperate(this)) {
            return (mask >> 7) & 1;
        }

        if (!(mask & 0x10) || IsUntrackable(this)) {
            return 0;
        }

        return 1;
    }

    if (!owner || !owner->IsA(TYPE_PLAYER)) {
        if (IsUntrackable(this) && player->CanAttack(this)) {
            return 0;
        }

        int32_t type = this->GetCreatureType();

        if (type != 8 && type != 12) {
            return (mask >> 1) & 1;
        }

        return (mask >> 10) & 1;
    }

    bool hostile = player->CanAttack(this);

    if (hostile && IsUntrackable(this)) {
        return 0;
    }

    // FUN_00716140 / FUN_0053b8c0
    bool created = data->createdBy != 0;
    bool summoned = data->summonedBy != 0;

    if ((!created || summoned || !((data->flags >> 9) & 1)) && this->GetCreatureType() != 8
        && this->GetCreatureType() != 12) {
        if (this->GetCreatureType() == 11) {
            return hostile ? (mask >> 6) & 1 : (mask >> 9) & 1;
        }

        // FUN_0071b600: made (not charmed, not summoned) is a guardian.
        if (!data->charmedBy && !data->summonedBy && data->createdBy) {
            return (mask >> 12) & 1;
        }

        auto charmer = data->charmedBy ? static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(data->charmedBy, TYPE_UNIT, ".\\Unit_C.cpp", 0x2d69)) : nullptr;

        if (!player->CanCooperate(this)
            && (!charmer || charmer->GetTransportGUID() != this->GetGUID() || (player != charmer && !player->CanCooperate(charmer)))) {
            return (mask >> 5) & 1;
        }

        return (mask >> 8) & 1;
    }

    return (mask >> 10) & 1;
}

// ref: FUN_00521bf0
// The colour a unit's name (or a guid's) takes: a player's by PvP and group standing, a
// creature's by its reaction to the player; a party member's, a friend's, or grey for the dead
// or tapped.
void GameUIGetUnitColor(WOWGUID guid, CImVector* out, int32_t forName) {
    static const uint32_t s_reactionColors[8] = {
        0xffff0000, 0xffff0000, 0xffff8000, 0xffffff00, 0xff00ff00, 0xff00ff00, 0xff00ff00, 0xff00ff00,
    };
    static const uint32_t s_nameColors[2] = { 0xff0000ff, 0xff6060ff };

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\GameUI.cpp", 0x39ba));
    auto player = CGPlayer_C::GetActivePtr();

    if (unit && player) {
        CGUnit_C* viewer = player;

        if (auto ride = player->m_vehiclePassenger) {
            if (ride->m_seat && (ride->m_seat->m_flags & 0x100000)) {
                if (auto vehicle = ride->GetVehicleUnit()) {
                    viewer = vehicle;
                }
            }
        }

        if (unit->Unit()->flags & 0x8) {
            if (unit->CanAttack(viewer)) {
                out->value = viewer->CanAttack(unit) ? 0xffff0000 : s_nameColors[forName ? 1 : 0];
                return;
            }

            if (viewer->CanAttack(unit)) {
                out->value = 0xffffff00;
                return;
            }

            uint8_t flags = PvPFlags(unit);
            bool pvp = (flags & 0x1) && !(flags & 0x8) && !(PvPFlags(viewer) & 0x8);

            if (forName) {
                if (CGPartyInfo::IsPlayerOrMemberOrPet(guid) && CGPartyInfo::GetRealNumMembers() != 0) {
                    out->value = pvp ? 0xffaaffaa : 0xffaaaaff;
                    return;
                }

                if (IsFriend(guid)) {
                    out->value = 0xff53c9ff;
                    return;
                }
            }

            if (pvp) {
                out->value = s_reactionColors[unit->UnitReaction(viewer) & 7];
                return;
            }

            out->value = s_nameColors[forName ? 1 : 0];
            return;
        }

        if (forName && (unit->Unit()->health < 1 || (unit->Unit()->dynamicFlags & 0x20))) {
            out->value = 0xff7f7f7f;
            return;
        }

        out->value = s_reactionColors[unit->UnitReaction(viewer) & 7];
        return;
    }

    if (forName) {
        if (CGPartyInfo::IsPlayerOrMemberOrPet(guid) && CGPartyInfo::GetRealNumMembers() != 0) {
            out->value = 0xffaaaaff;
            return;
        }

        if (IsFriend(guid)) {
            out->value = 0xff53c9ff;
            return;
        }
    }

    out->value = s_nameColors[forName ? 1 : 0];
}
