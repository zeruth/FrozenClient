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

#include "ui/FrameScript.hpp"
#include <cstring>

// ref: FUN_005ae380
bool EquipmentManagerGetSetNames(char* dest, uint32_t destSize, const WOWGUID& item) {
    auto left = destSize - 1;
    const char* separator = "";
    auto delimiter = FrameScript_GetText("PLAYER_LIST_DELIMITER", -1, GENDER_NOT_APPLICABLE);
    auto cursor = dest;

    for (auto node = s_equipmentSets.Head(); node; node = s_equipmentSets.Next(node)) {
        auto set = node->set;

        for (int32_t slot = 0; slot < 19; slot++) {
            if (set->items[slot] != item) {
                continue;
            }

            auto nameLength = SStrLen(set->name);
            auto separatorLength = SStrLen(separator);

            if (left < separatorLength + nameLength) {
                break;
            }

            memcpy(cursor, separator, separatorLength);
            memcpy(cursor + separatorLength, set->name, nameLength);
            cursor += separatorLength + nameLength;
            left -= separatorLength + nameLength;
            separator = delimiter;
        }
    }

    *cursor = '\0';

    return cursor != dest;
}
