#include "ui/Util.hpp"
#include "event/Types.hpp"
#include "gx/font/TextBlock.hpp"
#include "util/Lua.hpp"
#include <storm/String.hpp>
#include <tempest/Rect.hpp>
#include <tempest/Vector.hpp>

const char* LanguageProcess(const char* string) {
    // TODO
    return string;
}

namespace {

struct BlendEntry {
    const char* string;
    EGxBlend value;
};

// One table, read from both directions, which is how the reference keeps the two conversions from
// drifting apart: FUN_00815040 walks the same five pairs StringToBlendMode matches against.
BlendEntry g_blendMap[] = {
    { "DISABLE",    GxBlend_Opaque },
    { "BLEND",      GxBlend_Alpha },
    { "ALPHAKEY",   GxBlend_AlphaKey },
    { "ADD",        GxBlend_Add },
    { "MOD",        GxBlend_Mod },
};

}

// ref: FUN_007dc010
// The mouse button a FrameXML name stands for, as a MOUSEBUTTON flag, or 0 for a name that is not
// one. The reference spells all thirty-one comparisons out in a row; the mapping underneath is
// uniform -- LeftButton is bit 0, MiddleButton bit 1, RightButton bit 2, and ButtonN bit N-1 from
// Button4 up to Button31 -- so the tail is a loop here. The first three are deliberately not in
// that sequence: the reference orders them left, right, middle while the bits go left, middle,
// right, and following the bits instead would silently swap two buttons.
uint32_t StringToMouseButton(const char* name) {
    if (!name || !*name) {
        return 0;
    }

    if (!SStrCmpI(name, "LeftButton", STORM_MAX_STR)) {
        return MOUSE_BUTTON_LEFT;
    }

    if (!SStrCmpI(name, "RightButton", STORM_MAX_STR)) {
        return MOUSE_BUTTON_RIGHT;
    }

    if (!SStrCmpI(name, "MiddleButton", STORM_MAX_STR)) {
        return MOUSE_BUTTON_MIDDLE;
    }

    for (int32_t button = 4; button <= 31; button++) {
        char candidate[16];
        SStrPrintf(candidate, sizeof(candidate), "Button%d", button);

        if (!SStrCmpI(name, candidate, STORM_MAX_STR)) {
            return 1u << (button - 1);
        }
    }

    return 0;
}

int32_t StringToBlendMode(const char* string, EGxBlend& blend) {
    for (const auto& entry : g_blendMap) {
        if (!SStrCmpI(entry.string, string)) {
            blend = entry.value;
            return true;
        }
    }

    return false;
}

