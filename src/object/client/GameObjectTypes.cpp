#include "object/client/GameObjectTypes.hpp"
#include "client/ClientServices.hpp"
#include "db/Db.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2Scene.hpp"
#include "object/client/CGGameObject_C.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "object/client/ObjMgr.hpp"
#include "sound/SI2.hpp"
#include "sound/SoundKitProperties.hpp"
#include "ui/game/CGGameUI.hpp"
#include "ui/game/CGPartyInfo.hpp"
#include "ui/game/CGRaidInfo.hpp"
#include "ui/game/Cursor.hpp"
#include "world/CWorld.hpp"
#include <common/DataStore.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <cmath>

namespace {

// Which data field means what, per GAMEOBJECT_TYPE_* (0x00a36f30: {name, count, fields, end} per
// type). GameObjectDataIndex answers with the position of a meaning in its type's list.
const int32_t s_dataDoor[] = { 1, 4, 3, 57, 89, 90, 117, 85 };
const int32_t s_dataButton[] = { 1, 4, 3, 30, 57, 48, 89, 90, 55, 85 };
const int32_t s_dataQuestGiver[] = { 4, 5, 17, 38, 22, 57, 89, 55, 91, 48, 85 };
const int32_t s_dataChest[] = { 4, 6, 7, 8, 25, 26, 13, 30, 20, 31, 55, 56, 62, 66, 89, 92, 19, 85 };
const int32_t s_dataGeneric[] = { 19, 18, 37, 48, 63, 20, 85 };
const int32_t s_dataTrap[] = { 4, 31, 24, 10, 9, 23, 3, 32, 37, 65, 48, 58, 89, 90, 123, 85 };
const int32_t s_dataChair[] = { 11, 12, 94, 13, 85 };
const int32_t s_dataSpellFocus[] = { 14, 24, 30, 37, 20, 48, 19, 63, 85 };
const int32_t s_dataText[] = { 15, 16, 17, 91, 85 };
const int32_t s_dataGoober[] = { 4, 20, 21, 3, 22, 8, 23, 15, 16, 17, 10, 57, 30, 48, 89, 90, 55, 91, 19, 38, 127, 63, 85 };
const int32_t s_dataTransport[] = { 93, 1, 3, 125, 126, 87 };
const int32_t s_dataAreaDamage[] = { 4, 24, 27, 28, 29, 3, 89, 90 };
const int32_t s_dataCamera[] = { 4, 33, 21, 89, 85 };
const int32_t s_dataMOTransport[] = { 35, 36, 43, 44, 45, 99, 87, 67, 128 };
const int32_t s_dataRitual[] = { 34, 10, 39, 40, 49, 50, 54, 46, 85 };
const int32_t s_dataMailbox[] = { 85 };
const int32_t s_dataGuardPost[] = { 42, 9 };
const int32_t s_dataSpellCaster[] = { 10, 9, 47, 91, 48, 85 };
const int32_t s_dataMeetingStone[] = { 51, 52, 53 };
const int32_t s_dataFlagStand[] = { 4, 59, 24, 60, 61, 57, 89, 55, 85 };
const int32_t s_dataFishingHole[] = { 24, 6, 25, 26, 4 };
const int32_t s_dataFlagDrop[] = { 4, 21, 59, 57, 89 };
const int32_t s_dataMiniGame[] = { 64 };
const int32_t s_dataCapturePoint[] = { 24, 10, 67, 68, 70, 71, 72, 73, 74, 75, 76, 77, 78, 69, 79, 80, 81, 82, 48, 18, 118, 119 };
const int32_t s_dataAuraGenerator[] = { 1, 24, 83, 85, 84, 86, 37 };
const int32_t s_dataDungeonDifficulty[] = { 87, 88 };
const int32_t s_dataBarberChair[] = { 12, 121 };
const int32_t s_dataDestructible[] = { 95, 97, 103, 109, 103, 96, 103, 103, 103, 110, 103, 103, 103, 103, 111, 103, 112, 103, 124, 114, 103, 103, 120, 103 };
const int32_t s_dataGuildBank[] = { 85 };
const int32_t s_dataTrapDoor[] = { 93, 1, 3 };

struct GameObjectDataFields {
    int32_t count;
    const int32_t* fields;
};

#define GO_DATA_FIELDS(a) { static_cast<int32_t>(sizeof(a) / sizeof(a[0])), a }

const GameObjectDataFields s_dataFields[0x24] = {
    GO_DATA_FIELDS(s_dataDoor),
    GO_DATA_FIELDS(s_dataButton),
    GO_DATA_FIELDS(s_dataQuestGiver),
    GO_DATA_FIELDS(s_dataChest),
    { 0, nullptr },                                     // binder
    GO_DATA_FIELDS(s_dataGeneric),
    GO_DATA_FIELDS(s_dataTrap),
    GO_DATA_FIELDS(s_dataChair),
    GO_DATA_FIELDS(s_dataSpellFocus),
    GO_DATA_FIELDS(s_dataText),
    GO_DATA_FIELDS(s_dataGoober),
    GO_DATA_FIELDS(s_dataTransport),
    GO_DATA_FIELDS(s_dataAreaDamage),
    GO_DATA_FIELDS(s_dataCamera),
    { 0, nullptr },                                     // map object
    GO_DATA_FIELDS(s_dataMOTransport),
    { 0, nullptr },                                     // duel arbiter
    { 0, nullptr },                                     // fishing node
    GO_DATA_FIELDS(s_dataRitual),
    GO_DATA_FIELDS(s_dataMailbox),
    { 0, nullptr },                                     // auction house
    GO_DATA_FIELDS(s_dataGuardPost),
    GO_DATA_FIELDS(s_dataSpellCaster),
    GO_DATA_FIELDS(s_dataMeetingStone),
    GO_DATA_FIELDS(s_dataFlagStand),
    GO_DATA_FIELDS(s_dataFishingHole),
    GO_DATA_FIELDS(s_dataFlagDrop),
    GO_DATA_FIELDS(s_dataMiniGame),
    { 0, nullptr },                                     // lottery kiosk
    GO_DATA_FIELDS(s_dataCapturePoint),
    GO_DATA_FIELDS(s_dataAuraGenerator),
    GO_DATA_FIELDS(s_dataDungeonDifficulty),
    GO_DATA_FIELDS(s_dataBarberChair),
    GO_DATA_FIELDS(s_dataDestructible),
    GO_DATA_FIELDS(s_dataGuildBank),
    GO_DATA_FIELDS(s_dataTrapDoor),
};

#undef GO_DATA_FIELDS

// The animation each animation state plays (0x00ada938) and its name (0x00ada978): Spawn, Closed,
// Opening, Open, Closing, Destroying, Destroyed, Rebuilding, Custom0..3 and Despawn.
const int32_t s_animStateSequences[13] = { 145, 147, 148, 149, 146, 150, 151, 152, 153, 154, 155, 156, 157 };
const char* const s_animStateNames[13] = {
    "Spawn", "Closed", "Opening", "Open", "Closing", "Destroying", "Destroyed", "Rebuilding",
    "Custom0", "Custom1", "Custom2", "Custom3", "Despawn",
};

// The GameObject animations PlayAnimState falls back between.
enum {
    ANIM_CLOSE = 0x92,
    ANIM_CLOSED = 0x93,
    ANIM_OPEN = 0x94,
    ANIM_OPENED = 0x95,
    ANIM_DESTROY = 0x96,
    ANIM_DESTROYED = 0x97,
};

// The animation states.
enum {
    ANIMSTATE_SPAWN = 0,
    ANIMSTATE_CLOSED = 1,
    ANIMSTATE_OPENING = 2,
    ANIMSTATE_OPEN = 3,
    ANIMSTATE_CLOSING = 4,
    ANIMSTATE_DESTROYING = 5,
    ANIMSTATE_DESTROYED = 6,
    ANIMSTATE_REBUILDING = 7,
    ANIMSTATE_CUSTOM0 = 8,
};

// The animation progress (GAMEOBJECT_DYNAMIC's high half) meaning "none given".
const uint16_t NO_ANIM_PROGRESS = 0xFFFF;

bool IsTransitionState(int32_t state) {
    return state == ANIMSTATE_OPENING || state == ANIMSTATE_CLOSING
        || state == ANIMSTATE_DESTROYING || state == ANIMSTATE_REBUILDING;
}

bool IsRestingState(int32_t state) {
    return state == -1 || state == ANIMSTATE_OPEN || state == ANIMSTATE_CLOSED || state == ANIMSTATE_DESTROYED;
}

CGPlayer_C* ActivePlayer() {
    return static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));
}

} // namespace

