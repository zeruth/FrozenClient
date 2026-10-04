#include "object/client/DBCacheInstances.hpp"
#include "client/ClientServices.hpp"
#include "console/CVar.hpp"
#include "util/Filesystem.hpp"
#include "util/guid/SmartGUID.hpp"
#include <common/DataStore.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <cstdio>
#include <cstring>

// The instances, in the order and with the arguments the reference's static initializers use
// (0x009cb680 onward): signature, file, query opcode, the fifth argument, whether the query carries
// a guid, whether the cache persists, and the query rate.
CreatureStatsCache g_creatureCache(0x574D4F42, "creaturecache.wdb", CMSG_QUERY_CREATURE, 0, true, true, 0);
GameObjectStatsCache g_gameObjectCache(0x57474F42, "gameobjectcache.wdb", CMSG_QUERY_GAME_OBJECT, 0, true, true, 0);
ItemNameCache g_itemNameCache(0x574E4442, "itemnamecache.wdb", CMSG_ITEM_NAME_QUERY, 0, true, true, 0);
ItemStatsCache g_itemCache(0x57494442, "itemcache.wdb", CMSG_ITEM_QUERY_SINGLE, 0x57, false, true, 0x200);
NameCache g_nameCache(0x574E414D, "namecache.wdb", CMSG_NAME_QUERY, 0, false, false, 0x100);
PetNameCache g_petNameCache(0x57504E4D, "petnamecache.wdb", CMSG_QUERY_PET_NAME, 0, true, false, 0);
PetitionCache g_petitionCache(0x5750544E, "petitioncache.wdb", CMSG_PETITION_QUERY, 0, true, false, 0);

// ------------------------------------------------------------------------------------------------
// The WDB file
// ------------------------------------------------------------------------------------------------

// ref: FUN_00667ef0
// Cache\WDB\<locale>, made when it is not there. The reference builds "Cache/%s/%s" over "WDB" and
// the locale when the install has the common archive layout, and plain "WDB" otherwise; every
// 3.3.5a install has the common layout, so that is the path built here.
void DBCacheGetDirectory(char* buffer, size_t size) {
    const char* locale = "enUS";

    if (auto var = CVar::Lookup("locale")) {
        locale = var->GetString();
    }

    OsCreateDirectory("Cache", 0);

    char wdb[STORM_MAX_PATH];
    SStrPrintf(wdb, sizeof(wdb), "Cache/%s", "WDB");
    OsCreateDirectory(wdb, 0);

    SStrPrintf(buffer, size, "Cache/%s/%s", "WDB", locale);
    OsCreateDirectory(buffer, 0);
}

uint32_t DBCacheGetLocaleTag() {
    const char* locale = "enUS";

    if (auto var = CVar::Lookup("locale")) {
        locale = var->GetString();
    }

    if (SStrLen(locale) < 4) {
        locale = "enUS";
    }

    return (static_cast<uint32_t>(static_cast<uint8_t>(locale[0])) << 24)
        | (static_cast<uint32_t>(static_cast<uint8_t>(locale[1])) << 16)
        | (static_cast<uint32_t>(static_cast<uint8_t>(locale[2])) << 8)
        | static_cast<uint32_t>(static_cast<uint8_t>(locale[3]));
}