// ref: FUN_00815040
// A blend mode frozen supports but the table does not name comes back as "UNKNOWN" rather than
// null, so a caller pushing this straight onto the Lua stack cannot push nil by accident.
const char* BlendModeToString(EGxBlend blend) {
    for (const auto& entry : g_blendMap) {
        if (entry.value == blend) {
            return entry.string;
        }
    }

    return "UNKNOWN";
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

// ref: FUN_0096e9c0
// The reference compares all 64 names in this order; ButtonNDown is bit N-1 and ButtonNUp is
// bit N+30, so the down bits are 0..30 and the up bits 31..61. AnyDown and AnyUp are the two
// masks, and AnyUp sets bits 31..63 as the reference writes it.
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
        return 0x100000000ull;
    }

    if (!SStrCmpI(string, "RightButtonDown")) {
        return 4;
    }

    if (!SStrCmpI(string, "RightButtonUp")) {
        return 0x200000000ull;
    }

    if (!SStrCmpI(string, "Button4Down")) {
        return 8;
    }

    if (!SStrCmpI(string, "Button4Up")) {
        return 0x400000000ull;
    }

    if (!SStrCmpI(string, "Button5Down")) {
        return 0x10;
    }

    if (!SStrCmpI(string, "Button5Up")) {
        return 0x800000000ull;
    }

    if (!SStrCmpI(string, "Button6Down")) {
        return 0x20;
    }

    if (!SStrCmpI(string, "Button6Up")) {
        return 0x1000000000ull;
    }

    if (!SStrCmpI(string, "Button7Down")) {
        return 0x40;
    }

    if (!SStrCmpI(string, "Button7Up")) {
        return 0x2000000000ull;
    }

    if (!SStrCmpI(string, "Button8Down")) {
        return 0x80;
    }

    if (!SStrCmpI(string, "Button8Up")) {
        return 0x4000000000ull;
    }

    if (!SStrCmpI(string, "Button9Down")) {
        return 0x100;
    }

    if (!SStrCmpI(string, "Button9Up")) {
        return 0x8000000000ull;
    }

    if (!SStrCmpI(string, "Button10Down")) {
        return 0x200;
    }

    if (!SStrCmpI(string, "Button10Up")) {
        return 0x10000000000ull;
    }

    if (!SStrCmpI(string, "Button11Down")) {
        return 0x400;
    }

    if (!SStrCmpI(string, "Button11Up")) {
        return 0x20000000000ull;
    }

    if (!SStrCmpI(string, "Button12Up")) {
        return 0x40000000000ull;
    }

    if (!SStrCmpI(string, "Button12Down")) {
        return 0x800;
    }

    if (!SStrCmpI(string, "Button13Up")) {
        return 0x80000000000ull;
    }

    if (!SStrCmpI(string, "Button13Down")) {
        return 0x1000;
    }

    if (!SStrCmpI(string, "Button14Up")) {
        return 0x100000000000ull;
    }

    if (!SStrCmpI(string, "Button14Down")) {
        return 0x2000;
    }

    if (!SStrCmpI(string, "Button15Up")) {
        return 0x200000000000ull;
    }

    if (!SStrCmpI(string, "Button15Down")) {
        return 0x4000;
    }

    if (!SStrCmpI(string, "Button16Up")) {
        return 0x400000000000ull;
    }

    if (!SStrCmpI(string, "Button16Down")) {
        return 0x8000;
    }

    if (!SStrCmpI(string, "Button17Up")) {
        return 0x800000000000ull;
    }

    if (!SStrCmpI(string, "Button17Down")) {
        return 0x10000;
    }

    if (!SStrCmpI(string, "Button18Up")) {
        return 0x1000000000000ull;
    }

    if (!SStrCmpI(string, "Button18Down")) {
        return 0x20000;
    }

    if (!SStrCmpI(string, "Button19Up")) {
        return 0x2000000000000ull;
    }

    if (!SStrCmpI(string, "Button19Down")) {
        return 0x40000;
    }

    if (!SStrCmpI(string, "Button20Down")) {
        return 0x80000;
    }

    if (!SStrCmpI(string, "Button20Up")) {
        return 0x4000000000000ull;
    }

    if (!SStrCmpI(string, "Button21Down")) {
        return 0x100000;
    }

    if (!SStrCmpI(string, "Button21Up")) {
        return 0x8000000000000ull;
    }

    if (!SStrCmpI(string, "Button22Up")) {
        return 0x10000000000000ull;
    }

    if (!SStrCmpI(string, "Button22Down")) {
        return 0x200000;
    }

    if (!SStrCmpI(string, "Button23Up")) {
        return 0x20000000000000ull;
    }

    if (!SStrCmpI(string, "Button23Down")) {
        return 0x400000;
    }

    if (!SStrCmpI(string, "Button24Up")) {
        return 0x40000000000000ull;
    }

    if (!SStrCmpI(string, "Button24Down")) {
        return 0x800000;
    }

    if (!SStrCmpI(string, "Button25Up")) {
        return 0x80000000000000ull;
    }

    if (!SStrCmpI(string, "Button25Down")) {
        return 0x1000000;
    }

    if (!SStrCmpI(string, "Button26Up")) {
        return 0x100000000000000ull;
    }

    if (!SStrCmpI(string, "Button26Down")) {
        return 0x2000000;
    }

    if (!SStrCmpI(string, "Button27Up")) {
        return 0x200000000000000ull;
    }

    if (!SStrCmpI(string, "Button27Down")) {
        return 0x4000000;
    }

    if (!SStrCmpI(string, "Button28Up")) {
        return 0x400000000000000ull;
    }

    if (!SStrCmpI(string, "Button28Down")) {
        return 0x8000000;
    }

    if (!SStrCmpI(string, "Button29Up")) {
        return 0x800000000000000ull;
    }

    if (!SStrCmpI(string, "Button29Down")) {
        return 0x10000000;
    }

    if (!SStrCmpI(string, "Button30Down")) {
        return 0x20000000;
    }

    if (!SStrCmpI(string, "Button30Up")) {
        return 0x1000000000000000ull;
    }

    if (!SStrCmpI(string, "Button31Down")) {
        return 0x40000000;
    }

    if (!SStrCmpI(string, "Button31Up")) {
        return 0x2000000000000000ull;
    }

    if (!SStrCmpI(string, "AnyDown")) {
        return 0x7FFFFFFF;
    }

    if (!SStrCmpI(string, "AnyUp")) {
        return 0xFFFFFFFF80000000ull;
    }

    return 0;
}

namespace {

struct LayerEntry {
    const char* string;
    int32_t layer;
};

LayerEntry g_layerMap[] = {
    { "BACKGROUND", DRAWLAYER_BACKGROUND },
    { "BORDER",     DRAWLAYER_BACKGROUND_BORDER },
    { "ARTWORK",    DRAWLAYER_ARTWORK },
    { "OVERLAY",    DRAWLAYER_ARTWORK_OVERLAY },
    { "HIGHLIGHT",  DRAWLAYER_HIGHLIGHT },
};

}

