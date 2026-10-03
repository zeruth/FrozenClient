#ifndef OBJECT_CLIENT_MIRROR_HPP
#define OBJECT_CLIENT_MIRROR_HPP

#include "object/Types.hpp"
#include "util/GUID.hpp"
#include <cstdint>

class CDataStore;
class CGObject_C;

// What a field change calls: the object's guid, the field's byte offset within its type's
// fields, its size, the value it had before the update, and the parameter it was registered with.
typedef int32_t (*MIRRORHANDLER)(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param);

// ref: FUN_004d5ba0
// Call `handler` whenever the `size` bytes at `offset` within a `type` object's own fields change.
// `always` calls it for every update that touches the field, changed or not.
void MirrorRegisterHandler(OBJECT_TYPE_ID type, uint32_t offset, uint32_t size, MIRRORHANDLER handler,
                           void* param, int32_t a6, int32_t always);

void MirrorBeginUpdate();
void MirrorNoteChange(WOWGUID guid, uint32_t block);

int32_t CallMirrorHandlers(CDataStore* msg, bool a2, WOWGUID guid);

int32_t FillInPartialObjectData(CGObject_C* object, WOWGUID guid, CDataStore* msg, bool forFullUpdate, bool zeroZeroBits);

int32_t SkipPartialObjectUpdate(CDataStore* msg);

#endif