// ref: FUN_00746190
int32_t GameObjectDataIndex(int32_t type, int32_t meaning) {
    if (type < 0x24 && meaning < 0x81 && type >= 0) {
        auto& entry = s_dataFields[type];

        for (int32_t i = 0; i < entry.count; i++) {
            if (entry.fields[i] == meaning) {
                return i;
            }
        }
    }

    return -1;
}

// ref: FUN_0070bf70
void GameObjectPlayDisplaySound(int32_t displayID, int32_t index, const C3Vector* position, SOUNDKITOBJECT* sound) {
    if (index == -1) {
        return;
    }

    auto display = g_gameObjectDisplayInfoDB.GetRecord(displayID);

    if (!display) {
        return;
    }

    int32_t soundKit = display->m_sound[index];

    SoundKitProperties properties;

    if (!SI2::SoundKitFollowsListener(soundKit)) {
        properties.ResetToDefaults();
        SI2::PlaySoundKit(soundKit, position, nullptr, &properties, 0, nullptr, 1, 0);

        return;
    }

    // PHASE4(Sound): the reference also leaves it when the same kit already plays within reach
    // (FUN_004cfe00 over SESound's channel list), as CGUnit_C::UpdateMountSound notes.
    if (!sound || SI2::IsPlaying(sound)) {
        return;
    }

    properties.ResetToDefaults();
    properties.uint1c = 1;
    properties.byte38 = 1;
    properties.uint48 = 1;
    properties.int30 = 0;

    SI2::PlaySoundKit(soundKit, position, sound, &properties, 0, nullptr, 1, 0);
}

// ref: FUN_0070c050
void GameObjectHandleAnimEvent(uint32_t eventId, uint32_t data, const C3Vector* position,
                               SOUNDKITOBJECT* sound, int32_t displayID) {
    switch (eventId) {
        case 0x304F4724: GameObjectPlayDisplaySound(displayID, 0, position, sound); return;  // $GO0
        case 0x314F4724: GameObjectPlayDisplaySound(displayID, 1, position, sound); return;  // $GO1
        case 0x324F4724: GameObjectPlayDisplaySound(displayID, 2, position, sound); return;  // $GO2
        case 0x334F4724: GameObjectPlayDisplaySound(displayID, 3, position, sound); return;  // $GO3
        case 0x344F4724: GameObjectPlayDisplaySound(displayID, 4, position, sound); return;  // $GO4
        case 0x354F4724: GameObjectPlayDisplaySound(displayID, 5, position, sound); return;  // $GO5
        case 0x30434724: GameObjectPlayDisplaySound(displayID, 6, position, sound); return;  // $GC0
        case 0x31434724: GameObjectPlayDisplaySound(displayID, 7, position, sound); return;  // $GC1
        case 0x32434724: GameObjectPlayDisplaySound(displayID, 8, position, sound); return;  // $GC2
        case 0x33434724: GameObjectPlayDisplaySound(displayID, 9, position, sound); return;  // $GC3

        case 0x4B485324: {  // $SHK
            // TODO(Camera): the three shakes of SpellEffectCameraShakes.dbc[data] go to the active
            // camera (FUN_004f5960, CGCamera::AddShakeByID FUN_00606410); frozen has no camera
            // shakes yet.
            return;
        }

        case 0x4C534424: {  // $DSL
            if (!sound || SI2::IsPlaying(sound)) {
                return;
            }

            // PHASE4(Sound): FUN_004cfe00, as above.
            SoundKitProperties properties;
            properties.ResetToDefaults();
            properties.uint10 = 0x3F800000u;
            properties.uint14 = 0x3F800000u;
            properties.uint1c = 1;
            properties.int30 = 0;
            properties.byte38 = 1;
            properties.uint48 = 1;

            SI2::PlaySoundKit(static_cast<int32_t>(data), position, sound, &properties, 0, nullptr, 1, 0);

            return;
        }

        case 0x4F534424:    // $DSO
        case 0x444E5324: {  // $SND
            SI2::PlaySoundKit(static_cast<int32_t>(data), position, nullptr, nullptr, 0, nullptr, 1, 0);

            return;
        }

        default:
            return;
    }
}

