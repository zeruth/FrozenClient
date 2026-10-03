#ifndef OBJECT_CLIENT_OBJ_MGR_HPP
#define OBJECT_CLIENT_OBJ_MGR_HPP

#include "object/client/CGObject_C.hpp"
#include "object/client/ClntObjMgr.hpp"
#include "object/Types.hpp"
#include <cstdint>

CGObject_C* ClntObjMgrAllocObject(OBJECT_TYPE_ID typeID, WOWGUID guid);

uint32_t ClntObjMgrGetTypeFieldsOffset(OBJECT_TYPE_ID typeID);

int32_t ClntObjMgrEnumVisibleObjects(int32_t (*callback)(WOWGUID, void*), void* param);

WOWGUID ClntObjMgrGetActivePlayer();

void ClntObjMgrFreeObject(CGObject_C* object);

ClntObjMgr* ClntObjMgrGetCurrent();

uint32_t ClntObjMgrGetMapID();

PLAYER_TYPE ClntObjMgrGetPlayerType();

void ClntObjMgrInitializeShared();

void ClntObjMgrInitializeStd(uint32_t mapID);

void ClntObjMgrLinkInNewObject(CGObject_C* object);

// ref: FUN_004d4ca0
// The object stops being updated with the visible objects each frame.
void ClntObjMgrUnlinkVisible(WOWGUID guid);

// ref: FUN_004d4d00
// The object is updated with the visible objects each frame.
void ClntObjMgrLinkVisible(WOWGUID guid);

// ref: FUN_004d4d60
// Hold the object: while it is held an out-of-range only marks it to go (an animation that has to
// finish, a despawn that has to play).
void ClntObjMgrLockObject(WOWGUID guid);

// ref: FUN_004d77e0
// Let go of the object; the last hold let go of an object marked to go lets it go now.
void ClntObjMgrUnlockObject(WOWGUID guid, const char* fileName, int32_t lineNumber);

CGObject_C* ClntObjMgrObjectPtr(WOWGUID guid, OBJECT_TYPE type, const char* fileName, int32_t lineNumber);

void ClntObjMgrPop();

void ClntObjMgrPush(ClntObjMgr* mgr);

void ClntObjMgrSetActivePlayer(WOWGUID guid);

void ClntObjMgrSetHandlers();

#endif
