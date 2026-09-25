#include "ui/game/CGMinimapFrame.hpp"
#include <common/DataStore.hpp>
#include "client/ClientServices.hpp"
#include "ui/game/ScriptUtil.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/AuraCache.hpp"
#include "object/client/SpellBook.hpp"
#include "db/Db.hpp"
#include "ui/game/CGMinimapFrameScript.hpp"
#include "console/CVar.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/Types.hpp"

HTEXTURE CGMinimapFrame::s_unknownTexture;
HTEXTURE CGMinimapFrame::s_overlayTextures[7];
// ref: UNK_00a41e04 / UNK_00a41e1c
const int32_t CGMinimapFrame::s_zoomChunksOutdoor[6] = { 14, 12, 10, 8, 6, 4 };
const float CGMinimapFrame::s_zoomRadiusIndoor[6] = { 150.0f, 120.0f, 90.0f, 60.0f, 40.0f, 25.0f };
const float CGMinimapFrame::s_chunkYards = 33.33333206176758f;
int32_t CGMinimapFrame::s_metatable;
int32_t CGMinimapFrame::s_objectType;

HTEXTURE CGMinimapFrame::s_staticPOIArrowTexture = nullptr;
HTEXTURE CGMinimapFrame::s_corpsePOIArrowTexture = nullptr;
HTEXTURE CGMinimapFrame::s_poiArrowTexture = nullptr;
HTEXTURE CGMinimapFrame::s_maskTexture = nullptr;
HTEXTURE CGMinimapFrame::s_classBlipTexture = nullptr;
HTEXTURE CGMinimapFrame::s_blipTexture = nullptr;
HTEXTURE CGMinimapFrame::s_iconTexture = nullptr;

uint32_t CGMinimapFrame::s_zoom[2] = { 3, 3 };
uint8_t CGMinimapFrame::s_indoors = 0;
const uint32_t CGMinimapFrame::s_zoomLevels = 6;

// ref: DAT_00a11c50
// Read out of the reference's .rdata. The NPC flag values are the ones the minimap matches against
// UNIT_NPC_FLAGS; mailbox is a game object type rather than a flag, and trivial quests are neither.
const MINIMAP_TRACKING_TYPE CGMinimapFrame::s_trackingTypes[NUM_MINIMAP_TRACKING_TYPES] = {
    { 1, 0x00001000, "MINIMAP_TRACKING_REPAIR",             "Repair",        0x00000000 },
    { 1, 0x00000200, "MINIMAP_TRACKING_VENDOR_FOOD",        "Food",          0x00000000 },
    { 1, 0x00000400, "MINIMAP_TRACKING_VENDOR_POISON",      "Poisons",       0x00000010 },
    { 1, 0x00000100, "MINIMAP_TRACKING_VENDOR_AMMO",        "Ammunition",    0x0000001a },
    { 1, 0x00000800, "MINIMAP_TRACKING_VENDOR_REAGENT",     "Reagents",      0x00000000 },
    { 1, 0x00010000, "MINIMAP_TRACKING_INNKEEPER",          "Innkeeper",     0x00000000 },
    { 1, 0x00002000, "MINIMAP_TRACKING_FLIGHTMASTER",       "FlightMaster",  0x00000000 },
    { 1, 0x00400000, "MINIMAP_TRACKING_STABLEMASTER",       "StableMaster",  0x00000008 },
    { 1, 0x00100000, "MINIMAP_TRACKING_BATTLEMASTER",       "BattleMaster",  0x00000000 },
    { 1, 0x00000020, "MINIMAP_TRACKING_TRAINER_CLASS",      "Class",         0x00000000 },
    { 1, 0x00000040, "MINIMAP_TRACKING_TRAINER_PROFESSION", "Profession",    0x00000000 },
    { 1, 0x00200000, "MINIMAP_TRACKING_AUCTIONEER",         "Auctioneer",    0x00000000 },
    { 1, 0x00020000, "MINIMAP_TRACKING_BANKER",             "Banker",        0x00000000 },
    { 2, 0x00000013, "MINIMAP_TRACKING_MAILBOX",            "Mailbox",       0x00000000 },
    { 3, 0x00000000, "MINIMAP_TRACKING_TRIVIAL_QUESTS",     "TrivialQuests", 0x00000000 },
};

