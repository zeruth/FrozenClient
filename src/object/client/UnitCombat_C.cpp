#include "object/client/UnitCombat_C.hpp"
#include "client/ClientServices.hpp"
#include "console/CVar.hpp"
#include "db/Db.hpp"
#include "model/CM2Model.hpp"
#include "net/Types.hpp"
#include "object/client/CEffect.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/Spell_C.hpp"
#include "sound/SI2.hpp"
#include "sound/SOUNDKITDEF.hpp"
#include "sound/SOUNDKITOBJECT.hpp"
#include "sound/SoundKitProperties.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/CGCamera.hpp"
#include "ui/game/CGGameUI.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "util/DataStore.hpp"
#include "util/GUID.hpp"
#include "util/Random.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldParam.hpp"
#include "ui/game/PlayerName.hpp"
#include "db/rec/SpellRec.hpp"
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <tempest/random/CRandom.hpp>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

namespace {

// ---- the weapon sound tables (SoundInterface2DSP.cpp) ------------------------------------------

// 0x00b4ae7c: per weapon subclass, per impact type (ten), per parry sound type (two), the normal
// and the critical impact sound.
struct WEAPON_IMPACT {
    int32_t sound[2];
};

std::vector<WEAPON_IMPACT> s_weaponImpacts;     // subclass * 20 + type * 2 + parry
uint32_t s_weaponSubclassCount = 0;              // DAT_00c5d684
int32_t s_defaultWeaponSubclass = 0;            // DAT_00c5d660 +4: the subclass marked 0x4

// 0x00b4ae88: per swing size (three), the normal and the critical whoosh.
int32_t s_weaponSwings[3][2] = {};

// FUN_00706fa0: whether a material sounds like metal (Material.dbc flag 0x1).
bool MaterialIsMetal(int32_t material) {
    auto rec = g_materialDB.GetRecord(material);

    return rec && (rec->m_flags & 0x1);
}

// ref: FUN_004d0540
// A weapon's impact on what the victim holds: the attacker's weapon subclass (the default one bare
// handed) and metal, against a weapon (6, or 5 metal), a shield (4, or 3 metal) or nothing (6).
void PlayWeaponImpact(const UNIT_WEAPON_INFO* weapon, const UNIT_WEAPON_INFO* victimItem, int32_t critical,
                      const C3Vector& position, int32_t involvesPlayer) {
    if (s_weaponImpacts.empty()) {
        return;
    }

    uint32_t subclass;
    uint32_t parry;

    if (!weapon) {
        parry = 0;
        subclass = static_cast<uint32_t>(s_defaultWeaponSubclass);
    } else {
        subclass = weapon->m_soundOverride != 0xff ? weapon->m_soundOverride : weapon->m_subclass;
        parry = MaterialIsMetal(weapon->m_material) ? 1 : 0;
    }

    int32_t type = 6;

    if (victimItem) {
        if (victimItem->m_class == 2) {
            type = 6 - (MaterialIsMetal(victimItem->m_material) ? 1 : 0);
        } else if (victimItem->m_class == 4) {
            type = 4 - (MaterialIsMetal(victimItem->m_material) ? 1 : 0);
        }
    }

    uint32_t index = parry + (static_cast<uint32_t>(type) + subclass * 10) * 2;

    if (s_weaponImpacts.size() <= index) {
        return;
    }

    C3Vector at = { position.x, position.y, position.z + 2.0f };

    SoundKitProperties properties;
    properties.ResetToDefaults();
    properties.m_type = 0xe;

    if (involvesPlayer) {
        properties.int20 = 0x6e;
    }

    SI2::PlaySoundKit(s_weaponImpacts[index].sound[critical ? 1 : 0], &at, nullptr, &properties, 0, nullptr, 1, 0);
}

// ref: FUN_004d0660
// A weapon's impact on flesh: `impactType` from the victim's sound data.
void PlayWeaponFleshImpact(const UNIT_WEAPON_INFO* weapon, int32_t impactType, int32_t critical,
                           const C3Vector* position, int32_t involvesPlayer) {
    if (s_weaponImpacts.empty()) {
        return;
    }

    uint32_t subclass;
    uint32_t parry;

    if (!weapon) {
        parry = 0;
        subclass = static_cast<uint32_t>(s_defaultWeaponSubclass);
    } else {
        subclass = weapon->m_soundOverride != 0xff ? weapon->m_soundOverride : weapon->m_subclass;
        parry = MaterialIsMetal(weapon->m_material) ? 1 : 0;
    }

    uint32_t index = parry + (static_cast<uint32_t>(impactType) + subclass * 10) * 2;

    if (s_weaponImpacts.size() <= index) {
        return;
    }

    SoundKitProperties properties;
    properties.ResetToDefaults();
    properties.m_type = 0xe;

    if (involvesPlayer) {
        properties.int20 = 0x6e;
    }

    SI2::PlaySoundKit(s_weaponImpacts[index].sound[critical ? 1 : 0], position, nullptr, &properties, 0, nullptr, 1, 0);
}

// ref: FUN_004d0720
// A swing's whoosh by its size, quieter for an off-hand swing.
void PlayWeaponSwing(int32_t swingSize, int32_t critical, const C3Vector* position, int32_t offHand,
                     int32_t involvesPlayer) {
    if (2 < swingSize || swingSize < 0) {
        return;
    }

    SoundKitProperties properties;
    properties.ResetToDefaults();
    properties.m_type = 10;
    properties.m_fadeInTime = offHand ? 0.5f : 1.0f;

    if (involvesPlayer) {
        properties.int20 = 0x6e;
    }

    SI2::PlaySoundKit(s_weaponSwings[swingSize][critical ? 1 : 0], position, nullptr, &properties, 0, nullptr, 1, 0);
}

// ref: FUN_004d0850
void PlayCombatMiss(bool oneHanded, const C3Vector& position, int32_t involvesPlayer) {
    SoundKitProperties properties;
    properties.ResetToDefaults();

    if (involvesPlayer) {
        properties.int20 = 0x6e;
    }

    C3Vector at = position;
    SI2::PlaySoundKit(oneHanded ? "(DONOTRENAME)Combat Miss 1H" : "(DONOTRENAME)Combat Miss 2H", &at, nullptr, &properties);
}

// ---- unit combat helpers -----------------------------------------------------------------------

CGUnit_C* UnitPtr(WOWGUID guid, int32_t line) {
    if (!guid) {
        return nullptr;
    }

    return static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, ".\\UnitCombat_C.cpp", line));
}

