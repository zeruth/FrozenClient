#include "object/client/GameObjectTypes.hpp"
#include "object/client/CVehicle_C.hpp"
#include <cstring>
#include <algorithm>
#include "util/Log.hpp"
#include "object/client/CGUnit_C.hpp"
#include <tempest/Math.hpp>
#include <common/Time.hpp>
#include "ui/game/CGWorldFrame.hpp"
#include "ui/game/CGCamera.hpp"
#include "object/client/CMovementData_C.hpp"
#include "object/client/CMovement_C.hpp"
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

// ------------------------------------------------------------------------------------------------
// The map object types: the object's display placed in the world as a building or a prop, and,
// for the transports, moved along a path with what rides it.
// ------------------------------------------------------------------------------------------------

// ref: FUN_0070d900
CGGameObjectMapObject::~CGGameObjectMapObject() {
    if (this->m_mapObject) {
        CWorld::RemoveDynamicObject(this->m_mapObject);
        this->m_mapObject = nullptr;
    }
}

// ref: FUN_00711a10
// Transports (11, 15) and trap doors (35) own their building -- it carries their guid, so what
// stands on it knows what it rides -- and an elevator's is flagged 0x1000; a destructible building
// (33) makes its own; anything else's carries no owner.
void CGGameObjectMapObject::CreateMapObject() {
    auto owner = this->m_owner;

    switch (owner->GameObject()->type) {
        case 11:
        case 15:
        case 35:
            this->m_mapObject = owner->AddMapObject(owner->GetGUID(), 0, 0, nullptr);

            if (this->m_mapObject && owner->GameObject()->type == 11) {
                CWorld::SetDynamicObjectFlag1000(this->m_mapObject, 1);
            }

            break;

        case 33:
            break;

        default:
            this->m_mapObject = owner->AddMapObject(0, 0, 0, nullptr);
            break;
    }
}

// ref: FUN_0070b290
void CGGameObjectMapObject::DestroyMapObject() {
    if (this->m_mapObject) {
        CWorld::RemoveDynamicObject(this->m_mapObject);
        this->m_mapObject = nullptr;
    }
}

// ref: FUN_00713250
CGGameObjectTransportBase::CGGameObjectTransportBase(CGGameObject_C* owner) : CGGameObjectMapObject(owner) {
    this->m_position = owner->m_passenger.GetPosition(owner->m_passenger.m_position);
}

// ref: FUN_00712610
CGGameObjectTransportBase::~CGGameObjectTransportBase() {
    if (this->m_mapObject) {
        MovementUnlinkTransport(this->m_owner);
    }

    // FUN_007cecd0
    while (auto passenger = this->m_passengers.Head()) {
        passenger->m_transportLink.Unlink();
    }
}

// ref: FUN_00711ab0
// A passenger is aboard: it is threaded onto the list (again, if it was), and one that moves on
// its own is held to the server's word (MOVEFLAG_SPLINE_ENABLED) until the building is in. The
// active player's movement takes the transport's path time.
void CGGameObjectTransportBase::UpdatePassenger(CPassenger* passenger, int32_t mode) {
    passenger->m_transportLink.Unlink();
    this->m_passengers.LinkToTail(passenger);

    if (mode && (passenger->m_passengerFlags & 0x1)) {
        if (!this->m_mapObject || !CWorld::DynamicObjectIsLoaded(this->m_mapObject)) {
            // FUN_009872a0
            static_cast<CMovementShared*>(passenger)->m_moveFlags |= MOVEFLAG_SPLINE_ENABLED;
        }
    }

    if (passenger->m_guid == ClntObjMgrGetActivePlayer()) {
        MovementSetTransportTime(this->m_adjustedTime);
    }
}

// ref: FUN_0070b360
int32_t CGGameObjectTransportBase::Virtual06C(const C3Vector* position) {
    if (!this->m_mapObject) {
        return 1;
    }

    return CWorld::DynamicObjectContains(this->m_mapObject, *position);
}

// ref: FUN_0070b330
void CGGameObjectTransportBase::OnStatsLoaded() {
    this->CreateMapObject();

    if (this->m_mapObject) {
        MovementLinkTransport(this->m_owner);
    }
}

void CGGameObjectTransportBase::OnPostReenable() {
    this->CreateMapObject();

    if (this->m_mapObject) {
        MovementLinkTransport(this->m_owner);
    }
}

// ref: FUN_0070ffd0
// The transport is disabled: everyone riding it is put off -- the camera no longer relative to it,
// a unit dropped where it is (falling if nothing holds it up), anything else just left in the
// world -- and its building goes.
void CGGameObjectTransportBase::OnDisable() {
    auto camera = CGWorldFrame::GetActiveCamera();
    int32_t now = static_cast<int32_t>(OsGetAsyncTimeMs());

    for (auto passenger = this->m_passengers.Head(); passenger;) {
        auto next = this->m_passengers.Next(passenger);

        if (camera->GetTarget() == passenger->m_guid) {
            camera->SetRelativeTo(0);
        }

        if (!(passenger->m_passengerFlags & 0x1)) {
            passenger->ChangeTransport(0);
        } else {
            auto move = static_cast<CMovementData_C*>(passenger);
            move->ClearSplineEnabled();

            if (passenger->m_guid == ClntObjMgrGetActivePlayer()) {
                move->QueueFallIfUnsupported(now);
            } else if (move->FallIfUnsupported() && !move->m_moverLink.IsLinked() && MovementGetGlobals()) {
                // FUN_006eb650
                MovementLinkMover(move);
            }

            move->SetSplineTransport(0, 0xff, 1);
        }

        passenger = next;
    }

    if (this->m_mapObject) {
        MovementUnlinkTransport(this->m_owner);
    }

    this->DestroyMapObject();
}

// ---- type 11 ------------------------------------------------------------------------------------

