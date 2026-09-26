#ifndef WORLD_MAP_MAP_WEATHER_TABLE_HPP
#define WORLD_MAP_MAP_WEATHER_TABLE_HPP

#include <cstdint>

// What the reference's three weather effects are built from, read out of its image rather than
// guessed. Item 9 is 24 functions and 17.1 KB spanning 0x00789000..0x0078d600; none of it is
// ported, and this is the part that is awkward to recover from the decompilation alone, because
// every number arrives as a bare DAT_ reference.
//
// A weather record holds three independent effects at +0x13c, +0x140 and +0x144, and its +0x194
// is the enable flag. FUN_0078ca50 draws whichever of the three exist; FUN_0078d0b0 destroys
// them; FUN_0078d130 resets the record.
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
