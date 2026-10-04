#ifndef OBJECT_CLIENT_CG_PLAYER_C_HPP
#define OBJECT_CLIENT_CG_PLAYER_C_HPP

#include "object/client/CClientObjCreate.hpp"
#include "object/client/CGPlayer.hpp"
#include "object/client/CGUnit_C.hpp"
#include "net/Types.hpp"
#include <cstdint>
#include "object/client/CGBag_C.hpp"

#include <storm/List.hpp>

class SpellRec;

class CGItem_C;

// An enchantment expiration for an item the client has not created yet (FUN_006e61b0), applied
// when the item arrives (FUN_006e6250). 0x18 bytes.
struct PlayerPendingItemExpiration {
    TSLink<PlayerPendingItemExpiration> m_link;
    WOWGUID item;
    int32_t slot;
    int32_t seconds;
};

// One equipped item of an inspected player, or of a player seen only through their visible items
// (FUN_006de360). 0x48 bytes.
struct INVENTORYITEMINFO {
    WOWGUID creator;                // +0x00
    int32_t itemID;                 // +0x08
    int32_t enchant[12];            // +0x0c
    int16_t randomPropertyID;       // +0x3c
    uint8_t flags;                  // +0x3e bit 0: the visible item's id was negative
    uint8_t pad3f;
    int32_t suffixFactor;           // +0x40
    int32_t unk44;
};

// The inspected player's gear, filled by SMSG_INSPECT_RESULTS: their guid, then the nineteen
// equipped slots.
struct INSPECTDATA {
    WOWGUID guid;
    INVENTORYITEMINFO items[19];
};

extern INSPECTDATA* g_inspectData;                  // ref: DAT_00c9eae0

class CreatureModelDataRec;
class CCharacterComponent;

class CGPlayer_C : public CGUnit_C, public CGPlayer {
    public:
        // +0x18f0: the player's inventory, all 150 slots for the active player and none for anyone
        // else, walked in section order with the bank's sections gated on the bank being open.
        CGBag_C m_bag;

        // +0x1920: the stand state the server last set for the active player (FUN_006e2b30).
        uint32_t m_requestedStandState = 0;

    // The object the player has open for looting (reference +0x18e0), zero when nothing is open.
    // CGUnit_C::IsLooting and CanShowLootAnimation read it through this class exactly as the
    // reference does, after checking the unit IS the active player. Nothing sets it yet.
    public:
        WOWGUID m_lootTarget = 0;
    public:
        // The logged-in character, kept past the glue. The reference holds one CHARACTER_INFO at
        // 00c79d10 and reads the player's own name, race, class, sex and level straight out of it
        // rather than through the object manager, which is how the player frame has a name before
        // -- and independently of -- the player object resolving. Frozen's glue clears
        // CCharacterSelection::s_characterList on the way into the world, so this copy is taken
        // before that happens.
        static CHARACTER_INFO s_localPlayerInfo;

        // Public static functions
        static CGPlayer_C* GetActivePtr();

        // ref: FUN_006b1050
        static const CHARACTER_INFO* GetLocalPlayerInfo();

        // ref: FUN_006b1060
        static const char* GetLocalPlayerName();

        // ref: FUN_006b1070
        static uint8_t GetLocalPlayerRace();

        // ref: FUN_006b1080
        static uint8_t GetLocalPlayerClass();

        // ref: FUN_006b1090
        static uint8_t GetLocalPlayerSex();

        // ref: FUN_006b10a0
        static uint8_t GetLocalPlayerLevel();

        static void SetLocalPlayerInfo(const CHARACTER_INFO& info);

        // The subclass mask the player is proficient in, per item class (0..16). The reference
        // tests an item's subclass bit against it before calling the item usable. Nothing fills it
        // yet, so every class reads 0 -- which the callers treat as "no restriction".
        static uint32_t s_itemProficiency[17];

        // ref: FUN_006cde90
        // The mask for one item class; 0 past the seventeenth.
        static uint32_t GetItemProficiency(uint8_t itemClass);

        // ref: FUN_006d4450
        // CMSG_GROUP_ACCEPT with the accept flags the binding assembled.
        static void SendGroupAccept(uint32_t flags);

        // Who offered the pending resurrection, cleared once it is answered. Nothing sets it yet:
        // SMSG_RESURRECT_REQUEST is not handled.
        static WOWGUID s_resurrectRequester;    // ref: DAT_00c9eab8

        // The NPCs the Check*Dist bindings measure against. Nothing sets them yet: the gossip
        // handlers that do are not ported.
        static WOWGUID s_spiritHealerGUID;      // ref: DAT_00c9eb10
        static WOWGUID s_talentMasterGUID;      // ref: DAT_00c9eb18
        static WOWGUID s_binderGUID;            // ref: DAT_00c9eb20

        // ref: FUN_006d1d30
        // CMSG_RESURRECT_RESPONSE to the pending offer, if there is one.
        static void SendResurrectResponse(uint8_t accept);

        // ref: FUN_006d1e20
        static void SendGossipHello(const WOWGUID& guid);