namespace {

// ref: FUN_0070c8c0
// The first TransportAnimation row for a transport entry (the table is sorted by entry), or -1.
int32_t FindTransportAnimation(int32_t entry) {
    int32_t lo = 0;
    int32_t hi = g_transportAnimationDB.GetNumRecords();

    while (lo < hi) {
        int32_t mid = (lo + hi) / 2;

        if (g_transportAnimationDB.GetRecordByIndex(mid)->m_transportID < entry) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    if (lo < g_transportAnimationDB.GetNumRecords() && g_transportAnimationDB.GetRecordByIndex(lo)->m_transportID == entry) {
        return lo;
    }

    return -1;
}

// ref: FUN_0070c930
int32_t FindTransportRotation(int32_t entry) {
    int32_t lo = 0;
    int32_t hi = g_transportRotationDB.GetNumRecords();

    while (lo < hi) {
        int32_t mid = (lo + hi) / 2;

        if (g_transportRotationDB.GetRecordByIndex(mid)->m_gameObjectsID < entry) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    if (lo < g_transportRotationDB.GetNumRecords() && g_transportRotationDB.GetRecordByIndex(lo)->m_gameObjectsID == entry) {
        return lo;
    }

    return -1;
}

// The last animation key's time: the length of the path.
uint32_t LastKeyTime(const CGGameObjectTransport* transport) {
    return g_transportAnimationDB.GetRecordByIndex(transport->m_animFirst + transport->m_animCount - 1)->m_timeIndex;
}

// ref: FUN_0070b2b0
// The building's sequence ended: an opening plays on into its held pose, a closing into its rest.
void TransportSequenceDone(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner) {
    (void)boneId;
    (void)a5;
    (void)owner;

    if (a4 != 0) {
        return;
    }

    uint32_t next;

    switch (animId) {
        case 0x92: next = 0x93; break;
        case 0x94: next = 0x95; break;
        case 0xa2: next = 0xa3; break;
        case 0xa4: next = 0; break;
        default: return;
    }

    model->SetBoneSequence(-1, next, -1, 0, 1.0f, 1, 1);
}

} // namespace

// ref: FUN_00713820
// The keyed transport: its TransportAnimation and TransportRotation runs found by entry, then its
// first place on the path taken.
CGGameObjectTransport::CGGameObjectTransport(CGGameObject_C* owner) : CGGameObjectTransportBase(owner) {
    int32_t entry = owner->GetEntryID();
    int32_t first = FindTransportAnimation(entry);

    if (first != -1) {
        this->m_animFirst = static_cast<uint32_t>(first);
        this->m_animCount = 1;

        for (int32_t i = first + 1; i < g_transportAnimationDB.GetNumRecords(); i++) {
            if (g_transportAnimationDB.GetRecordByIndex(i)->m_transportID != entry) {
                break;
            }

            this->m_animCount++;
        }
    }

    first = FindTransportRotation(entry);

    if (first != -1) {
        this->m_rotFirst = static_cast<uint32_t>(first);
        this->m_rotCount = 1;

        for (int32_t i = first + 1; i < g_transportRotationDB.GetNumRecords(); i++) {
            if (g_transportRotationDB.GetRecordByIndex(i)->m_gameObjectsID != entry) {
                break;
            }

            this->m_rotCount++;
        }
    }

    this->Initialize();
}

// ref: FUN_00710570
// How long the path runs in a state: the whole loop with no level set, the way up (state 0) or
// down otherwise.
uint32_t CGGameObjectTransport::PathDuration(int32_t state) {
    if (this->m_animCount == 0) {
        return 0;
    }

    uint32_t level = static_cast<uint32_t>(this->m_owner->GameObject()->level);

    if (level == 0) {
        return LastKeyTime(this);
    }

    if (state == 0) {
        return level;
    }

    return LastKeyTime(this) - level;
}

// ref: FUN_00710640
// Where on the path a moment falls: a loop wraps; a two-stop transport runs from its start time
// (and its reversal's offset) and holds at the stop.
uint32_t CGGameObjectTransport::AdjustTime(int32_t state, uint32_t time) {
    if (this->m_animCount == 0) {
        return time;
    }

    uint32_t level = static_cast<uint32_t>(this->m_owner->GameObject()->level);

    if (level == 0) {
        return time % LastKeyTime(this);
    }

    time -= this->m_pathStart;

    if (state == this->m_animState) {
        time += this->m_pathOffset;
    } else if (time < this->m_pathOffset) {
        time = this->m_pathOffset - time;
    } else {
        time = 0;
    }

    if (this->m_animState == 0) {
        if (level < time) {
            return level;
        }
    } else {
        time += level;

        if (LastKeyTime(this) <= time) {
            time = 0;
        }
    }

    return time;
}

// ref: FUN_00713990
uint32_t CGGameObjectTransport::AdjustTime(uint32_t time) {
    return this->AdjustTime(this->m_owner->GameObject()->state, time);
}

// ref: FUN_00710780
// How far through its state's run the path is at `time` (0..1).
float CGGameObjectTransport::PathProgress(uint32_t time, int32_t state) {
    int32_t at = static_cast<int32_t>(this->AdjustTime(state, time));
    int32_t current = this->m_animState;
    uint32_t duration = this->PathDuration(current);

    if (current != 0) {
        if (at == 0) {
            at = static_cast<int32_t>(LastKeyTime(this));
        }

        at -= this->m_owner->GameObject()->level;
    }

    float done = state == current ? static_cast<float>(static_cast<uint32_t>(at))
                                  : static_cast<float>(duration - static_cast<uint32_t>(at));

    return done / static_cast<float>(duration);
}

// ref: FUN_007105d0
// A two-stop transport's run began animProgress/65535 of the way through, at `time`.
void CGGameObjectTransport::SetPathStart(uint32_t time) {
    auto data = this->m_owner->GameObject();

    if (data->level == 0 || this->m_animCount == 0) {
        return;
    }

    uint32_t duration = this->PathDuration(data->state);

    float into = static_cast<float>(data->animProgress) * 1.52590219e-05f * static_cast<float>(duration);
    this->m_pathStart = time - static_cast<uint32_t>(static_cast<int32_t>(std::nearbyint(into)));
}

// ref: FUN_0070da40
// The path restarted for the object's state: at the stop for state 0, at the far end otherwise.
uint32_t CGGameObjectTransport::ResetPath(uint32_t time) {
    auto data = this->m_owner->GameObject();
    int32_t state = data->state;
    uint32_t level = static_cast<uint32_t>(data->level);

    this->m_animState = state;
    this->m_pathOffset = 0;

    if (state == 0) {
        this->m_pathStart = time - level;
        return level;
    }

    this->m_pathStart = level - LastKeyTime(this) + time;

    return 0;
}

// ref: FUN_0070daa0
// The offset at a path time: the two keys around it lerped and turned by the object's own
// rotation. A key's sequence starts on the building as the path reaches it.
//
// PARTIAL: the owner's effect kit for the sequence (FUN_0070bb10, through FUN_006f17f0 and the
// CEffect list) is the ObjectEffect port's.
C3Vector CGGameObjectTransport::EvaluatePosition(uint32_t time) {
    if (this->m_animCount < 2) {
        return { 0.0f, 0.0f, 0.0f };
    }

    uint32_t next = this->m_animKey + 1;
    next = next != this->m_animCount ? next : 0;

    const TransportAnimationRec* a;
    const TransportAnimationRec* b;

    while (true) {
        b = g_transportAnimationDB.GetRecordByIndex(this->m_animFirst + next);
        a = g_transportAnimationDB.GetRecordByIndex(this->m_animFirst + this->m_animKey);

        if (a->m_timeIndex <= time && time < b->m_timeIndex) {
            break;
        }

        this->m_animKey = next;
        next = next + 1 != this->m_animCount ? next + 1 : 0;
    }

    float t = static_cast<float>(time - a->m_timeIndex) / static_cast<float>(b->m_timeIndex - a->m_timeIndex);

    if (this->m_mapObject && a->m_sequenceID != this->m_sequence) {
        this->m_sequence = a->m_sequenceID;
        CWorld::SetDynamicObjectSequence(this->m_mapObject, a->m_sequenceID, 0, 0);
    }

    auto parent = this->m_owner->GameObject()->parentRotation;
    C44Matrix turn(C4Quaternion(parent[0], parent[1], parent[2], parent[3]));

    float u = 1.0f - t;
    float x = a->m_pos[0] * u + t * b->m_pos[0];
    float y = a->m_pos[1] * u + b->m_pos[1] * t;
    float z = b->m_pos[2] * t + a->m_pos[2] * u;

    return {
        x * turn.a0 + turn.b0 * y + turn.c0 * z,
        turn.a1 * x + turn.b1 * y + turn.c1 * z,
        x * turn.a2 + y * turn.b2 + turn.c2 * z,
    };
}

// ref: FUN_0070dc10
// The rotation at a path time: the keys around it slerped, after the object's own rotation. A key
// at time 0 closes the loop at the path's length. A transport with fewer than two keys keeps the
// object's rotation.
C4Quaternion CGGameObjectTransport::EvaluateRotation(uint32_t time) {
    if (this->m_rotCount < 2) {
        return this->m_owner->GetRotation();
    }

    auto parent = this->m_owner->GameObject()->parentRotation;
    C4Quaternion base(parent[0], parent[1], parent[2], parent[3]);

    uint32_t next = this->m_rotKey + 1;
    next = next != this->m_rotCount ? next : 0;

    while (true) {
        auto a = g_transportRotationDB.GetRecordByIndex(this->m_rotFirst + this->m_rotKey);
        auto b = g_transportRotationDB.GetRecordByIndex(this->m_rotFirst + next);
        uint32_t end = b->m_timeIndex;

        if (end == 0 && this->m_animCount != 0) {
            end = LastKeyTime(this);
        }

        if (a->m_timeIndex <= time && time < end) {
            float t = static_cast<float>(time - a->m_timeIndex) / static_cast<float>(b->m_timeIndex - a->m_timeIndex);
            C4Quaternion q = C4Quaternion::Slerp(t, C4Quaternion(a->m_rot[0], a->m_rot[1], a->m_rot[2], a->m_rot[3]),
                                                 C4Quaternion(b->m_rot[0], b->m_rot[1], b->m_rot[2], b->m_rot[3]));

            return q * base;
        }

        this->m_rotKey = next;
        next = next + 1 != this->m_rotCount ? next + 1 : 0;
    }
}

// ref: FUN_007106d0
// The path's offset and rotation at `time`; a reversed two-stop transport that has reached the
// stop it was heading for restarts its path from there.
void CGGameObjectTransport::Evaluate(uint32_t time, C3Vector* offset, C4Quaternion* rotation) {
    auto data = this->m_owner->GameObject();
    int32_t state = data->state;
    uint32_t at = this->AdjustTime(state, time);

    if (this->m_pathOffset != 0) {
        bool atStop = (this->m_animState == state && this->m_animState == 0) ? at == static_cast<uint32_t>(data->level) : at == 0;

        if (atStop) {
            at = this->ResetPath(time);
        }
    }

    *offset = this->EvaluatePosition(at);
    *rotation = this->EvaluateRotation(at);
}

// ref: FUN_0070c650
// The object's world matrix rebuilt from its rotation at the transport's place.
C44Matrix* CGGameObjectTransport::UpdateWorldMatrix() {
    auto owner = this->m_owner;

    owner->m_worldMatrix = C44Matrix(owner->GetRotation());
    owner->m_worldMatrix.d0 = this->m_position.x;
    owner->m_worldMatrix.d1 = this->m_position.y;
    owner->m_worldMatrix.d2 = this->m_position.z;

    return &owner->m_worldMatrix;
}

// ref: FUN_00711f20
// The transport's first place: its state, the path started from the object's progress, and the
// offset and rotation there applied to where the object rests.
void CGGameObjectTransport::Initialize() {
    auto owner = this->m_owner;

    this->m_animState = owner->GameObject()->state;

    uint32_t time = static_cast<uint32_t>(OsGetAsyncTimeMs()) + static_cast<uint32_t>(owner->m_timeOffset);
    this->SetPathStart(time);
    this->m_time = time;

    C3Vector offset = { 0.0f, 0.0f, 0.0f };
    C4Quaternion rotation(0.0f, 0.0f, 0.0f, 1.0f);
    this->Evaluate(time, &offset, &rotation);

    this->m_position = owner->m_passenger.GetPosition(owner->m_passenger.m_position) + offset;

    owner->m_passenger.SetPackedRotation(rotation);
    owner->m_passenger.m_passengerFlags |= 0x2;
    this->UpdateWorldMatrix();
}

// ref: FUN_00710820
// The object's state changed (a two-stop transport was called): short of 95% of the way it
// reverses from where it is, keeping the share already travelled; past that it starts afresh.
void CGGameObjectTransport::OnStateChanged(int32_t from, int32_t to) {
    if (this->m_owner->GameObject()->level == 0 || this->m_animCount == 0 || this->m_animState == to) {
        return;
    }

    uint32_t time = static_cast<uint32_t>(OsGetAsyncTimeMs()) + static_cast<uint32_t>(this->m_owner->m_timeOffset);
    float progress = this->PathProgress(time, from);

    if (progress < 0.95f) {
        uint32_t duration = this->PathDuration(from);
        float travelled = static_cast<float>(duration) * progress;

        this->m_pathStart = time;
        this->m_pathOffset = static_cast<uint32_t>(static_cast<int32_t>(std::nearbyint(travelled - 0.5f)));
        return;
    }

    this->m_pathStart = time;
    this->m_animState = to;
    this->m_pathOffset = 0;
}

// ref: FUN_0070b5c0
// Its stats are in: the building is made and joins the moving transports, its sequences answer
// their ends, and it is placed by the object's matrix.
void CGGameObjectTransport::OnStatsLoaded() {
    this->CreateMapObject();

    if (this->m_mapObject) {
        MovementLinkTransport(this->m_owner);
    }

    if (this->m_mapObject) {
        CWorld::SetDynamicObjectSequenceDone(this->m_mapObject, &TransportSequenceDone, 0);

        C44Matrix world;
        this->m_owner->GetWorldMatrix(world);
        CWorld::SetDynamicObjectPlacement(this->m_mapObject, world);
    }
}

// ref: FUN_00712010
void CGGameObjectTransport::OnReenable() {
    this->m_animState = -1;
    this->m_arrived = 0;
    this->Initialize();
}

// ref: FUN_0070b630
void CGGameObjectTransport::OnPostReenable() {
    this->m_sequence = 0x1FA;
    this->CreateMapObject();

    if (this->m_mapObject) {
        MovementLinkTransport(this->m_owner);
    }

    if (this->m_mapObject) {
        CWorld::SetDynamicObjectSequenceDone(this->m_mapObject, &TransportSequenceDone, 0);
    }
}

// ref: FUN_0070c550
// The step from the transport's place to `to` over `elapsed` ms: its length, its direction, and
// the speed it implies.
//
// PARTIAL: a change of speed tells the owner's effects (FUN_006f3910, the ObjectEffect port's).
float CGGameObjectTransportBase::StepTo(uint32_t elapsed, const C3Vector& to, C3Vector* direction) {
    float dx = to.x - this->m_position.x;
    float dy = to.y - this->m_position.y;
    float dz = to.z - this->m_position.z;
    float length = std::sqrt(dz * dz + dy * dy + dx * dx);

    this->m_speed = length / (static_cast<float>(elapsed) * 0.001f);

    if (direction && 2.384185791015625e-07f <= std::fabs(length)) {
        float inv = 1.0f / length;
        *direction = { inv * dx, inv * dy, dz * inv };
    }

    return length;
}

// ref: FUN_007132e0
// Everyone riding it follows: the building is placed, each passenger's world entry re-placed in
// the transport's space, and the active player's movement takes the path time when it rides.
// `cameraRides` reports whether the camera's target is aboard.
void CGGameObjectTransportBase::MovePassengers(int32_t* cameraRides) {
    *cameraRides = 0;

    auto owner = this->m_owner;
    C44Matrix* world = &owner->m_worldMatrix;
    bool isMapObj = false;

    if (this->m_mapObject) {
        CWorld::SetDynamicObjectPlacement(this->m_mapObject, *world);
        isMapObj = CWorld::DynamicObjectIsMapObj(this->m_mapObject);
    }

    WOWGUID cameraTarget = CGWorldFrame::GetActiveCamera()->GetTarget();
    auto player = static_cast<CGObject_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_UNIT, ".\\GameObject_C.cpp", 0x682));