bool IsPlayer(CGUnit_C* unit) {
    return unit->IsA(TYPE_PLAYER);
}

bool IsActivePlayer(CGUnit_C* unit) {
    return unit->GetGUID() == ClntObjMgrGetActivePlayer();
}

bool InvolvesMover(const CombatInfo& info) {
    return CGUnit_C::s_activeMover == info.attacker || CGUnit_C::s_activeMover == info.target;
}

// ref: FUN_00755540
// The weapon a unit swings with in `offHand`, when it has weapons out; with `loaded`, only once
// its model holds that hand's weapon loaded.
const UNIT_WEAPON_INFO* GetSwingWeapon(CGUnit_C* unit, int32_t offHand, bool loaded) {
    if (unit->m_sheathState == 0) {
        return nullptr;
    }

    auto weapon = unit->GetWeaponInfo(offHand != 0, 0);

    if (!weapon || weapon->m_class != 2) {
        return nullptr;
    }

    if (!loaded) {
        return weapon;
    }

    auto model = unit->m_model;

    if (!model || !model->IsLoaded(0, 0)) {
        return nullptr;
    }

    for (auto child = model->m_attachList; child; child = child->m_attachNext) {
        if (child->m_attachId == static_cast<uint32_t>((offHand != 0) + 1)) {
            return child->IsLoaded(0, 0) ? weapon : nullptr;
        }
    }

    return nullptr;
}

// ref: FUN_00754ef0
// What the victim meets a blow with: its main-hand weapon, else (or when `offHand`) the off hand's
// weapon or shield.
const UNIT_WEAPON_INFO* GetDefenseItem(CGUnit_C* unit, bool offHand) {
    auto item = unit->GetWeaponInfo(0, 0);

    if (offHand || (item && item->m_class != 2)) {
        item = unit->GetWeaponInfo(1, 0);

        if (item && item->m_class != 2 && item->m_class != 4) {
            item = nullptr;
        }
    }

    return item;
}

// ref: FUN_00746540
// The impact of the attacker's weapon on what `victim` holds.
void PlayVictimImpact(CGUnit_C* victim, bool offHand, const CombatInfo& info, const C3Vector& position) {
    auto attacker = UnitPtr(info.attacker, 0x16f);
    auto weapon = attacker ? GetSwingWeapon(attacker, (info.hitInfo >> 2) & 1, false) : nullptr;
    auto item = GetDefenseItem(victim, offHand);

    if (item) {
        PlayWeaponImpact(weapon, item, info.hitInfo & 0x200, position, InvolvesMover(info));
    }
}

// ref: FUN_0073b050
// The parry pose for what the unit holds.
void PlayParry(CGUnit_C* unit) {
    if (unit->IsDeadOrFeigning() || !unit->m_model || !unit->m_model->IsLoaded(0, 0) || unit->IsAnimationLocked()) {
        return;
    }

    auto weapon = unit->GetWeaponInfo(0, 0);

    if (weapon && weapon->m_class == 2 && unit->m_sheathState != 0) {
        auto offHand = unit->GetWeaponInfo(1, 0);

        switch (weapon->m_subclass) {
            case 0: case 4: case 7: case 11: case 14: case 15: case 20:
                unit->SetAnimation(0x15, 0);
                return;

            case 1: case 5: case 8: case 12:
                unit->SetAnimation(0x16 - (offHand ? 1 : 0), 0);
                return;

            case 6: case 10: case 17:
                unit->SetAnimation(0x17, 0);
                return;

            case 13:
                break;

            default:
                return;
        }
    }

    unit->SetAnimation(0x14, 0);
}

// ref: FUN_00755380
// The victim's blood: a spurt in front or behind it by where the attacker stands, large on a
// critical, at the unit's blood level for the violence level.
void PlayBloodSpurt(CGUnit_C* victim, const WOWGUID& attackerGUID, int32_t critical) {
    // FUN_00719ff0
    auto levels = victim->m_bloodRec;
    auto blood = levels ? g_unitBloodDB.GetRecord(levels->m_violencelevel[CWorldParam::s_violenceLevel]) : nullptr;

    if (!blood) {
        return;
    }

    int32_t attachment = 0xf;

    if (auto attacker = UnitPtr(attackerGUID, 0x2ca)) {
        C3Vector here = victim->GetPosition();
        C3Vector there = attacker->GetPosition();
        float dx = there.x - here.x;
        float dy = there.y - here.y;

        // FUN_007156a0: the facing the victim draws at.
        float facing = victim->m_localMove.GetFacing(victim->m_lowerBodyFacing);

        if (dx * std::cos(facing) + dy * std::sin(facing) < 0.0f) {
            attachment = 0x10;
        }
    }

    int32_t effectID = attachment == 0xf ? blood->m_combatBloodSpurtFront[critical ? 1 : 0]
                                         : blood->m_combatBloodSpurtBack[critical ? 1 : 0];

    auto effectName = g_spellVisualEffectNameDB.GetRecord(effectID);

    if (!effectName) {
        return;
    }

    void* mem = SMemAlloc(sizeof(CEffect), ".\\UnitCombat_C.cpp", 0x2f1, 0);
    auto effect = mem ? new (mem) CEffect() : nullptr;

    if (!effect) {
        return;
    }

    WOWGUID owner = victim->GetGUID();
    effect->InitializeAttached(attachment, 0, nullptr, effectName, attackerGUID, owner, 0x20,
                               &CGObject_C::KitEffectOneShot, nullptr, 0, nullptr);
    effect->Release();
}

