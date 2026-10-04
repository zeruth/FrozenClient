#ifndef OBJECT_CLIENT_DB_CACHE_INSTANCES_HPP
#define OBJECT_CLIENT_DB_CACHE_INSTANCES_HPP

#include "object/client/DBCache.hpp"
#include "net/Types.hpp"
#include "util/GUID.hpp"
#include <cstdint>
#include <string>

class CDataStore;

// The records the client caches and the caches that hold them (DBCacheInstances.cpp).
//
// Each record reads itself from the server's answer and writes itself back out in the same form
// for the WDB file, so Read and Write must stay mirror images: the file is read with Read.

// CreatureStats.cpp: SMSG_CREATURE_QUERY_RESPONSE's template. 0x6c bytes and version 1 in the WDB
// header; the field order is the reader's (FUN_0098d4c0) and the writer's (FUN_0098d3a0).
class CreatureStats_C {
    public:
        static const uint32_t RECORD_SIZE = 0x6c;
        static const uint32_t RECORD_VERSION = 1;

        CreatureStats_C() = default;
        CreatureStats_C(const CreatureStats_C&) = delete;
        CreatureStats_C& operator=(const CreatureStats_C&) = delete;
        // ref: FUN_00667f40
        ~CreatureStats_C();

        static void PutQueryId(CDataStore* msg, const DBCACHEKEY32& id);

        // ref: FUN_0098d4c0
        void Read(CDataStore* msg);
        // ref: FUN_0098d3a0
        void Write(CDataStore* msg) const;

        // +0x5c. Four names; the first is the one shown. An empty name is a null pointer, which
        // is how the reference stores it.
        char* m_names[4] = {};
        char* m_subName = nullptr;          // +0x04, "Innkeeper"
        char* m_iconName = nullptr;         // +0x08, the cursor the creature gives
        uint32_t m_typeFlags = 0;           // +0x0c
        int32_t m_type = 0;                 // +0x10, CreatureType.dbc
        int32_t m_family = 0;               // +0x14, CreatureFamily.dbc
        int32_t m_rank = 0;                 // +0x18, 0 normal .. 4 rare
        int32_t m_killCredit[2] = {};       // +0x1c
        int32_t m_displayID[4] = {};        // +0x24
        float m_healthMultiplier = 0.0f;    // +0x34
        float m_powerMultiplier = 0.0f;     // +0x38
        bool m_racialLeader = false;        // +0x3c
        int32_t m_questItems[6] = {};       // +0x40
        int32_t m_movementID = 0;           // +0x58

        const char* Name() const { return this->m_names[0]; }
};

// GameObjectStats.cpp: SMSG_QUERY_GAME_OBJECT_RESPONSE's template. 0xa0 bytes and version 1 in the
// WDB header (FUN_0067c0e0 checks both); the field order is the reader's (FUN_0098d750).
class GameObjectStats_C {
    public:
        static const uint32_t RECORD_SIZE = 0xa0;
        static const uint32_t RECORD_VERSION = 1;

        GameObjectStats_C() = default;
        GameObjectStats_C(const GameObjectStats_C&) = delete;
        GameObjectStats_C& operator=(const GameObjectStats_C&) = delete;
        ~GameObjectStats_C();

        static void PutQueryId(CDataStore* msg, const DBCACHEKEY32& id);

        // ref: FUN_0098d750
        void Read(CDataStore* msg);
        void Write(CDataStore* msg) const;

        int32_t m_type = 0;                 // +0x00, the GAMEOBJECT_TYPE_*
        int32_t m_displayID = 0;            // +0x04, GameObjectDisplayInfo.dbc
        char* m_iconName = nullptr;         // +0x08, the cursor the object gives ("Interact")
        char* m_castBarCaption = nullptr;   // +0x0c
        char* m_unk1 = nullptr;             // +0x10
        // +0x14: the type's data fields. Which field means what depends on the type; the client
        // looks them up by meaning through GameObjectDataIndex (FUN_00746190).
        int32_t m_data[24] = {};
        float m_size = 0.0f;                // +0x74
        int32_t m_questItems[6] = {};       // +0x78
        // +0x90: four names, the first the one shown; an empty one is null, as for creatures.
        char* m_names[4] = {};