// ------------------------------------------------------------------------------------------------
// The root
// ------------------------------------------------------------------------------------------------

// ref: FUN_0070eef0
int32_t CGGameObjectType::GetData(int32_t meaning) const {
    return this->m_owner->GetData(meaning);
}

// ref: FUN_0070c2b0
C3Vector CGGameObjectType::GetPosition() {
    auto& passenger = this->m_owner->m_passenger;

    return passenger.GetPosition(passenger.m_position);
}

// ref: FUN_0070c2e0
C3Vector CGGameObjectType::GetRawPosition() {
    return this->m_owner->m_passenger.m_position;
}

// ref: FUN_0070c310
float CGGameObjectType::GetFacing() {
    auto& passenger = this->m_owner->m_passenger;

    return passenger.GetFacing(passenger.GetRotationFacing());
}

// ref: FUN_0070c330
float CGGameObjectType::GetRawFacing() {
    return this->m_owner->m_passenger.GetRotationFacing();
}

// ref: FUN_0070f9b0
int32_t CGGameObjectType::GetCursor() {
    bool canUse = this->CanUseNow(nullptr, nullptr, nullptr);

    auto lock = this->m_owner->GetLockRec();

    if (lock) {
        auto lockType = g_lockTypeDB.GetRecord(lock->m_index[0]);

        if (lockType && lockType->m_cursorName && *lockType->m_cursorName) {
            char name[32];

            if (lockType->m_ID != 1 && !canUse) {
                SStrPrintf(name, sizeof(name), "Unable%s", lockType->m_cursorName);

                return CursorGetIndex(name);
            }

            SStrCopy(name, lockType->m_cursorName, sizeof(name));

            return CursorGetIndex(name);
        }
    }

    return canUse ? 5 : 0x1F;
}

// ref: FUN_007112a0
bool CGGameObjectType::CanUse() {
    auto player = ActivePlayer();

    if (!player) {
        return false;
    }

    auto owner = this->m_owner;
    auto data = owner->GameObject();

    if (data->type != 6) {
        if (owner->GetReaction(player) < 2) {
            return false;
        }
    } else {
        // A trap: only a hostile one with a lock, and a player's one only when its maker is someone
        // the player may fight.
        if (!owner->GetLockRec()) {
            return false;
        }

        if (owner->GetReaction(player) > 1) {
            return false;
        }

        WOWGUID creator = data->createdBy;

        if ((creator >> 60) == 0 && (creator & 0xF07FFFFFFFFFFFFFull) != 0) {
            auto maker = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(creator, TYPE_PLAYER, __FILE__, __LINE__));

            if (!maker) {
                return false;
            }

            // UNIT_BYTE2_FLAG_FFA_PVP makes the maker's trap anyone's -- except the maker's own
            // group's, while the player is in a group (FUN_004fb0b0: the first party slot is set).
            bool ffa = false;

            if ((maker->Unit()->bytes2 >> 8) & 0x4) {
                WOWGUID makerGuid = maker->GetGUID();
                bool grouped = CGPartyInfo::GetMemberGuid(0) != 0
                    && (CGPartyInfo::IsPlayerOrMember(makerGuid) || CGRaidInfo::IndexOf(makerGuid) != 0);

                ffa = !grouped;
            }

            // TODO(Unit_C): FUN_0071f5c0, whether the maker may attack the player, also lets it
            // through; it is not ported, so only UNIT_BYTE2_FLAG_PVP and free-for-all do.
            if (!((maker->Unit()->bytes2 >> 8) & 0x1) && !ffa) {
                return false;
            }
        }
    }

    uint16_t dynamic = data->dynamicFlags;
    uint32_t flags = data->flags;

    // GO_DYNFLAG_LO_NO_INTERACT; GO_FLAG_IN_USE and GO_FLAG_NOT_SELECTABLE; despawning; and
    // GO_FLAG_INTERACT_COND without GO_DYNFLAG_LO_ACTIVATE.
    if ((dynamic & 0x4) || (flags & 0x11) || owner->m_disablePending || ((flags & 0x4) && !(dynamic & 0x1))) {
        return false;
    }

    // GO_DATA onlyCreatorUse.
    if (this->GetData(94) && player->GetGUID() != data->createdBy) {
        return false;
    }

    return true;
}

// ref: FUN_00711470
bool CGGameObjectType::CanUseNow(int32_t* error, float* range, const char** spellName) {
    auto player = ActivePlayer();

    // TODO(Unit_C): the reference refuses (0x87) when the player's slot 0x128 says so (dead);
    // frozen's CGUnit_C has no such slot.
    if (!player) {
        if (error) {
            *error = 0x87;
        }

        return false;
    }

    // TODO(ClientServices): the reference refuses with 0x88 while DAT_00bcfb8c is clear.

    auto unit = player->Unit();

    // UNIT_FLAG_STUNNED: refused, naming the stun when there is one.
    if (unit->flags & 0x40000) {
        // TODO(Spell_C): the stunning aura's spell (FUN_00804590) names the refusal (0x200).
        if (error) {
            *error = 0x1B2;
        }

        return false;
    }

    if (!this->m_owner->GetLockRec()) {
        // TODO(Unit_C): mounted (FUN_0051a230) the player may use only what allows it
        // (CGGameObject_C::IsUsableMounted) unless FUN_0071b0c0 says otherwise (0x1c0); in a
        // non-stance form only what the form allows (SpellShapeshiftForm flag 8, FUN_0071b130,
        // 0x1c1). Those unit callees are not ported.
    }

    // UNIT_FLAG_IN_COMBAT, for what may not be used in combat.
    if ((unit->flags & 0x80000) && !this->m_owner->IsUsableInCombat()) {
        if (error) {
            *error = 0x1C2;
        }

        return false;
    }

    if (static_cast<int32_t>(unit->flags) < 0 && !this->m_owner->IsUsableUnderFlag31()) {
        if (error) {
            *error = 0x181;
        }

        return false;
    }

    // GO_FLAG_LOCKED, and the player cannot open the lock.
    if ((this->m_owner->GameObject()->flags & 0x2) && this->m_owner->CheckLock(nullptr, nullptr, nullptr, nullptr, nullptr)) {
        if (error) {
            *error = this->m_useError;
        }

        return false;
    }

    if (range) {
        *range = this->m_useRange;
    }

    bool inRange = this->IsInUseRange();

    if (!inRange && error) {
        *error = 0xF0;
    }

    return inRange;
}

