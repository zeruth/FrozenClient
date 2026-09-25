#include "object/client/ObjMgr.hpp"
#include "client/ClientServices.hpp"
#include "console/Command.hpp"
#include "net/Connection.hpp"
#include "object/client/CGContainer_C.hpp"
#include "object/client/CGCorpse_C.hpp"
#include "object/client/CGDynamicObject_C.hpp"
#include "object/client/CGGameObject_C.hpp"
#include "object/client/CGItem_C.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/MessageHandlers.hpp"
#include "object/client/Util.hpp"
#include "util/Unimplemented.hpp"
#include <common/ObjectAlloc.hpp>
#include <storm/Memory.hpp>

static bool s_heapsAllocated;
static uint32_t s_objHeapId[8];
static ClntObjMgr* s_savMgr;

#if defined(WHOA_SYSTEM_WIN)
static thread_local ClntObjMgr* s_curMgr;
#else
static ClntObjMgr* s_curMgr;
#endif

static uint32_t s_objTotalSize[] = {
    static_cast<uint32_t>(sizeof(CGObject_C)            + CGObject::GetDataSize()           + CGObject::GetDataSizeSaved()),
    static_cast<uint32_t>(sizeof(CGItem_C)              + CGItem::GetDataSize()             + CGItem::GetDataSizeSaved()),
    static_cast<uint32_t>(sizeof(CGContainer_C)         + CGContainer::GetDataSize()        + CGContainer::GetDataSizeSaved()),
    static_cast<uint32_t>(sizeof(CGUnit_C)              + CGUnit::GetDataSize()             + CGUnit::GetDataSizeSaved()),
    static_cast<uint32_t>(sizeof(CGPlayer_C)            + CGPlayer::GetRemoteDataSize()     + CGPlayer::GetRemoteDataSizeSaved()),
    static_cast<uint32_t>(sizeof(CGGameObject_C)        + CGGameObject::GetDataSize()       + CGGameObject::GetDataSizeSaved()),
    static_cast<uint32_t>(sizeof(CGDynamicObject_C)     + CGDynamicObject::GetDataSize()    + CGDynamicObject::GetDataSizeSaved()),
    static_cast<uint32_t>(sizeof(CGCorpse_C)            + CGCorpse::GetDataSize()           + CGCorpse::GetDataSizeSaved()),
};

static const char* s_objNames[] = {
    "CGObject_C",
    "CGItem_C",
    "CGContainer_C",
    "CGUnit_C",
    "CGPlayer_C",
    "CGGameObject_C",
    "CGDynamicObject_C",
    "CGCorpse_C",
};

static uint32_t s_heapSizes[] = {
    0,
    512,
    32,
    64,
    64,
    64,
    32,
    32,
};

int32_t CCommand_ObjUsage(const char* command, const char* arguments) {
    WHOA_UNIMPLEMENTED(0);
}

void MirrorInitialize() {
    // TODO
}

CGObject_C* ClntObjMgrAllocObject(OBJECT_TYPE_ID typeID, WOWGUID guid) {
    auto playerGUID = ClntObjMgrGetActivePlayer();

    // Heap allocate player object for current player
    if (guid == playerGUID) {
        return static_cast<CGObject_C*>(STORM_ALLOC(sizeof(CGPlayer_C) + CGPlayer::GetDataSize() + CGPlayer::GetDataSizeSaved()));
    }

    GarbageCollect(typeID, 10000);

    uint32_t memHandle;
    void* mem;

    if (!ObjectAlloc(s_objHeapId[typeID], &memHandle, &mem, false)) {
        return nullptr;
    }

    // TODO pointer should be fetched via ObjectPtr
    auto object = static_cast<CGObject_C*>(mem);
    object->m_memHandle = memHandle;

    return object;
}

void ClntObjMgrFreeObject(CGObject_C* object) {
    auto playerGUID = ClntObjMgrGetActivePlayer();
    auto isActivePlayer = object->GetGUID() == playerGUID;

    switch (object->GetType()) {
        case TYPE_OBJECT:
        case HIER_TYPE_ITEM:
        case HIER_TYPE_CONTAINER:
        case HIER_TYPE_UNIT:
        case HIER_TYPE_PLAYER:
        case HIER_TYPE_GAMEOBJECT:
        case HIER_TYPE_DYNAMICOBJECT:
        case HIER_TYPE_CORPSE: {
            object->~CGObject_C();

            break;
        }

        default: {
            break;
        }
    }

    if (isActivePlayer) {
        STORM_FREE(object);
    } else {
        ObjectFree(s_objHeapId[object->GetTypeID()], object->m_memHandle);
    }
}