const MINIMAP_TRACKING_TYPE* CGMinimapFrame::s_otherTracking = nullptr;
// ref: FUN_007fdf60
// A spell is a tracking spell when any of its three effect auras is one of the three tracking
// aura types. Nothing else about the spell matters -- not its school, not its category.
bool CGMinimapFrame::IsTrackingSpell(uint32_t spellID) {
    auto spell = g_spellDB.GetRecord(static_cast<int32_t>(spellID));

    if (!spell) {
        return false;
    }

    for (auto aura : spell->m_effectAura) {
        if (aura == 44 || aura == 45 || aura == 151) {
            return true;
        }
    }

    return false;
}

// Walked from the spellbook rather than kept alongside it. The reference appends to its list when
// a spell is learned and removes on unlearn; frozen has the known-spell list already, so deriving
// is the same answer without a second thing to keep in step.
//
// The all-ranks view is used, so a player who knows two ranks of a tracking spell sees both -- as
// the reference does, since it appends every learned spell that matches.
uint32_t CGMinimapFrame::GetNumTrackingSpells() {
    uint32_t count = 0;

    for (int32_t i = 0; i < SpellBookCount(); i++) {
        if (CGMinimapFrame::IsTrackingSpell(SpellBookSpellAt(i))) {
            count++;
        }
    }

    return count;
}

uint32_t CGMinimapFrame::GetTrackingSpell(uint32_t index) {
    uint32_t seen = 0;

    for (int32_t i = 0; i < SpellBookCount(); i++) {
        auto spellID = SpellBookSpellAt(i);

        if (CGMinimapFrame::IsTrackingSpell(spellID)) {
            if (seen == index) {
                return spellID;
            }

            seen++;
        }
    }

    return 0;
}

// ref: FUN_0053bd10
// Walks the list from its end, as the reference does.
int32_t CGMinimapFrame::HasTrackingSpell(uint32_t spellID) {
    uint32_t index = CGMinimapFrame::GetNumTrackingSpells();

    do {
        if (index == 0) {
            return 0;
        }

        index--;
    } while (spellID != CGMinimapFrame::GetTrackingSpell(index));

    return 1;
}

uint32_t CGMinimapFrame::s_trackingSpell = 0;

namespace {

// The reference reads the class off the active player and shifts by it directly, so the bit for
// warrior (class 1) is 0x02, not 0x01. With no player it uses a mask of zero, which leaves only the
// rows that are open to everyone -- so the guard here is not a defensive extra, it is the behaviour.
uint32_t LocalPlayerClassMask() {
    auto playerClass = CGPlayer_C::GetLocalPlayerClass();

    return playerClass ? 1u << (playerClass & 0x1f) : 0u;
}

bool TrackingTypeAllowed(const MINIMAP_TRACKING_TYPE& type, uint32_t classMask) {
    return !type.classMask || (classMask & type.classMask);
}

} // namespace

float CGMinimapFrame::s_pingX = 0.0f;
float CGMinimapFrame::s_pingY = 0.0f;

namespace {

// Both the ping event and GetPingPosition report the same thing: where the ping is relative to the
// player, scaled so that the minimap's edge is 0.5. The reference computes it in both places.
//
// NOT ported: when the rotateMinimap CVar is set, the reference rotates this offset by the
// player's facing first (a Z rotation built from -DAT_00beba70). Frozen does not rotate the
// minimap, and the CVar defaults to "0", so the unrotated answer is the one that matches today --
// but a rotating minimap would place pings wrongly until that lands.
bool PingOffset(float* outX, float* outY) {
    auto player = CGPlayer_C::GetActivePtr();

    if (!player) {
        return false;
    }

    auto position = player->GetPosition();
    auto dx = CGMinimapFrame::s_pingX - position.x;
    auto dy = CGMinimapFrame::s_pingY - position.y;

    auto scale = 1.0f / (CGMinimapFrame::GetRadius() * 2.0f);

    *outX = -dy * scale;
    *outY = dx * scale;

    return true;
}

} // namespace

