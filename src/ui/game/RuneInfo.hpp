#ifndef UI_GAME_RUNE_INFO_HPP
#define UI_GAME_RUNE_INFO_HPP

#include "net/Types.hpp"
#include <cstdint>

class CDataStore;
class SpellRec;

// A death knight's runes, as the reference keeps them beside the paper doll (0x00c24304..
// 0x00c24388): eight slots, each with its current type (0 blood, 1 unholy, 2 frost, 3 death), the
// type it reverts to, when it started regenerating and when it will be ready.

void RuneReset();
void RuneSetRegenStart(int32_t rune, uint32_t start);
bool RuneIsValid(uint32_t rune);
uint32_t RuneGetType(int32_t rune, bool base);
void RuneConvert(int32_t rune, uint32_t type);
uint32_t RuneGetRegenStart(int32_t rune);
void RuneSetCooldown(int32_t rune, float progress);
uint32_t RuneGetCooldownStart(int32_t rune);
void RuneUpdateReady(uint32_t oldReady, uint32_t newReady, const uint8_t* progress);
int32_t RuneCountReady(int32_t type);
uint32_t RuneGetReadyMask();
int32_t RuneGetCount();

// Whether the active player has the runes `spellID` costs; the shortfall of each type goes to
// `missing` (blood, unholy, frost, death) when it is not null.
bool SpellHasRunes(int32_t spellID, int32_t* missing);

// When the runes a spell lacks will next be ready. False when it lacks none.
bool SpellGetRuneCooldown(const SpellRec* spell, uint32_t* ready);

int32_t RuneConvertHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t RuneResyncHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t RuneAddPowerHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

#endif
