#ifndef OBJECT_CLIENT_CG_ITEM_C_HPP
#define OBJECT_CLIENT_CG_ITEM_C_HPP

#include "object/client/CClientObjCreate.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/CGItem.hpp"
#include "net/Types.hpp"

class CDataStore;
class LockRec;

// What the server said a refundable item cost (SMSG_ITEM_REFUND_INFO_RESPONSE), 0x3c bytes.
struct ITEMREFUNDINFO {
    uint32_t money;                 // +0x00
    uint32_t honor;                 // +0x04
    uint32_t arenaPoints;           // +0x08
    int32_t itemID[5];              // +0x0c
    uint32_t itemCount[5];          // +0x20
    uint32_t unk34;                 // +0x34
    uint32_t refundTimeLeft;        // +0x38
};

class CGItem_C : public CGObject_C, public CGItem {
    public:
        // Static functions
        static int32_t GetUseSpellID(int32_t itemID);
        static int32_t GetUseSpellCharges(int32_t itemID);
        static void GetUseSpellCooldowns(int32_t itemID, int32_t* cooldown, int32_t* categoryCooldown, int32_t* category);
        static int32_t GetRandomSuffixPoints(int32_t slot, int32_t randomPropertyID, int32_t suffixFactor);
        static bool CheckLock(const LockRec* lock, int32_t itemLevel, int32_t* spellID, int32_t* skillValue, int32_t* required, int32_t* lockType, CGItem_C** key, uint32_t* index);

        // Virtual public member functions
        virtual ~CGItem_C();

        // Public member functions
        CGItem_C(uint32_t time, CClientObjCreate& objCreate);
        void PostInit(uint32_t time, const CClientObjCreate& init, bool a4);
        void SetStorage(uint32_t* storage, uint32_t* saved);

        // Fields of this item's Item.dbc record, keyed by its entry id; 0 when there is none.
        int32_t GetClassID() const;
        int32_t GetSubclassID() const;
        int32_t GetDisplayInfoID() const;
        int32_t GetInventoryType() const;

        // The item-cache record's flags (index 0) or flags2 (index 1); 0 before it arrives.
        uint32_t GetStatsFlags(int32_t index) const;
        // A guild or arena charter (ITEM_FLAG 0x2000): its enchantment fields hold the petition.
        bool IsCharter() const;

        int32_t GetUseSpellIndex() const;
        int32_t GetUseEnchantment() const;
        int32_t GetMaxCharges() const;
        int32_t GetCharges() const;
        bool HasUseEnchantment() const;
        int32_t GetUseSpell(int32_t effect) const;

        void SetExpiration(int32_t seconds);
        int32_t GetExpirationTimeLeft() const;
        void SetEnchantExpiration(int32_t slot, int32_t seconds);
        int32_t GetEnchantTimeLeft(int32_t slot) const;

        bool HasSoulboundEnchantment() const;
        bool IsSoulbound() const;
        bool HasEnchantments() const;
        bool IsTradeWindowExpired() const;
        int32_t GetSuffixAllocationColumn() const;
        int32_t GetSuffixAllocation() const;
        int32_t GetEnchantPoints(int32_t slot) const;
        int32_t GetPrismaticSocketCount() const;
        int32_t GetSocketCount() const;
        int32_t GetRepairCost() const;
        int32_t GetPageText() const;

        void SetRefundInfo(ITEMREFUNDINFO* info);
        void RequestRefundInfo();
        void SetSocketedGems(uint32_t gem1, uint32_t gem2, uint32_t gem3, uint32_t bonus);

        // Member variables
        // +0x394: bit 0 the item is held by the cursor, bit 1 its info has loaded, bit 2 a refund
        // query is outstanding.
        uint32_t m_itemFlags = 0;
        int32_t m_expiration = 0;               // +0x3a0, time() at which it expires
        int32_t m_enchantExpiration[12] = {};   // +0x3a4, the world clock at which each ends
        ITEMREFUNDINFO* m_refundInfo = nullptr; // +0x3d4
};

// The item and enchantment time and refund messages (FUN_006e6330 / FUN_006d1650).
int32_t ItemTimeUpdateHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t ItemRefundInfoHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

#endif