// ref: FUN_0057eb80
// Stores the ping and signals MINIMAP_PING with the unit that sent it. A ping from someone who is
// neither the player nor in the group resolves to no token and is dropped -- the reference builds
// "player", "partyN" or "raidN" by hand, which is what Script_GetTokenFromGUID does here.
void CGMinimapFrame::SetPing(WOWGUID pinger, float x, float y) {
    // Everything below is measured against the player, so with no player there is nothing to say.
    if (!CGPlayer_C::GetActivePtr()) {
        return;
    }

    CGMinimapFrame::s_pingX = x;
    CGMinimapFrame::s_pingY = y;

    auto token = Script_GetTokenFromGUID(pinger);

    if (!token) {
        return;
    }

    float offsetX = 0.0f;
    float offsetY = 0.0f;

    if (PingOffset(&offsetX, &offsetY)) {
        FrameScript_SignalEvent(SCRIPT_MINIMAP_PING, "%s%f%f", token, offsetX, offsetY);
    }
}

bool CGMinimapFrame::GetPingOffset(float* x, float* y) {
    return PingOffset(x, y);
}

// MSG_MINIMAP_PING: the guid of whoever pinged, then the position as two floats. The client sends
// the same opcode with only the two floats; the server fills in who.
int32_t ReceiveMinimapPing(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    if (!msg) {
        return 1;
    }

    WOWGUID pinger = 0;
    float x = 0.0f;
    float y = 0.0f;

    msg->Get(pinger);
    msg->Get(x);
    msg->Get(y);

    CGMinimapFrame::SetPing(pinger, x, y);

    return 1;
}

void CGMinimapFrameRegisterHandlers() {
    ClientServices::SetMessageHandler(MSG_MINIMAP_PING, &ReceiveMinimapPing, nullptr);
}

// ref: FUN_007f3b90
// Indoors reads a table of yards directly; outdoors reads a table of chunks and converts. The
// reference writes the outdoor conversion as * 0.5f * 33.3333f rather than folding the two, and
// the halving is not a fudge -- the stored number is a diameter in chunks.
float CGMinimapFrame::GetRadius() {
    auto level = CGMinimapFrame::s_indoors
        ? CGMinimapFrame::s_zoom[1]
        : CGMinimapFrame::s_zoom[0];

    if (level >= CGMinimapFrame::s_zoomLevels) {
        level = CGMinimapFrame::s_zoomLevels - 1;
    }

    if (CGMinimapFrame::s_indoors) {
        return CGMinimapFrame::s_zoomRadiusIndoor[level];
    }

    return CGMinimapFrame::s_zoomChunksOutdoor[level] * 0.5f * CGMinimapFrame::s_chunkYards;
}

// ref: FUN_0057e980
uint32_t CGMinimapFrame::GetNumOtherTrackingTypes() {
    auto classMask = LocalPlayerClassMask();
    uint32_t count = 0;

    for (auto& type : CGMinimapFrame::s_trackingTypes) {
        if (TrackingTypeAllowed(type, classMask)) {
            count++;
        }
    }

    return count;
}

// ref: FUN_0057eb00
// Indexes the filtered list, not the table, so the ids FrameXML hands back line up with what
// GetNumOtherTrackingTypes counted.
const MINIMAP_TRACKING_TYPE* CGMinimapFrame::GetOtherTrackingType(uint32_t index) {
    auto classMask = LocalPlayerClassMask();
    uint32_t seen = 0;

    for (auto& type : CGMinimapFrame::s_trackingTypes) {
        if (TrackingTypeAllowed(type, classMask)) {
            if (seen == index) {
                return &type;
            }

            seen++;
        }
    }

    return nullptr;
}

// ref: FUN_0057ea30
// Setting a tracking spell drops whatever table row was selected: the two halves of the tracking
// list share one slot, and only one thing is ever tracked. SetOtherTracking signals the update in
// that case; clearing has to signal for itself.
void CGMinimapFrame::SetTrackingSpell(uint32_t spellID) {
    CGMinimapFrame::s_trackingSpell = spellID;

    if (spellID) {
        CGMinimapFrame::SetOtherTracking(nullptr);

        return;
    }

    FrameScript_SignalEvent(SCRIPT_MINIMAP_UPDATE_TRACKING, nullptr);
}

