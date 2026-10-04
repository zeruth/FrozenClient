#ifndef UI_GAME_TOOLTIP_COLORS_HPP
#define UI_GAME_TOOLTIP_COLORS_HPP

#include <tempest/Vector.hpp>

// The colour table the tooltip builders read (0x00ad2d2c onward), as CImVector { b, g, r, a }.
static const CImVector TOOLTIP_COLOR_NORMAL = { 0x00, 0xD2, 0xFF, 0xFF };          // ref: DAT_00ad2d2c
static const CImVector TOOLTIP_COLOR_HIGHLIGHT = { 0xFF, 0xFF, 0xFF, 0xFF };       // ref: DAT_00ad2d30
static const CImVector TOOLTIP_COLOR_RED = { 0x20, 0x20, 0xFF, 0xFF };             // ref: DAT_00ad2d34
static const CImVector TOOLTIP_COLOR_GRAY = { 0x80, 0x80, 0x80, 0xFF };            // ref: DAT_00ad2d38
static const CImVector TOOLTIP_COLOR_LIGHT_BLUE = { 0xFF, 0xAA, 0x88, 0xFF };      // ref: DAT_00ad2d3c
static const CImVector TOOLTIP_COLOR_GREEN = { 0x00, 0xFF, 0x00, 0xFF };           // ref: DAT_00ad2d40
static const CImVector TOOLTIP_COLOR_YELLOW = { 0x00, 0xFF, 0xFF, 0xFF };          // ref: DAT_00ad2d44
static const CImVector TOOLTIP_COLOR_PURE_RED = { 0x00, 0x00, 0xFF, 0xFF };        // ref: DAT_00ad2d48
static const CImVector TOOLTIP_COLOR_LIGHT_YELLOW = { 0x97, 0xFF, 0xFF, 0xFF };    // ref: DAT_00ad2d50

// How hard a skill is against what it needs: trivial, easy, medium, optimal, impossible.
static const CImVector TOOLTIP_COLOR_SKILL[5] = {                                   // ref: DAT_00ad2d54
    { 0x80, 0x80, 0x80, 0xFF },
    { 0x40, 0xC0, 0x40, 0xFF },
    { 0x00, 0xFF, 0xFF, 0xFF },
    { 0x40, 0x80, 0xFF, 0xFF },
    { 0x20, 0x20, 0xFF, 0xFF },
};

// By item quality, poor to heirloom.
static const CImVector TOOLTIP_COLOR_QUALITY[8] = {                                 // ref: DAT_00ad2d84
    { 0x9D, 0x9D, 0x9D, 0xFF },
    { 0xFF, 0xFF, 0xFF, 0xFF },
    { 0x00, 0xFF, 0x1E, 0xFF },
    { 0xDD, 0x70, 0x00, 0xFF },
    { 0xEE, 0x35, 0xA3, 0xFF },
    { 0x00, 0x80, 0xFF, 0xFF },
    { 0x80, 0xCC, 0xE6, 0xFF },
    { 0x80, 0xCC, 0xE6, 0xFF },
};

static const CImVector TOOLTIP_COLOR_GLYPH = { 0xFF, 0xBB, 0x66, 0xFF };           // ref: DAT_00ad2da4
static const CImVector TOOLTIP_COLOR_REFUND = { 0xFF, 0xCC, 0x00, 0xFF };          // ref: DAT_00ad2da8
static const CImVector TOOLTIP_COLOR_BIND_TRADE = { 0xFF, 0xCC, 0x00, 0xFF };      // ref: DAT_00ad2dac

#endif