    for (auto passenger = this->m_passengers.Head(); passenger; passenger = this->m_passengers.Next(passenger)) {
        if (this->m_arrived && (passenger->m_passengerFlags & 0x1)
            && (static_cast<CMovementShared*>(passenger)->m_moveFlags & MOVEFLAG_SPLINE_ENABLED)) {
            static_cast<CMovementShared*>(passenger)->ClearSplineEnabled();
        }

        if (passenger->m_guid == cameraTarget) {
            *cameraRides = 1;
        }

        auto object = static_cast<CGObject_C*>(ClntObjMgrObjectPtr(passenger->m_guid, TYPE_OBJECT, ".\\GameObject_C.cpp", 0x68d));

        if (object->IsA(TYPE_UNIT)) {
            auto unit = static_cast<CGUnit_C*>(object);

            if (unit->m_vehicle && unit->m_vehicle->m_rec) {
                unit->m_vehicle->UpdateMatrix(world);
            }
        }

        object->UpdateWorldObject((object == player || !isMapObj) ? 0 : 1);
        object->SetParticleRelative(world);
    }

    if (player && player->GetTransportGUID() == owner->GetGUID()) {
        MovementSetTransportTime(this->m_adjustedTime);
    }
}

// ref: FUN_007139e0
// One movement poll: once the building is in, an unkeyed transport is placed and stays; a long gap
// is taken in a jump to 250 ms before now; then the path is stepped, the transport placed, and its
// passengers carried.
//
// PARTIAL: the passengers' sweep against the world as a prop carries them (FUN_00760720, the
// collision port's), which runs for each self-moving passenger before the transport moves.
void CGGameObjectTransport::UpdateTransport(uint32_t time, int32_t elapsed) {
    auto owner = this->m_owner;

    if (!this->m_arrived && this->m_mapObject && CWorld::DynamicObjectIsLoaded(this->m_mapObject)) {
        this->m_arrived = 1;

        if (this->m_animCount == 0 && this->m_rotCount == 0) {
            C44Matrix world;
            owner->GetWorldMatrix(world);
            CWorld::SetDynamicObjectPlacement(this->m_mapObject, world);
            return;
        }
    }

    uint32_t step = static_cast<uint32_t>(elapsed);

    if (250 < step) {
        uint32_t jump = static_cast<uint32_t>(owner->m_timeOffset) - 250 + time;
        this->m_time = jump;

        if (owner->GameObject()->level != 0 && static_cast<int32_t>(this->m_pathStart - jump) >= 0) {
            this->m_time = this->m_pathStart;
        }

        C3Vector offset = { 0.0f, 0.0f, 0.0f };
        C4Quaternion rotation(0.0f, 0.0f, 0.0f, 1.0f);
        this->Evaluate(this->m_time, &offset, &rotation);

        owner->m_passenger.SetPackedRotation(rotation);
        owner->m_passenger.m_passengerFlags |= 0x2;

        this->m_position = owner->m_passenger.GetPosition(owner->m_passenger.m_position) + offset;
        step = 250;
    }

    this->m_time = time + static_cast<uint32_t>(owner->m_timeOffset);
    this->m_adjustedTime = this->AdjustTime(this->m_animState, this->m_time);

    C3Vector offset = { 0.0f, 0.0f, 0.0f };
    C4Quaternion rotation(0.0f, 0.0f, 0.0f, 1.0f);
    this->Evaluate(this->m_time, &offset, &rotation);

    C3Vector to = owner->m_passenger.GetPosition(owner->m_passenger.m_position) + offset;
    C3Vector direction = { 0.0f, 0.0f, 0.0f };
    float length = this->StepTo(step, to, &direction);

    if (this->m_passengers.Head()) {
        if (9.5367431640625e-07f <= std::fabs(length)) {
            this->m_position = to;
        }

        owner->m_passenger.SetPackedRotation(rotation);
        owner->m_passenger.m_passengerFlags |= 0x2;
        this->UpdateWorldMatrix();

        int32_t cameraRides = 0;
        this->MovePassengers(&cameraRides);

        return;
    }

    this->m_position = to;
    owner->m_passenger.SetPackedRotation(rotation);
    owner->m_passenger.m_passengerFlags |= 0x2;
    auto world = this->UpdateWorldMatrix();

    if (this->m_mapObject) {
        CWorld::SetDynamicObjectPlacement(this->m_mapObject, *world);
    }
}

