#include "object/client/CGGameObject_C.hpp"
#include "object/client/ObjectEffect.hpp"
#include "client/ClientServices.hpp"
#include "db/Db.hpp"
#include "gx/Texture.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2Scene.hpp"
#include "model/CM2Shared.hpp"
#include "object/client/CEffect.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "object/client/GameObjectTypes.hpp"
#include "object/client/Mirror.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/SpellBook.hpp"
#include "ui/game/CGGameUI.hpp"
#include "util/Log.hpp"
#include "world/CWorld.hpp"
#include <common/DataStore.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <new>

float ClampRange(float value, float low, float high);

namespace {

// The custom animation states the pending index plays (0x00a335e8): none, Spawn, Custom0..3 and
// Despawn.
const int32_t s_customAnimStates[7] = { -1, 0, 8, 9, 10, 11, 12 };

CGGameObject_C* GameObjectPtr(WOWGUID guid, int32_t line) {
    return static_cast<CGGameObject_C*>(ClntObjMgrObjectPtr(guid, TYPE_GAMEOBJECT, ".\\GameObject_C.cpp", line));
}

CGPlayer_C* ActivePlayer() {
    return static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));
}

void ReleaseAttachedModel(CM2Model*& model) {
    if (!model) {
        return;
    }

    if (model->m_attachParent) {
        model->DetachFromParent();
    }

    model->Release();
    model = nullptr;
}

// The type behaviour the constructor makes for each GAMEOBJECT_TYPE_* (0x00714370 ..).
CGGameObjectType* CreateType(CGGameObject_C* object, uint8_t type) {
    CGGameObjectType* behaviour = nullptr;

    switch (type) {
        case 0:
            behaviour = STORM_NEW(CGGameObjectDoor)(object);
            break;

        case 1: {
            // FUN_00712560: a button.
            auto button = STORM_NEW(CGGameObjectAnimated)(object);
            button->m_useError = 0xEC;
            behaviour = button;
            break;
        }

        case 2:
            behaviour = STORM_NEW(CGGameObjectQuestGiver)(object);
            break;

        case 3:
        case 6:
        case 10:
        case 22:
            // FUN_00712770: chests, traps, goobers, spell casters.
            behaviour = STORM_NEW(CGGameObjectAnimated)(object);
            break;

        case 4:
            // FUN_0070c4e0: a binder.
            behaviour = STORM_NEW(CGGameObjectType)(object, 10.0f);
            break;

        case 5:
            behaviour = STORM_NEW(CGGameObjectGeneric)(object);
            break;

        case 7:
            behaviour = STORM_NEW(CGGameObjectChair)(object);
            break;

        case 8:
        case 16:
        case 25:
        case 30:
            behaviour = STORM_NEW(CGGameObjectSpellFocus)(object);
            break;

        case 9:
            behaviour = STORM_NEW(CGGameObjectText)(object);
            break;

        case 12: {
            // FUN_00712590: an area damage object, used from anywhere.
            auto area = STORM_NEW(CGGameObjectAnimated)(object);
            area->m_useRange = 0.0f;
            behaviour = area;
            break;
        }

        case 13:
            // FUN_0070c9a0: a camera.
            behaviour = STORM_NEW(CGGameObjectType)(object, 5.0f);
            break;

        case 17:
            behaviour = STORM_NEW(CGGameObjectFishingNode)(object);
            break;

        case 18:
            behaviour = STORM_NEW(CGGameObjectRitual)(object);
            break;

        case 19:
            behaviour = STORM_NEW(CGGameObjectMailbox)(object);
            break;

        case 23:
            behaviour = STORM_NEW(CGGameObjectMeetingStone)(object);
            break;

        case 24:
        case 26:
        case 27: {
            // FUN_007127b0: flag stands, flag drops and mini games.
            auto flag = STORM_NEW(CGGameObjectAnimated)(object);
            flag->m_useRange = 5.5555553f;
            behaviour = flag;
            break;
        }

        case 29:
            behaviour = STORM_NEW(CGGameObjectCapturePoint)(object);
            break;

        case 32:
            behaviour = STORM_NEW(CGGameObjectBarberChair)(object);
            break;

        case 34:
            behaviour = STORM_NEW(CGGameObjectGuildBank)(object);
            break;

        case 11:
            behaviour = STORM_NEW(CGGameObjectTransport)(object);
            break;

        case 14:
            behaviour = STORM_NEW(CGGameObjectMapObject)(object);
            break;

        case 15:
            behaviour = STORM_NEW(CGGameObjectMOTransport)(object);
            break;

        case 31:
            behaviour = STORM_NEW(CGGameObjectDungeonDifficulty)(object);
            break;

        case 35:
            behaviour = STORM_NEW(CGGameObjectTrapDoor)(object);
            break;

        case 33:
            behaviour = STORM_NEW(CGGameObjectDestructible)(object);
            break;

        default:
            SysMsgPrintf(SYSMSG_WARNING, "BADBASEGAMEOBJECT|%d", type);
            behaviour = STORM_NEW(CGGameObjectTypeUnknown)(object);
            break;
    }

    return behaviour;
}