// ref: FUN_007190a0
// Whether `attacker` is the player (`byPlayer` set) or one of the player's own -- its pet, or its
// pet's -- (`byPlayer` clear). False for anyone else.
bool IsPlayersAttack(const WOWGUID& attacker, bool* byPlayer) {
    if (ClntObjMgrGetActivePlayer() == attacker) {
        *byPlayer = true;
        return true;
    }

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(attacker, TYPE_UNIT, ".\\Unit_C.cpp", 0x2d83));

    if (!unit) {
        return false;
    }

    auto data = unit->Unit();
    WOWGUID owner = data->charmedBy ? data->charmedBy : data->createdBy;

    if (!owner) {
        return false;
    }

    auto ownerUnit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(owner, TYPE_UNIT, ".\\Unit_C.cpp", 0x2d85));

    if (owner == ClntObjMgrGetActivePlayer()) {
        *byPlayer = false;
        return true;
    }

    if (ownerUnit) {
        auto ownerData = ownerUnit->Unit();
        WOWGUID master = ownerData->charmedBy ? ownerData->charmedBy : ownerData->createdBy;

        if (master == ClntObjMgrGetActivePlayer()) {
            *byPlayer = false;
            return true;
        }
    }

    return false;
}

// ref: FUN_0071f990
// The floater a combat result raises over the unit -- "Dodge", "Parry", "Immune" and the rest --
// when the player or one of its own caused it and the combat damage cvars allow it: white for the
// player's swings, orange for its pet's, yellow for spells.
void ShowCombatText(CGUnit_C* unit, const WOWGUID& source, int32_t text, const SpellRec* spell, bool periodic) {
    static const char* const s_texts[] = {
        "COMBAT_TEXT_NONE", "COMBAT_TEXT_MISS", "COMBAT_TEXT_RESIST", "COMBAT_TEXT_DODGE", "COMBAT_TEXT_PARRY",
        "COMBAT_TEXT_BLOCK", "COMBAT_TEXT_EVADE", "COMBAT_TEXT_IMMUNE", "COMBAT_TEXT_IMMUNE",
        "COMBAT_TEXT_DEFLECT", "COMBAT_TEXT_ABSORB",   // 0x00adbfdc
    };

    // The world text style each result takes (0x00a34c20).
    static const int32_t s_textStyles[] = { 11, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1 };

    // The colours of a pet's swing and of a spell (0x00adaa70, 0x00adaa6c).
    static const CImVector s_petMeleeColor = { 0x00, 0x84, 0xff, 0xff };
    static const CImVector s_spellColor = { 0x00, 0xde, 0xff, 0xff };

    bool melee = !spell || (spell->m_attributesEx3 & 0x8000);
    bool byPlayer = false;

    if (unit->GetGUID() == CGUnit_C::s_activeMover || !IsPlayersAttack(source, &byPlayer)) {
        return;
    }

    if (!CGGameUI::s_combatDamageCvar || !CGGameUI::s_combatDamageCvar->GetInt()) {
        return;
    }

    if (periodic && (!CGGameUI::s_combatLogPeriodicSpellsCvar || !CGGameUI::s_combatLogPeriodicSpellsCvar->GetInt())) {
        return;
    }

    const CImVector* color;

    if (melee) {
        if (!byPlayer) {
            if (!CGGameUI::s_petMeleeDamageCvar || !CGGameUI::s_petMeleeDamageCvar->GetInt()) {
                return;
            }

            color = &s_petMeleeColor;
        } else {
            color = nullptr;
        }
    } else {
        if (!byPlayer && (!CGGameUI::s_petSpellDamageCvar || !CGGameUI::s_petSpellDamageCvar->GetInt())) {
            return;
        }

        color = &s_spellColor;
    }

    if (text < 0 || static_cast<size_t>(text) >= sizeof(s_texts) / sizeof(s_texts[0])) {
        return;
    }

    auto string = FrameScript_GetText(s_texts[text], -1, GENDER_NOT_APPLICABLE);

    if (string && *string) {
        PlayerNameAddWorldText(unit->m_nameDesc, s_textStyles[text], string, color, nullptr);
    }
}

// ref: FUN_00722340
// The damage number over the unit, under the same rules: larger for a critical.
void ShowDamageText(CGUnit_C* unit, const WOWGUID& attacker, int32_t damage, const SpellRec* spell, bool critical, bool periodic, bool tick) {
    static const CImVector s_petMeleeColor = { 0x00, 0x84, 0xff, 0xff };
    static const CImVector s_spellColor = { 0x00, 0xde, 0xff, 0xff };

    bool melee = !tick && (!spell || (spell->m_attributesEx3 & 0x8000));
    bool byPlayer = false;

    if (!damage || unit->GetGUID() == CGUnit_C::s_activeMover || !IsPlayersAttack(attacker, &byPlayer)) {
        return;
    }

    if (!CGGameUI::s_combatDamageCvar || !CGGameUI::s_combatDamageCvar->GetInt()) {
        return;
    }

    if (periodic && (!CGGameUI::s_combatLogPeriodicSpellsCvar || !CGGameUI::s_combatLogPeriodicSpellsCvar->GetInt())) {
        return;
    }

    char text[32];
    SStrPrintf(text, sizeof(text), "%d", damage);

    const CImVector* color;

    if (melee) {
        if (!byPlayer) {
            if (!CGGameUI::s_petMeleeDamageCvar || !CGGameUI::s_petMeleeDamageCvar->GetInt()) {
                return;
            }

            color = &s_petMeleeColor;
        } else {
            color = nullptr;
        }
    } else {
        if (!byPlayer && (!CGGameUI::s_petSpellDamageCvar || !CGGameUI::s_petSpellDamageCvar->GetInt())) {
            return;
        }

        color = &s_spellColor;
    }

    PlayerNameAddWorldText(unit->m_nameDesc, critical ? 2 : 0, text, color, nullptr);
}

// ref: FUN_00722440
// The healing number over the unit, when the player or one of its own healed it.
void ShowHealText(CGUnit_C* unit, const WOWGUID& healer, int32_t amount, bool critical) {
    bool byPlayer = false;

    if (!amount || unit->GetGUID() == CGUnit_C::s_activeMover || !IsPlayersAttack(healer, &byPlayer)) {
        return;
    }

    if (!CGGameUI::s_combatHealingCvar || !CGGameUI::s_combatHealingCvar->GetInt()) {
        return;
    }

    char text[32];
    SStrPrintf(text, sizeof(text), "+%d", amount);

    if (byPlayer) {
        PlayerNameAddWorldText(unit->m_nameDesc, critical ? 7 : 6, text, nullptr, nullptr);
    }
}

