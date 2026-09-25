#include "ui/game/EquipmentManager.hpp"
#include <storm/String.hpp>

// The saved sets. Filled from the equipment set list message, which is not handled yet.
static TSList<EQUIPMENT_SET_NODE, TSGetLink<EQUIPMENT_SET_NODE>> s_equipmentSets; // ref: DAT_00acfc00

// ref: FUN_005ae4f0
bool EquipmentManagerHasSet(int32_t setID) {
    for (auto node = s_equipmentSets.Head(); node; node = s_equipmentSets.Next(node)) {
        if (node->set->setID == setID) {
            return true;
        }
    }

    return false;
}

// ref: FUN_005ae5c0
EQUIPMENT_SET* EquipmentManagerGetSet(int32_t setID) {
    for (auto node = s_equipmentSets.Head(); node; node = s_equipmentSets.Next(node)) {
        if (node->set->setID == setID) {
            return node->set;
        }
    }

    return nullptr;
}

// ref: FUN_005ae600
EQUIPMENT_SET* EquipmentManagerGetSetByName(const char* name) {
    if (!name) {
        return nullptr;
    }

    for (auto node = s_equipmentSets.Head(); node; node = s_equipmentSets.Next(node)) {
        if (!SStrCmp(node->set->name, name, STORM_MAX_STR)) {
            return node->set;
        }
    }

    return nullptr;
}