        // ref: FUN_006d1ea0
        static void SendQuestGiverHello(const WOWGUID& guid);

        // ref: FUN_006d2340
        static void SendBankerActivate(const WOWGUID& guid);

        // ref: FUN_006d2480
        static void SendPetitionShowList(const WOWGUID& guid);

        // ref: FUN_006d25c0
        static void SendBattlemasterHello(const WOWGUID& guid);

        // ref: FUN_006d2640
        static void SendAuctionHello(const WOWGUID& guid);

        // ref: FUN_006d26c0
        static void SendListStabledPets(const WOWGUID& guid);

        // ref: FUN_006d2740
        static void SendSpellClick(const WOWGUID& guid);

        // ref: FUN_006d27c0
        static void SendPlayerVehicleEnter(const WOWGUID& guid);

        // ref: FUN_006d2c20
        static void SendAutostoreLootItem(uint8_t slot);

        // ref: FUN_006d2d40
        static void SendSellItem(WOWGUID vendor, WOWGUID item, uint32_t count);

        // ref: FUN_006d2ea0
        // entry points at the merchant slot's pair: [0] is sent second, [1] first. Nothing is sent
        // without it.
        static void SendBuyItemInSlot(WOWGUID vendor, const uint32_t* entry, uint32_t count, WOWGUID bag, uint8_t bagSlot);

        // ref: FUN_006d43c0
        static void SendGroupUninviteGuid(WOWGUID guid, const char* reason);

        // ref: FUN_006d45b0
        static void SendPartySilence(WOWGUID guid, uint8_t value);

        // ref: FUN_006d4640
        static void SendPartyUnsilence(WOWGUID guid, uint8_t value);

        // ref: FUN_006d4c10
        static void SendQuestGiverQueryQuest(const WOWGUID& giver, uint32_t questID);

        // ref: FUN_006d4e60
        static void SendQuestGiverChooseReward(const WOWGUID& giver, uint32_t questID, uint32_t reward);

        // ref: FUN_006d4f00
        static void SendPushQuestToParty(uint32_t questID);

        // ref: FUN_006d5c90
        static void SendReadItem(uint8_t bag, uint8_t slot);

        // ref: FUN_006d62a0
        static void SendEnableTaxiNode(const WOWGUID& guid);

        // ref: FUN_006d6320
        static void SendTaxiQueryAvailableNodes(const WOWGUID& guid);

        // ref: FUN_006d6e00
        // Nothing is sent for a null guid.
        static void SendPetitionShowSignatures(WOWGUID guid);

        // ref: FUN_006d7200
        // MSG_PARTY_ASSIGNMENT setting an assignment on a member.
        static void SendPartyAssignmentSet(uint8_t assignment, WOWGUID guid);

        // ref: FUN_006d7290
        // MSG_PARTY_ASSIGNMENT clearing an assignment from a member.
        static void SendPartyAssignmentClear(uint8_t assignment, WOWGUID guid);

        // Virtual public member functions
        virtual ~CGPlayer_C();
        void* Virtual024() override;                // 0x024, the same bag as GetBag
        CGBag_C* GetBag() override;                 // 0x028, the player's own bag (+0x18f0)

        // Public member functions
        CGPlayer_C(uint32_t time, CClientObjCreate& objCreate);
        // The two halves of a school's spell power. Both refuse for anyone but the active
        // player, because these descriptor fields are only ever sent for them -- reading another
        // unit's copy would report whatever was last left there.
        //
        // school is 0-based here; the Lua side is 1-based and converts.
        int32_t GetModDamageDonePos(uint32_t school) const;
        int32_t GetModDamageDoneNeg(uint32_t school) const;

        // The percentage modifier for a school, the third of the set. A float where the other two
        // are integers.
        float GetModDamageDonePct(uint32_t school) const;

        int32_t GetAttackPowerForStat(int32_t stat, int32_t value) const;

        // ref: FUN_006d1fc0
        // Within the spirit healer's combat reach plus 4 yards.
        int32_t IsInSpiritHealerRange() const;

        // ref: FUN_006d2070
        int32_t IsInTalentMasterRange() const;

        // ref: FUN_006d2290
        int32_t IsInBinderRange() const;

        // ref: FUN_006d5e70
        // The base language of the player's race; 0 when the race has no record.
        int32_t GetBaseLanguage() const;

        // ref: FUN_006d6e90
        // The side the race's faction template belongs to: 0 Horde, 1 Alliance, -1 neither.
        int32_t GetFactionSide() const;

        // ref: FUN_006dc1c0
        // The skill-info slot holding a skill line, -1 when none does.
        int32_t GetSkillIndex(uint32_t skillLine) const;

        // ref: FUN_006de330
        // One visible-item entry, null outside 0..18.
        CVisibleItemData* GetVisibleItem(uint32_t slot) const;

        // ref: FUN_005cd920
        // The skill line in one skill-info slot. Only the active player's copy is sent, so any
        // other player answers 0.
        uint16_t GetSkillLineID(uint32_t index) const;
        WOWGUID GetFarsightObject() const;
        uint32_t GetMoney() const;