        const char* Name() const { return this->m_names[0]; }
};

// ItemStats.cpp: SMSG_ITEM_QUERY_SINGLE_RESPONSE's record. 0x204 bytes and version 5 in the WDB
// header. The reader is FUN_0098d910; docs/ref/parity-itemcache.md has the wire order.
class ItemStats_C {
    public:
        static const uint32_t RECORD_SIZE = 0x204;
        static const uint32_t RECORD_VERSION = 5;

        static void PutQueryId(CDataStore* msg, const DBCACHEKEY32& id);

        // ref: FUN_0098d910
        void Read(CDataStore* msg);
        void Write(CDataStore* msg) const;

        const char* Name() const { return this->name.c_str(); }

        int32_t itemClass = 0;
        int32_t subClass = 0;
        int32_t soundOverrideSubclass = 0;

        // Four names; only the first is the item's name. The reference keeps all four and the
        // WDB file carries all four, so they are all kept here too.
        std::string names[4];
        // names[0], under the name every reader already uses.
        std::string name;

        int32_t displayInfoID = 0;
        int32_t quality = 0;
        uint32_t flags = 0;
        uint32_t flags2 = 0;

        int32_t buyPrice = 0;
        int32_t sellPrice = 0;
        int32_t inventoryType = 0;
        int32_t allowableClass = 0;
        int32_t allowableRace = 0;
        int32_t itemLevel = 0;
        int32_t requiredLevel = 0;
        int32_t requiredSkill = 0;
        int32_t requiredSkillRank = 0;
        int32_t requiredSpell = 0;
        int32_t requiredHonorRank = 0;
        int32_t requiredCityRank = 0;
        int32_t requiredReputationFaction = 0;
        int32_t requiredReputationRank = 0;

        // The most of this item a character may own at once. NOT what GetItemInfo reports as the
        // stack size -- that is the next field, which the reference reads at record offset 0x5c.
        int32_t maxCount = 0;

        // The stack size GetItemInfo reports.
        int32_t stackable = 0;
        int32_t containerSlots = 0;

        // The stat block. The reference keeps ten slots and fills the unused TYPES with -1 while
        // leaving their values at zero, so a reader walks until it sees -1 rather than counting.
        static const int32_t MAX_STATS = 10;
        int32_t statsCount = 0;
        int32_t statType[MAX_STATS] = {};
        int32_t statValue[MAX_STATS] = {};

        int32_t scalingStatDistribution = 0;
        int32_t scalingStatValue = 0;

        // Two damage bands, each a float pair and a school.
        static const int32_t MAX_DAMAGES = 2;
        float damageMin[MAX_DAMAGES] = {};
        float damageMax[MAX_DAMAGES] = {};
        int32_t damageType[MAX_DAMAGES] = {};

        // Armor first, then the six schools in the order the wire sends them: holy, fire, nature,
        // frost, shadow, arcane.
        int32_t armor = 0;
        int32_t resistance[6] = {};

        int32_t delay = 0;
        int32_t ammoType = 0;
        float rangedModRange = 0.0f;

        // Five "on use"/"on equip" spell slots, six parallel arrays -- which is how the wire
        // carries them: the reference reads one index across all six before moving on.
        static const int32_t MAX_SPELLS = 5;
        int32_t spellID[MAX_SPELLS] = {};
        int32_t spellTrigger[MAX_SPELLS] = {};
        int32_t spellCharges[MAX_SPELLS] = {};
        int32_t spellCooldown[MAX_SPELLS] = {};
        int32_t spellCategory[MAX_SPELLS] = {};
        int32_t spellCategoryCooldown[MAX_SPELLS] = {};

        int32_t bonding = 0;
        std::string description;

