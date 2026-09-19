#include "object/client/Mirror.hpp"
#include "object/client/CGContainer.hpp"
#include "object/client/CGCorpse.hpp"
#include "object/client/CGDynamicObject.hpp"
#include "object/client/CGGameObject.hpp"
#include "object/client/CGItem.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/CGPlayer.hpp"
#include "object/client/CGUnit.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/Util.hpp"
#include "object/Types.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/Types.hpp"
#include <vector>
#include <common/DataStore.hpp>

#define MAX_CHANGE_MASKS 42

static uint32_t s_objMirrorBlocks[] = {
    CGObject::TotalFields(),
    CGItem::TotalFields(),
    CGContainer::TotalFields(),
    CGUnit::TotalFields(),
    CGPlayer::TotalFields(),
    CGGameObject::TotalFields(),
    CGDynamicObject::TotalFields(),
    CGCorpse::TotalFields(),
};

/**
 * Given a message data store, extract the dirty change masks contained inside. Any masks not
 * present are zeroed out. This function assumes the provided masks pointer has enough space for
 * MAX_CHANGE_MASKS.
 */
int32_t ExtractDirtyMasks(CDataStore* msg, uint8_t* maskCount, uint32_t* masks) {
    uint8_t count;
    msg->Get(count);

    *maskCount = count;

    if (count > MAX_CHANGE_MASKS) {
        return 0;
    }

    for (int32_t i = 0; i < count; i++) {
        msg->Get(masks[i]);
    }

    // Zero out masks that aren't present
    memset(&masks[count], 0, (MAX_CHANGE_MASKS - count) * sizeof(uint32_t));

    return 1;
}

/**
 * Given an object type hierarchy and GUID, return the number of DWORD blocks backing the object's
 * data storage.
 */
uint32_t GetNumDwordBlocks(OBJECT_TYPE type, WOWGUID guid) {
    switch (type) {
        case HIER_TYPE_OBJECT:
            return CGObject::TotalFields();

        case HIER_TYPE_ITEM:
            return CGItem::TotalFields();

        case HIER_TYPE_CONTAINER:
            return CGContainer::TotalFields();

        case HIER_TYPE_UNIT:
            return CGUnit::TotalFields();

        case HIER_TYPE_PLAYER:
            return guid == ClntObjMgrGetActivePlayer() ? CGPlayer::TotalFields() : CGPlayer::TotalRemoteFields();

        case HIER_TYPE_GAMEOBJECT:
            return CGGameObject::TotalFields();

        case HIER_TYPE_DYNAMICOBJECT:
            return CGDynamicObject::TotalFields();

        case HIER_TYPE_CORPSE:
            return CGCorpse::TotalFields();

        default:
            return 0;
    }
}

/**
 * Accounting for the full hierarchy of the given object, return the next inherited type ID after
 * the given current type ID. If there is no next inherited type, return NUM_CLIENT_OBJECT_TYPES
 * to indicate the end of the hierarchy.
 */
OBJECT_TYPE_ID IncTypeID(CGObject_C* object, OBJECT_TYPE_ID curTypeID) {
    switch (object->GetType()) {
        // ID_OBJECT -> ID_ITEM -> ID_CONTAINER
        case HIER_TYPE_ITEM:
        case HIER_TYPE_CONTAINER:
            if (curTypeID == ID_OBJECT) {
                return ID_ITEM;
            }

            if (curTypeID == ID_ITEM) {
                return ID_CONTAINER;
            }

            return NUM_CLIENT_OBJECT_TYPES;

        // ID_OBJECT -> ID_UNIT -> ID_PLAYER
        case HIER_TYPE_UNIT:
        case HIER_TYPE_PLAYER:
            if (curTypeID == ID_OBJECT) {
                return ID_UNIT;
            }

            if (curTypeID == ID_UNIT) {
                return ID_PLAYER;
            }

            return NUM_CLIENT_OBJECT_TYPES;

        // ID_OBJECT -> ID_GAMEOBJECT
        case HIER_TYPE_GAMEOBJECT:
            if (curTypeID == ID_OBJECT) {
                return ID_GAMEOBJECT;
            }

            return NUM_CLIENT_OBJECT_TYPES;

        // ID_OBJECT -> ID_DYNAMICOBJECT
        case HIER_TYPE_DYNAMICOBJECT:
            if (curTypeID == ID_OBJECT) {
                return ID_DYNAMICOBJECT;
            }

            return NUM_CLIENT_OBJECT_TYPES;

        // ID_OBJECT -> ID_CORPSE
        case HIER_TYPE_CORPSE:
            if (curTypeID == ID_OBJECT) {
                return ID_CORPSE;
            }

            return NUM_CLIENT_OBJECT_TYPES;

        default:
            return NUM_CLIENT_OBJECT_TYPES;
    }
}