// ref: FUN_00755a60
// What a resolved hit shows and sounds like: the miss or avoidance floater and the miss whoosh,
// or the weapon's impact on the victim; then the combat log.
//
// TODO(UnitCombatLog_C): the combat log entry (FUN_005133b0) is the combat log's.
void ResolveHitFeedback(const CombatInfo& info) {
    WOWGUID player = ClntObjMgrGetActivePlayer();

    if (info.spellID != 0 && info.victimState != 1) {
        return;
    }

    auto target = UnitPtr(info.target, 0xd4);
    int32_t involves = (player == info.attacker || player == info.target) ? 1 : 0;

    auto missSound = [&](CGUnit_C* attacker) {
        if (!attacker) {
            return;
        }

        auto weapon = attacker->GetWeaponInfo(0, 0);
        bool twoHanded = IsTwoHandedWeapon(reinterpret_cast<const uint8_t*>(weapon));
        PlayCombatMiss(!twoHanded, attacker->GetPosition(), involves);
    };

    switch (info.victimState) {
        case 8:
            if (target) ShowCombatText(target, info.attacker, 9, nullptr, false);
            return;

        case 3:
            if (target) ShowCombatText(target, info.attacker, 4, nullptr, false);
            return;

        case 6:
            if (target) ShowCombatText(target, info.attacker, 6, nullptr, false);
            return;

        case 2:
            missSound(UnitPtr(info.attacker, 0xd4));
            if (target) ShowCombatText(target, info.attacker, 3, nullptr, false);
            return;

        case 5:
            if (target) ShowCombatText(target, info.attacker, 5, nullptr, false);
            return;

        case 7:
            if (target) ShowCombatText(target, info.attacker, 7, nullptr, false);
            return;

        default:
            break;
    }

    if (info.damage == 0 && !(info.hitInfo & 0x1000000)) {
        if (player != info.target && !(info.hitInfo & 0x4000) && target) {
            int32_t text = (info.hitInfo & 0x20) ? 10 : ((info.hitInfo & 0x80) ? 2 : 1);
            ShowCombatText(target, info.attacker, text, nullptr, false);
        }

        missSound(UnitPtr(info.attacker, 0x109));

        return;
    }

    auto attacker = UnitPtr(info.attacker, 0xe8);

    if (attacker && target) {
        C3Vector position = target->GetPosition();
        int32_t impactType = target->GetImpactSoundType();
        auto weapon = GetSwingWeapon(attacker, (info.hitInfo >> 2) & 1, true);

        PlayWeaponFleshImpact(weapon, impactType, info.hitInfo & 0x200, &position, involves);
    }

    // The damage number (0x00755e26).
    if (target) {
        ShowDamageText(target, info.attacker, info.damage, nullptr, (info.hitInfo >> 9) & 1, false, false);
    }
}

// ref: FUN_00755e40
// The hit lands on `victim`: its wound or the hit's blood, the impact or block sound (once in a
// while for a unit that cannot change its pose), and the victim's grunt.
void ApplyHit(CGUnit_C* victim, const CombatInfo& info) {
    static uint32_t s_nextLockedSound = 0;          // DAT_00ca1600

    if (victim->Unit()->dynamicFlags & 0x1) {
        return;
    }

    ResolveHitFeedback(info);

    if (info.hitInfo & 0x2) {
        victim->PlayWoundAnimation(info.hitInfo & 0x200);

        if ((info.damage != 0 || (info.hitInfo & 0x1000000)) && (info.victimState == 1 || info.victimState == 4)) {
            PlayBloodSpurt(victim, info.attacker, info.hitInfo & 0x8000);
        }
    }

    if (victim->IsAnimationLocked()) {
        uint32_t now = CWorld::GetCurTimeMs();

        if (now <= s_nextLockedSound) {
            return;
        }

        uint32_t roll = static_cast<uint32_t>((static_cast<uint64_t>(CRandom::uint32(g_rndSeed)) * 2000) >> 32);
        s_nextLockedSound = roll + 3000 + now;
    }

    C3Vector position = victim->GetPosition();

    if (info.victimState == 3) {
        PlayVictimImpact(victim, false, info, position);
    } else if (info.blocked != 0) {
        PlayVictimImpact(victim, true, info, position);
    } else if (info.victimState == 8) {
        C3Vector at = { position.x, position.y, position.z + 2.0f };
        SI2::PlaySoundKit("(DONOTRENAME)ShieldWoodImpact", &at, nullptr, nullptr);
    }

    if ((info.hitInfo & 0xa0) || info.victimState == 7) {
        C3Vector at = { position.x, position.y, position.z + 2.0f };
        SI2::PlaySoundKit("(DONOTRENAME)AbsorbGetHit", &at, nullptr, nullptr);
        return;
    }

    if (!(info.hitInfo & 0x2)) {
        return;
    }

    if (info.hitInfo & 0x20000) {
        victim->PlayUnitSound(9, 0);
    } else if (info.hitInfo & 0x200) {
        victim->PlayUnitSound(3, 0);
    } else if (!(info.hitInfo & 0x10)) {
        victim->PlayUnitSound(2, 0);
    }
}

// ref: FUN_00755130
// The attacker's swing for its weapon: the attack for the main hand's kind, or the off hand's,
// unarmed (0x10) without one; when the swing does not wait on its event (no spell), at once.
void PlaySwing(CGUnit_C* attacker, const CombatInfo& info) {
    if (attacker->Unit()->health < 1 || (info.hitInfo & 0x40000)) {
        return;
    }

    uint32_t animID;

    if (!(info.hitInfo & 0x4)) {
        auto weapon = attacker->GetWeaponInfo(0, 0);
        animID = 0x10;

        if (weapon && weapon->m_class == 2) {
            auto offHand = attacker->GetWeaponInfo(1, 0);

            switch (weapon->m_subclass) {
                case 0: case 4: case 7: case 11: case 14:
                    animID = 0x11;
                    break;

                case 1: case 5: case 8: case 12:
                    animID = 0x12 - (offHand ? 1 : 0);
                    break;

                case 6: case 10: case 17: case 20:
                    animID = 0x13;
                    break;

                case 15:
                    animID = 0x55;
                    break;

                default:
                    break;
            }
        }
    } else {
        auto offHand = attacker->GetWeaponInfo(1, 0);

        if (!offHand || offHand->m_class != 2) {
            animID = 0x75;
        } else {
            animID = (offHand->m_subclass == 0xf ? 1 : 0) + 0x57;
        }
    }

    if (info.spellID == 0) {
        attacker->SetAnimation(animID, 0);
    }

    if (info.victimState != 0) {
        attacker->PlayUnitSound((info.hitInfo >> 9) & 1, 0);
    }
}