// ref: FUN_0070fa70
bool CGGameObjectType::IsInUseRange() {
    auto player = ActivePlayer();

    if (!player) {
        return false;
    }

    float range = this->m_useRange;
    int32_t spell = 0;

    if (!this->m_owner->CheckLock(&spell, nullptr, nullptr, nullptr, nullptr) && spell != 0) {
        // TODO(Spell_C): the lock's spell's range (FUN_00802c30) replaces the use range.
    } else if (this->m_owner->m_state == 2) {
        return false;
    }

    auto owner = this->m_owner;

    // A spell focus is in range by distance alone.
    if (owner->GameObject()->type == 8) {
        C3Vector d = owner->GetPosition() - player->GetPosition();

        return d.x * d.x + d.y * d.y + d.z * d.z <= range * range;
    }

    CAaBox box;

    if (owner->GameObject()->type == 0) {
        // A door's box is the one its current animation was authored with.
        auto model = owner->m_model;
        M2BoneSequenceState state;

        if (!model || !model->GetBoneSequenceState(0xFFFFFFFF, &state)) {
            return false;
        }

        M2SequenceInfo info;
        memset(&info, 0, sizeof(info));
        model->GetSequenceInfo(state.uint90, static_cast<int32_t>(state.uint94), info);
        box = info.extent;
    } else {
        auto display = owner->m_stats ? g_gameObjectDisplayInfoDB.GetRecord(owner->m_stats->m_displayID) : nullptr;

        if (display) {
            box = { { display->m_geoBoxMin[0], display->m_geoBoxMin[1], display->m_geoBoxMin[2] },
                    { display->m_geoBoxMax[0], display->m_geoBoxMax[1], display->m_geoBoxMax[2] } };
        } else {
            if (!owner->m_model || !owner->m_model->IsLoaded(0, 0)) {
                return false;
            }

            owner->m_model->GetBoundingBox(box);
        }
    }

    // The box in the object's space, grown by the range, around the player brought into that space.
    float scale = owner->m_obj->m_scale;
    box.b = box.b * scale;
    box.t = box.t * scale;
    box.b = box.b - C3Vector(range, range, range);
    box.t = box.t + C3Vector(range, range, range);

    C44Matrix world;
    owner->GetWorldMatrix(world);
    world.Scale(1.0f / scale);

    C3Vector local = player->GetPosition() * world.AffineInverse();

    return box.b.x < local.x && box.b.y < local.y && box.b.z < local.z
        && local.x < box.t.x && local.y < box.t.y && local.z < box.t.z;
}

// ref: FUN_007116c0
int32_t CGGameObjectType::Use() {
    int32_t spell = 0;
    int32_t item = 0;
    uint32_t entry = 0;

    if (!this->m_owner->CheckLock(&spell, nullptr, nullptr, &item, &entry)) {
        auto player = ActivePlayer();

        if (spell == 0) {
            if (!player) {
                return 0;
            }

            // TODO(Unit_C): mounted, the player dismounts first (FUN_007412e0), and a
            // shapeshift that forbids it is cancelled (FUN_00726ce0).

            CDataStore msg;
            msg.Put(static_cast<uint32_t>(CMSG_GAME_OBJ_USE));
            msg.Put(this->m_owner->GetGUID());
            msg.Finalize();
            ClientServices::Send(&msg);
        } else {
            // TODO(Spell_C): cast the lock's spell on the object (FUN_0080da40, FUN_0080bc80).
            return 0;
        }

        // TODO(Vehicle): FUN_0074b8b0 / FUN_0074cce0, the vehicle seat the use leaves.
        return 1;
    }

    // Refused: say why, from the lock's first entry.
    auto lock = this->m_owner->GetLockRec();

    if (!lock) {
        return 0;
    }

    if (!this->m_owner->IsLockActionAllowed(lock->m_action[0])) {
        if (this->m_owner->GameObject()->type == 3 && this->m_owner->m_state == 0) {
            CGGameUI::DisplayError(0xE8);
        }

        return 0;
    }

    switch (lock->m_type[0]) {
        case LockRec::TYPE_ITEM: {
            // TODO(Item_C): the item's name (FUN_0067ca30, FUN_004fd200) goes with 0xed.
            return 0;
        }

        case LockRec::TYPE_LOCKTYPE: {
            auto lockType = g_lockTypeDB.GetRecord(lock->m_index[0]);
            int32_t skillNeeded = lock->m_skill[0];

            if (skillNeeded == 0) {
                skillNeeded = this->m_owner->GameObject()->level * 5;
            }

            const char* name = lockType ? lockType->m_name : "UNKNOWN";

            if (spell == 0) {
                CGGameUI::DisplayError(0xEE, name);
            } else {
                CGGameUI::DisplayError(0xEF, name, skillNeeded);
            }

            return 0;
        }

        case LockRec::TYPE_SPELL: {
            auto spellRec = g_spellDB.GetRecord(lock->m_index[0]);

            if (spellRec) {
                CGGameUI::DisplayError(0xEE, spellRec->m_name);
            }

            return 0;
        }

        default:
            CGGameUI::DisplayError(0xE9);

            return 0;
    }
}

// ------------------------------------------------------------------------------------------------
// The animated types
// ------------------------------------------------------------------------------------------------

// ref: FUN_007124b0
CGGameObjectAnimated::CGGameObjectAnimated(CGGameObject_C* owner) : CGGameObjectType(owner, 5.0f) {
    this->m_sound = STORM_NEW(SOUNDKITOBJECT);

    this->InitAnimState();
}