// ref: FUN_0070be90
// SMSG_GAME_OBJECT_CUSTOM_ANIM: a guid and one of the four custom animations.
int32_t ReceiveCustomAnim(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WOWGUID guid;
    uint32_t anim;
    msg->Get(guid);
    msg->Get(anim);

    auto object = GameObjectPtr(guid, 100);

    if (object && anim < 4) {
        object->SetPendingCustomAnim(static_cast<int32_t>(anim));
    }

    return 1;
}

// ref: FUN_0070bef0
// SMSG_GAMEOBJECT_DESPAWN_ANIM: the object plays its despawn before it goes.
int32_t ReceiveDespawnAnim(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WOWGUID guid;
    msg->Get(guid);

    auto object = GameObjectPtr(guid, 0x80);

    if (!object) {
        return 1;
    }

    if (!object->IsModelLoaded()) {
        object->m_pendingAnim = 6;

        return 1;
    }

    object->m_type->PlayCustomAnim(12);
    object->m_pendingAnim = 0;

    return 1;
}

// ref: FUN_0070be30
// SMSG_PAGE_TEXT: the object's page to read.
int32_t ReceivePageText(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WOWGUID guid;
    msg->Get(guid);

    if (GameObjectPtr(guid, 0x4d)) {
        // TODO(ItemTextFrame): the page opens (FUN_0058a1a0(guid, 0)).
    }

    return 1;
}

// ref: FUN_00711050
int32_t OnStateChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto object = GameObjectPtr(guid, 0x1098);

    if (object) {
        object->UpdateState();
    }

    return 1;
}

// ref: FUN_0070ed00
int32_t OnFlagsChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto object = GameObjectPtr(guid, 0x10a6);

    if (object && object->m_type) {
        object->m_type->OnFlagsChanged(object->GameObject()->flags ^ *static_cast<const uint32_t*>(old));
    }

    return 1;
}

// ref: FUN_00713ed0
int32_t OnArtKitChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto object = GameObjectPtr(guid, 0x10b4);

    if (object && object->IsModelLoaded()) {
        object->ApplyArtKit(object->m_model);
    }

    return 1;
}

// ref: FUN_0070cb60
int32_t OnAnimProgressChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto object = GameObjectPtr(guid, 0x10c5);

    if (object && object->m_type) {
        object->m_type->OnAnimProgressChanged();
    }

    return 1;
}

// ref: FUN_00712b40
int32_t OnDynamicFlagsChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    auto object = GameObjectPtr(guid, 0x10d4);

    if (object && ((object->GameObject()->dynamicFlags ^ *static_cast<const uint16_t*>(old)) & 0x8)) {
        object->UpdateHighlightModel();
    }

    return 1;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Construction
// ------------------------------------------------------------------------------------------------

// ref: FUN_00714250
CGGameObject_C::CGGameObject_C(uint32_t time, CClientObjCreate& objCreate)
    : CGObject_C(time, objCreate)
    , m_passenger(objCreate.move.status.transport, objCreate.move.status.position18, this->m_obj->m_guid) {
    this->m_worldMatrix = C44Matrix();

    this->m_state = this->GameObject()->state;

    // UPDATEFLAG_ROTATION's packed rotation (create block +0x2d0).
    this->m_passenger.m_packedRotation = objCreate.uint2D4;
    // TODO the passenger's byte at +0x2c gains 2 here (0x00714336); what reads it is the
    // transports' (FUN_00711f20 sets the same bit).

    // The object is not walked with the visible objects until it has a model (PostInit).
    ClntObjMgrUnlinkVisible(this->GetGUID());

    // UPDATEFLAG_TRANSPORT's server time.
    if (objCreate.flags & 0x2) {
        this->m_timeOffset = static_cast<int32_t>(objCreate.uint2C0 - time);
    }

    this->m_type = CreateType(this, this->GameObject()->type);
    this->m_field214 = 0;
}

// ref: FUN_00712b80
CGGameObject_C::~CGGameObject_C() {
    if (!this->m_stats) {
        g_gameObjectCache.CancelCallback(DBCACHEKEY32(static_cast<uint32_t>(this->GetEntryID())), &CGGameObject_C::StatsCallback, this);
    }

    this->m_flag22 = 0;
    ReleaseAttachedModel(this->m_highlightModel);

    // TODO(PlayerName): a destructible building's name plate at +0xb0 is let go (FUN_007e6320).
    this->m_nameDesc = nullptr;

    if (this->m_type) {
        this->m_type->~CGGameObjectType();
        STORM_FREE(this->m_type);
        this->m_type = nullptr;
    }

    // TODO the twelve lists at +0x108 are emptied (not kept yet).
    this->m_transportLink.Unlink();
    this->m_passenger.m_transportLink.Unlink();
}