        int32_t pageText = 0;
        int32_t languageID = 0;
        int32_t pageMaterial = 0;
        int32_t startQuest = 0;
        int32_t lockID = 0;
        int32_t material = 0;
        int32_t sheath = 0;
        int32_t randomProperty = 0;
        int32_t randomSuffix = 0;
        int32_t block = 0;
        int32_t itemSet = 0;
        int32_t maxDurability = 0;
        int32_t area = 0;
        int32_t map = 0;
        int32_t bagFamily = 0;
        int32_t totemCategory = 0;

        static const int32_t MAX_SOCKETS = 3;
        int32_t socketColor[MAX_SOCKETS] = {};
        int32_t socketContent[MAX_SOCKETS] = {};

        int32_t socketBonus = 0;
        int32_t gemProperties = 0;
        int32_t requiredDisenchantSkill = 0;
        float armorDamageModifier = 0.0f;
        int32_t duration = 0;
        int32_t itemLimitCategory = 0;
        int32_t holidayID = 0;
};

// NameCache.h: SMSG_NAME_QUERY_RESPONSE's record. Never persisted (the instance is built with
// persist = false); the sizes are the reference's for completeness. The copy is FUN_006680c0.
class NameCacheRec {
    public:
        static const uint32_t RECORD_SIZE = 0x158;
        static const uint32_t RECORD_VERSION = 1;

        static void PutQueryId(CDataStore* msg, const DBCACHEKEY64& id);

        void Read(CDataStore* msg);
        void Write(CDataStore* msg) const;

        char m_name[48] = {};               // +0x00
        // +0x30: the five declined forms, 64 characters each, or empty when the realm has none.
        std::string m_declined[5];
        bool m_hasDeclined = false;
        char m_realm[256] = {};             // +0x34
        WOWGUID m_guid = 0;                 // +0x138
        uint32_t m_race = 0;                // +0x140
        uint32_t m_sex = 0;                 // +0x144
        uint32_t m_class = 0;               // +0x148
};

// PetNameCache.h: SMSG_PET_NAME_QUERY_RESPONSE's record, keyed by the pet number. Never persisted.
// The timestamp is what makes a renamed pet ask again: CGUnit_C::GetUnitName compares it with the
// descriptor's UNIT_FIELD_PET_NAME_TIMESTAMP and drops the record when they differ.
class PetNameRec {
    public:
        static const uint32_t RECORD_SIZE = 0x60;
        static const uint32_t RECORD_VERSION = 1;

        static void PutQueryId(CDataStore* msg, const DBCACHEKEY32& id);

        void Read(CDataStore* msg);
        void Write(CDataStore* msg) const;

        char m_name[80] = {};               // +0x00
        std::string m_declined[5];          // +0x50, 96 characters each on the wire
        bool m_hasDeclined = false;
        uint32_t m_petNumber = 0;           // +0x54
        uint32_t m_timestamp = 0;           // +0x58
};

// ItemName.cpp: SMSG_ITEM_NAME_QUERY_RESPONSE's record, keyed by the item id. 8 bytes and version
// 1 in the WDB header (the save at 0x0066a767): the reference holds the name as an SStrDupA'd
// pointer, read into a 400-character buffer first (FUN_0098d8c0).
class ItemNameRec {
    public:
        static const uint32_t RECORD_SIZE = 0x8;
        static const uint32_t RECORD_VERSION = 1;

        static void PutQueryId(CDataStore* msg, const DBCACHEKEY32& id);

        void Read(CDataStore* msg);
        void Write(CDataStore* msg) const;

        std::string m_name;                 // +0x00
        uint32_t m_inventoryType = 0;       // +0x04
};

// PetitionCache.h: SMSG_PETITION_QUERY_RESPONSE's record, keyed by the petition's id. 0x13c8 bytes
// and version 1 in the WDB header (the save at 0x0066bf47), never persisted. Cleared by
// FUN_00599fd0 and read by FUN_0098d090; the field names are the server's.
class PetitionRec {
    public:
        static const uint32_t RECORD_SIZE = 0x13c8;
        static const uint32_t RECORD_VERSION = 1;

