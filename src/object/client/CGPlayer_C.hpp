#ifndef OBJECT_CLIENT_CG_PLAYER_C_HPP
#define OBJECT_CLIENT_CG_PLAYER_C_HPP

#include "object/client/CClientObjCreate.hpp"
#include "object/client/CGPlayer.hpp"
#include "object/client/CGUnit_C.hpp"
#include "net/Types.hpp"
#include <cstdint>

class CreatureModelDataRec;
class CCharacterComponent;

class CGPlayer_C : public CGUnit_C, public CGPlayer {
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

        // Virtual public member functions
        virtual ~CGPlayer_C();

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

        // ref: FUN_005cd920
        // The skill line in one skill-info slot. Only the active player's copy is sent, so any
        // other player answers 0.
        uint16_t GetSkillLineID(uint32_t index) const;
        uint32_t GetMoney() const;
        // The rank in one skill-info slot plus its permanent bonus; a rank of 0 stays 0. The
        // active player's copy only, like GetSkillLineID.
        uint32_t GetSkillRank(uint32_t index) const;
        uint32_t GetNextLevelXP() const;
        uint32_t GetXP() const;
        void PostInit(uint32_t time, const CClientObjCreate& init, bool a4);
        void PostInitActivePlayer();
        void SetStorage(uint32_t* storage, uint32_t* saved);
        void UpdatePartyMemberState();
        void BuildCharacterComponent();

        CCharacterComponent* m_characterComponent = nullptr;
};

uint32_t Player_C_GetDisplayId(uint32_t race, uint32_t sex);

const CreatureModelDataRec* Player_C_GetModelName(uint32_t race, uint32_t sex);

#endif
