#ifndef WORLD_OVERHEAD_ICONS_HPP
#define WORLD_OVERHEAD_ICONS_HPP

// Camera-facing markers drawn above a unit's head -- today the quest "!" and "?".
//
// Draw it from the world frame's transparent block, after the models, so the icons sort over the
// units they belong to. Depth testing stays on so a marker behind a wall is hidden, which is what
// the reference does.
void OverheadIconsRender();

// Drop the cached textures on map unload.
void OverheadIconsRelease();

#endif
