#ifndef UI_GAME_EQUIPMENT_MANAGER_HPP
#define UI_GAME_EQUIPMENT_MANAGER_HPP

#include "util/GUID.hpp"
#include <storm/List.hpp>
#include <cstdint>

// One saved equipment set. Only the fields ported code reads are named. The layout past the name
// (and the name's own length) is not recovered: nothing ported depends on the size.
struct EQUIPMENT_SET {
    uint8_t unk00[0x10];
    int32_t setID;          // +0x10
    uint8_t unk14[0x100];
    // +0x114, read as a C string, running at most up to the item guids.
    char name[0x84];
    WOWGUID items[19];      // +0x198
};

struct EQUIPMENT_SET_NODE : TSLinkedNode<EQUIPMENT_SET_NODE> {
    EQUIPMENT_SET* set;
};

bool EquipmentManagerHasSet(int32_t setID);

EQUIPMENT_SET* EquipmentManagerGetSet(int32_t setID);

EQUIPMENT_SET* EquipmentManagerGetSetByName(const char* name);

// The names of the sets holding `item`, joined by PLAYER_LIST_DELIMITER. False when none does.
bool EquipmentManagerGetSetNames(char* dest, uint32_t destSize, const WOWGUID& item);

#endif