bool DBCacheReadFile(const char* path, uint8_t** data, uint32_t* size) {
    FILE* file = fopen(path, "rb");

    if (!file) {
        return false;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (length <= 0) {
        fclose(file);
        return false;
    }

    auto buffer = static_cast<uint8_t*>(SMemAlloc(static_cast<size_t>(length), __FILE__, __LINE__, 0));

    if (fread(buffer, 1, static_cast<size_t>(length), file) != static_cast<size_t>(length)) {
        SMemFree(buffer, __FILE__, __LINE__, 0);
        fclose(file);
        return false;
    }

    fclose(file);

    *data = buffer;
    *size = static_cast<uint32_t>(length);

    return true;
}

bool DBCacheWriteFile(const char* path, const void* data, uint32_t size) {
    FILE* file = fopen(path, "wb");

    if (!file) {
        // "OsCreateFile failed for cache file: %s" in the reference.
        return false;
    }

    fwrite(data, 1, size, file);
    fclose(file);

    return true;
}

// ------------------------------------------------------------------------------------------------
// Records
// ------------------------------------------------------------------------------------------------

namespace {

int32_t GetI32(CDataStore* msg) {
    uint32_t value = 0;
    msg->Get(value);

    return static_cast<int32_t>(value);
}

void PutI32(CDataStore* msg, int32_t value) {
    msg->Put(static_cast<uint32_t>(value));
}

// SStrDupA of a non-empty string, null for an empty one -- the creature reader's convention for
// its four names.
char* DupOrNull(const char* str) {
    return str[0] ? SStrDupA(str, __FILE__, __LINE__) : nullptr;
}

} // namespace

// ref: FUN_00667f40
CreatureStats_C::~CreatureStats_C() {
    for (auto& name : this->m_names) {
        if (name) {
            SMemFree(name, __FILE__, __LINE__, 0);
        }
    }

    if (this->m_subName) {
        SMemFree(this->m_subName, __FILE__, __LINE__, 0);
    }

    if (this->m_iconName) {
        SMemFree(this->m_iconName, __FILE__, __LINE__, 0);
    }
}

void CreatureStats_C::PutQueryId(CDataStore* msg, const DBCACHEKEY32& id) {
    msg->Put(id.m_id);
}

// ref: FUN_0098d4c0
// The four names are each freed and re-read, an empty one stored as null; the subname and icon
// name are always duplicated, even when empty. One byte, the racial-leader flag, sits between the
// dwords, which is what makes the record 77 bytes on the wire.
void CreatureStats_C::Read(CDataStore* msg) {
    char buffer[1024];

    for (auto& name : this->m_names) {
        if (name) {
            SMemFree(name, __FILE__, __LINE__, 0);
        }

        msg->GetString(buffer, sizeof(buffer));
        name = DupOrNull(buffer);
    }

    if (this->m_subName) {
        SMemFree(this->m_subName, __FILE__, __LINE__, 0);
    }

    msg->GetString(buffer, sizeof(buffer));
    this->m_subName = SStrDupA(buffer, __FILE__, __LINE__);

    if (this->m_iconName) {
        SMemFree(this->m_iconName, __FILE__, __LINE__, 0);
    }

    msg->GetString(buffer, sizeof(buffer));
    this->m_iconName = SStrDupA(buffer, __FILE__, __LINE__);

    msg->Get(this->m_typeFlags);
    this->m_type = GetI32(msg);
    this->m_family = GetI32(msg);
    this->m_rank = GetI32(msg);
    this->m_killCredit[0] = GetI32(msg);
    this->m_killCredit[1] = GetI32(msg);

    for (auto& display : this->m_displayID) {
        display = GetI32(msg);
    }

    msg->Get(this->m_healthMultiplier);
    msg->Get(this->m_powerMultiplier);

    uint8_t leader = 0;
    msg->Get(leader);
    this->m_racialLeader = leader != 0;

    for (auto& item : this->m_questItems) {
        item = GetI32(msg);
    }

    this->m_movementID = GetI32(msg);
}

// ref: FUN_0098d3a0
void CreatureStats_C::Write(CDataStore* msg) const {
    for (auto name : this->m_names) {
        msg->PutString(name ? name : "");
    }

    msg->PutString(this->m_subName ? this->m_subName : "");
    msg->PutString(this->m_iconName ? this->m_iconName : "");
    msg->Put(this->m_typeFlags);
    PutI32(msg, this->m_type);
    PutI32(msg, this->m_family);
    PutI32(msg, this->m_rank);
    PutI32(msg, this->m_killCredit[0]);
    PutI32(msg, this->m_killCredit[1]);

    for (auto display : this->m_displayID) {
        PutI32(msg, display);
    }

    msg->Put(this->m_healthMultiplier);
    msg->Put(this->m_powerMultiplier);
    msg->Put(static_cast<uint8_t>(this->m_racialLeader ? 1 : 0));

    for (auto item : this->m_questItems) {
        PutI32(msg, item);
    }

    PutI32(msg, this->m_movementID);
}

GameObjectStats_C::~GameObjectStats_C() {
    for (auto& name : this->m_names) {
        if (name) {
            SMemFree(name, __FILE__, __LINE__, 0);
        }
    }

    for (auto str : { this->m_iconName, this->m_castBarCaption, this->m_unk1 }) {
        if (str) {
            SMemFree(str, __FILE__, __LINE__, 0);
        }
    }
}

void GameObjectStats_C::PutQueryId(CDataStore* msg, const DBCACHEKEY32& id) {
    msg->Put(id.m_id);
}

// ref: FUN_0098d750
// The type and display, the four names (an empty one stored as null), the icon, the cast bar
// caption and an unused string (always duplicated), the 24 data fields, the size and the six
// quest items.
void GameObjectStats_C::Read(CDataStore* msg) {
    char buffer[1024];

    this->m_type = GetI32(msg);
    this->m_displayID = GetI32(msg);

    for (auto& name : this->m_names) {
        msg->GetString(buffer, sizeof(buffer));
        name = DupOrNull(buffer);
    }

    msg->GetString(buffer, sizeof(buffer));
    this->m_iconName = SStrDupA(buffer, __FILE__, __LINE__);

    msg->GetString(buffer, sizeof(buffer));
    this->m_castBarCaption = SStrDupA(buffer, __FILE__, __LINE__);

    msg->GetString(buffer, sizeof(buffer));
    this->m_unk1 = SStrDupA(buffer, __FILE__, __LINE__);

    for (auto& data : this->m_data) {
        data = GetI32(msg);
    }

    msg->Get(this->m_size);

    for (auto& item : this->m_questItems) {
        item = GetI32(msg);
    }
}

// The reader's mirror, for the WDB file.
void GameObjectStats_C::Write(CDataStore* msg) const {
    PutI32(msg, this->m_type);
    PutI32(msg, this->m_displayID);

    for (auto name : this->m_names) {
        msg->PutString(name ? name : "");
    }

    msg->PutString(this->m_iconName ? this->m_iconName : "");
    msg->PutString(this->m_castBarCaption ? this->m_castBarCaption : "");
    msg->PutString(this->m_unk1 ? this->m_unk1 : "");

    for (auto data : this->m_data) {
        PutI32(msg, data);
    }

    msg->Put(this->m_size);

    for (auto item : this->m_questItems) {
        PutI32(msg, item);
    }
}

void ItemStats_C::PutQueryId(CDataStore* msg, const DBCACHEKEY32& id) {
    msg->Put(id.m_id);
}

// ref: FUN_0098d910
// The stat block is the one place this diverges, deliberately: the reference reads statsCount
// pairs with no bound into a ten-slot pair of arrays, so a server sending eleven would walk it
// off the end of the record. Every pair is consumed, keeping the read in step with the stream,
// and only the first ten are stored.
void ItemStats_C::Read(CDataStore* msg) {
    this->itemClass = GetI32(msg);
    this->subClass = GetI32(msg);
    this->soundOverrideSubclass = GetI32(msg);

    for (auto& str : this->names) {
        char buffer[400] = { 0 };
        msg->GetString(buffer, sizeof(buffer));
        str = buffer;
    }

    this->name = this->names[0];

    this->displayInfoID = GetI32(msg);
    this->quality = GetI32(msg);
    msg->Get(this->flags);
    msg->Get(this->flags2);
    this->buyPrice = GetI32(msg);
    this->sellPrice = GetI32(msg);
    this->inventoryType = GetI32(msg);
    this->allowableClass = GetI32(msg);
    this->allowableRace = GetI32(msg);
    this->itemLevel = GetI32(msg);
    this->requiredLevel = GetI32(msg);
    this->requiredSkill = GetI32(msg);
    this->requiredSkillRank = GetI32(msg);
    this->requiredSpell = GetI32(msg);
    this->requiredHonorRank = GetI32(msg);
    this->requiredCityRank = GetI32(msg);
    this->requiredReputationFaction = GetI32(msg);
    this->requiredReputationRank = GetI32(msg);
    this->maxCount = GetI32(msg);
    this->stackable = GetI32(msg);
    this->containerSlots = GetI32(msg);

    this->statsCount = GetI32(msg);

    for (int32_t i = 0; i < this->statsCount; i++) {
        int32_t type = GetI32(msg);
        int32_t value = GetI32(msg);

        if (i < MAX_STATS) {
            this->statType[i] = type;
            this->statValue[i] = value;
        }
    }

    for (int32_t i = this->statsCount < 0 ? 0 : this->statsCount; i < MAX_STATS; i++) {
        this->statType[i] = -1;
        this->statValue[i] = 0;
    }

    this->scalingStatDistribution = GetI32(msg);
    this->scalingStatValue = GetI32(msg);

    for (int32_t i = 0; i < MAX_DAMAGES; i++) {
        msg->Get(this->damageMin[i]);
        msg->Get(this->damageMax[i]);
        this->damageType[i] = GetI32(msg);
    }

    this->armor = GetI32(msg);

    for (auto& resist : this->resistance) {
        resist = GetI32(msg);
    }

    this->delay = GetI32(msg);
    this->ammoType = GetI32(msg);
    msg->Get(this->rangedModRange);

    for (int32_t i = 0; i < MAX_SPELLS; i++) {
        this->spellID[i] = GetI32(msg);
        this->spellTrigger[i] = GetI32(msg);
        this->spellCharges[i] = GetI32(msg);
        this->spellCooldown[i] = GetI32(msg);
        this->spellCategory[i] = GetI32(msg);
        this->spellCategoryCooldown[i] = GetI32(msg);
    }

    this->bonding = GetI32(msg);

    {
        char buffer[1024] = { 0 };
        msg->GetString(buffer, sizeof(buffer));
        this->description = buffer;
    }

    this->pageText = GetI32(msg);
    this->languageID = GetI32(msg);
    this->pageMaterial = GetI32(msg);
    this->startQuest = GetI32(msg);
    this->lockID = GetI32(msg);
    this->material = GetI32(msg);
    this->sheath = GetI32(msg);
    this->randomProperty = GetI32(msg);
    this->randomSuffix = GetI32(msg);
    this->block = GetI32(msg);
    this->itemSet = GetI32(msg);
    this->maxDurability = GetI32(msg);
    this->area = GetI32(msg);
    this->map = GetI32(msg);
    this->bagFamily = GetI32(msg);
    this->totemCategory = GetI32(msg);

    for (int32_t i = 0; i < MAX_SOCKETS; i++) {
        this->socketColor[i] = GetI32(msg);
        this->socketContent[i] = GetI32(msg);
    }

    this->socketBonus = GetI32(msg);
    this->gemProperties = GetI32(msg);
    this->requiredDisenchantSkill = GetI32(msg);
    msg->Get(this->armorDamageModifier);
    this->duration = GetI32(msg);
    this->itemLimitCategory = GetI32(msg);
    this->holidayID = GetI32(msg);
}

// The WDB writer: Read's mirror. The stat count written is the number of slots kept, so a file
// written from a record the reader truncated reads back as what was kept.
void ItemStats_C::Write(CDataStore* msg) const {
    PutI32(msg, this->itemClass);
    PutI32(msg, this->subClass);
    PutI32(msg, this->soundOverrideSubclass);

    for (auto& str : this->names) {
        msg->PutString(str.c_str());
    }

    PutI32(msg, this->displayInfoID);
    PutI32(msg, this->quality);
    msg->Put(this->flags);
    msg->Put(this->flags2);
    PutI32(msg, this->buyPrice);
    PutI32(msg, this->sellPrice);
    PutI32(msg, this->inventoryType);
    PutI32(msg, this->allowableClass);
    PutI32(msg, this->allowableRace);
    PutI32(msg, this->itemLevel);
    PutI32(msg, this->requiredLevel);
    PutI32(msg, this->requiredSkill);
    PutI32(msg, this->requiredSkillRank);
    PutI32(msg, this->requiredSpell);
    PutI32(msg, this->requiredHonorRank);
    PutI32(msg, this->requiredCityRank);
    PutI32(msg, this->requiredReputationFaction);
    PutI32(msg, this->requiredReputationRank);
    PutI32(msg, this->maxCount);
    PutI32(msg, this->stackable);
    PutI32(msg, this->containerSlots);

    int32_t count = this->statsCount < 0 ? 0 : (this->statsCount > MAX_STATS ? MAX_STATS : this->statsCount);
    PutI32(msg, count);

    for (int32_t i = 0; i < count; i++) {
        PutI32(msg, this->statType[i]);
        PutI32(msg, this->statValue[i]);
    }

    PutI32(msg, this->scalingStatDistribution);
    PutI32(msg, this->scalingStatValue);

    for (int32_t i = 0; i < MAX_DAMAGES; i++) {
        msg->Put(this->damageMin[i]);
        msg->Put(this->damageMax[i]);
        PutI32(msg, this->damageType[i]);
    }

    PutI32(msg, this->armor);

    for (auto resist : this->resistance) {
        PutI32(msg, resist);
    }

    PutI32(msg, this->delay);
    PutI32(msg, this->ammoType);
    msg->Put(this->rangedModRange);

    for (int32_t i = 0; i < MAX_SPELLS; i++) {
        PutI32(msg, this->spellID[i]);
        PutI32(msg, this->spellTrigger[i]);
        PutI32(msg, this->spellCharges[i]);
        PutI32(msg, this->spellCooldown[i]);
        PutI32(msg, this->spellCategory[i]);
        PutI32(msg, this->spellCategoryCooldown[i]);
    }

    PutI32(msg, this->bonding);
    msg->PutString(this->description.c_str());
    PutI32(msg, this->pageText);
    PutI32(msg, this->languageID);
    PutI32(msg, this->pageMaterial);
    PutI32(msg, this->startQuest);
    PutI32(msg, this->lockID);
    PutI32(msg, this->material);
    PutI32(msg, this->sheath);
    PutI32(msg, this->randomProperty);
    PutI32(msg, this->randomSuffix);
    PutI32(msg, this->block);
    PutI32(msg, this->itemSet);
    PutI32(msg, this->maxDurability);
    PutI32(msg, this->area);
    PutI32(msg, this->map);
    PutI32(msg, this->bagFamily);
    PutI32(msg, this->totemCategory);

    for (int32_t i = 0; i < MAX_SOCKETS; i++) {
        PutI32(msg, this->socketColor[i]);
        PutI32(msg, this->socketContent[i]);
    }

    PutI32(msg, this->socketBonus);
    PutI32(msg, this->gemProperties);
    PutI32(msg, this->requiredDisenchantSkill);
    msg->Put(this->armorDamageModifier);
    PutI32(msg, this->duration);
    PutI32(msg, this->itemLimitCategory);
    PutI32(msg, this->holidayID);
}

void NameCacheRec::PutQueryId(CDataStore* msg, const DBCACHEKEY64& id) {
    msg->Put(static_cast<uint64_t>(id.m_id));
}

// The name record as ReceiveNameQueryResponse reads it, minus the guid and status in front.
void NameCacheRec::Read(CDataStore* msg) {
    msg->GetString(this->m_name, sizeof(this->m_name));
    msg->GetString(this->m_realm, sizeof(this->m_realm));

    uint8_t race = 0, sex = 0, cls = 0, declined = 0;
    msg->Get(race);
    msg->Get(sex);
    msg->Get(cls);
    this->m_race = race;
    this->m_sex = sex;
    this->m_class = cls;

    msg->Get(declined);
    this->m_hasDeclined = declined != 0;

    for (auto& str : this->m_declined) {
        str.clear();
    }

    if (this->m_hasDeclined) {
        for (auto& str : this->m_declined) {
            char buffer[64] = { 0 };
            msg->GetString(buffer, sizeof(buffer));
            str = buffer;
        }
    }
}

void NameCacheRec::Write(CDataStore* msg) const {
    msg->PutString(this->m_name);
    msg->PutString(this->m_realm);
    msg->Put(static_cast<uint8_t>(this->m_race));
    msg->Put(static_cast<uint8_t>(this->m_sex));
    msg->Put(static_cast<uint8_t>(this->m_class));
    msg->Put(static_cast<uint8_t>(this->m_hasDeclined ? 1 : 0));

    if (this->m_hasDeclined) {
        for (auto& str : this->m_declined) {
            msg->PutString(str.c_str());
        }
    }
}

void PetNameRec::PutQueryId(CDataStore* msg, const DBCACHEKEY32& id) {
    msg->Put(id.m_id);
}

// What ReceivePetNameQueryResponse reads after the pet number: the name, the timestamp, and the
// declined forms behind a flag byte.
void PetNameRec::Read(CDataStore* msg) {
    msg->GetString(this->m_name, sizeof(this->m_name));
    msg->Get(this->m_timestamp);

    uint8_t declined = 0;
    msg->Get(declined);
    this->m_hasDeclined = declined != 0;

    for (auto& str : this->m_declined) {
        str.clear();
    }

    if (this->m_hasDeclined) {
        for (auto& str : this->m_declined) {
            char buffer[96] = { 0 };
            msg->GetString(buffer, sizeof(buffer));
            str = buffer;
        }
    }
}

void PetNameRec::Write(CDataStore* msg) const {
    msg->PutString(this->m_name);
    msg->Put(this->m_timestamp);
    msg->Put(static_cast<uint8_t>(this->m_hasDeclined ? 1 : 0));

    if (this->m_hasDeclined) {
        for (auto& str : this->m_declined) {
            msg->PutString(str.c_str());
        }
    }
}

void ItemNameRec::PutQueryId(CDataStore* msg, const DBCACHEKEY32& id) {
    msg->Put(id.m_id);
}

// ref: FUN_0098d8c0
void ItemNameRec::Read(CDataStore* msg) {
    char name[400];
    msg->GetString(name, sizeof(name));
    this->m_name = name;

    msg->Get(this->m_inventoryType);
}

void ItemNameRec::Write(CDataStore* msg) const {
    msg->PutString(this->m_name.c_str());
    msg->Put(this->m_inventoryType);
}

void PetitionRec::PutQueryId(CDataStore* msg, const DBCACHEKEY32& id) {
    msg->Put(id.m_id);
}

// ref: FUN_0098d090
void PetitionRec::Read(CDataStore* msg) {
    uint32_t id = 0;
    msg->Get(id);
    this->m_id = static_cast<int32_t>(id);
    msg->Get(this->m_creator);
    msg->GetString(this->m_title, sizeof(this->m_title));
    msg->GetString(this->m_body, sizeof(this->m_body));
    msg->Get(this->m_flags);
    msg->Get(this->m_minSignatures);
    msg->Get(this->m_maxSignatures);
    msg->Get(this->m_deadline);
    msg->Get(this->m_issueDate);
    msg->Get(this->m_allowedGuildID);
    msg->Get(this->m_allowedClasses);
    msg->Get(this->m_allowedRaces);
    msg->Get(this->m_allowedGenders);
    msg->Get(this->m_allowedMinLevel);
    msg->Get(this->m_allowedMaxLevel);

    for (auto& text : this->m_choiceText) {
        msg->GetString(text, sizeof(text));
    }

    msg->Get(this->m_muid);
    msg->Get(this->m_type);
}

void PetitionRec::Write(CDataStore* msg) const {
    msg->Put(static_cast<uint32_t>(this->m_id));
    msg->Put(this->m_creator);
    msg->PutString(this->m_title);
    msg->PutString(this->m_body);
    msg->Put(this->m_flags);
    msg->Put(this->m_minSignatures);
    msg->Put(this->m_maxSignatures);
    msg->Put(this->m_deadline);
    msg->Put(this->m_issueDate);
    msg->Put(this->m_allowedGuildID);
    msg->Put(this->m_allowedClasses);
    msg->Put(this->m_allowedRaces);
    msg->Put(this->m_allowedGenders);
    msg->Put(this->m_allowedMinLevel);
    msg->Put(this->m_allowedMaxLevel);

    for (auto& text : this->m_choiceText) {
        msg->PutString(text);
    }

    msg->Put(this->m_muid);
    msg->Put(this->m_type);
}

// ------------------------------------------------------------------------------------------------
// Handlers and lifecycle
// ------------------------------------------------------------------------------------------------

void DBCacheIgnoreCallback(uint32_t id, const WOWGUID* guid, void* param, bool found) {
}

// ref: FUN_00635190
int32_t ReceiveCreatureQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    g_creatureCache.Response(msg, true);

    return 1;
}