// ---- type 15 ------------------------------------------------------------------------------------

// GameObjectDataIndex meanings a ship carries (s_dataMOTransport).
static const int32_t GO_DATA_TAXI_PATH = 0x23;
static const int32_t GO_DATA_MOVE_SPEED = 0x24;
static const int32_t GO_DATA_ACCEL_RATE = 0x2b;
static const int32_t GO_DATA_TRANSPORT_PHYSICS = 99;
static const int32_t GO_DATA_ALLOW_STOPPING = 0x80;

// ref: FUN_007101c0
// A ship that may stop is told to by state 1 and set off by any other.
void CGGameObjectMOTransport::OnStateChanged(int32_t from, int32_t to) {
    if (this->GetData(GO_DATA_ALLOW_STOPPING) == 0) {
        return;
    }

    if (from == to) {
        this->m_path.SetStopped(this->m_time, to != 1);
    }

    this->m_path.SetStopped(this->m_time, to == 1);
}

// ref: FUN_00711b50
// Its stats are in: the building, the moving transports, the path built from the object's data and
// the server's period, the path synced, and the building placed.
void CGGameObjectMOTransport::OnStatsLoaded() {
    this->CreateMapObject();

    if (this->m_mapObject) {
        MovementLinkTransport(this->m_owner);
    }

    auto physics = g_transportPhysicsDB.GetRecord(this->GetData(GO_DATA_TRANSPORT_PHYSICS));
    int32_t accel = this->GetData(GO_DATA_ACCEL_RATE);
    int32_t speed = this->GetData(GO_DATA_MOVE_SPEED);
    int32_t path = this->GetData(GO_DATA_TAXI_PATH);

    this->m_path.Initialize(path, static_cast<float>(speed), static_cast<float>(accel), physics);
    this->m_path.SetLength(static_cast<uint32_t>(this->m_owner->GameObject()->level));
    this->SyncPath();

    if (this->m_mapObject) {
        C44Matrix world;
        this->m_owner->GetWorldMatrix(world);
        CWorld::SetDynamicObjectPlacement(this->m_mapObject, world);
    }
}

// ref: FUN_00710190
void CGGameObjectMOTransport::OnPostReenable() {
    this->CreateMapObject();

    if (this->m_mapObject) {
        MovementLinkTransport(this->m_owner);
    }

    this->SyncPath();
}

// ref: FUN_007100d0
void CGGameObjectMOTransport::SyncPath() {
    auto now = static_cast<uint32_t>(OsGetAsyncTimeMs());
    auto owner = this->m_owner;

    if (this->GetData(GO_DATA_ALLOW_STOPPING) != 0) {
        uint32_t time = static_cast<uint32_t>(owner->m_timeOffset) + now;
        auto data = owner->GameObject();

        this->m_path.SetProgress(time, static_cast<float>(data->animProgress) * 1.52590219e-05f);

        if (!(data->dynamicFlags & 0x10)) {
            this->m_path.SetStopped(time, data->state == 1);
        } else {
            this->m_path.StopNow(time);
        }
    }

    this->UpdateTransport(now, 0);
}

// ref: FUN_007134a0
// One movement poll: the path evaluated at the server's time; on another map only the active
// player's path time is kept. Otherwise the ship is placed by the path's point, heading, pitch and
// roll, its building follows (and once in, answers its sequences and plays the path's), and its
// passengers are carried.
//
// PARTIAL: the sequence's effect kit (FUN_0070b390, the ObjectEffect port's); and when the camera
// rides it onto another leg, the reference shows the path's loading screen (FUN_0040ae30) and
// reloads the world round the camera's target (FUN_00781500 -> FUN_007bd9f0, CWorld's synchronous
// reload), neither ported.
void CGGameObjectMOTransport::UpdateTransport(uint32_t time, int32_t elapsed) {
    auto owner = this->m_owner;
    bool first = this->m_animState == -1;

    C3Vector position = { 0.0f, 0.0f, 0.0f };
    int32_t mapID = -1;
    uint32_t sequence = 0;
    float facing = 0.0f;
    uint32_t leg = 0;
    float roll = 0.0f;
    float pitch = 0.0f;
    uint32_t at = static_cast<uint32_t>(owner->m_timeOffset) + time;

    this->m_path.Evaluate(at, static_cast<uint32_t>(elapsed), &mapID, &sequence, &position, &facing, &leg, &roll, &pitch, 0);

    if (static_cast<uint32_t>(mapID) != ClntObjMgrGetMapID()) {
        auto player = static_cast<CGObject_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_UNIT, ".\\GameObject_C.cpp", 0x75c));

        if (!player) {
            return;
        }

        if (player->GetTransportGUID() == owner->GetGUID()) {
            MovementSetTransportTime(this->m_adjustedTime);
        }

        return;
    }

    this->m_time = at;
    this->m_adjustedTime = this->AdjustTime(at);
    this->StepTo(static_cast<uint32_t>(elapsed), position);
    this->m_position = position;

    auto& world = owner->m_worldMatrix;
    world = C44Matrix();
    world.d0 = this->m_position.x;
    world.d1 = this->m_position.y;
    world.d2 = this->m_position.z;
    world.RotateAroundZ(facing);
    world.RotateAroundY(pitch);
    world.RotateAroundX(roll);

    owner->m_passenger.SetPackedRotation(C4Quaternion(world));
    owner->m_passenger.m_passengerFlags |= 0x2;

    if (this->m_mapObject) {
        CWorld::SetDynamicObjectPlacement(this->m_mapObject, world);

        if (!this->m_arrived && CWorld::DynamicObjectIsLoaded(this->m_mapObject)) {
            CWorld::SetDynamicObjectSequenceDone(this->m_mapObject, &TransportSequenceDone, 0);
            this->m_arrived = 1;
        }

        if (this->m_arrived && static_cast<uint32_t>(this->m_animState) != sequence) {
            CWorld::SetDynamicObjectSequence(this->m_mapObject, sequence, 0, 0);
            this->m_animState = static_cast<int32_t>(sequence);
        }
    }

    int32_t cameraRides = 0;
    this->MovePassengers(&cameraRides);

    if (this->m_leg != leg) {
        this->m_leg = leg;

        if (cameraRides && !first) {
            // The leg change's loading screen and world reload (see above).
        }
    }
}

// ---- type 35 ------------------------------------------------------------------------------------

