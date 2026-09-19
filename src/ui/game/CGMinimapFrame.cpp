#include "ui/game/CGMinimapFrame.hpp"
#include "ui/game/CGMinimapFrameScript.hpp"

HTEXTURE CGMinimapFrame::s_unknownTexture;
HTEXTURE CGMinimapFrame::s_overlayTextures[7];
float CGMinimapFrame::s_zoomRadius[4][2];
int32_t CGMinimapFrame::s_metatable;
int32_t CGMinimapFrame::s_objectType;

HTEXTURE CGMinimapFrame::s_staticPOIArrowTexture = nullptr;
HTEXTURE CGMinimapFrame::s_corpsePOIArrowTexture = nullptr;
HTEXTURE CGMinimapFrame::s_poiArrowTexture = nullptr;
HTEXTURE CGMinimapFrame::s_maskTexture = nullptr;
HTEXTURE CGMinimapFrame::s_classBlipTexture = nullptr;
HTEXTURE CGMinimapFrame::s_blipTexture = nullptr;
HTEXTURE CGMinimapFrame::s_iconTexture = nullptr;

uint32_t CGMinimapFrame::s_zoom[2] = { 3, 3 };
uint8_t CGMinimapFrame::s_indoors = 0;
const uint32_t CGMinimapFrame::s_zoomLevels = 6;

CSimpleFrame* CGMinimapFrame::Create(CSimpleFrame* parent) {
    // TODO use CDataAllocator

    return STORM_NEW(CGMinimapFrame)(parent);
}

void CGMinimapFrame::CreateScriptMetaTable() {
    auto L = FrameScript_GetContext();
    CGMinimapFrame::s_metatable = FrameScript_Object::CreateScriptMetaTable(L, &CGMinimapFrame::RegisterScriptMethods);
}

int32_t CGMinimapFrame::GetObjectType() {
    if (!CGMinimapFrame::s_objectType) {
        CGMinimapFrame::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CGMinimapFrame::s_objectType;
}

void CGMinimapFrame::RegisterScriptMethods(lua_State* L) {
    CSimpleFrame::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, CGMinimapFrameMethods, NUM_CG_MINIMAP_FRAME_SCRIPT_METHODS);
}

// ref: FUN_007f3b60
uint32_t CGMinimapFrame::GetZoomLevels() {
    return CGMinimapFrame::s_zoomLevels;
}

// ref: FUN_007f3b40
uint32_t CGMinimapFrame::GetZoom() {
    return CGMinimapFrame::s_zoom[CGMinimapFrame::s_indoors ? 1 : 0];
}

// ref: FUN_007f3ae0
void CGMinimapFrame::SetZoom(uint32_t level) {
    auto& zoom = CGMinimapFrame::s_zoom[CGMinimapFrame::s_indoors ? 1 : 0];

    // The reference compares unsigned, so a level that arrives negative wraps and clamps to the
    // closest-in level rather than to zero.
    if (level >= CGMinimapFrame::s_zoomLevels - 1) {
        level = CGMinimapFrame::s_zoomLevels - 1;
    }

    // DIVERGENCE: when the level actually changes the reference also raises the minimap's dirty bit
    // (DAT_00d3922c |= 1) and re-issues the terrain read that refills the minimap texture
    // (FUN_00766940). Frozen draws no minimap yet and has neither, so the level is only recorded.
    zoom = level;
}

CGMinimapFrame::CGMinimapFrame(CSimpleFrame* parent) : CSimpleFrame(parent) {
    // TODO
}

int32_t CGMinimapFrame::GetScriptMetaTable() {
    return CGMinimapFrame::s_metatable;
}