// ref: FUN_00713d10
CGGameObjectAnimated::~CGGameObjectAnimated() {
    if (this->m_sound) {
        SI2::StopOrFadeOut(this->m_sound, 0, -1.0f, 1);
        // FUN_004c7440: the sound lets go of its channel, fading a loop out.
        this->m_sound->m_sound.DetachWithLoopFade();
        this->m_sound->~SOUNDKITOBJECT();
        STORM_FREE(this->m_sound);
        this->m_sound = nullptr;
    }
}

// ref: FUN_0070d160
void CGGameObjectAnimated::OnFlagsChanged(uint32_t changed) {
    if (!(changed & 0x80)) {
        return;
    }

    auto model = this->m_owner->m_model;

    if (!model || !model->IsLoaded(0, 0) || !IsTransitionState(this->m_animState)) {
        return;
    }

    // GO_FLAG 0x80 holds a moving door where it is.
    if (this->m_owner->GameObject()->flags & 0x80) {
        uint32_t time = model->m_scene->m_time;
        model->m_animationHeldTime = time ? time : 1;
    } else {
        model->m_animationHeldTime = 0;
    }
}

// ref: FUN_0070fd10
void CGGameObjectAnimated::OnAnimProgressChanged() {
    if (this->m_owner->m_inReenable || !this->m_owner->IsModelLoaded() || !this->m_owner->m_model) {
        return;
    }

    this->PlayAnimState(this->m_owner->m_model);
}

// ref: FUN_0070d690
void CGGameObjectAnimated::OnStateChanged(int32_t from, int32_t to) {
    if (this->m_owner->m_inReenable) {
        return;
    }

    SI2::StopOrFadeOut(this->m_sound, 0, -1.0f, 1);

    bool noProgress = this->m_owner->GameObject()->animProgress == NO_ANIM_PROGRESS;

    if (to == 0) {
        this->SetAnimState(from != 1 && noProgress ? ANIMSTATE_OPEN : ANIMSTATE_OPENING);
    } else if (to == 1) {
        if (!noProgress) {
            from = 0;
        }

        if (from == 0) {
            this->SetAnimState(ANIMSTATE_CLOSING);
        } else if (from == 2) {
            this->SetAnimState(ANIMSTATE_REBUILDING);
        } else {
            this->SetAnimState(ANIMSTATE_CLOSED);
        }
    } else if (to == 2) {
        this->SetAnimState(from != 1 && noProgress ? ANIMSTATE_DESTROYED : ANIMSTATE_DESTROYING);
    }
}

// ref: FUN_0070c3c0
int32_t CGGameObjectAnimated::PlaceModel(float elapsed) {
    auto owner = this->m_owner;

    // An object waiting to go (its despawn held for an animation) goes once that animation has
    // finished or been held still.
    if (owner->m_disablePending && owner->m_model) {
        auto model = owner->m_model;

        if (model->IsLoaded(0, 0) && !model->m_animatePrev
            && (model->m_animationHeldTime != 0
                || (this->m_animEndTime != -1 && static_cast<int32_t>(CWorld::GetCurTimeMs() - this->m_animEndTime) >= 0))) {
            return this->OnSequenceInterrupted();
        }
    }

    return 1;
}

// ref: FUN_0070d7b0
void CGGameObjectAnimated::OnAnimEvent(uint32_t eventId, uint32_t data, const C3Vector* position, uint32_t a6) {
    GameObjectHandleAnimEvent(eventId, data, position, this->m_sound, this->m_owner->GameObject()->displayID);
}

// ref: FUN_0070c430
int32_t CGGameObjectAnimated::OnSequenceInterrupted() {
    if (IsRestingState(this->m_playingState)) {
        return 1;
    }

    ClntObjMgrUnlockObject(this->m_owner->GetGUID(), __FILE__, __LINE__);

    return 0;
}

// ref: FUN_0070d7e0
void CGGameObjectAnimated::OnSequenceDone() {
    int32_t played = this->m_playingState;
    this->m_playingState = -1;

    switch (this->m_animState) {
        case ANIMSTATE_SPAWN:
        case 8:
        case 9:
        case 10:
        case 11: {
            if (!this->m_owner->m_disablePending) {
                int32_t state = this->m_owner->m_state;
                this->OnStateChanged(state, state);
            }

            break;
        }

        case ANIMSTATE_CLOSED:
        case ANIMSTATE_OPEN:
        case ANIMSTATE_DESTROYED:
            this->PlayAnimState(this->m_owner->m_model);
            break;

        case ANIMSTATE_OPENING:
            this->SetAnimState(ANIMSTATE_OPEN);
            break;

        case ANIMSTATE_CLOSING:
        case ANIMSTATE_REBUILDING:
            this->SetAnimState(ANIMSTATE_CLOSED);
            break;

        case ANIMSTATE_DESTROYING:
            this->SetAnimState(ANIMSTATE_DESTROYED);
            break;

        default:
            break;
    }

    if (!IsRestingState(played)) {
        ClntObjMgrUnlockObject(this->m_owner->GetGUID(), __FILE__, __LINE__);
    }
}

// ref: FUN_0070c480
void CGGameObjectAnimated::PlayCustomAnim(int32_t state) {
    auto model = this->m_owner->m_model;

    if (!model || !model->HasSequence(s_animStateSequences[state])) {
        return;
    }

    this->SetAnimState(state);

    if (state == ANIMSTATE_SPAWN) {
        this->m_owner->SetAlpha(this->m_owner->GetFadeInAlpha(), 0);
    }
}

// ref: FUN_0070b260
const char* CGGameObjectAnimated::GetStateName() {
    // The reference indexes its name table with the state as it stands, -1 included; frozen
    // answers "" for a state outside the table rather than read before it.
    if (this->m_animState < 0 || this->m_animState >= 13) {
        return "";
    }

    return s_animStateNames[this->m_animState];
}

// ref: FUN_0070c370
void CGGameObjectAnimated::PostInit(int32_t a4) {
    if (!a4) {
        return;
    }

    auto owner = this->m_owner;

    if (!owner->IsModelLoaded()) {
        owner->m_pendingAnim = 1;

        return;
    }

    this->PlayCustomAnim(ANIMSTATE_SPAWN);
    owner->m_pendingAnim = 0;
}