// ref: FUN_00710460
// Open for any state but 0; shut, it lets go of the camera's target and drops the active player.
void CGGameObjectTrapDoor::OnStateChanged(int32_t from, int32_t to) {
    (void)from;

    if (!this->m_mapObject) {
        return;
    }

    auto owner = this->m_owner;

    if (to != 0) {
        CWorld::SetDynamicObjectSequence(this->m_mapObject, 0x92, 0, 0);
        owner->m_collidable = 1;
        CWorld::SetDynamicObjectCollides(this->m_mapObject, 1);
        return;
    }

    auto camera = CGWorldFrame::GetActiveCamera();

    for (auto passenger = this->m_passengers.Head(); passenger;) {
        auto next = this->m_passengers.Next(passenger);

        if (camera->GetTarget() == passenger->m_guid) {
            camera->SetRelativeTo(0);
        }

        if (passenger->m_guid == ClntObjMgrGetActivePlayer()) {
            auto move = static_cast<CMovementData_C*>(passenger);
            move->QueueFallIfUnsupported(static_cast<int32_t>(OsGetAsyncTimeMs()));
            move->SetSplineTransport(0, 0xff, 0);
        }

        passenger = next;
    }

    CWorld::SetDynamicObjectSequence(this->m_mapObject, 0x94, 0, 0);
    owner->m_collidable = 0;
    CWorld::SetDynamicObjectCollides(this->m_mapObject, 0);
}

// ref: FUN_0070d980
// Its stats are in: the building made, placed by the object's rotation at the transport's place,
// its sequences answering their ends, and shut unless the object's state says open.
void CGGameObjectTrapDoor::OnStatsLoaded() {
    this->CreateMapObject();

    if (!this->m_mapObject) {
        return;
    }

    MovementLinkTransport(this->m_owner);

    auto owner = this->m_owner;
    owner->m_worldMatrix = C44Matrix(owner->GetRotation());
    owner->m_worldMatrix.d0 = this->m_position.x;
    owner->m_worldMatrix.d1 = this->m_position.y;
    owner->m_worldMatrix.d2 = this->m_position.z;

    CWorld::SetDynamicObjectSequenceDone(this->m_mapObject, &TransportSequenceDone, 0);

    C44Matrix world;
    owner->GetWorldMatrix(world);
    CWorld::SetDynamicObjectPlacement(this->m_mapObject, world);

    if (owner->GameObject()->state != 1) {
        this->OnStateChanged(1, 0);
    }
}

// ref: FUN_0070b580
void CGGameObjectTrapDoor::OnPostReenable() {
    this->CreateMapObject();

    if (this->m_mapObject) {
        MovementLinkTransport(this->m_owner);
    }

    if (this->m_mapObject) {
        CWorld::SetDynamicObjectSequenceDone(this->m_mapObject, &TransportSequenceDone, 0);
    }
}

// ref: FUN_007137b0
// It does not move: once the building is in its passengers are placed on it once; before that it
// is kept at the object's place.
void CGGameObjectTrapDoor::UpdateTransport(uint32_t time, int32_t elapsed) {
    (void)time;
    (void)elapsed;

    if (!this->m_mapObject) {
        return;
    }

    if (!this->m_arrived && CWorld::DynamicObjectIsLoaded(this->m_mapObject)) {
        this->m_arrived = 1;

        int32_t cameraRides = 0;
        this->MovePassengers(&cameraRides);
        return;
    }

    C44Matrix world;
    this->m_owner->GetWorldMatrix(world);
    CWorld::SetDynamicObjectPlacement(this->m_mapObject, world);
}

// ---- type 31 ------------------------------------------------------------------------------------

static const int32_t GO_DATA_DIFFICULTY_MAP = 0x57;
static const int32_t GO_DATA_DIFFICULTY = 0x58;

// ref: FUN_00712820
CGGameObjectDungeonDifficulty::CGGameObjectDungeonDifficulty(CGGameObject_C* owner) : CGGameObjectType(owner, 5.0f) {
    this->m_sound = STORM_NEW(SOUNDKITOBJECT);
}

// ref: FUN_00712870
CGGameObjectDungeonDifficulty::~CGGameObjectDungeonDifficulty() {
    if (this->m_sound) {
        SI2::StopOrFadeOut(this->m_sound, 0, -1.0f, 1);
        this->m_sound->~SOUNDKITOBJECT();
        STORM_FREE(this->m_sound);
        this->m_sound = nullptr;
    }
}

// ref: FUN_0070dd50
// Half faded until the object is flagged lit (dynamic flag 0x2).
float CGGameObjectDungeonDifficulty::GetFadeInAlpha() {
    return (this->m_owner->GameObject()->dynamicFlags & 0x2) ? 1.0f : 0.5f;
}

// ref: FUN_0070dd70
// The model's events sound only while the object shows.
void CGGameObjectDungeonDifficulty::OnAnimEvent(uint32_t eventId, uint32_t data, const C3Vector* position, uint32_t a6) {
    (void)a6;

    int32_t hidden = 0;
    int32_t hiddenOther = 0;
    this->GetHidden(0, &hidden, &hiddenOther);

    if (hiddenOther == 0) {
        GameObjectHandleAnimEvent(eventId, data, position, this->m_sound, this->m_owner->GameObject()->displayID);
    }
}

// ref: FUN_00710a50
// Shown only at its own difficulty: a raid map's against the raid difficulty in force (on a map
// with dynamic difficulty, the map's form of it; otherwise also a heroic raid falling back to its
// normal size when the map has no heroic row), anything else against the dungeon difficulty.
void CGGameObjectDungeonDifficulty::UpdateFrame(uint32_t time) {
    (void)time;

    int32_t hide = 1;
    int32_t mapID = this->GetData(GO_DATA_DIFFICULTY_MAP);
    int32_t difficulty = this->GetData(GO_DATA_DIFFICULTY);
    auto map = mapID ? g_mapDB.GetRecord(mapID) : nullptr;

    if (map && map->m_instanceType == 2) {
        if (map->m_flags & 0x100) {
            if (static_cast<int32_t>(CGPartyInfo::GetEffectiveMapRaidDifficulty()) == difficulty) {
                hide = 0;
            }
        } else {
            auto raid = static_cast<int32_t>(CGPartyInfo::GetEffectiveRaidDifficulty());

            if (raid == difficulty) {
                hide = 0;
            } else if (2 <= static_cast<uint32_t>(raid) && raid - 2 == difficulty
                       && !MapDifficultyFind(mapID, raid, nullptr)) {
                hide = 0;
            }
        }
    } else if (static_cast<int32_t>(CGPartyInfo::GetEffectiveDungeonDifficulty()) == difficulty) {
        hide = 0;
    }

    CWorld::SetObjectHidden(this->m_owner->m_worldObject, hide);

    if (hide && SI2::IsPlaying(this->m_sound)) {
        SI2::StopOrFadeOut(this->m_sound, 0, -1.0f, 1);
    }
}

// ---- type 33 ------------------------------------------------------------------------------------

static const int32_t GO_DATA_DESTRUCTIBLE_DATA = 0x7c;

namespace {

// ref: FUN_0070b830
// The impact effect plays through (0x99 into 0x9a) and stops.
void DestructibleImpactDone(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner) {
    (void)boneId;
    (void)a5;
    (void)owner;

    if (a4 != 0) {
        return;
    }

    model->SetBoneSequence(-1, animId == 0x99 ? 0x9a : 0, -1, 0, 1.0f, 1, 1);
}

// ref: FUN_0070b870
// The ambient set loops: once its start has played it holds 0x9a.
void DestructibleAmbientDone(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner) {
    (void)boneId;
    (void)a5;
    (void)owner;

    if (a4 == 0 && animId != 0x9a) {
        model->SetBoneSequence(-1, 0x9a, -1, 0, 1.0f, 1, 1);
    }
}

// ref: FUN_0070b8b0
// An outgoing state's set plays out (0x99, 0x9a) into its end (0x9b), then stops.
void DestructibleOutgoingDone(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner) {
    (void)boneId;
    (void)a5;
    (void)owner;

    if (a4 != 0) {
        return;
    }

    if (animId == 0x99 || animId == 0x9a) {
        model->SetBoneSequence(-1, 0x9b, -1, 0, 1.0f, 1, 1);
    } else if (animId == 0x9b) {
        model->SetBoneSequence(-1, 0, -1, 0, 1.0f, 1, 1);
    }
}

// ref: FUN_0070b800
void DestructibleDestructionDone(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner) {
    (void)boneId;
    (void)a5;
    (void)owner;

    if (a4 == 0 && animId != 0) {
        model->SetBoneSequence(-1, 0, -1, 0, 1.0f, 1, 1);
    }
}

// ref: FUN_0070ca40
// A building doodad's model event goes to the game object it belongs to.
void DestructibleAnimEvent(CM2Model* model, uint32_t boneId, uint32_t eventId, uint32_t eventData,
                           const C3Vector* position, uint32_t a6, WOWGUID owner) {
    (void)model;
    (void)boneId;

    auto object = static_cast<CGGameObject_C*>(ClntObjMgrObjectPtr(owner, TYPE_GAMEOBJECT, ".\\GameObject_C.cpp", 0xd38));

    if (object) {
        object->m_type->OnAnimEvent(eventId, eventData, position, a6);
    }
}

const char* DisplayModelName(int32_t displayID) {
    auto display = g_gameObjectDisplayInfoDB.GetRecord(displayID);

    return display ? display->m_modelName : nullptr;
}

} // namespace