// ref: FUN_006351b0
int32_t ReceiveGameObjectQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    g_gameObjectCache.Response(msg, true);

    return 1;
}

// ref: FUN_006354d0
// Registered for 0x58 and for 0x39b both, as the reference does.
int32_t ReceiveItemQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    g_itemCache.Response(msg, true);

    return 1;
}

// ref: FUN_006357d0
// A packed guid and a status byte. Status 0 is the name; 1 is "no such character", which drops
// the entry and tells its callbacks; 2 puts the entry back on the pending list to be asked again;
// 3 is a name the server will not give, stored as "?" and never saved. A name for a guid of the
// 0x1fd0 high type is stored but not saved either.
int32_t ReceiveNameQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    SmartGUID smart;
    *msg >> smart;
    WOWGUID guid = smart.guid;

    uint8_t status = 0;
    msg->Get(status);

    DBCACHEKEY64 key(guid);

    if (status == 0) {
        NameCacheRec record;
        record.Read(msg);
        record.m_guid = guid;

        g_nameCache.Store(key, record);

        uint32_t high = static_cast<uint32_t>(guid >> 32);

        if ((high & 0xF0000000) != 0x10000000 || (high & 0x0FF00000) != 0x0FD00000) {
            return 1;
        }
    } else if (status == 2) {
        // FUN_0067a360: the entry goes back to wait for another query.
        g_nameCache.Requeue(key);

        return 1;
    } else if (status != 3) {
        g_nameCache.NotFound(key);

        return 1;
    } else {
        NameCacheRec record;
        SStrCopy(record.m_name, "?", sizeof(record.m_name));
        record.m_guid = guid;

        g_nameCache.Store(key, record);
    }

    // FUN_0067a4c0
    if (auto entry = g_nameCache.Find(key)) {
        entry->m_noSave = true;
    }

    return 1;
}