// ref: FUN_0070c350
void CGGameObjectAnimated::OnDisable() {
    SI2::StopOrFadeOut(this->m_sound, 0, -1.0f, 1);
}

// ref: FUN_0070fce0
void CGGameObjectAnimated::OnPostReenable() {
    auto model = this->m_owner->m_model;

    if (model && model->IsLoaded(0, 0)) {
        this->InitAnimState();
    }
}

// ref: FUN_0070b230
void CGGameObjectAnimated::UpdateFrame(uint32_t time) {
    C3Vector position = this->GetPosition();

    this->m_sound->SetPosition(position);
}

// ref: FUN_0070fcd0
void CGGameObjectAnimated::OnModelLoaded() {
    this->InitAnimState();
}

// ref: FUN_0070d510
void CGGameObjectAnimated::SetAnimState(int32_t state) {
    if (state == this->m_animState) {
        return;
    }

    CM2Model* model = nullptr;
    auto owner = this->m_owner;

    if (owner->IsModelLoaded()) {
        model = owner->m_model;

        // Turning back part way through Opening/Closing or Destroying/Rebuilding: the new
        // animation starts where the old one is, mirrored.
        if (model && owner->GameObject()->animProgress == NO_ANIM_PROGRESS) {
            int32_t from = this->m_animState;
            bool reverse = (from == ANIMSTATE_OPENING && state == ANIMSTATE_CLOSING)
                || (from == ANIMSTATE_CLOSING && state == ANIMSTATE_OPENING)
                || (from == ANIMSTATE_DESTROYING && state == ANIMSTATE_REBUILDING)
                || (from == ANIMSTATE_REBUILDING && state == ANIMSTATE_DESTROYING);

            if (reverse) {
                M2BoneSequenceState sequence;
                model->GetBoneSequenceState(0xFFFFFFFF, &sequence);

                int32_t length = static_cast<int32_t>(sequence.endTime - sequence.startTime);

                if (length > 0) {
                    int32_t elapsed = static_cast<int32_t>(model->m_scene->m_time - sequence.startTime);
                    float played = static_cast<float>(elapsed);

                    if (elapsed < 0) {
                        played += 4294967296.0f;
                    }

                    float t = played / static_cast<float>(length);

                    if (t <= 1.0f) {
                        owner->SetAnimProgress(1.0f - t);
                    }
                }
            }
        }
    }

    this->m_animState = state;

    if (model) {
        this->PlayAnimState(model);
    }
}

// ref: FUN_0070d1e0
int32_t CGGameObjectAnimated::PlayAnimState(CM2Model* model) {
    auto owner = this->m_owner;
    float speed = 1.0f;
    uint32_t sequence = static_cast<uint32_t>(s_animStateSequences[this->m_animState]);

    if (model->IsLoaded(0, 0) && !model->HasSequence(sequence)) {
        // A model short of the animation plays its partner instead, or the partner frozen.
        bool play = true;

        switch (sequence) {
            case ANIM_CLOSE:
                if (!model->HasSequence(ANIM_OPEN)) {
                    sequence = ANIM_CLOSED;
                }

                break;

            case ANIM_CLOSED:
                if (!model->HasSequence(ANIM_CLOSE)) {
                    if (!model->HasSequence(ANIM_OPEN)) {
                        sequence = 0;
                        play = false;
                    } else {
                        sequence = ANIM_OPEN;
                        speed = 0.0f;
                    }
                }

                break;

            case ANIM_OPEN:
                if (!model->HasSequence(ANIM_CLOSE)) {
                    sequence = model->HasSequence(ANIM_DESTROY) ? ANIM_DESTROY : ANIM_OPENED;
                }

                break;

            case ANIM_OPENED:
                if (!model->HasSequence(ANIM_OPEN)) {
                    if (model->HasSequence(ANIM_CLOSE)) {
                        sequence = ANIM_CLOSE;
                        speed = 0.0f;
                    } else {
                        sequence = ANIM_DESTROYED;
                    }
                }

                break;

            default:
                break;
        }

        if (play && sequence == model->GetBoneUint90(0xFFFFFFFF)) {
            return 1;
        }
    }

    uint32_t current = model->GetBoneUint90(0xFFFFFFFF);

    if (owner->m_objectEffects && current != sequence) {
        // PHASE4(ObjectEffect): the effects tied to the old animation stop and the new one's start
        // (FUN_006f17f0, FUN_006f61d0, FUN_006f7270); nothing creates the manager yet.
    }

    M2SequenceInfo info;
    memset(&info, 0, sizeof(info));
    model->GetSequenceInfo(sequence, 0, info);

    uint32_t startTime = 0;
    uint32_t hold = 0;
    int32_t state = this->m_animState;

    if (IsTransitionState(state)) {
        auto data = owner->GameObject();
        hold = (data->flags >> 7) & 1;

        // A given progress starts the animation that far in; it is spent once used.
        if (data->animProgress != NO_ANIM_PROGRESS) {
            startTime = static_cast<uint32_t>(lroundf(static_cast<float>(info.duration) * static_cast<float>(data->animProgress) * (1.0f / 65535.0f)));
            data->animProgress = static_cast<uint16_t>(lroundf(65535.0f));
        }
    }

    model->SetBoneSequence(0xFFFFFFFF, sequence, 0xFFFFFFFF, startTime, speed, state != ANIMSTATE_SPAWN, 1);

    if (hold) {
        uint32_t time = model->m_scene->m_time;
        model->m_animationHeldTime = time ? time : 1;
    } else {
        model->m_animationHeldTime = 0;
    }

    // While it moves, the object is kept from despawning under it.
    if (state != ANIMSTATE_OPEN && state != ANIMSTATE_CLOSED && state != ANIMSTATE_DESTROYED) {
        ClntObjMgrLockObject(owner->GetGUID());
    }

    this->m_playingState = this->m_animState;

    uint32_t now = CWorld::GetCurTimeMs();

    if (speed <= 0.0f) {
        this->m_animEndTime = static_cast<int32_t>(now);

        return 1;
    }

    float remaining = static_cast<float>(static_cast<int32_t>(info.duration - startTime));

    if (static_cast<int32_t>(info.duration - startTime) < 0) {
        remaining += 4294967296.0f;
    }

    this->m_animEndTime = static_cast<int32_t>(llroundf(remaining / speed) + now);

    return 1;
}

