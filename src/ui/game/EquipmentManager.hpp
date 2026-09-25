#ifndef UI_GAME_EQUIPMENT_MANAGER_HPP
#define UI_GAME_EQUIPMENT_MANAGER_HPP

#include <storm/List.hpp>
#include <cstdint>

// One saved equipment set. Only the fields ported code reads are named. The layout past the name
// (and the name's own length) is not recovered: nothing ported depends on the size.
struct EQUIPMENT_SET {
    uint8_t unk00[0x10];
    int32_t setID;          // +0x10
    uint8_t unk14[0x100];
    // +0x114, read as a C string; its length (and so the struct's size) is not recovered yet.
    // Only ever reached through the node's pointer, never allocated or copied here.
    char name[1];
};

struct EQUIPMENT_SET_NODE : TSLinkedNode<EQUIPMENT_SET_NODE> {
    EQUIPMENT_SET* set;
};

bool EquipmentManagerHasSet(int32_t setID);

EQUIPMENT_SET* EquipmentManagerGetSet(int32_t setID);

EQUIPMENT_SET* EquipmentManagerGetSetByName(const char* name);

#endif