int32_t StringToDrawLayer(const char* string, int32_t& layer) {
    for (const auto& entry : g_layerMap) {
        if (!SStrCmpI(string, entry.string)) {
            layer = entry.layer;
            return true;
        }
    }

    return false;
}

// ref: FUN_00814fb0
// The reference hands this the low nibble of the region's layer byte -- the high nibble carries
// something else -- so callers mask before asking. An unknown layer answers "UNKNOWN" rather than
// null, so a caller pushing the result straight onto the stack cannot push nil by accident.
const char* DrawLayerToString(int32_t layer) {
    for (const auto& entry : g_layerMap) {
        if (entry.layer == layer) {
            return entry.string;
        }
    }

    return "UNKNOWN";
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

namespace {

struct OrientationEntry {
    const char* string;
    ORIENTATION value;
};

OrientationEntry g_orientationMap[] = {
    { "HORIZONTAL", ORIENTATION_HORIZONTAL },
    { "VERTICAL",   ORIENTATION_VERTICAL },
};

}

// ref: FUN_00815160
const char* OrientationToString(ORIENTATION orientation) {
    for (const auto& entry : g_orientationMap) {
        if (entry.value == orientation) {
            return entry.string;
        }
    }

    return "UNKNOWN";
}

int32_t StringToOrientation(const char* string, ORIENTATION& orientation) {
    for (const auto& entry : g_orientationMap) {
        if (!SStrCmpI(entry.string, string)) {
            orientation = entry.value;
            return true;
        }
    }

    return false;
}

// ref: FUN_008150d0
// The reference drives both directions from one table of {flag, name} pairs at 00a44050, in this
// order, so THICKOUTLINE is listed after OUTLINE and a flag word of FONT_OUTLINE|FONT_THICKOUTLINE
// spells out as "OUTLINE, THICKOUTLINE".
struct FontFlagEntry {
    uint32_t value;
    const char* string;
};

static const FontFlagEntry s_fontFlagNames[] = {
    { FONT_OUTLINE,      "OUTLINE" },
    { FONT_THICKOUTLINE, "THICKOUTLINE" },
    { FONT_MONOCHROME,   "MONOCHROME" },
};

// ref: FUN_008151a0
// A substring test, not an equality test: "THICKOUTLINE" contains "OUTLINE", so it sets both bits.
uint32_t StringToFontFlags(const char* string) {
    uint32_t fontFlags = 0x0;

    for (const auto& entry : s_fontFlagNames) {
        if (SStrStr(string, entry.string)) {
            fontFlags |= entry.value;
        }
    }

    return fontFlags;
}

// ref: FUN_008151e0
const char* FontFlagsToString(uint32_t fontFlags) {
    static char s_fontFlagsBuffer[0x40];

    s_fontFlagsBuffer[0] = '\0';

    for (const auto& entry : s_fontFlagNames) {
        if (entry.value & fontFlags) {
            if (s_fontFlagsBuffer[0]) {
                SStrPack(s_fontFlagsBuffer, ", ", sizeof(s_fontFlagsBuffer));
            }

            SStrPack(s_fontFlagsBuffer, entry.string, sizeof(s_fontFlagsBuffer));
        }
    }

    return s_fontFlagsBuffer;
}

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

// ref: FUN_00512410
void RectFramePoints(C2Vector* points, const CRect& rect) {
    points[FRAMEPOINT_TOPLEFT] = { rect.minX, rect.maxY };
    points[FRAMEPOINT_TOP] = { (rect.maxX - rect.minX) * 0.5f + rect.minX, rect.maxY };
    points[FRAMEPOINT_TOPRIGHT] = { rect.maxX, rect.maxY };
    points[FRAMEPOINT_LEFT] = { rect.minX, (rect.maxY - rect.minY) * 0.5f + rect.minY };
    points[FRAMEPOINT_CENTER] = { (rect.maxX - rect.minX) * 0.5f + rect.minX, (rect.maxY - rect.minY) * 0.5f + rect.minY };
    points[FRAMEPOINT_RIGHT] = { rect.maxX, (rect.maxY - rect.minY) * 0.5f + rect.minY };
    points[FRAMEPOINT_BOTTOMLEFT] = { rect.minX, rect.minY };
    points[FRAMEPOINT_BOTTOM] = { (rect.maxX - rect.minX) * 0.5f + rect.minX, rect.minY };
    points[FRAMEPOINT_BOTTOMRIGHT] = { rect.maxX, rect.minY };
}