// ref: FUN_006352c0
// The pet number, then the record. An empty name is the server saying it has no such pet, which
// drops the entry and tells its callbacks (FUN_0067a840); anything else is stored (FUN_0067ebd0).
int32_t ReceivePetNameQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint32_t petNumber = 0;
    msg->Get(petNumber);

    PetNameRec record;
    record.Read(msg);
    record.m_petNumber = petNumber;

    DBCACHEKEY32 key(petNumber);

    if (record.m_name[0] == '\0') {
        g_petNameCache.NotFound(key);
    } else {
        g_petNameCache.Store(key, record);
    }

    return 1;
}

// ref: FUN_006351d0
int32_t ReceiveItemNameQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    g_itemNameCache.Response(msg, true);

    return 1;
}

// ref: FUN_00635390
// The record carries its own id: a positive one is stored (FUN_0067f110), and anything else is
// the server's "no such petition" for the negated id (FUN_0067a9b0).
int32_t ReceivePetitionQueryResponse(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    PetitionRec record;
    record.Read(msg);

    if (record.m_id > 0) {
        g_petitionCache.Store(DBCACHEKEY32(record.m_id), record);
    } else {
        g_petitionCache.NotFound(DBCACHEKEY32(-record.m_id));
    }

    return 1;
}