// ref: FUN_00712f30
void CGGameObject_C::PostInit(uint32_t time, const CClientObjCreate& init, bool a4) {
    this->m_passenger.JoinTransport(init.move.status.position28);

    this->CGObject_C::PostInit(time, init, a4);

    auto model = this->m_model;

    if (model) {
        ClntObjMgrLinkVisible(this->GetGUID());

        model->SetAnimEventCallback(&CGGameObject_C::AnimEventCallback, this->GetGUID());
        model->SetSequenceDoneCallback(&CGGameObject_C::SequenceDoneCallback, this->GetGUID());

        if (model->IsLoaded(0, 0)) {
            if (!model->m_shared->m_m2DataLoaded) {
                model->WaitForLoad(nullptr);
            }

            this->m_worldBox = TransformBox(model->m_shared->m_data->collisionBounds.extent, this->m_worldMatrix);

            if (this->m_type->UsesModelBounds()) {
                auto& box = this->m_worldBox;
                bool degenerate = !(box.b.x < box.t.x) || !(box.b.y < box.t.y) || !(box.b.z < box.t.z);
                this->m_collidable = degenerate ? 0 : 1;
            }
        }
    }

    this->m_type->PostInit(a4);

    WOWGUID guid = this->GetGUID();
    auto stats = g_gameObjectCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(this->GetEntryID())), &guid,
                                             &CGGameObject_C::StatsCallback, this, false);

    if (stats) {
        this->OnStatsLoaded(stats);
    }
}

void CGGameObject_C::SetStorage(uint32_t* storage, uint32_t* saved) {
    this->CGObject_C::SetStorage(storage, saved);

    this->m_gameObj = reinterpret_cast<CGGameObjectData*>(&storage[CGGameObject::GetBaseOffset()]);
    this->m_gameObjSaved = &saved[CGGameObject::GetBaseOffsetSaved()];
}

// ref: FUN_00712ae0
void CGGameObject_C::StatsCallback(uint32_t id, const WOWGUID* guid, void* param, bool found) {
    auto object = GameObjectPtr(*guid, 0x103a);

    auto stats = g_gameObjectCache.Peek(DBCACHEKEY32(id));

    if (object && stats) {
        object->OnStatsLoaded(stats);
    }
}

// ref: FUN_00712400
void CGGameObject_C::OnStatsLoaded(const GameObjectStats_C* stats) {
    this->m_stats = stats;

    this->m_type->OnStatsLoaded();

    auto display = g_gameObjectDisplayInfoDB.GetRecord(stats->m_displayID);

    if (display && display->m_objectEffectPackageID) {
        this->m_objectEffects = new CObjectEffect();
        this->m_objectEffects->Init(display->m_objectEffectPackageID, this);
    }

    this->UpdateHighlightModel();
}

// ------------------------------------------------------------------------------------------------
// The vtable, in slot order
// ------------------------------------------------------------------------------------------------

// ref: FUN_007130a0
void CGGameObject_C::Disable() {
    this->CGObject_C::Disable();

    if (!this->m_stats) {
        g_gameObjectCache.CancelCallback(DBCACHEKEY32(static_cast<uint32_t>(this->GetEntryID())), &CGGameObject_C::StatsCallback, this);
    } else {
        this->m_type->OnDisable();
    }

    // FUN_0079f820
    this->m_transportLink.Unlink();

    ReleaseAttachedModel(this->m_highlightModel);

    // TODO(PlayerName): the name plate at +0xb0 (FUN_007e6320).
    this->m_nameDesc = nullptr;
}

// ref: FUN_0070ed50
void CGGameObject_C::Reenable() {
    this->CGObject_C::Reenable();

    if (!this->m_stats) {
        ClntObjMgrUnlinkVisible(this->GetGUID());
    } else {
        this->m_type->OnReenable();
    }

    if (this->GameObject()->type == 33) {
        // TODO(PlayerName): a destructible building's name plate is made again (FUN_007e6320,
        // FUN_007e5fd0).
    }
}

// ref: FUN_00713130
void CGGameObject_C::PostReenable() {
    this->m_passenger.LeaveTransport();

    this->CGObject_C::PostReenable();

    if (this->m_stats) {
        this->m_type->OnPostReenable();
        this->UpdateHighlightModel();

        return;
    }

    WOWGUID guid = this->GetGUID();
    auto stats = g_gameObjectCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(this->GetEntryID())), &guid,
                                             &CGGameObject_C::StatsCallback, this, false);

    if (stats) {
        this->OnStatsLoaded(stats);
    }
}