int32_t IsMaskBitSet(uint32_t* masks, uint32_t block) {
    return masks[block / 32] & (1 << (block % 32));
}

namespace {

// Blocks whose value actually changed during this SMSG_UPDATE_OBJECT.
//
// The reference does not need this: each registered watcher keeps its own copy of the bytes it
// watches and compares against that (docs/ref/parity-mirror.md). Frozen has no watcher registry
// yet, and by the time the second pass runs the first has already stored the new value, so there
// is nothing left to compare against unless the change is recorded as it happens.
struct MirrorChange {
    WOWGUID guid;
    uint32_t block;
};

std::vector<MirrorChange> s_changes;

}

void MirrorBeginUpdate() {
    s_changes.clear();
}

void MirrorNoteChange(WOWGUID guid, uint32_t block) {
    s_changes.push_back({ guid, block });
}

namespace {

// The token FrameXML knows this unit by, or null when it has none. The reference resolves the whole
// set -- pet, focus, party1-4, raid1-40 -- from rosters frozen does not keep yet, so this answers
// for the two that the player and target frames need. A unit with no token is not skipped for being
// unimportant; it is skipped because there is no name to hand the event.
const char* UnitToken(const CGUnit_C* unit) {
    auto guid = unit->GetGUID();

    if (guid == ClntObjMgrGetActivePlayer()) {
        return "player";
    }

    auto player = CGPlayer_C::GetActivePtr();
    auto playerData = player ? player->Unit() : nullptr;

    if (playerData && playerData->target && guid == playerData->target) {
        return "target";
    }

    return nullptr;
}

// Signals the events the changed blocks of a unit stand for. Ranges are handled the way the
// reference's watchers do -- power and maxPower are seven dwords each and any of them means the
// same event -- so the arrays are compared as spans rather than seven separate cases.
void SignalUnitFieldEvents(CGUnit_C* unit, WOWGUID guid) {
    auto data = unit->Unit();

    if (!data) {
        return;
    }

    auto token = UnitToken(unit);

    if (!token) {
        return;
    }

    auto health = unit->BlockIndexOf(&data->health);
    auto maxHealth = unit->BlockIndexOf(&data->maxHealth);
    auto power = unit->BlockIndexOf(&data->power[0]);
    auto maxPower = unit->BlockIndexOf(&data->maxPower[0]);
    auto level = unit->BlockIndexOf(&data->level);
    auto faction = unit->BlockIndexOf(&data->factionTemplate);

    // power and maxPower are parallel arrays; take their length from the struct rather
    // than restating it, so adding a power type cannot silently narrow the range test.
    const uint32_t POWER_COUNT = sizeof(data->power) / sizeof(data->power[0]);

    // One event per kind however many of its dwords moved: a power tick that changes two entries
    // should not make FrameXML redraw twice.
    bool healthChanged = false;
    bool maxHealthChanged = false;
    bool powerChanged = false;
    bool maxPowerChanged = false;
    bool levelChanged = false;
    bool factionChanged = false;

    for (const auto& change : s_changes) {
        if (change.guid != guid) {
            continue;
        }

        auto block = change.block;

        if (block == health) {
            healthChanged = true;
        } else if (block == maxHealth) {
            maxHealthChanged = true;
        } else if (block >= power && block < power + POWER_COUNT) {
            powerChanged = true;
        } else if (block >= maxPower && block < maxPower + POWER_COUNT) {
            maxPowerChanged = true;
        } else if (block == level) {
            levelChanged = true;
        } else if (block == faction) {
            factionChanged = true;
        }
    }

    if (healthChanged) {
        FrameScript_SignalEvent(SCRIPT_UNIT_HEALTH, "%s", token);
    }

    if (maxHealthChanged) {
        FrameScript_SignalEvent(SCRIPT_UNIT_MAXHEALTH, "%s", token);
    }

    if (powerChanged) {
        FrameScript_SignalEvent(SCRIPT_UNIT_MANA, "%s", token);
    }

    if (maxPowerChanged) {
        FrameScript_SignalEvent(SCRIPT_UNIT_MAXMANA, "%s", token);
    }

    if (levelChanged) {
        FrameScript_SignalEvent(SCRIPT_UNIT_LEVEL, "%s", token);
    }

    if (factionChanged) {
        FrameScript_SignalEvent(SCRIPT_UNIT_FACTION, "%s", token);
    }
}

}