// ref: FUN_00756770
// SMSG_ATTACK_STOP's body: the unit stops swinging; a stop with a reason faces it to its victim.
void StopAttack(CGUnit_C* unit, WOWGUID victim, int32_t reason) {
    unit->m_attackTarget = 0;
    unit->m_combatIdle = 0;
    unit->m_attackStopSent = 0;

    UnitFlushCombatInfo(unit);

    if (!victim || !reason) {
        return;
    }

    if (auto target = UnitPtr(victim, 0x299)) {
        unit->m_stateFlags |= 0x1;
        unit->m_attackFacing = FacingBetween(unit->GetPosition(), target->GetPosition());
    }
}

// ref: FUN_006d4ad0
// The player's swing reached a target out of reach: a melee range and a half away, auto-attack
// stops.
void PlayerCheckSwingRange(CGUnit_C* player, CGUnit_C* target) {
    if (!target) {
        return;
    }

    float range = target->Unit()->combatReach + player->Unit()->combatReach + 1.3333334f;

    if (range < 5.0f) {
        range = 5.0f;
    }

    range = range * 1.5f;

    C3Vector a = target->GetPosition();
    C3Vector b = player->GetPosition();
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float dz = b.z - a.z;
    float distance = dy * dy + dz * dz + dx * dx;

    if (range * range < distance) {
        UnitSendAttackStop(player);
    }
}

// ref: FUN_00755270
// SMSG_ENVIRONMENTAL_DAMAGE_LOG's body: the kind's visual kit.
//
// TODO(UnitCombatLog_C): the combat log line (FUN_00751150) and the player's damage feedback
// (FUN_00513380) are the combat log's.
void OnEnvironmentalDamage(CGUnit_C* unit, uint32_t type, int32_t damage, int32_t absorb, int32_t resist) {
    (void)damage;
    (void)absorb;
    (void)resist;

    if (6 <= type) {
        return;
    }

    int32_t kitID = 0;

    for (int32_t i = 0; i < g_environmentalDamageDB.GetNumRecords(); i++) {
        auto rec = g_environmentalDamageDB.GetRecordByIndex(i);

        if (rec && static_cast<uint32_t>(rec->m_enumID) == type) {
            kitID = rec->m_visualKitID;
        }
    }

    auto kit = g_spellVisualKitDB.GetRecord(kitID);

    if (!kit) {
        return;
    }

    SPELLVISUALKITPARAMS params;
    params.m_kit = kit;
    params.m_stateParam = 1;
    params.m_param9 = -1;

    unit->PlayKit(params);
}

// ref: FUN_00756800
int32_t UnitCombatHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    switch (msgId) {
        case SMSG_ENVIRONMENTAL_DAMAGE_LOG: {
            WOWGUID guid = 0;
            msg->Get(guid);

            uint8_t type;
            msg->Get(type);

            uint32_t damage = 0;
            uint32_t absorb = 0;
            uint32_t resist = 0;
            msg->Get(damage);
            msg->Get(absorb);
            msg->Get(resist);

            if (auto unit = UnitPtr(guid, 0x4d)) {
                OnEnvironmentalDamage(unit, type, static_cast<int32_t>(damage), static_cast<int32_t>(absorb),
                                      static_cast<int32_t>(resist));
            }

            return 1;
        }

        case SMSG_ATTACK_START: {
            WOWGUID attacker = 0;
            WOWGUID victim = 0;
            msg->Get(attacker);
            msg->Get(victim);

            auto unit = UnitPtr(attacker, 0x58);

            if (!unit) {
                return 0;
            }

            unit->m_combatIdle = 0;

            // FUN_00754ff0
            unit->m_attackTarget = victim;
            unit->m_combatIdle = 0;

            if (!IsActivePlayer(unit)) {
                return 1;
            }

            // PLAYER_ENTER_COMBAT; FUN_005206e0, the action bar's refresh, is the UI's.
            FrameScript_SignalEvent(0x9b, nullptr);

            return 1;
        }

        case SMSG_ATTACK_STOP:
        case SMSG_COMBAT_EVENT_FAILED: {
            SmartGUID attacker;
            SmartGUID victim;
            *msg >> attacker;
            *msg >> victim;

            uint32_t reason;
            msg->Get(reason);

            auto unit = UnitPtr(attacker, 0x6e);

            if (!unit) {
                return 1;
            }

            StopAttack(unit, victim, static_cast<int32_t>(reason));

            if (!IsActivePlayer(unit)) {
                return 1;
            }

            // PLAYER_LEAVE_COMBAT
            FrameScript_SignalEvent(0x9c, nullptr);

            return 1;
        }

        case SMSG_ATTACKSWING_NOTINRANGE:
        case SMSG_ATTACKSWING_BADFACING:
            // PHASE4(Player_C): FUN_006cee70 keeps the swing error (1 range, 2 facing) and its
            // time for the player's next swing request.
            return 0;

        case SMSG_ATTACKSWING_DEADTARGET:
        case SMSG_ATTACKSWING_CANT_ATTACK:
            // PHASE4(Player_C): FUN_006e1660, the player's auto-attack and auto-repeat stop.
            if (auto player = CGPlayer_C::GetActivePtr()) {
                if (player->IsAttacking() || player->m_combatIdle != 0) {
                    UnitSendAttackStop(player);
                }
            }

            return 0;

        case SMSG_ATTACKER_STATE_UPDATE: {
            CombatInfo info;
            CombatInfoRead(&info, msg);

            auto attacker = UnitPtr(info.attacker, 0x7d);

            if (!attacker) {
                if (auto victim = UnitPtr(info.target, 0x95)) {
                    ApplyHit(victim, info);
                }

                return 0;
            }

            if (attacker->m_sheathState != 1) {
                attacker->SetSheathState(1, 1, 0);
            }

            attacker->m_combatIdle = 0;

            UnitFlushCombatInfo(attacker);
            attacker->m_combatInfo = info;

            auto victim = UnitPtr(info.target, 0x89);

            if (IsActivePlayer(attacker) && victim) {
                PlayerCheckSwingRange(attacker, victim);
            }

            PlaySwing(attacker, info);

            if (attacker->m_combatInfo.target == ClntObjMgrGetActivePlayer()) {
                // FUN_00755020: a player with no target takes the one hitting it.
                if (!CGGameUI::GetLockedTarget()) {
                    CGGameUI::SetTarget(attacker->GetGUID());
                }
            }

            // FUN_00721fc0 / UnitIsClickMoving: a click-to-move toward the victim ends.
            // TODO(UnitCombatLog_C): FUN_007512b0, the combat log entry.
            return 0;
        }

        case SMSG_CLEAR_TARGET: {
            WOWGUID guid = 0;
            msg->Get(guid);
            CGGameUI::ClearTarget(guid, 1);
            return 0;
        }

        default:
            return 0;
    }
}

} // namespace

