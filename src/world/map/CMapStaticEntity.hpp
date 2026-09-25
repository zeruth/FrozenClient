#ifndef WORLD_MAP_C_MAP_STATIC_ENTITY_HPP
#define WORLD_MAP_C_MAP_STATIC_ENTITY_HPP

#include "world/map/CMapBaseObj.hpp"
#include <tempest/Box.hpp>
#include <tempest/Sphere.hpp>
#include <tempest/Vector.hpp>

class CM2Model;

class CMapStaticEntity : public CMapBaseObj {
    public:
        // Member variables
        // TODO
        CM2Model* m_model = nullptr;             // +0x34
        // Where it stands and how far it reaches, both in world space and both filled in once
        // its model's own bounds have arrived; until then they sit on the position.
        CAaSphere m_sphere;                      // +0x38
        CAaBox m_bounds;                         // +0x48
        C3Vector m_position = { 0.0f, 0.0f, 0.0f };  // +0x6c
        float m_scale = 1.0f;                    // +0x78
        // TODO
        CImVector m_ambient = {};
        CImVector m_interiorDirColor = {};
        float m_dirLightScale = 0.0f;
};

#endif