// ref: FUN_0070cbe0
// The object's world matrix is its position, its full rotation and its scale; the model is placed
// by it here, not each frame, and the world entry takes the model's bounds.
void CGGameObject_C::UpdateWorldObject(int32_t noRelink) {
    this->m_worldMatrix = C44Matrix();
    this->m_worldMatrix.Translate(this->GetPosition());
    this->m_worldMatrix.Rotate(this->GetRotation());
    this->m_worldMatrix.Scale(this->GetBaseScale());

    CAaBox box = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    CAaSphere sphere = { { 0.0f, 0.0f, 0.0f }, 0.0f };
    C3Vector center = { 0.0f, 0.0f, 0.0f };

    auto model = this->m_model;

    if (model) {
        if (model->IsLoaded(0, 0)) {
            if (!model->m_shared->m_m2DataLoaded) {
                model->WaitForLoad(nullptr);
            }

            const CAaBox& collision = model->m_shared->m_data->collisionBounds.extent;
            this->m_worldBox = TransformBox(collision, this->m_worldMatrix);

            model->GetBoundingBox(box);
            model->GetBoundingSphere(sphere);

            center = {
                (collision.t.x + collision.b.x) * 0.5f,
                (collision.t.y + collision.b.y) * 0.5f,
                (collision.t.z + collision.b.z) * 0.5f
            };
        }

        // FUN_004d8630.
        model->m_flag8000 = 1;
        model->matrixB4 = this->m_worldMatrix;
    }

    if (this->m_worldObject) {
        CWorld::UpdateObject(this->m_worldObject, this->m_worldMatrix, box, sphere, center, noRelink, 0xFFFFFFFF);
    }
}

// ref: FUN_0070f940
C3Vector CGGameObject_C::GetHeadPosition() const {
    if (this->GameObject()->type == 33) {
        // TODO(World): a destructible building's head is over its current map object's bounds
        // (FUN_0070ec10, FUN_00780130).
    }

    C3Vector position = this->GetPosition();
    position.z += this->m_height * this->m_scale * 1.25f;

    return position;
}

// ref: FUN_00712cf0
C3Vector CGGameObject_C::GetPosition() const {
    if (!this->m_type) {
        return this->m_passenger.GetPosition(this->m_passenger.m_position);
    }

    return this->m_type->GetPosition();
}

// ref: FUN_00712d50
C3Vector CGGameObject_C::GetRawPosition() const {
    if (!this->m_type) {
        return this->m_passenger.m_position;
    }

    return this->m_type->GetRawPosition();
}

// ref: FUN_00712db0
float CGGameObject_C::GetFacing() const {
    if (!this->m_type) {
        return this->m_passenger.GetFacing(this->m_passenger.GetRotationFacing());
    }

    return this->m_type->GetFacing();
}

// ref: FUN_00712df0
float CGGameObject_C::GetRawFacing() const {
    if (!this->m_type) {
        return this->m_passenger.GetRotationFacing();
    }

    return this->m_type->GetRawFacing();
}

// ref: FUN_00706560
WOWGUID CGGameObject_C::GetTransportGUID() const {
    return this->m_passenger.m_transportGUID;
}

// ref: FUN_00712e20
C4Quaternion CGGameObject_C::GetRotation() const {
    return this->m_passenger.GetRotation();
}

// ref: FUN_0070f7b0
int32_t CGGameObject_C::CanHaveQuestStatus() {
    return this->GameObject()->type == 2;
}

// ref: FUN_007111a0
// A quest giver the player is not hostile to asks for its quest status; anything else drops its
// marker.
void CGGameObject_C::OnReenable() {
    auto player = ActivePlayer();

    if (!player) {
        return;
    }

    if (this->GetReaction(player) > 2 && this->CanHaveQuestStatus()) {
        // TODO(Player_C): CMSG_QUESTGIVER_STATUS_QUERY for the object (FUN_006d5080).
        return;
    }

    this->m_questStatus = 0;
    this->m_questMarkerOverride = 0;
    this->ReleaseQuestMarker();
}

// ref: FUN_007124a0
void CGGameObject_C::UpdateQuestMarker() {
    this->CGObject_C::UpdateQuestMarker();

    this->UpdateHighlightModel();
}

// ref: FUN_0070cf30
// The marker hangs at attachment 0x12, kept upright when the model leans it over.
void CGGameObject_C::AttachQuestMarker() {
    if (!this->m_questMarker || !this->m_model || !this->m_model->IsLoaded(0, 0)) {
        return;
    }

    if (this->m_questMarker->m_attachParent) {
        this->m_questMarker->DetachFromParent();
    }

    if (!this->m_model->HasAttachment(0x12)) {
        return;
    }

    this->m_questMarker->AttachToParent(this->m_model, 0x12, nullptr, 0);

    C44Matrix attach = this->m_model->GetAttachmentWorldTransform(0x12);

    if (attach.c1 < 0.99f) {
        C44Matrix upright = attach.AffineInverse();
        upright.d0 = 0.0f;
        upright.d1 = 0.0f;
        upright.d2 = 0.0f;

        // FUN_004d8630.
        this->m_questMarker->m_flag8000 = 1;
        this->m_questMarker->matrixB4 = upright;
    }

    this->ScaleQuestMarker();
    this->UpdateQuestMarkerSequence();
}