// ref: FUN_004d3bf0
// The byte offset in an object's descriptor where the fields its own type adds begin: past the
// object fields for most types, past the item fields for a container and past the unit fields for
// a player. The reference takes the type in EAX.
uint32_t ClntObjMgrGetTypeFieldsOffset(OBJECT_TYPE_ID typeID) {
    switch (typeID) {
        case ID_ITEM:
        case ID_UNIT:
        case ID_GAMEOBJECT:
        case ID_DYNAMICOBJECT:
        case ID_CORPSE:
            return 0x18;

        case ID_CONTAINER:
            return 0x100;

        case ID_PLAYER:
            return 0x250;

        default:
            return 0;
    }
}

// ref: FUN_004d4b30
// Calls callback for every visible object in list order and stops at the first that returns 0.
int32_t ClntObjMgrEnumVisibleObjects(int32_t (*callback)(WOWGUID, void*), void* param) {
    auto mgr = ClntObjMgrGetCurrent();

    for (auto object = mgr->m_visibleObjects.Head(); object; object = mgr->m_visibleObjects.Next(object)) {
        if (!callback(object->GetGUID(), param)) {
            return 0;
        }
    }

    return 1;
}

WOWGUID ClntObjMgrGetActivePlayer() {
    if (!s_curMgr) {
        return 0;
    }

    return s_curMgr->m_activePlayer;
}

// ref: FUN_004d3730
ClntObjMgr* ClntObjMgrGetCurrent() {
    return s_curMgr;
}

uint32_t ClntObjMgrGetMapID() {
    if (!s_curMgr) {
        return 0;
    }

    return s_curMgr->m_mapID;
}

// ref: FUN_004d37c0
PLAYER_TYPE ClntObjMgrGetPlayerType() {
    return s_curMgr->m_type;
}

void ClntObjMgrInitializeShared() {
    if (!s_heapsAllocated) {
        for (int32_t i = ID_ITEM; i < NUM_CLIENT_OBJECT_TYPES; i++) {
            s_objHeapId[i] = ObjectAllocAddHeap(s_objTotalSize[i], s_heapSizes[i], s_objNames[i], true);
        }

        s_heapsAllocated = true;
    }

    MirrorInitialize();

    ConsoleCommandRegister("ObjUsage", &CCommand_ObjUsage, GAME, nullptr);
}

void ClntObjMgrInitializeStd(uint32_t mapID) {
    // TODO last instance time

    auto mgr = STORM_NEW(ClntObjMgr)(PLAYER_NORMAL);

    g_clientConnection->SetObjMgr(mgr);
    mgr->m_net = g_clientConnection;

    s_curMgr = mgr;

    ClntObjMgrSetHandlers();

    mgr->m_mapID = mapID;
}

void ClntObjMgrLinkInNewObject(CGObject_C* object) {
    CHashKeyGUID key(object->GetGUID());
    s_curMgr->m_objects.Insert(object, object->GetGUID(), key);
}

CGObject_C* ClntObjMgrObjectPtr(WOWGUID guid, OBJECT_TYPE type, const char* fileName, int32_t lineNumber) {
    if (!s_curMgr || !guid) {
        return nullptr;
    }

    auto object = FindActiveObject(guid);

    if (!object) {
        return nullptr;
    }

    if (!(object->GetType() & type)) {
        return nullptr;
    }

    return object;
}

void ClntObjMgrPop() {
    if (!s_savMgr) {
        return;
    }

    s_curMgr = s_savMgr;
    s_savMgr = nullptr;
}

void ClntObjMgrPush(ClntObjMgr* mgr) {
    if (s_savMgr || mgr == s_curMgr) {
        return;
    }

    s_savMgr = s_curMgr;
    s_curMgr = mgr;
}

void ClntObjMgrSetActivePlayer(WOWGUID guid) {
    s_curMgr->m_activePlayer = guid;
}

void ClntObjMgrSetHandlers() {
    s_curMgr->m_net->SetMessageHandler(SMSG_UPDATE_OBJECT, &ObjectUpdateHandler, nullptr);
    s_curMgr->m_net->SetMessageHandler(SMSG_COMPRESSED_UPDATE_OBJECT, &ObjectCompressedUpdateHandler, nullptr);
    s_curMgr->m_net->SetMessageHandler(SMSG_DESTROY_OBJECT, &OnObjectDestroy, nullptr);
}
