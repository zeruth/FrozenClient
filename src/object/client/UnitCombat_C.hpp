// UnitCombat_C.cpp: melee combat as the client sees it -- the attack messages, the swing a unit
// plays when the server reports it, and what the hit does to the victim when the swing's
// animation lands it (wound, parry, block, dodge, the impact sounds and the blood).
//
// SMSG_ATTACKER_STATE_UPDATE arrives before the swing lands: the attacker keeps the hit
// (CGUnit_C::m_combatInfo) and plays its swing, and the hit is applied when the swing's "$CAH"
// (or "$AH0".."$AH3") event fires -- or at once, when the attacker is not in view.
#ifndef OBJECT_CLIENT_UNIT_COMBAT_C_HPP
#define OBJECT_CLIENT_UNIT_COMBAT_C_HPP

#include <tempest/Vector.hpp>
#include <cstdint>

class CDataStore;
class CGUnit_C;
struct CombatInfo;

// ref: FUN_00756bd0
void UnitCombatInitialize();

// ref: FUN_00755630
void CombatInfoRead(CombatInfo* info, CDataStore* msg);

// ref: FUN_00756240
// The combat animation events: "$AH0".."$AH3" and "$CAH" (the hit lands), "$DTH" (death),
// "$BWP" (a bow drawn), "$CPP" (the victim's parry or block), "$CSS" (the swing's whoosh).
void UnitCombatAnimEvent(CGUnit_C* unit, uint32_t eventId, uint32_t eventData, const C3Vector* position);

// ref: FUN_00756180
// Apply and forget a hit the unit still holds (its swing never landed it).
void UnitFlushCombatInfo(CGUnit_C* unit);

// ref: FUN_007559e0
// CMSG_ATTACK_STOP.
void UnitSendAttackStop(CGUnit_C* unit);

// ref: FUN_00746d60
// A spoken or emote sound at the unit's head, from the unit's voice object.
void UnitPlayVoiceSound(CGUnit_C* unit, int32_t soundID, int32_t emote, uint32_t type);

// The weapon sound tables (SoundInterface2DSP.cpp).

// ref: FUN_004d0b70 (with FUN_004d04e0 and the subclass count of FUN_00634cc0)
void WeaponSoundsInitialize();

#endif