CGGameObjectDestructible::CGGameObjectDestructible(CGGameObject_C* owner) : CGGameObjectType(owner, 5.0f) {
    this->m_sound = STORM_NEW(SOUNDKITOBJECT);
}

// ref: FUN_00712980
// PARTIAL: the map's proxy (FUN_0077f290 -> FUN_0079eff0) is not kept; see PostInit.
CGGameObjectDestructible::~CGGameObjectDestructible() {
    for (auto& state : this->m_states) {
        if (state.m_object) {
            CWorld::RemoveDynamicObject(state.m_object);
            state.m_object = nullptr;
        }
    }

    if (this->m_rebuildFx) {
        CWorld::RemoveDynamicObject(this->m_rebuildFx);
        this->m_rebuildFx = nullptr;
    }

    this->m_mapObject = nullptr;

    if (this->m_collisionProxy) {
        CWorld::RemoveDynamicObject(this->m_collisionProxy);
        this->m_collisionProxy = nullptr;
    }

    if (this->m_sound) {
        SI2::StopOrFadeOut(this->m_sound, 0, -1.0f, 1);
        this->m_sound->m_sound.DetachWithLoopFade();
        this->m_sound->~SOUNDKITOBJECT();
        STORM_FREE(this->m_sound);
        this->m_sound = nullptr;
    }
}

// GO flags 0x200 and 0x400 (with 0x800 above them): 0 intact, 1 damaged, 2 destroyed, 3 rebuilding.
int32_t CGGameObjectDestructible::DamageState() const {
    return static_cast<int32_t>((this->m_owner->GameObject()->flags >> 8) & 0xe) >> 1;
}

// ref: FUN_0070b720
uint16_t CGGameObjectDestructible::ImpactSet(int32_t state) const {
    auto data = this->m_modelData;

    if (!data) {
        return 0;
    }

    switch (state) {
        case 0: return static_cast<uint16_t>(data->m_state0ImpactEffectDoodadSet);
        case 1: return static_cast<uint16_t>(data->m_state1ImpactEffectDoodadSet);
        case 2: return static_cast<uint16_t>(data->m_state2ImpactEffectDoodadSet);
        default: return 0;
    }
}

// ref: FUN_0070b760
uint16_t CGGameObjectDestructible::DestructionSet(int32_t state) const {
    auto data = this->m_modelData;

    if (!data) {
        return 0;
    }

    switch (state) {
        case 1: return static_cast<uint16_t>(data->m_state1DestructionDoodadSet);
        case 2: return static_cast<uint16_t>(data->m_state2DestructionDoodadSet);
        case 3: return static_cast<uint16_t>(data->m_state3InitDoodadSet);
        default: return 0;
    }
}

// ref: FUN_0070b7a0
uint16_t CGGameObjectDestructible::AmbientSet(int32_t state) const {
    auto data = this->m_modelData;

    if (!data) {
        return 0;
    }

    switch (state) {
        case 0: return static_cast<uint16_t>(data->m_state0AmbientDoodadSet);
        case 1: return static_cast<uint16_t>(data->m_state1AmbientDoodadSet);
        case 2: return static_cast<uint16_t>(data->m_state2AmbientDoodadSet);
        case 3: return static_cast<uint16_t>(data->m_state3AmbientDoodadSet);
        default: return 0;
    }
}

WOWGUID CGGameObjectDestructible::OwnerKey() const {
    return this->m_owner->GetGUID();
}

// ref: FUN_0070df30
// Not while the active player stands in it; otherwise unless its data forbids it.
bool CGGameObjectDestructible::CanHighlight() {
    auto player = static_cast<CGObject_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, ".\\Player_C.h", 0xa0));

    if (player && CWorld::GetObjectBuildingOwner(player->m_worldObject) == this->m_owner->GetGUID()) {
        return false;
    }

    return this->m_modelData && this->m_modelData->m_doNotHighlight == 0;
}

// ref: FUN_0070ca90
int32_t CGGameObjectDestructible::NoHighlight() {
    if (this->m_modelData && this->m_modelData->m_doNotHighlight != 0) {
        return 1;
    }

    return this->m_owner && this->m_owner->m_state != 1;
}

// ref: FUN_0070e6a0
// A damage flag changed: a hit (0x100) plays the state's impact set, then the state follows.
void CGGameObjectDestructible::OnFlagsChanged(uint32_t changed) {
    if (!this->m_modelData || !(changed & 0xf00)) {
        return;
    }

    if (changed & 0x100) {
        uint16_t set = this->ImpactSet(this->DamageState());

        if (set != 0) {
            CWorld::SetDynamicObjectDoodadSetShown(this->m_mapObject, 1, set);
            CWorld::SetDynamicObjectSequence(this->m_mapObject, 0x99, 0, set);
            CWorld::SetDynamicObjectSequenceDone(this->m_mapObject, &DestructibleImpactDone, set, set);
            CWorld::SetDynamicObjectAnimEvent(this->m_mapObject, &DestructibleAnimEvent, this->OwnerKey(), set);
        }
    }

    this->ChangeState();
}

// ref: FUN_0070ec80
void CGGameObjectDestructible::OnAnimEvent(uint32_t eventId, uint32_t data, const C3Vector* position, uint32_t a6) {
    (void)a6;

    GameObjectHandleAnimEvent(eventId, data, position, this->m_sound, this->m_owner->GameObject()->displayID);
}

// ref: FUN_0070ddd0
// The map's proxy for the building: where it stands, how far its geobox reaches, and its id.
//
// PARTIAL: the proxy is not handed to the map (FUN_0077f290 -> FUN_0079eff0, the
// CDestructibleProxy list the map's visibility walks read), so a map's own copy of the building
// is not hidden under it.
void CGGameObjectDestructible::PostInit(int32_t a4) {
    (void)a4;

    auto owner = this->m_owner;
    auto display = g_gameObjectDisplayInfoDB.GetRecord(owner->GameObject()->displayID);

    if (display) {
        this->m_proxyPosition = owner->m_passenger.GetPosition(owner->m_passenger.m_position);

        float x = std::max(display->m_geoBoxMin[0] * display->m_geoBoxMin[0], display->m_geoBoxMax[0] * display->m_geoBoxMax[0]);
        float y = std::max(display->m_geoBoxMin[1] * display->m_geoBoxMin[1], display->m_geoBoxMax[1] * display->m_geoBoxMax[1]);
        float z = std::max(display->m_geoBoxMin[2] * display->m_geoBoxMin[2], display->m_geoBoxMax[2] * display->m_geoBoxMax[2]);

        this->m_proxyRadius = std::sqrt(std::sqrt(z) * std::sqrt(z) + std::sqrt(y) * std::sqrt(y) + std::sqrt(x) * std::sqrt(x));

        uint32_t parent;
        std::memcpy(&parent, &owner->GameObject()->parentRotation[0], sizeof(parent));
        this->m_proxyID = parent == 0x5476ed ? 0x5476ed : 0;

        if (this->m_proxyRadius < 0.001f) {
            this->m_proxyRadius = 50.0f;
            SysMsgPrintf(SYSMSG_ERROR, "Destructible building WMO(%s) has invalid geobox", display->m_modelName);
        }
    }

    owner->UpdateWorldObject(0);
}