// ref: FUN_004d5550
// The second half of an object update. The first pass has already stored every changed field
// (UpdateObject -> FillInPartialObjectData); this pass re-reads the same bytes to run the per-field
// change handlers, which is where UNIT_HEALTH and the rest of the UNIT_* events come from.
//
// No handlers are dispatched yet, so nothing tells the interface a field moved and FrameXML -- which
// does not poll -- never redraws the unit frames. See docs/ref/parity-mirror.md.
//
// Reading each changed dword and discarding it is CORRECT here, not an oversight: the value is
// already applied. Adding object->SetBlock() to the loop below, which is what its sibling
// FillInPartialObjectData does and looks like the obvious one-line repair, would re-apply bytes the
// first pass has handled.
int32_t CallMirrorHandlers(CDataStore* msg, bool a2, WOWGUID guid) {
    if (!a2) {
        SmartGUID _guid;
        *msg >> _guid;

        guid = _guid;
    }

    auto object = FindActiveObject(guid);

    if (!object) {
        return SkipPartialObjectUpdate(msg);
    }

    uint8_t changeMaskCount;
    uint32_t changeMasks[MAX_CHANGE_MASKS];
    if (!ExtractDirtyMasks(msg, &changeMaskCount, changeMasks)) {
        return 0;
    }

    OBJECT_TYPE_ID typeID = ID_OBJECT;
    uint32_t blockOffset = 0;
    uint32_t numBlocks = GetNumDwordBlocks(object->GetType(), guid);

    for (int32_t block = 0; block < numBlocks; block++) {
        if (block >= s_objMirrorBlocks[typeID]) {
            blockOffset = s_objMirrorBlocks[typeID];
            typeID = IncTypeID(object, typeID);
        }

        if (IsMaskBitSet(changeMasks, block)) {
            // Read past it. The value is already stored; see the note above.
            uint32_t blockValue = 0;
            msg->Get(blockValue);
        }
    }

    // TODO the reference walks its watcher list here, one pass per block. Until that exists, the
    // unit fields the player and target frames depend on are signalled from the recorded change
    // set. Deliberately narrow: see docs/ref/parity-mirror.md.
    if (object->IsA(TYPE_UNIT)) {
        SignalUnitFieldEvents(static_cast<CGUnit_C*>(object), guid);
    }

    return 1;
}

int32_t FillInPartialObjectData(CGObject_C* object, WOWGUID guid, CDataStore* msg, bool forFullUpdate, bool zeroZeroBits) {
    uint8_t changeMaskCount;
    uint32_t changeMasks[MAX_CHANGE_MASKS];
    if (!ExtractDirtyMasks(msg, &changeMaskCount, changeMasks)) {
        return 0;
    }

    OBJECT_TYPE_ID typeID = ID_OBJECT;
    uint32_t blockOffset = 0;
    uint32_t numBlocks = GetNumDwordBlocks(object->GetType(), guid);

    for (int32_t block = 0; block < numBlocks; block++) {
        if (block >= s_objMirrorBlocks[typeID]) {
            blockOffset = s_objMirrorBlocks[typeID];
            typeID = IncTypeID(object, typeID);
        }

        if (!forFullUpdate) {
            // TODO
        }

        if (IsMaskBitSet(changeMasks, block)) {
            uint32_t blockValue;
            msg->GetArray(reinterpret_cast<uint8_t*>(&blockValue), sizeof(blockValue));

            // Before the write, while the old value is still readable. A field the server resends
            // unchanged is not a change, and signalling one would have FrameXML redraw on every
            // packet rather than on every difference.
            if (!forFullUpdate && object->GetBlock(block) != blockValue) {
                MirrorNoteChange(guid, block);
            }

            object->SetBlock(block, blockValue);
        } else if (zeroZeroBits) {
            object->SetBlock(block, 0);
        }
    }

    // TODO

    return 1;
}

int32_t SkipPartialObjectUpdate(CDataStore* msg) {
    uint8_t changeMaskCount;
    uint32_t changeMasks[MAX_CHANGE_MASKS];
    if (!ExtractDirtyMasks(msg, &changeMaskCount, changeMasks)) {
        return 0;
    }

    for (int32_t block = 0; block < changeMaskCount * 32; block++) {
        if (IsMaskBitSet(changeMasks, block)) {
            uint32_t blockValue;
            msg->Get(blockValue);
        }
    }

    return 1;
}
