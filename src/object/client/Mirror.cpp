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
#include "ui/game/CGGameUI.hpp"
#include "ui/game/ScriptUtil.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/Types.hpp"
#include <map>
#include <vector>
#include <cstring>
#include <storm/List.hpp>
#include <common/DataStore.hpp>

#define MAX_CHANGE_MASKS 42

// The most descriptor blocks any type has (the active player's), which is the stride of the
// handler table.
#define MIRROR_BLOCKS_PER_TYPE 0x52E

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

// ------------------------------------------------------------------------------------------------
// The field handlers
// ------------------------------------------------------------------------------------------------

namespace {

#include "object/client/MirrorSavedFields.inl"

// One registered field handler (RTTI CMirrorHandler, 0x30 bytes).
struct CMirrorHandler {
    TSLink<CMirrorHandler> m_link;      // +0x00, in its (type, block) list
    TSLink<CMirrorHandler> m_pending;   // +0x08, in an update's pending list
    MIRRORHANDLER m_handler = nullptr;  // +0x10
    void* m_param = nullptr;            // +0x14
    uint32_t m_countdown = 0;           // +0x18, blocks left in the field
    uint32_t m_offset = 0;              // +0x1c, byte offset in the object's storage
    uint32_t m_size = 0;                // +0x24
    int32_t m_a6 = 0;                   // +0x28
    uint8_t m_firing = 0;               // +0x2c
    uint8_t m_delete = 0;               // +0x2d
    uint8_t m_always = 0;               // +0x2e
    // Where the field's own type starts, which the offset the handler is told is relative to.
    uint32_t m_typeBase = 0;
};

typedef STORM_EXPLICIT_LIST(CMirrorHandler, m_link) HANDLER_LIST;
typedef STORM_EXPLICIT_LIST(CMirrorHandler, m_pending) PENDING_LIST;

// The handlers by type and by the block their field starts at (0x00b4b020). On the heap and never
// freed: the client keeps them for its life, and a static would run its destructor over freed
// Storm memory at exit.
HANDLER_LIST* s_handlers;

HANDLER_LIST& Handlers(uint32_t type, uint32_t block) {
    if (!s_handlers) {
        s_handlers = new HANDLER_LIST[NUM_CLIENT_OBJECT_TYPES * MIRROR_BLOCKS_PER_TYPE];
    }

    return s_handlers[type * MIRROR_BLOCKS_PER_TYPE + block];
}

// Where a type's fields start in the storage, in bytes (FUN_004d5ba0's switch).
uint32_t TypeBase(uint32_t type) {
    switch (type) {
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

// One object's own watchers, by the storage block their field starts at. The reference keeps a
// list per block inside the object (FUN_004d3d40); frozen keeps only the blocks that have one.
struct OBJECT_HANDLERS {
    std::map<uint32_t, HANDLER_LIST> blocks;
};

HANDLER_LIST* ObjectHandlers(CGObject_C* object, uint32_t block) {
    auto handlers = static_cast<OBJECT_HANDLERS*>(object->m_mirrorHandlers);

    if (!handlers) {
        return nullptr;
    }

    auto it = handlers->blocks.find(block);

    return it != handlers->blocks.end() ? &it->second : nullptr;
}

uint32_t FindSaved(const uint32_t* table, uint32_t count, uint32_t field, uint32_t missing) {
    for (uint32_t i = 0; i < count; i++) {
        if (table[i] == field) {
            return i;
        }
    }

    return missing;
}

// ref: FUN_004d43f0
// The byte in the saved array that keeps a copy of storage byte `offset` of a `type` object.
uint32_t SavedOffset(uint32_t type, uint32_t offset, bool activePlayer) {
    uint32_t base;
    uint32_t index;

    switch (type) {
        case ID_CONTAINER:
            if (0xFF < offset) {
                base = 3 + 0x2F;
                index = FindSaved(s_savedContainer, 0x48, (offset - 0x100) >> 2, 0x4A);
                break;
            }
            // fallthrough

        case ID_ITEM:
            if (0x17 < offset) {
                base = 3;
                index = FindSaved(s_savedItem, 0x2F, (offset - 0x18) >> 2, 0x3A);
                break;
            }

            base = 0;
            index = FindSaved(s_savedObject, 3, offset >> 2, 6);
            break;

        case ID_PLAYER:
            if (0x24F < offset) {
                base = 3 + 0x7B;
                index = FindSaved(s_savedPlayer, 0xAD, (offset - 0x250) >> 2, 0x49A);

                if (index == 0x49A && activePlayer) {
                    uint32_t extra = FindSaved(s_savedActivePlayer, 0x366, (offset - 0x250) >> 2, 0x49A);

                    if (extra != 0x49A) {
                        index = extra + 0xAD;
                    }
                }

                break;
            }
            // fallthrough

        case ID_UNIT:
            if (0x17 < offset) {
                base = 3;
                index = FindSaved(s_savedUnit, 0x7B, (offset - 0x18) >> 2, 0x8E);
                break;
            }

            base = 0;
            index = FindSaved(s_savedObject, 3, offset >> 2, 6);
            break;

        case ID_GAMEOBJECT:
            if (0x17 < offset) {
                base = 3;
                index = FindSaved(s_savedGameObject, 4, (offset - 0x18) >> 2, 0xC);
                break;
            }

            base = 0;
            index = FindSaved(s_savedObject, 3, offset >> 2, 6);
            break;

        case ID_CORPSE:
            if (0x17 < offset) {
                base = 3;
                index = FindSaved(s_savedCorpse, 3, (offset - 0x18) >> 2, 0x1E);
                break;
            }

            base = 0;
            index = FindSaved(s_savedObject, 3, offset >> 2, 6);
            break;

        default:
            base = 0;
            index = FindSaved(s_savedObject, 3, offset >> 2, 6);
            break;
    }

    return (offset & 3) + (base + index) * 4;
}

uint8_t* Storage(CGObject_C* object) {
    return reinterpret_cast<uint8_t*>(object->m_obj);
}

uint8_t* Saved(CGObject_C* object) {
    return reinterpret_cast<uint8_t*>(object->m_objSaved);
}

// ref: FUN_004d4850
// The handlers whose field has run out leave the pending list.
void AgePending(PENDING_LIST& pending) {
    for (auto handler = pending.Head(); handler; ) {
        auto next = pending.Next(handler);

        if (--handler->m_countdown == 0) {
            handler->m_pending.Unlink();
        }

        handler = next;
    }
}

// ref: FUN_004d5350
// The handlers whose field starts at this block join the pending list for as many blocks as the
// field covers.
bool AddPending(HANDLER_LIST& list, PENDING_LIST& pending) {
    if (!list.Head()) {
        return false;
    }

    for (auto handler = list.Head(); handler; handler = list.Next(handler)) {
        if (handler->m_pending.IsLinked()) {
            handler->m_pending.Unlink();
        }

        if (handler->m_a6 != 1) {
            pending.LinkToTail(handler);
        } else {
            pending.LinkToHead(handler);
        }

        handler->m_countdown = (handler->m_size + 3 + (handler->m_offset & 3)) >> 2;
    }

    return true;
}

// ref: FUN_004d52b0
// Before a block is overwritten, the fields that start in it are copied to the saved array.
void SaveFields(HANDLER_LIST& list, CGObject_C* object, bool activePlayer, uint32_t type) {
    for (auto handler = list.Head(); handler; handler = list.Next(handler)) {
        uint32_t saved = SavedOffset(type, handler->m_offset, activePlayer);
        memcpy(Saved(object) + saved, Storage(object) + handler->m_offset, handler->m_size);
    }
}

// ref: FUN_004d5150
// Every pending handler is called once, if its field differs from the saved copy (or always).
void Dispatch(PENDING_LIST& pending, WOWGUID guid, CGObject_C* object, uint32_t type, bool activePlayer) {
    for (auto handler = pending.Head(); handler; ) {
        auto next = pending.Next(handler);

        handler->m_firing = 1;
        handler->m_countdown = 1;

        const uint8_t* current = Storage(object) + handler->m_offset;
        const uint8_t* saved = Saved(object) + SavedOffset(type, handler->m_offset, activePlayer);

        if (handler->m_always || memcmp(current, saved, handler->m_size) != 0) {
            handler->m_handler(guid, handler->m_offset - handler->m_typeBase, handler->m_size, saved, handler->m_param);
        }

        handler->m_firing = 0;

        // Taken off while it ran (MirrorUnregisterObjectHandler).
        if (handler->m_delete) {
            handler->m_link.Unlink();
            handler->m_pending.Unlink();
            delete handler;
        }

        handler = next;
    }
}

} // namespace

// ref: FUN_004d5a80
void MirrorRegisterObjectHandler(WOWGUID guid, OBJECT_TYPE_ID type, uint32_t offset, uint32_t size,
                                 MIRRORHANDLER handler, void* param, int32_t a6, int32_t always) {
    // FUN_004d4bb0
    auto object = FindActiveObject(guid);

    if (!object) {
        return;
    }

    uint32_t base = TypeBase(type);

    auto handlers = static_cast<OBJECT_HANDLERS*>(object->m_mirrorHandlers);

    if (!handlers) {
        handlers = new OBJECT_HANDLERS();
        object->m_mirrorHandlers = handlers;
    }

    // FUN_004d5850
    auto mirror = new CMirrorHandler();
    mirror->m_offset = base + offset;
    mirror->m_size = size;
    mirror->m_handler = handler;
    mirror->m_param = param;
    mirror->m_a6 = a6;
    mirror->m_always = always != 0;
    mirror->m_typeBase = base;

    handlers->blocks[(base + offset) >> 2].LinkToTail(mirror);
}

// ref: FUN_004d5b40
void MirrorUnregisterObjectHandler(WOWGUID guid, OBJECT_TYPE_ID type, uint32_t offset, MIRRORHANDLER handler,
                                   void* param) {
    auto object = FindActiveObject(guid);

    if (!object) {
        return;
    }

    auto list = ObjectHandlers(object, (TypeBase(type) + offset) >> 2);

    if (!list) {
        return;
    }

    // FUN_004d5900
    for (auto mirror = list->Head(); mirror; mirror = list->Next(mirror)) {
        if (mirror->m_handler != handler || mirror->m_param != param) {
            continue;
        }

        if (mirror->m_firing) {
            mirror->m_delete = 1;
            return;
        }

        mirror->m_link.Unlink();

        if (mirror->m_pending.IsLinked()) {
            mirror->m_pending.Unlink();
        }

        delete mirror;
        return;
    }
}

void MirrorFreeObjectHandlers(CGObject_C* object) {
    auto handlers = static_cast<OBJECT_HANDLERS*>(object->m_mirrorHandlers);

    if (!handlers) {
        return;
    }

    for (auto& block : handlers->blocks) {
        while (auto mirror = block.second.Head()) {
            mirror->m_link.Unlink();

            if (mirror->m_pending.IsLinked()) {
                mirror->m_pending.Unlink();
            }

            delete mirror;
        }
    }

    delete handlers;
    object->m_mirrorHandlers = nullptr;
}

// ref: FUN_004d5ba0
void MirrorRegisterHandler(OBJECT_TYPE_ID type, uint32_t offset, uint32_t size, MIRRORHANDLER handler,
                           void* param, int32_t a6, int32_t always) {
    uint32_t base = TypeBase(type);

    // FUN_004d5850
    auto mirror = new CMirrorHandler();
    mirror->m_offset = base + offset;
    mirror->m_size = size;
    mirror->m_handler = handler;
    mirror->m_param = param;
    mirror->m_a6 = a6;
    mirror->m_always = always != 0;
    mirror->m_typeBase = base;

    Handlers(type, offset >> 2).LinkToTail(mirror);
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

// The token FrameXML knows this unit by. Script_GetTokenFromGUID is the one implementation, next
// to the resolver it is the inverse of.
const char* UnitToken(const CGUnit_C* unit) {
    return Script_GetTokenFromGUID(unit->GetGUID());
}

// Whether any block in [first, first + count) changed for this guid.
bool BlockRangeChanged(WOWGUID guid, uint32_t first, uint32_t count) {
    for (const auto& change : s_changes) {
        if (change.guid == guid && change.block >= first && change.block < first + count) {
            return true;
        }
    }

    return false;
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

    // Unit flags. The interface sees one event name for both of these; the client keeps two ids
    // and so does this, so a handler that cares which field moved can still tell.
    if (BlockRangeChanged(guid, unit->BlockIndexOf(&data->flags), 1)) {
        FrameScript_SignalEvent(SCRIPT_UNIT_FLAGS, "%s", token);
    }

    if (BlockRangeChanged(guid, unit->BlockIndexOf(&data->flags2), 1)) {
        FrameScript_SignalEvent(SCRIPT_UNIT_FLAGS2, "%s", token);
    }

    // Separate from the two above and separately eventful: this is the field that carries lootable,
    // tapped and tracked, which the unit frame and the world nameplates both read.
    if (BlockRangeChanged(guid, unit->BlockIndexOf(&data->dynamicFlags), 1)) {
        FrameScript_SignalEvent(SCRIPT_UNIT_DYNAMIC_FLAGS, "%s", token);
    }

    // The packed byte field, whose top byte is the power type -- Script_UnitPowerTypeOf reads it
    // from there. Signalled on any change to the dword rather than to that byte alone, because the
    // change set records which block moved and not what it held before. The other bytes are race,
    // class and gender, which do not change on a live unit, so the over-fire is theoretical.
    if (BlockRangeChanged(guid, unit->BlockIndexOf(&data->bytes0), 1)) {
        FrameScript_SignalEvent(SCRIPT_UNIT_DISPLAYPOWER, "%s", token);
    }

    // The three display ids sit together, and any of them changing is a model change: the base
    // display, the native one it falls back to, and the mount that replaces it. FrameXML rebuilds
    // the character portrait and the dress-up model from this, so missing one leaves a stale model
    // on screen rather than an obviously wrong number.
    if (BlockRangeChanged(guid, unit->BlockIndexOf(&data->displayID), 3)) {
        FrameScript_SignalEvent(SCRIPT_UNIT_MODEL_CHANGED, "%s", token);
    }

    // The target guid is eight bytes, so two blocks, and either may move on its own -- a target
    // change that only alters the high dword is still a target change.
    if (BlockRangeChanged(guid, unit->BlockIndexOf(&data->target), 2)) {
        FrameScript_SignalEvent(SCRIPT_UNIT_TARGET, "%s", token);

        // PLAYER_TARGET_CHANGED is the player's own target moving, and carries no argument. It is
        // what puts the target frame on screen at all, so it is worth having even while the unit
        // tokens beyond "player" and "target" are missing.
        if (unit->GetGUID() == ClntObjMgrGetActivePlayer()) {
            FrameScript_SignalEvent(SCRIPT_PLAYER_TARGET_CHANGED, nullptr);
        }
    }
}

// Fields that live on the player descriptor rather than the unit one. Only the active player has
// these sent to us at all, so there is no token question here.
void SignalPlayerFieldEvents(CGPlayer_C* player, WOWGUID guid) {
    auto data = player->Player();

    if (!data || guid != ClntObjMgrGetActivePlayer()) {
        return;
    }

    if (BlockRangeChanged(guid, player->BlockIndexOf(&data->xp), 1)
        || BlockRangeChanged(guid, player->BlockIndexOf(&data->nextLevelXP), 1)) {
        FrameScript_SignalEvent(SCRIPT_PLAYER_XP_UPDATE, nullptr);
    }

    if (BlockRangeChanged(guid, player->BlockIndexOf(&data->coinage), 1)) {
        FrameScript_SignalEvent(SCRIPT_PLAYER_MONEY, nullptr);
    }

    // Guild id and rank are adjacent, and either moving is a guild change -- joining sets both,
    // a promotion moves only the rank.
    if (BlockRangeChanged(guid, player->BlockIndexOf(&data->guildID), 2)) {
        FrameScript_SignalEvent(SCRIPT_PLAYER_GUILD_UPDATE, nullptr);
    }

    // Equipped slots and the backpack are both arrays of guids -- two descriptor blocks each -- so
    // the ranges are sized from the arrays rather than written as numbers. The character panel and
    // the paper doll redraw from the first; the bag windows from the second.
    //
    // The player token is hardcoded here rather than looked up: these fields only ever arrive for
    // the active player, since nobody else's inventory is sent.
    auto invBlocks = static_cast<uint32_t>(sizeof(data->invSlots) / sizeof(uint32_t));

    if (BlockRangeChanged(guid, player->BlockIndexOf(&data->invSlots[0]), invBlocks)) {
        FrameScript_SignalEvent(SCRIPT_UNIT_INVENTORY_CHANGED, "%s", "player");
    }

    auto packBlocks = static_cast<uint32_t>(sizeof(data->packSlots) / sizeof(uint32_t));

    if (BlockRangeChanged(guid, player->BlockIndexOf(&data->packSlots[0]), packBlocks)) {
        // Bag 0 is the backpack, which is what packSlots holds; the four equipped bags are objects
        // of their own and report through their own containers.
        FrameScript_SignalEvent(SCRIPT_BAG_UPDATE, "%d", 0);
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
    bool activePlayer = guid == ClntObjMgrGetActivePlayer();

    PENDING_LIST pending;

    for (int32_t block = 0; block < numBlocks; block++) {
        if (block >= s_objMirrorBlocks[typeID]) {
            blockOffset = s_objMirrorBlocks[typeID];
            typeID = IncTypeID(object, typeID);
        }

        AgePending(pending);
        AddPending(Handlers(typeID, block - blockOffset), pending);

        // The object's own watchers (FUN_004d3d40: the lists at +0x44 onwards).
        if (auto own = ObjectHandlers(object, block)) {
            AddPending(*own, pending);
        }

        if (IsMaskBitSet(changeMasks, block)) {
            // Read past it. The value is already stored; see the note above.
            uint32_t blockValue = 0;
            msg->Get(blockValue);

            Dispatch(pending, guid, object, typeID, activePlayer);
        } else if (a2) {
            Dispatch(pending, guid, object, typeID, activePlayer);
        }
    }

    // FUN_007cecd0
    while (auto handler = pending.Head()) {
        handler->m_pending.Unlink();
    }

    // FROZEN-ONLY until the unit and player field handlers are all registered: the events the
    // unit and player frames depend on, from the recorded change set. See docs/ref/parity-mirror.md.
    if (object->IsA(TYPE_UNIT)) {
        SignalUnitFieldEvents(static_cast<CGUnit_C*>(object), guid);
    }

    if (object->IsA(TYPE_PLAYER)) {
        SignalPlayerFieldEvents(static_cast<CGPlayer_C*>(object), guid);
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
            // The fields starting in this block keep their old value for the handlers
            // (FUN_004d53c0 -> FUN_004d52b0).
            auto& handlers = Handlers(typeID, block - blockOffset);

            if (handlers.Head()) {
                SaveFields(handlers, object, guid == ClntObjMgrGetActivePlayer(), typeID);
            }

            if (auto own = ObjectHandlers(object, block)) {
                SaveFields(*own, object, guid == ClntObjMgrGetActivePlayer(), typeID);
            }
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