// The reference drives this from the aura-applied path (FUN_00727760): an aura landing on the
// active player whose spell is in the tracking list becomes the tracked one. Frozen's auras arrive
// as a whole list rather than one application at a time, so this recomputes from what is currently
// on the player.
//
// Recomputing also covers the aura going away, which the reference handles on a separate path --
// one place here instead of two, and the observable state is the same.
void CGMinimapFrame::RefreshTrackingSpell() {
    auto player = ClntObjMgrGetActivePlayer();
    uint32_t tracking = 0;

    if (player) {
        auto count = AuraCacheCount(player, 0, 0);

        for (int32_t i = 0; i < count; i++) {
            auto aura = AuraCacheGet(player, i, 0, 0);

            if (aura && CGMinimapFrame::IsTrackingSpell(static_cast<uint32_t>(aura->spellID))) {
                tracking = static_cast<uint32_t>(aura->spellID);

                break;
            }
        }
    }

    // Guarded so an unrelated aura change does not re-signal, and does not clear the selected
    // table row every time a buff ticks.
    if (tracking != CGMinimapFrame::s_trackingSpell) {
        CGMinimapFrame::SetTrackingSpell(tracking);
    }
}

// ref: FUN_0057e070
void CGMinimapFrame::SetOtherTracking(const MINIMAP_TRACKING_TYPE* type) {
    auto wasTrivialQuests = CGMinimapFrame::s_otherTracking
        && CGMinimapFrame::s_otherTracking->kind == 3;

    CGMinimapFrame::s_otherTracking = type;

    // The selection persists across sessions as the global string name, not as an index -- the
    // index moves when the class filter does.
    auto cvar = CVar::Lookup("minimapTrackedInfo");

    if (cvar) {
        cvar->Set(type ? type->name : "", true, false, false, true);
    }

    auto isTrivialQuests = type && type->kind == 3;

    if (wasTrivialQuests != isTrivialQuests) {
        // TODO the reference walks every object here (FUN_004d4b30 over FUN_0057e020) to add or
        // drop the trivial-quest marker on each. There is no minimap POI renderer yet, so there is
        // nothing to walk; the selection itself is still reported correctly through GetTrackingInfo.
    }

    FrameScript_SignalEvent(SCRIPT_MINIMAP_UPDATE_TRACKING, nullptr);
}

CSimpleFrame* CGMinimapFrame::Create(CSimpleFrame* parent) {
    // TODO use CDataAllocator

    return STORM_NEW(CGMinimapFrame)(parent);
}

void CGMinimapFrame::CreateScriptMetaTable() {
    auto L = FrameScript_GetContext();
    CGMinimapFrame::s_metatable = FrameScript_Object::CreateScriptMetaTable(L, &CGMinimapFrame::RegisterScriptMethods);
}

int32_t CGMinimapFrame::GetObjectType() {
    if (!CGMinimapFrame::s_objectType) {
        CGMinimapFrame::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CGMinimapFrame::s_objectType;
}

bool CGMinimapFrame::IsA(int32_t type) {
    return type == CGMinimapFrame::GetObjectType()
        || CSimpleFrame::IsA(type);
}

void CGMinimapFrame::RegisterScriptMethods(lua_State* L) {
    CSimpleFrame::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, CGMinimapFrameMethods, NUM_CG_MINIMAP_FRAME_SCRIPT_METHODS);
}

// ref: FUN_007f3b60
uint32_t CGMinimapFrame::GetZoomLevels() {
    return CGMinimapFrame::s_zoomLevels;
}

// ref: FUN_007f3b40
uint32_t CGMinimapFrame::GetZoom() {
    return CGMinimapFrame::s_zoom[CGMinimapFrame::s_indoors ? 1 : 0];
}

// ref: FUN_007f3ae0
void CGMinimapFrame::SetZoom(uint32_t level) {
    auto& zoom = CGMinimapFrame::s_zoom[CGMinimapFrame::s_indoors ? 1 : 0];

    // The reference compares unsigned, so a level that arrives negative wraps and clamps to the
    // closest-in level rather than to zero.
    if (level >= CGMinimapFrame::s_zoomLevels - 1) {
        level = CGMinimapFrame::s_zoomLevels - 1;
    }

    // DIVERGENCE: when the level actually changes the reference also raises the minimap's dirty bit
    // (DAT_00d3922c |= 1) and re-issues the terrain read that refills the minimap texture
    // (FUN_00766940). Frozen draws no minimap yet and has neither, so the level is only recorded.
    zoom = level;
}

CGMinimapFrame::CGMinimapFrame(CSimpleFrame* parent) : CSimpleFrame(parent) {
    // TODO
}

int32_t CGMinimapFrame::GetScriptMetaTable() {
    return CGMinimapFrame::s_metatable;
}
