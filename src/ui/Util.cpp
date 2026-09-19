#include "ui/Util.hpp"
#include "util/Lua.hpp"
#include <storm/String.hpp>

const char* LanguageProcess(const char* string) {
    // TODO
    return string;
}

int32_t StringToBlendMode(const char* string, EGxBlend& blend) {
    struct BlendEntry {
        const char* string;
        EGxBlend value;
    };

    static BlendEntry blendMap[] = {
        { "DISABLE",    GxBlend_Opaque },
        { "BLEND",      GxBlend_Alpha },
        { "ALPHAKEY",   GxBlend_AlphaKey },
        { "ADD",        GxBlend_Add },
        { "MOD",        GxBlend_Mod },
    };

    for (const auto& entry : blendMap) {
        if (!SStrCmpI(entry.string, string)) {
            blend = entry.value;
            return true;
        }
    }

    return false;
}

int32_t StringToBOOL(const char* string) {
    return StringToBOOL(string, false);
}

bool StringToBOOL(const char* string, int32_t def) {
    if (!string) {
        return def;
    }

    switch (*string) {
        case '0':
        case 'F':
        case 'N':
        case 'f':
        case 'n':
            return false;

        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case 'T':
        case 'Y':
        case 't':
        case 'y':
            return true;

        default:
            if (!SStrCmpI(string, "off") || !SStrCmpI(string, "disabled")) {
                return false;
            }

            if (!SStrCmpI(string, "on") || !SStrCmpI(string, "enabled")) {
                return true;
            }

            return def;
    }
}

bool StringToBOOL(lua_State* L, int32_t idx, int32_t def) {
    switch (lua_type(L, idx)) {
        case LUA_TNIL:
            return false;

        case LUA_TBOOLEAN:
            return lua_toboolean(L, idx);

        case LUA_TNUMBER:
            return lua_tonumber(L, idx) != 0;

        case LUA_TSTRING:
            return StringToBOOL(lua_tostring(L, idx), def);

        default:
            return def;
    }
}

uint64_t StringToClickAction(const char* string) {
    if (!string || !*string) {
        return 0;
    }

    if (!SStrCmpI(string, "LeftButtonDown")) {
        return 1;
    }

    if (!SStrCmpI(string, "LeftButtonUp")) {
        return 0x80000000;
    }

    if (!SStrCmpI(string, "MiddleButtonDown")) {
        return 2;
    }

    if (!SStrCmpI(string, "MiddleButtonUp")) {
        return 0;
    }

    if (!SStrCmpI(string, "RightButtonDown")) {
        return 4;
    }

    if (!SStrCmpI(string, "RightButtonUp")) {
        return 0;
    }

    // TODO remaining buttons

    return 0;
}

int32_t StringToDrawLayer(const char* string, int32_t& layer) {
    struct LayerEntry {
        const char* string;
        int32_t layer;
    };

    static LayerEntry layerMap[] = {
        { "BACKGROUND", DRAWLAYER_BACKGROUND },
        { "BORDER",     DRAWLAYER_BACKGROUND_BORDER },
        { "ARTWORK",    DRAWLAYER_ARTWORK },
        { "OVERLAY",    DRAWLAYER_ARTWORK_OVERLAY },
        { "HIGHLIGHT",  DRAWLAYER_HIGHLIGHT },
    };

    for (const auto& entry : layerMap) {
        if (!SStrCmpI(string, entry.string)) {
            layer = entry.layer;
            return true;
        }
    }

    return false;
}

// The name FrameXML uses for an anchor point, for the queries that hand a point back to script.
const char* FramePointToString(FRAMEPOINT point) {
    static const char* const names[FRAMEPOINT_NUMPOINTS] = {
        "TOPLEFT",
        "TOP",
        "TOPRIGHT",
        "LEFT",
        "CENTER",
        "RIGHT",
        "BOTTOMLEFT",
        "BOTTOM",
        "BOTTOMRIGHT",
    };

    return point >= 0 && point < FRAMEPOINT_NUMPOINTS ? names[point] : nullptr;
}

int32_t StringToFramePoint(const char* string, FRAMEPOINT& point) {
    struct FramePointEntry {
        const char* string;
        FRAMEPOINT value;
    };

    static FramePointEntry framePointMap[] = {
        { "BOTTOM",         FRAMEPOINT_BOTTOM },
        { "BOTTOMLEFT",     FRAMEPOINT_BOTTOMLEFT },
        { "BOTTOMRIGHT",    FRAMEPOINT_BOTTOMRIGHT },
        { "CENTER",         FRAMEPOINT_CENTER },
        { "TOP",            FRAMEPOINT_TOP },
        { "TOPRIGHT",       FRAMEPOINT_TOPRIGHT },
        { "TOPLEFT",        FRAMEPOINT_TOPLEFT },
        { "LEFT",           FRAMEPOINT_LEFT },
        { "RIGHT",          FRAMEPOINT_RIGHT },
    };

    for (const auto& entry : framePointMap) {
        if (!SStrCmpI(entry.string, string)) {
            point = entry.value;
            return true;
        }
    }

    return false;
}