        static void PutQueryId(CDataStore* msg, const DBCACHEKEY32& id);

        void Read(CDataStore* msg);
        void Write(CDataStore* msg) const;

        int32_t m_id = 0;                   // +0x00, <= 0 when the server has no such petition
        WOWGUID m_creator = 0;              // +0x08
        char m_title[256] = {};             // +0x10
        char m_body[4096] = {};             // +0x110
        uint32_t m_flags = 0;               // +0x1110
        uint32_t m_minSignatures = 0;       // +0x1114
        uint32_t m_maxSignatures = 0;       // +0x1118
        uint32_t m_deadline = 0;            // +0x111c
        uint32_t m_issueDate = 0;           // +0x1120
        uint32_t m_allowedGuildID = 0;      // +0x1124
        uint32_t m_allowedClasses = 0;      // +0x1128
        uint16_t m_allowedRaces = 0;        // +0x112c
        uint32_t m_allowedGenders = 0;      // +0x1130
        uint32_t m_allowedMinLevel = 0;     // +0x1134
        char m_choiceText[10][64] = {};     // +0x1138
        uint32_t m_allowedMaxLevel = 0;     // +0x13b8
        // +0x13bc: 0 for a guild charter, else the arena team's size.
        uint32_t m_type = 0;
        uint32_t m_muid = 0;                // +0x13c0
};

typedef DBCache<CreatureStats_C, DBCACHEKEY32> CreatureStatsCache;
typedef DBCache<GameObjectStats_C, DBCACHEKEY32> GameObjectStatsCache;
typedef DBCache<ItemNameRec, DBCACHEKEY32> ItemNameCache;
typedef DBCache<ItemStats_C, DBCACHEKEY32> ItemStatsCache;
typedef DBCache<NameCacheRec, DBCACHEKEY64> NameCache;
typedef DBCache<PetNameRec, DBCACHEKEY32> PetNameCache;
typedef DBCache<PetitionRec, DBCACHEKEY32> PetitionCache;

// The instances, constructed with the reference's arguments (FUN_009cb680 and its neighbours).
extern CreatureStatsCache g_creatureCache;      // 0x00c5d690, 'WMOB', creaturecache.wdb
extern GameObjectStatsCache g_gameObjectCache;  // 0x00c5d718, 'WGOB', gameobjectcache.wdb
extern ItemNameCache g_itemNameCache;           // 0x00c5d7a0, 'WNDB', itemnamecache.wdb
extern ItemStatsCache g_itemCache;              // 0x00c5d828, 'WIDB', itemcache.wdb
extern NameCache g_nameCache;                   // 0x00c5d938, 'NAMW', namecache.wdb
extern PetNameCache g_petNameCache;             // 0x00c5db58, 'MNPW', petnamecache.wdb
extern PetitionCache g_petitionCache;           // 0x00c5dbe0, 'WPTN', petitioncache.wdb

// ref: FUN_00635b40
void DBCacheRegisterHandlers();
// ref: FUN_006355e0
void DBCacheUnregisterHandlers();
// ref: FUN_00635060
void DBCacheLoadAll();
// ref: FUN_00635100
void DBCacheUpdateAll();
// ref: FUN_00635540
void DBCacheShutdownAll();
// ref: FUN_00635680
void DBCacheClearUnloadedAll();
// ref: FUN_00635710
void DBCacheSetSessionAll(uint32_t session);

// The message handlers.
int32_t ReceiveCreatureQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
// ref: FUN_006351b0
int32_t ReceiveGameObjectQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t ReceiveItemQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t ReceiveNameQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t ReceivePetNameQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t ReceiveItemNameQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t ReceivePetitionQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
// ref: FUN_00464730
int32_t ReceiveClientCacheVersion(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

// A callback for the lookups whose reference callback has not been identified yet: the record is
// asked for, and whoever reads it next finds it. Nothing happens when it lands.
void DBCacheIgnoreCallback(uint32_t id, const WOWGUID* guid, void* param, bool found);

#endif
