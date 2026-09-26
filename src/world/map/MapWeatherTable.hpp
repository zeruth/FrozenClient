#ifndef WORLD_MAP_MAP_WEATHER_TABLE_HPP
#define WORLD_MAP_MAP_WEATHER_TABLE_HPP

#include <cstdint>

// What the reference's three weather effects are built from, read out of its image rather than
// guessed. This is the part that is awkward to recover from the decompilation alone, because every
// number arrives as a bare DAT_ reference.
//
// SIZE, corrected 2026-09-26: the module is 50 functions and about 34 KB across
// 0x00786000..0x0078d000, not the 24 functions and 17.1 KB this said before. The leaves are big --
// six of them are between 1.1 KB and 2.0 KB, and the finish they all share is 2.4 KB.
//
// A weather record holds three independent effects at +0x13c, +0x140 and +0x144, and its +0x194
// is the enable flag. FUN_0078ca50 draws whichever of the three exist; FUN_0078d0b0 destroys
// them; FUN_0078d130 resets the record.
//
// THE DRAW TREE, decoded 2026-09-26, so item 9 starts from the top rather than cold:
//
//   FUN_0078ca50(record)                 60 bytes, the entry
//     if (record->+0x194) then, for each of +0x13c / +0x140 / +0x144 that is non-null:
//       FUN_0078ae20 / FUN_0078ba60 / FUN_0078c3e0     74 / 60 / 60 bytes
//
//   All three of those have the SAME shape, which is the useful part:
//
//       Setup();                                  // 00787ce0 / 00788090 / 00788660
//       if (CWorldScene::s_window.depth < 0.0f) { // the global at 0x00adf580 is s_window + 0x10
//           return;                               // nothing is visible this frame, so no weather
//       }
//       if (*flag) { <two or three draws> } else { <two or three draws> }
//       Finish();                                 // FUN_00786e10, 2403 bytes, shared by all three
//
//   The per-layer flag picks between two complete draw paths, almost certainly shader against
//   fixed-function, since that is how every other pass in this client branches:
//
//       layer 1  flag set: FUN_0078a640 (2013), FUN_0078a030 (1551)
//                flag clear: FUN_007898a0 (1929), FUN_007891b0 (1776)
//       layer 2  flag set: FUN_0078aee0 (1155)   flag clear: FUN_0078b370 (1761)
//       layer 3  flag set: FUN_0078bee0 (1267)   flag clear: FUN_0078bb00 (979)
//
// WHERE IT IS CALLED FROM: CGWorldFrame::OnWorldRender, through the wrapper FUN_0077f030, TWICE
// per frame -- once in each arm of the transparent block, before the liquid bucket-1 pass above
// water and after it below. frozen's stand-in WeatherRender() already sits in both of those
// places, so the call sites do not need finding again; see the order table in CGWorldFrame.cpp.
//
// Two of the three are built through one shared constructor, FUN_0078c420, called as
//
//     FUN_0078c420(this, rate, &extents, texture, angleA, angleB, floatC, floatD, 0x80)
//
// which stores the extents at +0x04..+0x0c, the rate at +0x1c, the two angles at +0x24 and
// +0x28, and the last two floats at +0x34 and +0x38. What each one MEANS is not established --
// only what it is and where it goes -- so this is a transcription aid, not a spec.
namespace MapWeather {

struct SEffect {
    const char* texture;
    const char* vertexShader;
    float extents[3];   // the initial box, halved by the caller before use
    float rate;         // FUN_0078c420's first stack argument
    float angleA;       // -1.57, which is -pi/2 to five digits
    float angleB;       // 0.3490659, which is 20 degrees in radians
    float floatC;
    float floatD;
    int32_t count;      // 0x80
};

// The snow mist and the grainy weather mist, both through FUN_0078c420 with identical numbers
// and different textures. Read 2026-09-25 from 0x00a1047c, 0x00a3ec4c, 0x00a2e868, 0x00a3ec48,
// 0x00a3ea3c, 0x009e2ff8 and 0x009ebbc4.
static const SEffect SNOW_MIST = {
    "textures\\Weather\\SnowMist01.blp", "sand",
    { 44.0f, 44.0f, 25.0f }, 12.0f, -1.5700000524520874f, 0.3490658700466156f, 9.0f, 3.0f, 0x80
};

static const SEffect WEATHER_MIST = {
    "textures\\Weather\\WeatherMistGrainy01.blp", "sand",
    { 44.0f, 44.0f, 25.0f }, 12.0f, -1.5700000524520874f, 0.3490658700466156f, 9.0f, 3.0f, 0x80
};

// The rain sheet (FUN_0078ca90) does NOT go through the shared constructor. It loads its own
// splash texture and uses two vertex shaders rather than one: "rain" for the falling sheet and
// "patter" for the splashes. Its own constants have not been read out yet.
static const char* const RAIN_SPLASH_TEXTURE = "textures\\Weather\\RainDropSplash01.blp";
static const char* const RAIN_SHEET_SHADER = "rain";
static const char* const RAIN_SPLASH_SHADER = "patter";

}

#endif
