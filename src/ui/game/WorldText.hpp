#ifndef UI_GAME_WORLD_TEXT_HPP
#define UI_GAME_WORLD_TEXT_HPP

#include <tempest/Vector.hpp>
#include <cstdint>

class CGxString;

// The world text styles (0x00af4748, 11 rows of 0x1c bytes): how far the text rises, when it has
// faded in and starts to fade out, how long it lives, the height it starts and ends at, and its
// colour when the caller gives none. 0 is the combat text over the player's target, 2 a critical,
// 4 experience, 5 honor, 6 and 7 healing, 8 and 9 a destructible building's damage and healing.
struct WorldTextStyle {
    float riseDistance;     // +0x00
    uint32_t fadeInTime;    // +0x04
    uint32_t fadeOutTime;   // +0x08
    uint32_t duration;      // +0x0c
    float startHeight;      // +0x10
    float endHeight;        // +0x14
    uint32_t color;         // +0x18
};

// One floating text (WORLDTEXTSTRING, 0x80 bytes, its vtable 0x00a41610). It lives in the world at
// `m_position` and is drawn where that lands on the screen, in the world text batch.
struct WORLDTEXTSTRING {
    // ref: FUN_007e66b0
    virtual ~WORLDTEXTSTRING();

    uint32_t m_unk04 = 0;                       // +0x04
    int32_t m_style = 11;                       // +0x08
    float m_halfWidth = 0.0f;                   // +0x0c, NDC
    float m_halfHeight = 0.0f;                  // +0x10, NDC
    C3Vector m_position = { 0.0f, 0.0f, 0.0f }; // +0x14
    CImVector m_color = { 0, 0, 0, 0 };         // +0x20
    uint32_t m_startTime = 0;                   // +0x24
    CGxString* m_string = nullptr;              // +0x28
    float m_height = -1.0f;                     // +0x2c, DDC
    C3Vector m_screenPosition = { 0.0f, 0.0f, 0.0f }; // +0x30
    char m_text[0x40] = {};                     // +0x3c
    int32_t m_icon = 0;                         // +0x7c, a PvP rank (5..19) for honor text

    void BuildString();
    void UpdateFade(uint32_t elapsed);
    float GetRise(uint32_t elapsed) const;
    void UpdateHeight(uint32_t elapsed);
    bool Update(uint32_t now);
    void AddIcon();
};

// ref: FUN_007e7bb0
void WorldTextInitialize();

// ref: FUN_007e6a30
void WorldTextShutdown();

// ref: FUN_007e65e0
float WorldTextGetRiseDistance(int32_t style);

// ref: FUN_007e6dc0
// A new text, or null for an empty one. `color` overrides the style's; `icon` is read for honor
// text only.
WORLDTEXTSTRING* WorldTextCreate(int32_t style, const C3Vector& position, const char* text, const CImVector* color, const int32_t* icon);

// ref: FUN_007e6a90
void WorldTextDestroy(WORLDTEXTSTRING* text);

// ref: FUN_007e7450
bool WorldTextUpdate(WORLDTEXTSTRING* text, uint32_t now);

// ref: FUN_007e7470
void WorldTextAddIcon(WORLDTEXTSTRING* text);

// ref: FUN_007e7490
void WorldTextRender();

#endif