// ref: FUN_0070d600
void CGGameObjectAnimated::InitAnimState() {
    auto owner = this->m_owner;
    bool noProgress = owner->GameObject()->animProgress == NO_ANIM_PROGRESS;
    int32_t state = -1;

    if (owner->m_state == 0) {
        state = noProgress ? ANIMSTATE_OPEN : ANIMSTATE_OPENING;
    } else if (owner->m_state == 1) {
        state = noProgress ? ANIMSTATE_CLOSED : ANIMSTATE_CLOSING;
    } else if (owner->m_state == 2) {
        state = noProgress ? ANIMSTATE_DESTROYED : ANIMSTATE_DESTROYING;
    }

    this->m_animState = -1;
    this->m_playingState = -1;
    this->m_animEndTime = -1;

    this->SetAnimState(state);
}

// ------------------------------------------------------------------------------------------------
// The types with their own slots
// ------------------------------------------------------------------------------------------------

// ref: FUN_0070d8d0
void CGGameObjectDoor::SetAnimState(int32_t state) {
    this->CGGameObjectAnimated::SetAnimState(state);

    this->m_owner->m_collidable = this->m_animState == ANIMSTATE_CLOSED;
}

// ref: FUN_0070fd50
// Whether a door may be used as it stands: one with a lock always; one that starts open only while
// it is not open, and the rest only while they are not closed.
static bool DoorCanBeUsed(CGGameObjectDoor* door) {
    if (!door->GetData(GO_DATA_LOCK)) {
        return true;
    }

    if (door->GetData(1)) {
        return door->m_animState != ANIMSTATE_CLOSED;
    }

    return door->m_animState != ANIMSTATE_OPEN;
}

// ref: FUN_007119d0
bool CGGameObjectDoor::CanUseDoorNow(int32_t* error, float* range) {
    if (!DoorCanBeUsed(this)) {
        if (error) {
            *error = 0xF2;
        }

        return false;
    }

    return this->CanUseNow(error, range, nullptr);
}

// ref: FUN_0070fde0
int32_t CGGameObjectQuestGiver::GetCursor() {
    int32_t cursor = this->m_owner->GetQuestMarkerAnimKit(this->CanUseNow(nullptr, nullptr, nullptr) ? 0 : 1);

    if (cursor) {
        return cursor;
    }

    return this->CGGameObjectType::GetCursor();
}

// ref: FUN_0070fe10
void CGGameObjectQuestGiver::OnWindowOpened() {
    int32_t customAnim = this->GetData(GO_DATA_CUSTOM_ANIM);

    if (customAnim) {
        this->m_owner->SetPendingCustomAnim(customAnim - 1);

        return;
    }

    auto model = this->m_owner->m_model;

    if (!model || !model->IsLoaded(0, 0)) {
        return;
    }

    uint32_t opening = static_cast<uint32_t>(s_animStateSequences[ANIMSTATE_OPENING]);

    if (!model->HasSequence(opening)) {
        M2SequenceInfo info;
        memset(&info, 0, sizeof(info));
        model->GetSequenceInfo(opening, 0, info);
        opening = info.sequenceId;
    }

    if (model->GetBoneUint90(0xFFFFFFFF) != opening) {
        this->SetAnimState(ANIMSTATE_OPENING);
    }
}

// ref: FUN_0070ff20
void CGGameObjectQuestGiver::OnWindowClosed() {
    if (this->GetData(GO_DATA_CUSTOM_ANIM)) {
        return;
    }

    if (this->m_animState == ANIMSTATE_OPENING || this->m_animState == ANIMSTATE_OPEN) {
        this->SetAnimState(ANIMSTATE_CLOSING);
    }
}

bool CGGameObjectGeneric::CanHighlight() {
    return this->GetData(GO_DATA_HIGHLIGHT) != 0;
}

// ref: FUN_0070b540
int32_t CGGameObjectText::GetCursor() {
    return this->CanUseNow(nullptr, nullptr, nullptr) ? 7 : 0x21;
}

// ref: FUN_0070c850
int32_t CGGameObjectText::Use() {
    // TODO(ItemTextFrame): the page opens in the item text frame (FUN_0058a1a0(guid, 0)).
    return 1;
}

// ref: FUN_0070c880
void CGGameObjectText::OnStatsLoaded() {
    // TODO(ItemTextFrame): when the item text frame already shows this object's page it reads the
    // page again (FUN_0058a1a0(guid, 1)).
}

// ref: FUN_00712030
bool CGGameObjectFishingNode::CanUse() {
    auto player = ActivePlayer();

    if (!player) {
        return false;
    }

    // Only the bobber the player's channel is fishing with (UNIT_FIELD_CHANNEL_OBJECT).
    if (player->Unit()->channelObject != this->m_owner->GetGUID()) {
        return false;
    }

    return this->CGGameObjectType::CanUse();
}

// ref: FUN_00712080
bool CGGameObjectRitual::CanUseNow(int32_t* error, float* range, const char** spellName) {
    auto player = ActivePlayer();

    if (!player) {
        return false;
    }

    // A ritual whose spell may not be used in combat, while the player fights.
    auto spell = g_spellDB.GetRecord(this->GetData(10));

    if (spell && (spell->m_attributes & 0x10000000)) {
        // TODO(Unit_C): CGUnit_C::IsAttackingOrPetInCombat and FUN_0072aaf0.
    }

    WOWGUID creator = this->m_owner->GameObject()->createdBy;

    if ((creator >> 60) == 0 && (creator & 0xF07FFFFFFFFFFFFFull) != 0) {
        auto maker = ClntObjMgrObjectPtr(creator, TYPE_PLAYER, __FILE__, __LINE__);

        if (!maker) {
            return false;
        }

        // TODO(PartyInfo): only the maker's party or raid may join (FUN_00718ca0, FUN_00718d70).
    }

    return this->CGGameObjectType::CanUseNow(error, range, spellName);
}

// ref: FUN_0070b680
int32_t CGGameObjectMailbox::GetCursor() {
    return this->CanUseNow(nullptr, nullptr, nullptr) ? 0xF : 0x29;
}