// ref: FUN_007110b0
CMapBaseObj* CGGameObject_C::AddMapObject(WOWGUID owner, int32_t wait, int32_t extraSetCount, const uint16_t* extraSets) {
    auto name = this->GetDisplayModelName();

    if (!name) {
        return nullptr;
    }

    C3Vector position = this->m_passenger.GetPosition(this->m_passenger.m_position);
    float facing = this->GetFacing();

    auto object = CWorld::AddDynamicObject(name, position, facing, wait, 0, owner, extraSetCount, extraSets, nullptr, 0);

    if (!object) {
        SysMsgPrintf(SYSMSG_ERROR, "Game object (%d): failed to load: \"%s\"", this->GetEntryID(), name);
    }

    return object;
}

// ref: FUN_0070ee80
// The transports, map objects, destructible buildings and trap doors draw a map object instead.
int32_t CGGameObject_C::GetModelFileName(const char*& name) const {
    switch (this->GameObject()->type) {
        case 11:
        case 14:
        case 15:
        case 33:
        case 35:
            return 0;

        default:
            name = this->GetDisplayModelName();

            return name != nullptr;
    }
}

// ref: FUN_00713f50
void CGGameObject_C::OnModelLoaded(CM2Model* model) {
    CAaBox bounds;
    model->GetBoundingBox(bounds);
    this->m_height = bounds.t.z - bounds.b.z;

    if (this->m_postInited && !this->m_disabled) {
        this->UpdateWorldObject(0);
    }

    if (this->m_type->UsesModelBounds()) {
        auto& box = this->m_worldBox;
        this->m_collidable = (box.b.x < box.t.x && box.b.y < box.t.y && box.b.z < box.t.z) ? 1 : 0;
    }

    this->ApplyArtKit(model);

    this->m_type->OnModelLoaded();

    this->AttachQuestMarker();

    if (this->m_flag22) {
        this->AttachHighlightModel();
    }

    if (this->m_pendingAnim) {
        if (this->IsModelLoaded()) {
            this->m_type->PlayCustomAnim(s_customAnimStates[this->m_pendingAnim]);
            this->m_pendingAnim = 0;
        }
    }

    model->m_flag4 = 1;
}

// ref: FUN_0070b930
int32_t CGGameObject_C::PlaceModel(float elapsed) {
    return this->m_type->PlaceModel(elapsed) != 0;
}

// ref: FUN_0070b960
void CGGameObject_C::GetHidden(uint32_t flags, int32_t* hidden, int32_t* hiddenOther) {
    this->CGObject_C::GetHidden(flags, hidden, hiddenOther);

    this->m_type->GetHidden(flags, hidden, hiddenOther);
}

// ref: FUN_00712e60
int32_t CGGameObject_C::Virtual09C() {
    return this->m_collidable || this->CanHighlight();
}

// ref: FUN_0070f550
int32_t CGGameObject_C::Virtual0A0(int32_t a2) {
    if ((a2 & 0x8000) && this->GameObject()->type == 0) {
        return 0;
    }

    return this->m_collidable;
}

// ref: FUN_0070f580
// Highlighted when the type says so -- and one tied to a quest only while the quest is in the
// player's log.
int32_t CGGameObject_C::CanHighlight() {
    if (!this->m_type->CanHighlight()) {
        return 0;
    }

    int32_t quest = this->GetData(GO_DATA_QUEST);

    if (!this->m_stats || quest <= 0) {
        return 1;
    }

    auto player = ActivePlayer();

    if (!player) {
        return 0;
    }

    for (int32_t i = 0; i < 25; i++) {
        auto entry = player->GetQuestLog(i);

        if (entry && entry->questID == quest) {
            return 1;
        }
    }

    return 0;
}

// ref: FUN_0070f630
int32_t CGGameObject_C::Virtual0AC() {
    return this->GetData(GO_DATA_TOOLTIP) != 0;
}

// ref: FUN_00711140
// The player uses the object.
void CGGameObject_C::Virtual0B0() {
    if (!this->m_type->CanUse()) {
        return;
    }

    // TODO FUN_00523640(1, 0, 0), then the player's FUN_006cde50(FUN_00513700()), come first;
    // neither is identified or ported.

    // ref: FUN_0070f680
    int32_t error = 0;
    float range = 0.0f;
    const char* spellName = nullptr;

    if (this->m_type->CanUseNow(&error, &range, &spellName)) {
        this->m_type->Use();

        CDataStore msg;
        msg.Put(static_cast<uint32_t>(CMSG_GAME_OBJ_REPORT_USE));
        msg.Put(this->GetGUID());
        msg.Finalize();
        ClientServices::Send(&msg);

        return;
    }

    if (error == 0xF0 && this->GameObject()->type != 17) {
        // TODO(Player_C): out of range, the player walks to the object (FUN_0072b730,
        // click-to-move) instead of hearing the error.
    }

    if (spellName) {
        CGGameUI::DisplayError(error, spellName);
    } else {
        CGGameUI::DisplayError(error);
    }
}