        // ref: FUN_004f7290
        // How drunk the player is, 0 to 1: the larger of the server's drunk state and the faked
        // one, capped at a hundred.
        float GetDrunkenness() const;

        // ref: FUN_006e0fd0
        // PLAYER_FLAGS moved from `oldFlags`: name plates, portraits, the PvP timer and toggle
        // messages, the interface events the flags drive, and for the active player the move into
        // or out of the ghost state.
        void OnFlagsChanged(uint32_t oldFlags);

        // ref: FUN_006de750
        // Arm or clear the PvP flag's timer for a change of PLAYER_FLAGS from `oldFlags`.
        void UpdatePvPFlagTimer(uint32_t oldFlags);

        // ref: FUN_006df710
        // The active player became a ghost or came back to life.
        void OnGhostChanged();
        // The rank in one skill-info slot plus its permanent bonus; a rank of 0 stays 0. The
        // active player's copy only, like GetSkillLineID.
        uint32_t GetSkillRank(uint32_t index) const;
        uint32_t GetNextLevelXP() const;
        uint32_t GetXP() const;
        void PostInit(uint32_t time, const CClientObjCreate& init, bool a4);
        void PostInitActivePlayer();

        // ref: FUN_006de230
        CGItem_C* GetEquippedItemForSpell(const SpellRec* spell, uint32_t slots);
        void SetStorage(uint32_t* storage, uint32_t* saved);
        void UpdatePartyMemberState();
        // ref: FUN_006e04d0 / FUN_006dc7e0 / FUN_006e05d0
        // A player's hands hold its visible items, read through the item cache.
        virtual const UNIT_WEAPON_INFO* GetWeaponInfo(int32_t hand, int32_t ignoreHidden);
        virtual const ItemDisplayInfoRec* GetWeaponDisplay(int32_t hand);
        virtual int32_t GetWeaponDisplayID(int32_t hand);

        // ref: FUN_006e09e0
        // Every visible item onto the player's component and into its hands.
        int32_t ApplyVisibleItems();

        // ref: FUN_006e08c0
        void ApplyVisibleItem(const CVisibleItemData* item, int32_t slot);

        // ref: FUN_006de840
        // The active player sees others in their natural form (player flags byte 0xf42 bit 1).
        static bool ActivePlayerSeesNatural();

        const char* GetPvpRankName(int32_t rank) const;
        int32_t GetPlayedTime() const;
        void SetPlayedTime(int32_t seconds);
        int32_t GetSkillValue(uint32_t skillLine) const;
        void UpdateGemColorCounts();
        const INVENTORYITEMINFO* GetInventoryItemInfo(uint32_t slot) const;
        void AddPendingItemExpiration(WOWGUID item, int32_t slot, int32_t seconds);
        void ApplyPendingItemExpirations(CGItem_C* item);

        // +0x18b0: enchantment expirations that arrived before their item object did.
        STORM_EXPLICIT_LIST(PlayerPendingItemExpiration, m_link) m_pendingItemExpirations;

        // +0x1924: the equipped gems of each colour (meta, red, yellow, blue) that meta gem
        // conditions count.
        int32_t m_gemColorCounts[4] = {};

        // +0x194c: the played time SMSG_PLAYED_TIME last reported, -1 before it has, and the
        // time() at which it arrived.
        // +0x1940: when the player's PvP flag lapses (the world clock), 0 while PvP is on by choice.
        uint32_t m_pvpFlagExpire = 0;
        int32_t m_playedTime = -1;
        int32_t m_playedTimeReceived = 0;

        // +0x1908: what each hand holds (GetWeaponInfo).
        UNIT_WEAPON_INFO m_handInfo[3];

        // +0x1858: how the player sees the world. 0x1 through its farsight object, 0x2, 0x4 and
        // 0x8 not identified. PHASE4(Player_C): its writers (FUN_006e29b5 and friends) are the
        // Player_C port's.
        uint32_t m_viewFlags = 0;
};

uint32_t Player_C_GetDisplayId(uint32_t race, uint32_t sex);

const CreatureModelDataRec* Player_C_GetModelName(uint32_t race, uint32_t sex);

// Whether an inventory slot lies in one of the location groups `mask` selects: 0x1 equipped
// (0..18), 0x2 bag slots (19..22), 0x4 backpack (23..38), 0x8 bank and bank bag slots (39..73),
// 0x40 keyring (86..117), 0x200 currency tokens (118..149).
bool InventorySlotInLocations(uint32_t slot, uint32_t mask);

// ref: FUN_006e8ee0
void PlayerInitialize();

// ref: FUN_006e45d0
// The player descriptor's field handlers.
void PlayerRegisterFieldHandlers();

// ref: FUN_006da770
// The active player's watcher on PLAYER_FIELD_BYTES byte 3 (the invisibility glow).
int32_t PlayerOnFieldBytesChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param);

// ref: FUN_006ceec0
void PlayerReadBindPoint(CDataStore* msg);

// ref: FUN_006cef10
int32_t PlayerGetBindAreaID();

#endif