// ref: FUN_00710be0
// Its stats are in: the four states' buildings named (a state without one keeps the one before),
// each placed with its three effect sets and hidden, the current state's shown; the repair effect
// and the colliding intact copy.
//
// PARTIAL: the destructible's name plate (FUN_007e6320 / FUN_007e5fd0, the PlayerName port's), and
// the map's proxy (see PostInit).
void CGGameObjectDestructible::OnStatsLoaded() {
    auto owner = this->m_owner;

    this->m_states[0].m_displayID = owner->m_stats->m_displayID;
    auto name = DisplayModelName(this->m_states[0].m_displayID);

    if (!name) {
        SysMsgPrintf(SYSMSG_ERROR, "Destructible building - Game object id %d is missing its display record %d",
                     owner->GetEntryID(), owner->GameObject()->displayID);
    } else {
        this->m_states[0].m_name = name;
    }

    this->m_modelData = g_destructibleModelDataDB.GetRecord(this->GetData(GO_DATA_DESTRUCTIBLE_DATA));

    if (this->m_modelData) {
        this->m_states[1].m_displayID = this->m_modelData->m_state1Wmo;
        this->m_states[2].m_displayID = this->m_modelData->m_state2Wmo;
        this->m_states[3].m_displayID = this->m_modelData->m_state3Wmo;

        for (int32_t i = 1; i < 4; i++) {
            auto& state = this->m_states[i];
            const char* stateName = state.m_displayID ? DisplayModelName(state.m_displayID) : nullptr;

            if (state.m_displayID && !stateName) {
                SysMsgPrintf(SYSMSG_ERROR, "Destructible building - Game object id %d is missing its display record %d for state %d",
                             owner->GetEntryID(), owner->GameObject()->displayID, i);
            }

            if (stateName) {
                state.m_name = stateName;
            } else {
                state.m_displayID = this->m_states[i - 1].m_displayID;
                state.m_name = this->m_states[i - 1].m_name;
            }
        }
    }

    C3Vector position = owner->m_passenger.GetPosition(owner->m_passenger.m_position);

    for (int32_t i = 0; i < 4; i++) {
        auto& state = this->m_states[i];

        if (state.m_object || !state.m_name) {
            continue;
        }

        uint16_t sets[3] = { this->DestructionSet(i), this->ImpactSet(i), this->AmbientSet(i) };
        uint32_t id = i == 0 ? this->m_proxyID : 0;
        float facing = owner->GetFacing();

        state.m_object = CWorld::AddDynamicObject(state.m_name, position, facing, 0, 0, owner->GetGUID(), 3, sets, &this->m_proxyRadius, id);

        if (state.m_object) {
            CWorld::SetDynamicObjectShown(state.m_object, 0);
            CWorld::SetDynamicObjectCollides(state.m_object, 0);
        }

        state.m_loading = 1;
    }

    this->m_mapObject = this->m_states[this->DamageState()].m_object;

    if (this->m_mapObject) {
        CWorld::SetDynamicObjectShown(this->m_mapObject, 1);
        CWorld::SetDynamicObjectCollides(this->m_mapObject, 1);
    }

    this->m_loading = 1;

    if (this->m_modelData && this->m_modelData->m_repairGroundFx) {
        auto fxName = DisplayModelName(this->m_modelData->m_repairGroundFx);

        if (!fxName) {
            SysMsgPrintf(SYSMSG_ERROR, "Destructible building - Game object id %d is missing its rebuild FX display record %d",
                         owner->GetEntryID(), owner->GameObject()->displayID);
        } else {
            this->m_rebuildFx = CWorld::AddDynamicObject(fxName, position, owner->GetFacing(), 0, 0, 0, 0, nullptr, nullptr, 0);

            if (this->m_rebuildFx) {
                CWorld::SetDynamicObjectFlag2000(this->m_rebuildFx, 1);
                CWorld::SetDynamicObjectShown(this->m_rebuildFx, 0);
                CWorld::SetDynamicObjectCollides(this->m_rebuildFx, 0);
            }
        }
    }

    if (this->m_states[0].m_name) {
        this->m_collisionProxy = CWorld::AddDynamicObject(this->m_states[0].m_name, position, owner->GetFacing(), 0, 0,
                                                          owner->GetGUID(), 0, nullptr, nullptr, 0);

        if (this->m_collisionProxy) {
            CWorld::SetDynamicObjectShown(this->m_collisionProxy, 0);
            CWorld::SetDynamicObjectCollides(this->m_collisionProxy, 0);
        }
    }
}

// ref: FUN_0070b6e0
void CGGameObjectDestructible::OnDisable() {
    if (this->m_mapObject) {
        CWorld::SetDynamicObjectShown(this->m_mapObject, 0);
        CWorld::SetDynamicObjectCollides(this->m_mapObject, 0);
    }
}

// ref: FUN_0070b6a0
void CGGameObjectDestructible::OnPostReenable() {
    if (this->m_mapObject) {
        CWorld::SetDynamicObjectShown(this->m_mapObject, 1);
        CWorld::SetDynamicObjectCollides(this->m_mapObject, 1);
    }
}

// ref: FUN_0070dfa0
// The building follows the object's state: the new state's shown and colliding, the old one's not
// (a player caught inside a building that falls is dropped); the old state's impact and ambient sets
// play out in the new building; the state before that takes its sets back; then the new state's
// destruction set plays, or, entering it by rising, both buildings start to move; and its ambient
// set starts.
void CGGameObjectDestructible::ChangeState() {
    int32_t current = 0;

    while (current < 4 && this->m_states[current].m_object != this->m_mapObject) {
        current++;
    }

    int32_t next = this->DamageState();

    if (next == current) {
        return;
    }

    SI2::StopOrFadeOut(this->m_sound, 0, -1.0f, 1);

    auto& incoming = this->m_states[next];
    auto nextObject = incoming.m_object;

    if (nextObject) {
        CWorld::SetDynamicObjectShown(nextObject, 1);
        CWorld::SetDynamicObjectCollides(nextObject, 1);
    }

    if (this->m_mapObject) {
        CWorld::SetDynamicObjectShown(this->m_mapObject, 0);
        CWorld::SetDynamicObjectCollides(this->m_mapObject, 0);
    }

    if (this->m_mapObject && current < next && next != 3 && next != 0) {
        auto player = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_UNIT, ".\\GameObject_C.cpp", 0xe93));

        if (player && !player->IsTransportUnit() && !(player->m_localMove.m_moveFlags & 0x2000000)) {
            CAaBox box = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
            CWorld::GetDynamicObjectBounds(this->m_mapObject, box);

            C44Matrix world;
            this->m_owner->GetWorldMatrix(world);
            C3Vector local = player->GetPosition() * world.AffineInverse();

            // FUN_006cb930: strictly inside.
            if (box.b.x < local.x && box.b.y < local.y && box.b.z < local.z
                && local.x < box.t.x && local.y < box.t.y && local.z < box.t.z) {
                // FUN_007189f0
                player->m_localMove.QueueFallIfUnsupported(static_cast<int32_t>(OsGetAsyncTimeMs()));
            }
        }
    }

    uint16_t outgoing[2] = { this->ImpactSet(current), this->AmbientSet(current) };
    uint16_t base = static_cast<uint16_t>((current + 1) * 100);

    for (auto set : outgoing) {
        if (set == 0) {
            continue;
        }

        uint16_t moved = static_cast<uint16_t>(set + base);

        CWorld::SetDynamicObjectSequence(this->m_mapObject, 0x9b, 0, set);
        CWorld::SetDynamicObjectSequenceDone(this->m_mapObject, &DestructibleOutgoingDone, moved, set);
        CWorld::SetDynamicObjectAnimEvent(this->m_mapObject, &DestructibleAnimEvent, this->OwnerKey(), set);
        CWorld::MoveDynamicObjectDoodadSet(this->m_mapObject, set, nextObject, moved);
        CWorld::SetDynamicObjectDoodadSetShown(nextObject, 1, moved);
    }

    if (this->m_prevState != -1) {
        auto prevObject = this->m_states[this->m_prevState].m_object;
        uint16_t returning[2] = { this->ImpactSet(this->m_prevState), this->AmbientSet(this->m_prevState) };

        for (auto set : returning) {
            if (set == 0) {
                continue;
            }

            uint16_t moved = static_cast<uint16_t>((this->m_prevState + 1) * 100 + set);

            CWorld::MoveDynamicObjectDoodadSet(this->m_mapObject, moved, prevObject, set);
            CWorld::SetDynamicObjectSequenceDone(prevObject, nullptr, 0, set);
            CWorld::SetDynamicObjectDoodadSetShown(prevObject, 0, set);
            CWorld::SetDynamicObjectSequence(prevObject, 0, 0, set);
            CWorld::SetDynamicObjectAnimEvent(this->m_mapObject, &DestructibleAnimEvent, this->OwnerKey(), set);
        }
    }

    this->m_mapObject = nextObject;
    this->m_prevState = current;
    incoming.m_rises = (next == 0 || next == 3) ? 1 : 0;

    if (!this->m_mapObject || incoming.m_loading) {
        return;
    }

    auto data = this->m_modelData;

    if (!incoming.m_rises) {
        if (data) {
            uint16_t set = this->DestructionSet(this->DamageState());

            if (set != 0) {
                CWorld::SetDynamicObjectDoodadSetShown(this->m_mapObject, 1, set);
                CWorld::SetDynamicObjectSequence(this->m_mapObject, 0x99, 0, set);
                CWorld::SetDynamicObjectSequenceDone(this->m_mapObject, &DestructibleDestructionDone, set, set);
                CWorld::SetDynamicObjectAnimEvent(this->m_mapObject, &DestructibleAnimEvent, this->OwnerKey(), set);
            }
        }
    } else if (data) {
        float speed = 12.0f;

        if (0.0f < static_cast<float>(data->m_healEffectSpeed)) {
            speed = static_cast<float>(data->m_healEffectSpeed);
        }

        int32_t heal = data->m_healEffect;

        if (heal != 4 && 0.0f < speed) {
            CAaBox box = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
            CWorld::GetDynamicObjectBounds(this->m_mapObject, box);
            this->m_riseDepth = box.t.z - box.b.z;

            C3Vector position = this->m_owner->GetPosition();
            this->m_baseZ = position.z;
            CWorld::SetDynamicObjectPosition(this->m_mapObject, position, this->m_owner->GetFacing(), 0.0f, 0.0f, 0);

            this->m_rising = 1;
            this->m_riseStart = CWorld::GetCurTimeMs();
            this->m_riseDuration = static_cast<uint32_t>(static_cast<int64_t>(
                std::nearbyint(this->m_riseDepth / (speed * 0.33333334f) * 1000.0f)));

            if (this->m_prevState != -1 && heal != 3) {
                auto prevObject = this->m_states[this->m_prevState].m_object;
                CWorld::GetDynamicObjectBounds(prevObject, box);
                this->m_prevRiseDepth = box.t.z - box.b.z;
                CWorld::SetDynamicObjectShown(prevObject, 1);
                CWorld::SetDynamicObjectCollides(prevObject, 1);
            }

            if (this->m_collisionProxy) {
                CWorld::SetDynamicObjectCollides(this->m_collisionProxy, 1);
            }
        }
    }

    if (data) {
        uint16_t set = this->AmbientSet(this->DamageState());

        if (set != 0) {
            CWorld::SetDynamicObjectDoodadSetShown(this->m_mapObject, 1, set);
            CWorld::SetDynamicObjectSequence(this->m_mapObject, 0x99, 0, set);
            CWorld::SetDynamicObjectSequenceDone(this->m_mapObject, &DestructibleAmbientDone, set, set);
            CWorld::SetDynamicObjectAnimEvent(this->m_mapObject, &DestructibleAnimEvent, this->OwnerKey(), set);
        }
    }
}