// ref: FUN_00712e80
int32_t CGGameObject_C::Virtual0B4() {
    return this->m_type->NoHighlight();
}

// ref: FUN_00712ee0
void CGGameObject_C::GetWorldMatrix(C44Matrix& matrix) const {
    matrix = this->m_worldMatrix;
}

// ref: FUN_0070cdf0
const char* CGGameObject_C::GetName() {
    if (this->m_stats) {
        return this->m_stats->m_names[0];
    }

    return "";
}

// ref: FUN_00710280
int32_t CGGameObject_C::Virtual0DC(int32_t a2) {
    return this->GetData(GO_DATA_PAGE);
}

// ref: FUN_0070b9e0
int32_t CGGameObject_C::Virtual0E4() {
    return this->m_type->FadesIn() ? 1 : 0;
}

// ref: FUN_0070b9a0
float CGGameObject_C::GetFadeInAlpha() {
    return this->m_type->GetFadeInAlpha();
}

// ref: FUN_00712f20
// GO_FLAG_TRANSPORT.
bool CGGameObject_C::Virtual0EC() {
    return (this->GameObject()->flags >> 3) & 1;
}

// ref: FUN_00712e90
int32_t CGGameObject_C::Virtual0F0(const C3Vector* position) {
    return this->m_type->Virtual06C(position);
}

// ref: FUN_00712eb0
int32_t CGGameObject_C::Virtual0F4(CPassenger* passenger, int32_t mode) {
    this->m_type->UpdatePassenger(passenger, mode);

    return 1;
}

// ref: FUN_00712f00
float CGGameObject_C::Virtual0F8() {
    if (!this->m_type) {
        return -1.0f;
    }

    return this->m_type->GetScaleMultiplier();
}

// ------------------------------------------------------------------------------------------------
// Members
// ------------------------------------------------------------------------------------------------

// ref: FUN_0070d040
void CGGameObject_C::UpdateForFrame(uint32_t time) {
    this->m_type->UpdateFrame(time);

    if (this->m_worldObject) {
        CWorld::UpdateObjectLighting(this->m_worldObject);
    }
}

// ref: FUN_0070cac0
void CGGameObject_C::AnimEventCallback(CM2Model* model, uint32_t boneId, uint32_t eventId,
                                       uint32_t eventData, const C3Vector* position, uint32_t a6,
                                       WOWGUID owner) {
    auto object = GameObjectPtr(owner, 0x104b);

    if (object) {
        object->m_type->OnAnimEvent(eventId, eventData, position, a6);
    }
}

// ref: FUN_0070cb10
void CGGameObject_C::SequenceDoneCallback(CM2Model* model, uint32_t boneId, uint32_t animId,
                                          int32_t a4, int32_t a5, WOWGUID owner) {
    auto object = GameObjectPtr(owner, 0x1059);

    if (!object) {
        return;
    }

    if (a4) {
        object->m_type->OnSequenceInterrupted();
    } else {
        object->m_type->OnSequenceDone();
    }
}

// ref: FUN_00711210
void CGGameObject_C::UpdateHighlightModel() {
    bool show = (this->GameObject()->dynamicFlags & 0x8) != 0;

    if (!show && ActivePlayer()) {
        // TODO(Player_C): a resource the player tracks sparkles too (FUN_006dca90: a fishing
        // hole, or a lock whose LockType bit is in PLAYER_TRACK_RESOURCES).
    }

    if (!show) {
        this->m_flag22 = 0;
        ReleaseAttachedModel(this->m_highlightModel);

        return;
    }

    this->m_flag22 = 1;
    this->AttachHighlightModel();
}

// ref: FUN_0070d080
void CGGameObject_C::AttachHighlightModel() {
    if (this->m_highlightModel || !this->m_model || !this->m_model->IsLoaded(0, 0)) {
        return;
    }

    auto effect = CEffect::GetHardcodedEffect(4);

    if (!effect) {
        return;
    }

    this->m_highlightModel = CWorld::GetM2Scene()->CreateModel(effect->m_fileName, 0);

    if (!this->m_highlightModel) {
        return;
    }

    this->m_highlightModel->AttachToParent(this->m_model, 0x13, nullptr, 1);

    float scale = ClampRange(this->GetBaseScale() * this->m_height * 1.15f, 1.0f, 5.0f);

    C44Matrix matrix;
    matrix.Scale(scale);

    this->m_highlightModel->m_flag8000 = 1;
    this->m_highlightModel->matrixB4 = matrix;
}