// ref: FUN_00464730
int32_t ReceiveClientCacheVersion(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint32_t session = 0;
    msg->Get(session);

    DBCacheSetSessionAll(session);

    return 1;
}

// ref: FUN_00635b40
void DBCacheRegisterHandlers() {
    ClientServices::SetMessageHandler(SMSG_QUERY_CREATURE_RESPONSE, &ReceiveCreatureQueryResponse, nullptr);
    ClientServices::SetMessageHandler(SMSG_QUERY_GAME_OBJECT_RESPONSE, &ReceiveGameObjectQueryResponse, nullptr);
    ClientServices::SetMessageHandler(SMSG_ITEM_QUERY_SINGLE_RESPONSE, &ReceiveItemQueryResponse, nullptr);
    ClientServices::SetMessageHandler(SMSG_QUERY_PLAYER_NAME_RESPONSE, &ReceiveNameQueryResponse, nullptr);
    ClientServices::SetMessageHandler(SMSG_QUERY_PET_NAME_RESPONSE, &ReceivePetNameQueryResponse, nullptr);
    ClientServices::SetMessageHandler(SMSG_ITEM_NAME_QUERY_RESPONSE, &ReceiveItemNameQueryResponse, nullptr);
    ClientServices::SetMessageHandler(SMSG_PETITION_QUERY_RESPONSE, &ReceivePetitionQueryResponse, nullptr);
    ClientServices::SetMessageHandler(static_cast<NETMESSAGE>(0x39B), &ReceiveItemQueryResponse, nullptr);
    ClientServices::SetMessageHandler(SMSG_CACHE_VERSION, &ReceiveClientCacheVersion, nullptr);
}