// ref: FUN_0070e750
// While the buildings load, each that arrives has its effect sets paused and hidden (the shown one
// starts its ambient set); then a rise in progress moves the buildings, a rise at its end puts
// them in place.
void CGGameObjectDestructible::UpdateFrame(uint32_t time) {
    if (this->m_loading) {
        this->m_loading = 0;

        for (int32_t i = 0; i < 4; i++) {
            auto& state = this->m_states[i];

            if (!state.m_loading || !state.m_object) {
                continue;
            }

            if (!CWorld::DynamicObjectIsLoaded(state.m_object)) {
                this->m_loading = 1;
                continue;
            }

            state.m_loading = 0;

            uint16_t sets[3] = { this->DestructionSet(i), this->ImpactSet(i), this->AmbientSet(i) };

            for (auto set : sets) {
                if (set != 0) {
                    CWorld::SetDynamicObjectEmittersPaused(state.m_object, 1, set);
                    CWorld::SetDynamicObjectDoodadSetShown(state.m_object, 0, set);
                }
            }

            if (state.m_object == this->m_mapObject) {
                uint16_t set = this->AmbientSet(this->DamageState());

                if (set != 0) {
                    CWorld::SetDynamicObjectDoodadSetShown(this->m_mapObject, 1, set);
                    CWorld::SetDynamicObjectSequence(this->m_mapObject, 0x99, 0, set);
                    CWorld::SetDynamicObjectSequenceDone(this->m_mapObject, &DestructibleAmbientDone, set, set);
                    CWorld::SetDynamicObjectAnimEvent(this->m_mapObject, &DestructibleAnimEvent, this->OwnerKey(), set);
                }
            }
        }

        return;
    }

    if (!this->m_rising) {
        return;
    }

    auto owner = this->m_owner;
    float facing = owner->GetFacing();

    if (this->m_riseStart + this->m_riseDuration <= time) {
        if (this->m_rebuildFx) {
            C3Vector fx = owner->GetPosition();
            CWorld::SetDynamicObjectPosition(this->m_rebuildFx, fx, facing, 0.0f, 0.0f, 0);
            CWorld::SetDynamicObjectShown(this->m_rebuildFx, 0);
            CWorld::SetDynamicObjectCollides(this->m_rebuildFx, 0);
        }

        C3Vector position = owner->GetPosition();
        position.z = this->m_baseZ;
        CWorld::SetDynamicObjectPosition(this->m_mapObject, position, facing, 0.0f, 0.0f, 0);
        this->m_rising = 0;

        if (this->m_prevState != -1) {
            auto prevObject = this->m_states[this->m_prevState].m_object;
            CWorld::SetDynamicObjectShown(prevObject, 0);
            CWorld::SetDynamicObjectCollides(prevObject, 0);
            CWorld::SetDynamicObjectPosition(prevObject, position, facing, 0.0f, 0.0f, 0);
        }

        if (this->m_collisionProxy) {
            CWorld::SetDynamicObjectCollides(this->m_collisionProxy, 0);
        }

        return;
    }

    C3Vector here = owner->GetPosition();
    float f = static_cast<float>(time - this->m_riseStart) / static_cast<float>(this->m_riseDuration);
    int32_t heal = this->m_modelData ? this->m_modelData->m_healEffect : 4;
    auto prevObject = this->m_prevState != -1 ? this->m_states[this->m_prevState].m_object : nullptr;

    if (heal != 0) {
        // The new building rises out of the ground; with heal effect 2 the old one sinks into it.
        float g = 1.0f - f;
        C3Vector position = here;
        position.z = this->m_baseZ - (1.0f - (1.0f - g * g * g)) * this->m_riseDepth;
        CWorld::SetDynamicObjectPosition(this->m_mapObject, position, facing, 0.0f, 0.0f, 0);

        if (prevObject && heal == 2) {
            C3Vector old = owner->GetPosition();
            old.z = this->m_baseZ - f * f * f * this->m_prevRiseDepth;
            CWorld::SetDynamicObjectPosition(prevObject, old, facing, 0.0f, 0.0f, 0);
        }
    } else {
        // Heal effect 0: the old building sinks in the first half, the new rises in the second.
        float rise = 0.0f;

        if (0.5f < f) {
            float g = 1.0f - ((f - 0.5f) + (f - 0.5f));
            rise = 1.0f - g * g * g;
        }

        C3Vector position = here;
        position.z = this->m_baseZ - (1.0f - rise) * this->m_riseDepth;
        CWorld::SetDynamicObjectPosition(this->m_mapObject, position, facing, 0.0f, 0.0f, 0);

        if (prevObject) {
            float sink = 0.5f <= f ? 1.0f : f * f * f * 8.0f;
            C3Vector old = owner->GetPosition();
            old.z = this->m_baseZ - this->m_prevRiseDepth * sink;
            CWorld::SetDynamicObjectPosition(prevObject, old, facing, 0.0f, 0.0f, 0);
        }
    }

    if (this->m_rebuildFx) {
        CWorld::SetDynamicObjectShown(this->m_rebuildFx, 1);
        CWorld::SetDynamicObjectCollides(this->m_rebuildFx, 1);
        CWorld::SetDynamicObjectPosition(this->m_rebuildFx, here, facing, 0.0f, 0.0f, 0);
    }
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