// ref: FUN_00713da0
void CGGameObject_C::ApplyArtKit(CM2Model* model) {
    auto kit = g_gameObjectArtKitDB.GetRecord(this->GameObject()->artKit);

    for (uint32_t id = 0; id < 4; id++) {
        model->DetachAllChildrenById(id);
    }

    if (!kit) {
        return;
    }

    CStatus status;
    CGxTexFlags flags(GxTex_LinearMipNearest, 1, 1, 0, 0, 0, 1, 0, 0, 0);

    for (uint32_t i = 0; i < 3; i++) {
        if (*kit->m_textureVariation[i]) {
            auto texture = TextureCreate(kit->m_textureVariation[i], flags, &status, 0);

            if (texture) {
                model->ReplaceTexture(i + 11, texture);
                HandleClose(texture);
            }
        }
    }

    for (uint32_t i = 0; i < 4; i++) {
        if (*kit->m_attachModel[i]) {
            auto attached = CWorld::GetM2Scene()->CreateModel(kit->m_attachModel[i], 0);

            if (attached) {
                attached->AttachToParent(model, i, nullptr, 0);
                attached->Release();
            }
        }
    }
}

// ref: FUN_0070f810
void CGGameObject_C::UpdateState() {
    int32_t state = this->GameObject()->state;
    int32_t previous = this->m_state;

    // An MO transport hears every state write, changed or not.
    if (state != previous || this->GameObject()->type == 15) {
        this->m_state = state;

        if (this->m_type) {
            this->m_type->OnStateChanged(previous, state);
        }
    }
}

// ref: FUN_0070bde0
void CGGameObject_C::SetPendingCustomAnim(int32_t index) {
    int32_t pending = index + 2;

    if (!this->IsModelLoaded()) {
        this->m_pendingAnim = pending;

        return;
    }

    this->m_type->PlayCustomAnim(s_customAnimStates[pending]);
    this->m_pendingAnim = 0;
}

// ref: FUN_0070d010
void CGGameObject_C::SetAnimProgress(float progress) {
    this->GameObject()->animProgress = static_cast<uint16_t>(lroundf(progress * 65535.0f));
}

// ref: FUN_0070ee30
const char* CGGameObject_C::GetDisplayModelName() const {
    int32_t displayID = this->GameObject()->displayID;
    auto display = g_gameObjectDisplayInfoDB.GetRecord(displayID);

    if (display) {
        return display->m_modelName;
    }

    SysMsgPrintf(SYSMSG_ERROR, "Game object id %d is missing its display record %d", this->GetEntryID(), displayID);

    return nullptr;
}

// ref: FUN_0070eef0
int32_t CGGameObject_C::GetData(int32_t meaning) const {
    int32_t index = GameObjectDataIndex(this->GameObject()->type, meaning);

    if (this->m_stats && index >= 0 && index < 24) {
        return this->m_stats->m_data[index];
    }

    return 0;
}

// ref: FUN_0070ef30
const LockRec* CGGameObject_C::GetLockRec() const {
    if (!this->m_stats) {
        return nullptr;
    }

    int32_t lock = this->GetData(GO_DATA_LOCK);

    if (lock == 0) {
        return nullptr;
    }

    return g_lockDB.GetRecord(lock);
}

// ref: FUN_0070ef90
bool CGGameObject_C::IsLockActionAllowed(int32_t action) const {
    int32_t state = this->m_state;

    if (action == 4) {
        return state == 2;
    }

    if (state == 2) {
        return false;
    }

    // Opening (0), closing (1) and the quick kinds (3) need the object shut; the rest any state.
    if ((action == 0 || action == 1 || action == 3) && state != 1) {
        return false;
    }

    if (action == 0) {
        return !((this->GameObject()->flags >> 1) & 1);
    }

    if (action == 1) {
        return (this->GameObject()->flags >> 1) & 1;
    }

    if (action == 2 && state != 0) {
        return false;
    }

    return true;
}

// ref: FUN_0070f160
bool CGGameObject_C::CheckLock(int32_t* spell, int32_t* skillValue, int32_t* skillNeeded, int32_t* item, uint32_t* entry) {
    auto player = ActivePlayer();
    auto lock = this->GetLockRec();

    if (!player || !lock) {
        return false;
    }

    bool needs = false;

    for (uint32_t i = 0; i < 8; i++) {
        switch (lock->m_type[i]) {
            case LockRec::TYPE_LOCKTYPE: {
                needs = true;

                if (!this->IsLockActionAllowed(lock->m_action[i])) {
                    break;
                }

                int32_t needed = lock->m_skill[i];

                if (needed == 0) {
                    needed = this->GameObject()->level * 5;
                }

                // A known spell that opens this kind of lock (SPELL_EFFECT_OPEN_LOCK naming the
                // LockType). The reference walks its known-spell list (0x00be8e18), then the spell
                // being targeted (FUN_007fd620 / FUN_007fd630).
                int32_t count = SpellBookCount();

                for (int32_t slot = 0; slot < count; slot++) {
                    auto rec = g_spellDB.GetRecord(SpellBookSpellAt(slot));

                    if (!rec) {
                        continue;
                    }

                    for (int32_t e = 0; e < 3; e++) {
                        if (rec->m_effect[e] != 33 || rec->m_effectMiscValue[e] != lock->m_index[i]) {
                            continue;
                        }

                        // TODO(Spell_C): the effect's skill value (FUN_008016c0) is the player's
                        // skill with the lock; frozen has no skill values yet, so it reads 0.
                        int32_t value = 0;

                        if (spell) {
                            *spell = rec->m_ID;
                        }

                        if (skillValue) {
                            *skillValue = value;
                        }

                        if (skillNeeded) {
                            *skillNeeded = needed;
                        }

                        if (needed <= value) {
                            if (entry) {
                                *entry = i;
                            }

                            return false;
                        }
                    }
                }

                break;
            }

            case LockRec::TYPE_SPELL: {
                needs = true;

                if (!this->IsLockActionAllowed(lock->m_action[i])) {
                    break;
                }

                auto rec = g_spellDB.GetRecord(lock->m_index[i]);

                for (int32_t e = 0; rec && e < 3; e++) {
                    if (rec->m_effect[e] == 33) {
                        if (spell) {
                            *spell = rec->m_ID;
                        }

                        if (entry) {
                            *entry = i;
                        }

                        return false;
                    }
                }

                break;
            }

            case LockRec::TYPE_ITEM: {
                needs = true;

                // TODO(Bag_C): the key in the player's bags (FUN_00754a20) opens it, through the
                // item's open-lock spell (FUN_00707c60(33)); bags are not ported, so no key is
                // ever found.
                break;
            }

            default:
                break;
        }
    }

    return needs;
}