// ref: FUN_006355e0
void DBCacheUnregisterHandlers() {
    ClientServices::ClearMessageHandler(SMSG_QUERY_CREATURE_RESPONSE);
    ClientServices::ClearMessageHandler(SMSG_QUERY_GAME_OBJECT_RESPONSE);
    ClientServices::ClearMessageHandler(SMSG_ITEM_QUERY_SINGLE_RESPONSE);
    ClientServices::ClearMessageHandler(SMSG_QUERY_PLAYER_NAME_RESPONSE);
    ClientServices::ClearMessageHandler(SMSG_QUERY_PET_NAME_RESPONSE);
    ClientServices::ClearMessageHandler(SMSG_ITEM_NAME_QUERY_RESPONSE);
    ClientServices::ClearMessageHandler(SMSG_PETITION_QUERY_RESPONSE);
    ClientServices::ClearMessageHandler(static_cast<NETMESSAGE>(0x39B));
    ClientServices::ClearMessageHandler(SMSG_CACHE_VERSION);
}

// ref: FUN_00635060
void DBCacheLoadAll() {
    g_creatureCache.Load();
    g_gameObjectCache.Load();
    g_itemNameCache.Load();
    g_itemCache.Load();
    g_nameCache.Load();
    g_petNameCache.Load();
    g_petitionCache.Load();
}

