#ifndef WORLD_MAP_MAP_OCCLUSION_HPP
#define WORLD_MAP_MAP_OCCLUSION_HPP

#include "world/CWFrustum.hpp"
#include <tempest/Vector.hpp>
#include <cstdint>

// The map's distant occluders: hand-authored convex polygons baked into the reference's .data,
// not anything derived from the terrain. See MapOcclusionTable.hpp for the table itself.
//
// Each frame the volumes for the current map that are in view are turned into a silhouette
// pyramid from the camera, and CWorldScene::SphereOccludedByVolumes hides whatever is inside one.
namespace MapOcclusion {

// Rebuild the occlusion planes for one traversal. Takes the frustum's CORNERS and builds its
// own frustum from them, which is what the reference does -- so this is called per traversal
// rather than once a frame, and a nested portal traversal gets its own volumes.
// `restricted` keeps only the volumes flagged for it. ref: FUN_007cd850
void BuildVolumes(const C3Vector& camera, const C3Vector* corners, int32_t restricted);

}

#endif