// The name FrameXML uses for a strata, for GetFrameStrata.
// GameTooltip anchor names, as FrameXML spells them.
static const struct { const char* name; TOOLTIP_ANCHORPOINT value; } s_tooltipAnchors[] = {
    { "ANCHOR_TOPLEFT",      TOOLTIP_ANCHOR_TOPLEFT },
    { "ANCHOR_LEFT",         TOOLTIP_ANCHOR_LEFT },
    { "ANCHOR_TOPRIGHT",     TOOLTIP_ANCHOR_TOPRIGHT },
    { "ANCHOR_RIGHT",        TOOLTIP_ANCHOR_RIGHT },
    { "ANCHOR_BOTTOMLEFT",   TOOLTIP_ANCHOR_BOTTOMLEFT },
    { "ANCHOR_BOTTOM",       TOOLTIP_ANCHOR_BOTTOM },
    { "ANCHOR_BOTTOMRIGHT",  TOOLTIP_ANCHOR_BOTTOMRIGHT },
    { "ANCHOR_TOP",          TOOLTIP_ANCHOR_TOP },
    { "ANCHOR_CURSOR",       TOOLTIP_ANCHOR_CURSOR },
    { "ANCHOR_NONE",         TOOLTIP_ANCHOR_NONE },
    { "ANCHOR_PRESERVE",     TOOLTIP_ANCHOR_PRESERVE },
    { "ANCHOR_CURSOR_RIGHT", TOOLTIP_ANCHOR_CURSOR_RIGHT },
};

int32_t StringToTooltipAnchor(const char* string, TOOLTIP_ANCHORPOINT& anchor) {
    for (const auto& entry : s_tooltipAnchors) {
        if (!SStrCmpI(entry.name, string)) {
            anchor = entry.value;
            return true;
        }
    }

    return false;
}

const char* TooltipAnchorToString(TOOLTIP_ANCHORPOINT anchor) {
    for (const auto& entry : s_tooltipAnchors) {
        if (entry.value == anchor) {
            return entry.name;
        }
    }

    return nullptr;
}

const char* FrameStrataToString(FRAME_STRATA strata) {
    static const char* const names[] = {
        "WORLD",
        "BACKGROUND",
        "LOW",
        "MEDIUM",
        "HIGH",
        "DIALOG",
        "FULLSCREEN",
        "FULLSCREEN_DIALOG",
        "TOOLTIP",
    };

    int32_t index = static_cast<int32_t>(strata);

    return index >= 0 && index < static_cast<int32_t>(sizeof(names) / sizeof(names[0]))
        ? names[index]
        : nullptr;
}

int32_t StringToFrameStrata(const char* string, FRAME_STRATA& strata) {
    struct FrameStrataEntry {
        const char* string;
        FRAME_STRATA value;
    };

    // FRAME_STRATA_WORLD is hardcoded
    static FrameStrataEntry frameStrataMap[] = {
        { "BACKGROUND",         FRAME_STRATA_BACKGROUND },
        { "LOW",                FRAME_STRATA_LOW },
        { "MEDIUM",             FRAME_STRATA_MEDIUM },
        { "HIGH",               FRAME_STRATA_HIGH },
        { "DIALOG",             FRAME_STRATA_DIALOG },
        { "FULLSCREEN",         FRAME_STRATA_FULLSCREEN },
        { "FULLSCREEN_DIALOG",  FRAME_STRATA_FULLSCREEN_DIALOG },
        { "TOOLTIP",            FRAME_STRATA_TOOLTIP },
    };

    for (const auto& entry : frameStrataMap) {
        if (!SStrCmpI(entry.string, string)) {
            strata = entry.value;
            return true;
        }
    }

    return false;
}

int32_t StringToJustify(const char* string, uint32_t& justify) {
    struct JustifyEntry {
        const char* string;
        uint32_t value;
    };

    static JustifyEntry justifyMap[] = {
        { "LEFT",   0x1 },
        { "CENTER", 0x2 },
        { "RIGHT",  0x4 },
        { "TOP",    0x8 },
        { "MIDDLE", 0x10 },
        { "BOTTOM", 0x20 },
    };

    for (const auto& entry : justifyMap) {
        if (!SStrCmpI(entry.string, string)) {
            justify = entry.value;
            return true;
        }
    }

    return false;
}

int32_t StringToOrientation(const char* string, ORIENTATION& orientation) {
    struct OrientationEntry {
        const char* string;
        ORIENTATION value;
    };

    static OrientationEntry orientationMap[] = {
        { "HORIZONTAL", ORIENTATION_HORIZONTAL },
        { "VERTICAL",   ORIENTATION_VERTICAL },
    };

    for (const auto& entry : orientationMap) {
        if (!SStrCmpI(entry.string, string)) {
            orientation = entry.value;
            return true;
        }
    }

    return false;
}

// ref: FUN_008150d0
const char* JustifyToString(uint32_t justify) {
    static const struct {
        uint32_t value;
        const char* string;
    } justifyNames[] = {
        { 0x1, "LEFT" },
        { 0x2, "CENTER" },
        { 0x4, "RIGHT" },
        { 0x8, "TOP" },
        { 0x10, "MIDDLE" },
        { 0x20, "BOTTOM" },
    };

    for (const auto& entry : justifyNames) {
        if (entry.value & justify) {
            return entry.string;
        }
    }

    return "UNKNOWN";
}