// ref: FUN_0070c9d0
int32_t CGGameObjectMailbox::Use() {
    // TODO(MailFrame): the mail frame opens on the box (FUN_0056da60(guid)).
    return 1;
}

void CGGameObjectMailbox::OnWindowOpened() {
    this->m_owner->SetPendingCustomAnim(0);
}

// ref: FUN_00712270
int32_t CGGameObjectMeetingStone::Use() {
    auto player = ActivePlayer();

    if (!player) {
        return 0;
    }

    // TODO(PartyInfo): the reference checks the party and the summoning target first (0x1d6, 199,
    // 0x1d1, 0x1d2, 0x1d3, 0x1d7 from the party's member records); only the player's own level is
    // checked here.
    if (!this->IsLevelInRange(player->Unit()->level)) {
        CGGameUI::DisplayError(0x1D1);

        return 0;
    }

    return this->CGGameObjectType::Use();
}

// ref: FUN_007108d0
void CGGameObjectMeetingStone::Virtual0A0(int32_t a2) {
    // TODO(LFG): the stone's area (data 53) goes to FUN_006201c0 by its AreaTable record.
}

// ref: FUN_007121e0
bool CGGameObjectMeetingStone::IsLevelInRange(uint32_t level) {
    uint32_t minLevel = static_cast<uint32_t>(this->GetData(51));

    if (minLevel > level) {
        return false;
    }

    return level <= static_cast<uint32_t>(this->GetData(52));
}

// ref: FUN_0070ff80
bool CGGameObjectCapturePoint::CanHighlight() {
    return this->GetData(GO_DATA_HIGHLIGHT) != 0;
}

// ref: FUN_00710940
void CGGameObjectCapturePoint::OnStatsLoaded() {
    // TODO(WorldStateUI): the capture bar for the point's world states (data 24 and 67,
    // FUN_00527a30) shows.
}

// ref: FUN_0070ca30
void CGGameObjectCapturePoint::OnDisable() {
    // TODO(WorldStateUI): the capture bar lets go of the object (FUN_00529550 on 0x00ac8ae0).
}

// ref: FUN_0070b910
int32_t CGGameObjectGuildBank::GetCursor() {
    return this->CanUseNow(nullptr, nullptr, nullptr) ? 3 : 0x1D;
}

// ref: FUN_0070ecb0
int32_t CGGameObjectGuildBank::Use() {
    // TODO(GuildBankFrame): the guild bank opens on the object (FUN_006d23c0(guid)).
    return 1;
}

// ref: FUN_0070c9f0
void CGGameObjectGuildBank::OnWindowOpened() {
    this->m_owner->SetPendingCustomAnim(0);
}

// ref: FUN_00711ce0
bool CGGameObjectChair::CanUse() {
    if (!this->CGGameObjectType::CanUse()) {
        return false;
    }

    auto player = ActivePlayer();
    uint8_t standState = static_cast<uint8_t>(player->Unit()->bytes1);

    // Already sitting in a chair: not while one of its seats is the player's own.
    if (standState > 3 && standState < 7) {
        for (int32_t i = this->GetSeatCount(); i > 0; i--) {
            C3Vector seat = this->m_owner->m_passenger.GetPosition(this->m_seats[i - 1]);
            C3Vector d = player->GetPosition() - seat;

            if (d.x * d.x + d.y * d.y + d.z * d.z < 0.16666667f) {
                return false;
            }
        }
    }

    return true;
}

// ref: FUN_00711db0
bool CGGameObjectChair::CanUseNow(int32_t* error, float* range, const char** spellName) {
    if (!this->CGGameObjectType::CanUseNow(error, range, spellName)) {
        return false;
    }

    float reach = this->m_useRange * this->m_useRange;

    if (range) {
        *range = this->m_useRange;
    }

    auto player = ActivePlayer();

    if (player) {
        for (int32_t i = this->GetSeatCount(); i > 0; i--) {
            C3Vector seat = this->m_owner->m_passenger.GetPosition(this->m_seats[i - 1]);
            C3Vector d = player->GetPosition() - seat;

            if (d.x * d.x + d.y * d.y + d.z * d.z < reach) {
                return true;
            }
        }
    }

    if (error) {
        *error = 0xF0;
    }

    return false;
}

// ref: FUN_0070c700
void CGGameObjectChair::OnStatsLoaded() {
    uint32_t count = static_cast<uint32_t>(this->GetSeatCount());
    auto owner = this->m_owner;

    C44Matrix matrix;

    if (!owner->GetTransportGUID()) {
        owner->GetWorldMatrix(matrix);
    } else {
        // A chair on a transport keeps its seats in the transport's space: placed by its raw
        // position and its own rotation, not the transport's.
        matrix = C44Matrix();
        matrix.Translate(owner->GetRawPosition());
        matrix.Rotate(C4Quaternion(owner->m_passenger.m_packedRotation));
        matrix.Scale(owner->GetBaseScale());
    }

    // ref: FUN_004f55f0
    // The seats sit in a row along the chair's local Y, one yard apart and centred on it.
    float y = -((static_cast<float>(count) - 1.0f) * 0.5f);

    for (uint32_t i = 0; i < count && i < 5; i++) {
        C3Vector local = { 0.0f, y, 0.0f };
        this->m_seats[i] = local * matrix;
        y += 1.0f;
    }
}

// ref: FUN_00710240
int32_t CGGameObjectChair::GetSeatCount() {
    return this->GetData(GO_DATA_CHAIR_SLOTS);
}

// ref: FUN_00711eb0
bool CGGameObjectBarberChair::CanUseNow(int32_t* error, float* range, const char** spellName) {
    if (!this->CGGameObjectChair::CanUseNow(error, range, spellName)) {
        return false;
    }

    auto player = ActivePlayer();

    // Not while shapeshifted (UNIT_FIELD_DISPLAYID is not UNIT_FIELD_NATIVEDISPLAYID).
    // TODO(Unit_C): and not in a non-stance form (CGUnit_C::IsInNonStanceForm).
    if (player && player->Unit()->displayID == player->Unit()->nativeDisplayID) {
        return true;
    }

    if (error) {
        *error = 0x1C1;
    }

    return false;
}
