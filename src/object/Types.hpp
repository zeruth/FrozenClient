#ifndef OBJECT_TYPES_HPP
#define OBJECT_TYPES_HPP

enum INVENTORY_SLOTS {
    INVSLOT_HEAD        = 0,
    INVSLOT_FIRST       = INVSLOT_HEAD,
    EQUIPPED_FIRST      = INVSLOT_HEAD,
    INVSLOT_NECK        = 1,
    INVSLOT_SHOULDER    = 2,
    INVSLOT_BODY        = 3,
    INVSLOT_CHEST       = 4,
    INVSLOT_WAIST       = 5,
    INVSLOT_LEGS        = 6,
    INVSLOT_FEET        = 7,
    INVSLOT_WRIST       = 8,
    INVSLOT_HAND        = 9,
    INVSLOT_FINGER1     = 10,
    INVSLOT_FINGER2     = 11,
    INVSLOT_TRINKET1    = 12,
    INVSLOT_TRINKET2    = 13,
    INVSLOT_BACK        = 14,
    INVSLOT_MAINHAND    = 15,
    INVSLOT_OFFHAND     = 16,
    INVSLOT_RANGED      = 17,
    INVSLOT_TABARD      = 18,
    EQUIPPED_LAST       = INVSLOT_TABARD,
    INVSLOT_BAG0        = 19,
    INVSLOT_BAGFIRST    = INVSLOT_BAG0,
    INVSLOT_BAG1        = 20,
    INVSLOT_BAG2        = 21,
    INVSLOT_BAG3        = 22,
    INVSLOT_BAGLAST     = INVSLOT_BAG3,
    INVSLOT_LAST        = INVSLOT_BAG3,

    // TODO

    NUM_INVENTORY_SLOTS = INVSLOT_LAST - INVSLOT_FIRST + 1,
    NUM_BAG_SLOTS       = INVSLOT_BAGLAST - INVSLOT_BAGFIRST + 1,
};

enum OBJECT_TYPE {
    TYPE_OBJECT             = 0x1,
    TYPE_ITEM               = 0x2,
    TYPE_CONTAINER          = 0x4,
    TYPE_UNIT               = 0x8,
    TYPE_PLAYER             = 0x10,
    TYPE_GAMEOBJECT         = 0x20,
    TYPE_DYNAMICOBJECT      = 0x40,
    TYPE_CORPSE             = 0x80,
    // TODO
    HIER_TYPE_OBJECT        = TYPE_OBJECT,
    HIER_TYPE_ITEM          = TYPE_OBJECT | TYPE_ITEM,
    HIER_TYPE_CONTAINER     = TYPE_OBJECT | TYPE_ITEM | TYPE_CONTAINER,
    HIER_TYPE_UNIT          = TYPE_OBJECT | TYPE_UNIT,
    HIER_TYPE_PLAYER        = TYPE_OBJECT | TYPE_UNIT | TYPE_PLAYER,
    HIER_TYPE_GAMEOBJECT    = TYPE_OBJECT | TYPE_GAMEOBJECT,
    HIER_TYPE_DYNAMICOBJECT = TYPE_OBJECT | TYPE_DYNAMICOBJECT,
    HIER_TYPE_CORPSE        = TYPE_OBJECT | TYPE_CORPSE,
    // TODO
};

enum OBJECT_TYPE_ID {
    ID_OBJECT           = 0,
    ID_ITEM             = 1,
    ID_CONTAINER        = 2,
    ID_UNIT             = 3,
    ID_PLAYER           = 4,
    ID_GAMEOBJECT       = 5,
    ID_DYNAMICOBJECT    = 6,
    ID_CORPSE           = 7,
    NUM_CLIENT_OBJECT_TYPES,
    // TODO
};

enum OUT_OF_RANGE_TYPE {
    OUT_OF_RANGE_0      = 0,
    OUT_OF_RANGE_1      = 1,
    OUT_OF_RANGE_2      = 2,
};

enum PLAYER_TYPE {
    PLAYER_NORMAL       = 0,
    PLAYER_BOT          = 1,
};

enum INVENTORY_TYPE {
    INVTYPE_NON_EQUIP       = 0,
    INVTYPE_HEAD            = 1,
    INVTYPE_NECK            = 2,
    INVTYPE_SHOULDER        = 3,
    INVTYPE_BODY            = 4,
    INVTYPE_CHEST           = 5,
    INVTYPE_WAIST           = 6,
    INVTYPE_LEGS            = 7,
    INVTYPE_FEET            = 8,
    INVTYPE_WRISTS          = 9,
    INVTYPE_HANDS           = 10,
    INVTYPE_FINGER          = 11,
    INVTYPE_TRINKET         = 12,
    INVTYPE_WEAPON          = 13,
    INVTYPE_SHIELD          = 14,
    INVTYPE_RANGED          = 15,
    INVTYPE_CLOAK           = 16,
    INVTYPE_2HWEAPON        = 17,
    INVTYPE_BAG             = 18,
    INVTYPE_TABARD          = 19,
    INVTYPE_ROBE            = 20,
    INVTYPE_WEAPONMAINHAND  = 21,
    INVTYPE_WEAPONOFFHAND   = 22,
    INVTYPE_HOLDABLE        = 23,
    INVTYPE_AMMO            = 24,
    INVTYPE_THROWN          = 25,
    INVTYPE_RANGEDRIGHT     = 26,
    INVTYPE_QUIVER          = 27,
    INVTYPE_RELIC           = 28,
};

enum SHEATHE_TYPE {
    SHEATHE_0           = 0,
    SHEATHE_1           = 1,
    SHEATHE_2           = 2,
    SHEATHE_3           = 3,
    SHEATHE_4           = 4,
    NUM_SHEATHE_TYPES
};

enum UNIT_SEX {
    UNITSEX_MALE        = 0,
    UNITSEX_FEMALE      = 1,
    UNITSEX_NUM_SEXES   = 2,
    UNITSEX_NONE        = 2,
    UNITSEX_LAST,
    UNITSEX_BOTH        = 3,
};

#endif
