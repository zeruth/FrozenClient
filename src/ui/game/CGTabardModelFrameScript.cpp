#include "ui/game/CGTabardModelFrameScript.hpp"
#include "component/Types.hpp"
#include "ui/FrameScript.hpp"
#include "ui/FrameScript_Object.hpp"
#include "ui/game/CGTabardModelFrame.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <storm/String.hpp>

namespace {

// ---------------------------------------------------------------------------------------------
// Guild tabard texture names. These belong to the character component module in the reference --
// the player's own tabard getter builds the same six names from the guild fields -- but nothing in
// frozen has claimed that module yet, so they live here with the one caller that needs them.
// ---------------------------------------------------------------------------------------------

// ref: FUN_004e81c0
// Leaves the buffer untouched for any section other than the two torso halves, exactly as the
// reference does.
void TabardBackgroundFileName(int32_t section, int32_t backgroundColor, char* fileName, uint32_t size) {
    if (section == SECTION_TORSO_UPPER) {
        SStrPrintf(fileName, size, "Textures\\GuildEmblems\\Background_%02d_TU_U", backgroundColor);
    } else if (section == SECTION_TORSO_LOWER) {
        SStrPrintf(fileName, size, "Textures\\GuildEmblems\\Background_%02d_TL_U", backgroundColor);
    }
}

// ref: FUN_004e8210
void TabardEmblemFileName(int32_t section, int32_t emblemStyle, int32_t emblemColor, char* fileName, uint32_t size) {
    if (section == SECTION_TORSO_UPPER) {
        SStrPrintf(fileName, size, "Textures\\GuildEmblems\\Emblem_%02d_%02d_TU_U", emblemStyle, emblemColor);
    } else if (section == SECTION_TORSO_LOWER) {
        SStrPrintf(fileName, size, "Textures\\GuildEmblems\\Emblem_%02d_%02d_TL_U", emblemStyle, emblemColor);
    }
}

int32_t CGTabardModelFrame_InitializeTabardColors(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTabardModelFrame_Save(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTabardModelFrame_CycleVariation(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_005991d0
int32_t CGTabardModelFrame_GetUpperBackgroundFileName(lua_State* L) {
    auto frame = static_cast<CGTabardModelFrame*>(FrameScript_GetObjectThis(L, CGTabardModelFrame::GetObjectType()));
    char fileName[260] = {};

    TabardBackgroundFileName(SECTION_TORSO_UPPER, frame->m_backgroundColor, fileName, 260);
    lua_pushstring(L, fileName);

    return 1;
}

// ref: FUN_00599240
int32_t CGTabardModelFrame_GetLowerBackgroundFileName(lua_State* L) {
    auto frame = static_cast<CGTabardModelFrame*>(FrameScript_GetObjectThis(L, CGTabardModelFrame::GetObjectType()));
    char fileName[260] = {};

    TabardBackgroundFileName(SECTION_TORSO_LOWER, frame->m_backgroundColor, fileName, 260);
    lua_pushstring(L, fileName);

    return 1;
}

// ref: FUN_005992b0
int32_t CGTabardModelFrame_GetUpperEmblemFileName(lua_State* L) {
    auto frame = static_cast<CGTabardModelFrame*>(FrameScript_GetObjectThis(L, CGTabardModelFrame::GetObjectType()));
    char fileName[260] = {};

    TabardEmblemFileName(SECTION_TORSO_UPPER, frame->m_emblemStyle, frame->m_emblemColor, fileName, 260);
    lua_pushstring(L, fileName);

    return 1;
}

// ref: FUN_00599320
int32_t CGTabardModelFrame_GetLowerEmblemFileName(lua_State* L) {
    auto frame = static_cast<CGTabardModelFrame*>(FrameScript_GetObjectThis(L, CGTabardModelFrame::GetObjectType()));
    char fileName[260] = {};

    TabardEmblemFileName(SECTION_TORSO_LOWER, frame->m_emblemStyle, frame->m_emblemColor, fileName, 260);
    lua_pushstring(L, fileName);

    return 1;
}

int32_t CGTabardModelFrame_GetUpperEmblemTexture(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTabardModelFrame_GetLowerEmblemTexture(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGTabardModelFrame_CanSaveTabardNow(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

}

FrameScript_Method CGTabardModelFrameMethods[] = {
    { "InitializeTabardColors",     &CGTabardModelFrame_InitializeTabardColors },
    { "Save",                       &CGTabardModelFrame_Save },
    { "CycleVariation",             &CGTabardModelFrame_CycleVariation },
    { "GetUpperBackgroundFileName", &CGTabardModelFrame_GetUpperBackgroundFileName },
    { "GetLowerBackgroundFileName", &CGTabardModelFrame_GetLowerBackgroundFileName },
    { "GetUpperEmblemFileName",     &CGTabardModelFrame_GetUpperEmblemFileName },
    { "GetLowerEmblemFileName",     &CGTabardModelFrame_GetLowerEmblemFileName },
    { "GetUpperEmblemTexture",      &CGTabardModelFrame_GetUpperEmblemTexture },
    { "GetLowerEmblemTexture",      &CGTabardModelFrame_GetLowerEmblemTexture },
    { "CanSaveTabardNow",           &CGTabardModelFrame_CanSaveTabardNow },
};