void CombatInfoRead(CombatInfo* info, CDataStore* msg) {
    msg->Get(info->hitInfo);

    SmartGUID attacker;
    SmartGUID target;
    *msg >> attacker;
    *msg >> target;
    info->attacker = attacker;
    info->target = target;

    uint32_t value;
    msg->Get(value);
    info->damage = static_cast<int32_t>(value);
    msg->Get(value);
    info->overkill = static_cast<int32_t>(value);

    uint8_t count = 0;
    msg->Get(count);

    for (uint8_t i = 0; i < count && i < 2; i++) {
        msg->Get(info->schoolMask[i]);
        msg->Get(info->floatDamage[i]);
        msg->Get(value);
        info->intDamage[i] = static_cast<int32_t>(value);
    }

    if (info->hitInfo & 0x60) {
        for (uint8_t i = 0; i < count && i < 2; i++) {
            msg->Get(value);
            info->absorb[i] = static_cast<int32_t>(value);
        }
    }

    if (info->hitInfo & 0x180) {
        for (uint8_t i = 0; i < count && i < 2; i++) {
            msg->Get(value);
            info->resist[i] = static_cast<int32_t>(value);
        }
    }

    uint8_t victimState;
    msg->Get(victimState);
    info->victimState = victimState;

    msg->Get(value);
    info->unk58 = static_cast<int32_t>(value);
    msg->Get(value);
    info->spellID = static_cast<int32_t>(value);

    if (info->hitInfo & 0x2000) {
        msg->Get(value);
        info->blocked = static_cast<int32_t>(value);
    }

    if (info->hitInfo & 0x800000) {
        msg->Get(value);
        info->rage = static_cast<int32_t>(value);
    }

    if (info->hitInfo & 0x1) {
        for (auto& field : info->unk68) {
            msg->Get(field);
        }

        for (int32_t i = 0; i < 2; i++) {
            msg->Get(info->schoolMask[2 + i]);
            msg->Get(info->schoolMask[4 + i]);
        }

        msg->Get(info->unk8c);
    }

    // A blocked blow that did no damage reads as a block.
    if (UnitPtr(info->target, 0x3dd) && info->damage == 0 && info->blocked != 0) {
        info->victimState = 5;
    }
}

void UnitFlushCombatInfo(CGUnit_C* unit) {
    auto& info = unit->m_combatInfo;

    if (!info.attacker || !info.target) {
        return;
    }

    if (UnitPtr(info.target, 0x2a7)) {
        ResolveHitFeedback(info);
    }

    info = CombatInfo();
}

void UnitSendAttackStop(CGUnit_C* unit) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(0x142));     // CMSG_ATTACKSTOP
    msg.Finalize();
    ClientServices::Send(&msg);

    unit->m_attackStopSent = 1;
}