// ref: FUN_0070edd0
int32_t CGGameObject_C::GetReaction(CGUnit_C* unit) {
    auto data = this->GameObject();
    auto creator = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(data->createdBy, TYPE_UNIT, ".\\GameObject_C.cpp", 0x124b));

    if (creator) {
        return creator->GetReaction(unit);
    }

    if (data->faction) {
        // PHASE4(Unit_C): CGUnit_C::GetReaction(faction, unit) also weighs the player's
        // reputation; the faction templates' answer is what it falls back to.
        auto a = g_factionTemplateDB.GetRecord(data->faction);
        auto b = unit ? g_factionTemplateDB.GetRecord(unit->Unit()->factionTemplate) : nullptr;

        if (!a || !b) {
            return 3;
        }

        return CGUnit_C::GetFactionTemplateReaction(a, b);
    }

    return 3;
}

// ref: FUN_00710340
bool CGGameObject_C::IsUsableInCombat() const {
    int32_t index = GameObjectDataIndex(this->GameObject()->type, 62);

    if (this->m_stats && index >= 0 && index < 24) {
        return this->m_stats->m_data[index] == 0;
    }

    return true;
}

// ref: FUN_007103c0
bool CGGameObject_C::IsUsableUnderFlag31() const {
    if (this->GameObject()->type == 3) {
        return false;
    }

    int32_t index = GameObjectDataIndex(this->GameObject()->type, 57);

    if (this->m_stats && index >= 0 && index < 24) {
        return this->m_stats->m_data[index] == 0;
    }

    return true;
}

// ref: FUN_00710390
bool CGGameObject_C::IsUsableMounted() const {
    switch (this->GameObject()->type) {
        case 19:
            return true;

        case 32:
            return false;

        default:
            return this->GetData(91) != 0;
    }
}

// ------------------------------------------------------------------------------------------------
// Start and end
// ------------------------------------------------------------------------------------------------

// ref: FUN_007140a0
void GameObjectInitialize() {
    static bool registered = false;

    ClientServices::SetMessageHandler(SMSG_PAGE_TEXT, &ReceivePageText, nullptr);
    ClientServices::SetMessageHandler(SMSG_GAME_OBJECT_CUSTOM_ANIM, &ReceiveCustomAnim, nullptr);
    ClientServices::SetMessageHandler(SMSG_GAMEOBJECT_DESPAWN_ANIM, &ReceiveDespawnAnim, nullptr);

    // The reference undoes these in GameObjectShutdown; the mirror has no unregister yet, so a
    // second game in the same session registers nothing twice.
    if (registered) {
        return;
    }

    registered = true;

    MirrorRegisterHandler(ID_GAMEOBJECT, 0x2C, 1, &OnStateChanged, nullptr, 0, 1);
    MirrorRegisterHandler(ID_GAMEOBJECT, 0x0C, 4, &OnFlagsChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_GAMEOBJECT, 0x2E, 1, &OnArtKitChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_GAMEOBJECT, 0x22, 2, &OnAnimProgressChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_GAMEOBJECT, 0x20, 2, &OnDynamicFlagsChanged, nullptr, 0, 0);
}

// ref: FUN_00714150
void GameObjectShutdown() {
    ClientServices::ClearMessageHandler(SMSG_PAGE_TEXT);
    ClientServices::ClearMessageHandler(SMSG_GAME_OBJECT_CUSTOM_ANIM);
    ClientServices::ClearMessageHandler(SMSG_GAMEOBJECT_DESPAWN_ANIM);

    // TODO(ObjectMgrClient): the five field handlers come off (FUN_004d5c40); the mirror has no
    // unregister yet.
}