// ref: FUN_00635100
void DBCacheUpdateAll() {
    g_creatureCache.Update();
    g_gameObjectCache.Update();
    g_itemNameCache.Update();
    g_itemCache.Update();
    g_nameCache.Update();
    g_petNameCache.Update();
    g_petitionCache.Update();
}

// ref: FUN_00635540
void DBCacheShutdownAll() {
    g_creatureCache.Shutdown();
    g_gameObjectCache.Shutdown();
    g_itemNameCache.Shutdown();
    g_itemCache.Shutdown();
    g_nameCache.Shutdown();
    g_petNameCache.Shutdown();
    g_petitionCache.Shutdown();
}

// ref: FUN_00635680
void DBCacheClearUnloadedAll() {
    g_creatureCache.ClearUnloaded();
    g_gameObjectCache.ClearUnloaded();
    g_itemNameCache.ClearUnloaded();
    g_itemCache.ClearUnloaded();
    g_nameCache.ClearUnloaded();
    g_petNameCache.ClearUnloaded();
    g_petitionCache.ClearUnloaded();
}

// ref: FUN_00635710
void DBCacheSetSessionAll(uint32_t session) {
    g_creatureCache.SetSession(session);
    g_gameObjectCache.SetSession(session);
    g_itemNameCache.SetSession(session);
    g_itemCache.SetSession(session);
    g_nameCache.SetSession(session);
    g_petNameCache.SetSession(session);
    g_petitionCache.SetSession(session);
}