void UnitCombatAnimEvent(CGUnit_C* unit, uint32_t eventId, uint32_t eventData, const C3Vector* position) {
    (void)eventData;

    auto& info = unit->m_combatInfo;
    auto attacker = UnitPtr(info.attacker, 0x1c8);
    auto victim = UnitPtr(info.target, 0x1cb);

    auto id = [](const char* tag) {
        return static_cast<uint32_t>(static_cast<uint8_t>(tag[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(tag[1])) << 8)
            | (static_cast<uint32_t>(static_cast<uint8_t>(tag[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(tag[3])) << 24);
    };

    if (eventId == id("$BWP")) {
        if (IsActivePlayer(unit)) {
            auto spell = g_spellDB.GetRecord(unit->m_castSpellID);

            if (!spell || !(spell->m_attributes & 0x2)) {
                spell = g_spellDB.GetRecord(SpellGetAutoRepeatSpell());

                if (!spell || !(spell->m_attributes & 0x2)) {
                    return;
                }
            }
        }

        if (unit->m_localMove.m_moveFlags & 0x2e0100f) {
            return;
        }

        // FUN_00756040: the bow's string draws over what is left of the shot's pull.
        if (unit->m_rangedModel && unit->m_rangedModel->IsLoaded(0, 0) && unit->m_rangedModel->HasSequence(0xa0)
            && unit->m_model && unit->m_model->IsLoaded(0, 0)) {
            M2BoneSequenceState state;
            unit->m_model->GetBoneSequenceState(0xffffffff, &state);

            if (state.finished == 0 && 0.0f < state.speed) {
                int32_t done = static_cast<int32_t>(std::lround(static_cast<float>(state.currentTime) / state.speed));
                int32_t left = static_cast<int32_t>(state.endTime) - done - static_cast<int32_t>(state.startTime);

                if (0 < left) {
                    M2SequenceInfo sequence;
                    std::memset(&sequence, 0, sizeof(sequence));
                    unit->m_rangedModel->GetSequenceInfo(0xa0, 0, sequence);

                    unit->m_rangedModel->SetBoneSequence(0xffffffff, 0xa0, 0, 0,
                                                         static_cast<float>(sequence.duration) / static_cast<float>(left), 1, 1);
                }
            }
        }

        auto ranged = unit->GetWeaponInfo(2, 0);
        bool firearm = ranged && ranged->m_class == 2 && (ranged->m_subclass == 3 || ranged->m_subclass == 0x12);

        if (!firearm && unit->m_rangedAmmoModel && !unit->m_rangedAmmoModel->m_attachParent && unit->m_model) {
            unit->m_rangedAmmoModel->AttachToParent(unit->m_model, 0x23, nullptr, 0);
        }

        unit->m_animFlags |= 0x4000;

        return;
    }

    if (eventId == id("$DTH")) {
        // FUN_00746610: the body's thud on the ground (a splash in shallow water).
        C3Vector at = unit->GetPosition();
        uint32_t floorFlags;
        float floor;
        uint32_t area;
        int32_t wet = CWorld::GetObjectFloor(unit->m_worldObject, &floorFlags, &floor, &area);

        if (!wet || floor - at.z <= 2.0f) {
            int32_t size = unit->GetFootstepSize();
            auto terrain = g_terrainTypeDB.GetRecord(unit->m_terrainType);

            if (0 <= size && size < 5 && terrain) {
                int32_t sound = 0;

                for (int32_t i = 0; i < g_footstepTerrainLookupDB.GetNumRecords(); i++) {
                    auto row = g_footstepTerrainLookupDB.GetRecordByIndex(i);

                    if (row && row->m_creatureFootstepID == size && row->m_terrainSoundID == terrain->m_soundID) {
                        sound = wet ? row->m_soundIDSplash : row->m_soundID;
                    }
                }

                if (!sound) {
                    static const int32_t s_defaults[5] = { 0x38b, 0x390, 0x395, 0x39a, 0x39f };
                    // "INVALID DEATH THUD SOUND: DEFAULTING TO ID %d"
                    sound = s_defaults[size];
                }

                SI2::PlaySoundKit(sound, &at, nullptr, nullptr, 0, nullptr, 1, 0);
            }
        }

        // FUN_007555e0: the camera shakes with a big enough body's fall (not a pet's).
        if (unit->Unit()->petNumber == 0 && unit->m_modelData && unit->m_modelData->m_deathThudShakeSize) {
            if (auto camera = CGWorldFrame::GetActiveCamera()) {
                camera->AddShakeByID(unit->m_modelData->m_deathThudShakeSize, unit->GetPosition());
            }
        }

        // FUN_00717ba0
        unit->ShowLootSparkle();

        return;
    }

    if (eventId == id("$CPP")) {
        if (!victim || (victim->Unit()->flags2 & 0x1) || victim->GetStandStateByte() == 7) {
            return;
        }

        bool shrugs = victim->m_creatureStats && (victim->m_creatureStats->m_typeFlags & 0x200000);

        if (info.victimState == 3) {
            if (!shrugs) {
                PlayParry(victim);
            }

            info.hitInfo &= ~0x2u;
        } else if (info.victimState == 2 || info.victimState == 8) {
            if (!shrugs) {
                victim->SetAnimation(0x1e, 0);
            }

            info.hitInfo &= ~0x2u;
        } else if (info.blocked != 0) {
            if (!shrugs) {
                victim->SetAnimation(0x18, 0);
            }

            info.hitInfo &= ~0x2u;
        }

        return;
    }

    if (eventId == id("$CSS")) {
        if (!attacker || info.victimState == 0 || info.victimState == 2 || info.victimState == 6) {
            return;
        }

        // FUN_00746360: the swing size of the hand's weapon.
        bool offHand = (info.hitInfo >> 2) & 1;
        int32_t swingSize = 0;
        bool found = true;

        if (unit->GetWeaponDisplayID(offHand ? 1 : 0)) {
            auto weapon = unit->GetWeaponInfo(offHand ? 1 : 0, 0);
            found = false;

            if (weapon && weapon->m_class == 2) {
                for (int32_t i = 0; i < g_itemSubClassDB.GetNumRecords(); i++) {
                    auto rec = g_itemSubClassDB.GetRecordByIndex(i);

                    if (rec && rec->m_classID == 2 && rec->m_subClassID == weapon->m_subclass) {
                        swingSize = rec->m_weaponSwingSize;
                        found = true;
                        break;
                    }
                }
            }
        }

        if (found) {
            PlayWeaponSwing(swingSize, info.hitInfo & 0x200, position, info.hitInfo & 0x10, InvolvesMover(info));
        }

        return;
    }

    bool aimedHit = eventId == id("$AH0") || eventId == id("$AH1") || eventId == id("$AH2") || eventId == id("$AH3");

    if (!aimedHit && eventId != id("$CAH")) {
        return;
    }

    if (aimedHit && attacker && attacker->m_soundData) {
        SoundKitProperties properties;
        properties.ResetToDefaults();
        properties.m_type = 0xe;

        uint32_t which = (eventId >> 24) - '0';
        SI2::PlaySoundKit(attacker->m_soundData->m_customAttack[which & 3], position, nullptr, &properties, 0, nullptr, 1, 0);
    }

    if (aimedHit) {
        info.hitInfo |= 0x200000;
    }

    if (!attacker && !victim) {
        // A swing at nothing: the exertion.
        if (IsPlayer(unit)) {
            // FUN_00763570: the player's spell's exertion sound.
            auto spell = g_spellDB.GetRecord(unit->m_castSpellID);
            auto visual = spell ? unit->GetSpellVisualRec(spell) : nullptr;

            if (visual && visual->m_animEventSoundID) {
                C3Vector at = unit->GetPosition();
                at.z += 1.0f;
                SI2::PlaySoundKit(visual->m_animEventSoundID, &at, nullptr, nullptr, 0, nullptr, 1, 0);
            }
        } else if (auto sounds = unit->GetSoundData()) {
            SI2::PlaySoundKit(sounds->m_soundExertionID, position, nullptr, nullptr, 0, nullptr, 0, 0);
        }
    }

    if (attacker) {
        int32_t state = attacker->m_combatInfo.victimState;

        if (state == 0 || state == 2 || state == 6) {
            attacker->SetBoneSequenceSpeed(attacker->m_model, 0xffffffff, 0.5f, 0);
        }
    }

    if (victim) {
        ApplyHit(victim, info);
    }

    info.attacker = 0;
    info.target = 0;
}

void UnitPlayVoiceSound(CGUnit_C* unit, int32_t soundID, int32_t emote, uint32_t type) {
    if (IsPlayer(unit) && unit->Unit()->health < 1) {
        return;
    }

    if (emote) {
        static CVar* s_emoteSounds = CVar::Lookup("Sound_EnableEmoteSounds");

        if (!s_emoteSounds || s_emoteSounds->GetInt() == 0) {
            return;
        }

        static CVar* s_petSounds = CVar::Lookup("Sound_EnablePetSounds");

        if (s_petSounds && s_petSounds->GetInt() == 0 && unit->Unit()->petNumber != 0) {
            return;
        }
    }

    static CVar* s_listenerAtCharacter = CVar::Lookup("Sound_ListenerAtCharacter");
    bool isMover = CGUnit_C::s_activeMover == unit->GetGUID() || IsActivePlayer(unit);
    bool atCharacter = isMover && s_listenerAtCharacter && s_listenerAtCharacter->GetInt() != 0;

    // PHASE4(Player_C): a player mid-dance (+0x1944, the dance steps) makes no voice sound.

    C3Vector position;

    if (unit->m_model && unit->m_model->IsLoaded(0, 0) && unit->m_model->HasAttachment(0x11)) {
        position = unit->m_model->GetAttachmentWorldPosition(0x11);
    } else {
        C3Vector base = unit->m_vehiclePassenger ? unit->GetModelWorldPosition() : unit->GetPosition();
        position = { base.x, base.y, base.z + 2.0f };
    }

    SoundKitProperties properties;
    properties.ResetToDefaults();
    properties.int0c = static_cast<int32_t>(type);
    properties.uint24 = 2;

    if (isMover) {
        properties.int20 = 0x6e;

        if (atCharacter) {
            properties.m_fadeOutTime = 0.65f;
        }
    }

    if (soundID != 0x19b0 && ((unit->Unit()->bytes0 >> 8) & 0xff) == 6) {
        properties.uint60 = 1;
        SStrPrintf(properties.m_voiceName, sizeof(properties.m_voiceName), "Death Knight %s %s",
                   "", ((unit->Unit()->bytes0 >> 16) & 0xff) ? "Female" : "Male");
    }

    SI2::PlaySoundKit(soundID, atCharacter ? nullptr : &position, unit->m_voiceSound, &properties, 0, nullptr, 1, 0);

    if (!atCharacter) {
        unit->m_voiceSound->SetObjectGUID(unit->GetGUID());
    }
}

void WeaponSoundsInitialize() {
    // FUN_00634cc0: the weapon class's subclasses, and the one marked as the default (0x4).
    int32_t weaponClass = 2;

    for (int32_t i = 0; i < g_itemClassDB.GetNumRecords(); i++) {
        auto rec = g_itemClassDB.GetRecordByIndex(i);

        if (rec && (rec->m_flags & 0x1)) {
            weaponClass = rec->m_classID;
        }
    }

    s_weaponSubclassCount = 0;

    for (int32_t i = 0; i < g_itemSubClassDB.GetNumRecords(); i++) {
        auto rec = g_itemSubClassDB.GetRecordByIndex(i);

        if (rec && rec->m_classID == weaponClass) {
            if (s_weaponSubclassCount < static_cast<uint32_t>(rec->m_subClassID + 1)) {
                s_weaponSubclassCount = static_cast<uint32_t>(rec->m_subClassID + 1);
            }

            if (rec->m_flags & 0x4) {
                s_defaultWeaponSubclass = rec->m_subClassID;
            }
        }
    }

    // FUN_004d0b00 / FUN_004d0380
    s_weaponImpacts.assign(s_weaponSubclassCount * 20, WEAPON_IMPACT { { 0, 0 } });

    for (int32_t i = g_weaponImpactSoundsDB.GetNumRecords(); i != 0;) {
        i--;

        auto rec = g_weaponImpactSoundsDB.GetRecordByIndex(i);

        if (!rec || rec->m_weaponSubClassID < 0 || s_weaponSubclassCount <= static_cast<uint32_t>(rec->m_weaponSubClassID)) {
            continue;
        }

        for (uint32_t k = 0; k < 10; k++) {
            uint32_t index = static_cast<uint32_t>(rec->m_parrySoundType) + (k + static_cast<uint32_t>(rec->m_weaponSubClassID) * 10) * 2;

            if (index < s_weaponImpacts.size()) {
                s_weaponImpacts[index].sound[0] = rec->m_impactSoundID[k];
                s_weaponImpacts[index].sound[1] = rec->m_critImpactSoundID[k];
            }
        }
    }

    // FUN_004d04e0
    std::memset(s_weaponSwings, 0, sizeof(s_weaponSwings));

    for (int32_t i = g_weaponSwingSounds2DB.GetNumRecords(); i != 0;) {
        i--;

        auto rec = g_weaponSwingSounds2DB.GetRecordByIndex(i);

        if (rec && 0 <= rec->m_swingType && rec->m_swingType < 3 && 0 <= rec->m_crit && rec->m_crit < 2) {
            s_weaponSwings[rec->m_swingType][rec->m_crit != 0 ? 1 : 0] = rec->m_soundID;
        }
    }
}

void UnitCombatInitialize() {
    static bool registered = false;

    if (registered) {
        return;
    }

    registered = true;

    ClientServices::SetMessageHandler(SMSG_ATTACK_START, &UnitCombatHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_ATTACK_STOP, &UnitCombatHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_ATTACKER_STATE_UPDATE, &UnitCombatHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_ATTACKSWING_NOTINRANGE, &UnitCombatHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_ATTACKSWING_BADFACING, &UnitCombatHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_ATTACKSWING_DEADTARGET, &UnitCombatHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_ATTACKSWING_CANT_ATTACK, &UnitCombatHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_ENVIRONMENTAL_DAMAGE_LOG, &UnitCombatHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_CLEAR_TARGET, &UnitCombatHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_COMBAT_EVENT_FAILED, &UnitCombatHandler, nullptr);

    // TODO(UnitCombatLog_C): FUN_007538a0, the combat log's start.
}
