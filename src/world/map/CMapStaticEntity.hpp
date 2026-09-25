#ifndef WORLD_MAP_C_MAP_STATIC_ENTITY_HPP
#define WORLD_MAP_C_MAP_STATIC_ENTITY_HPP

#include "world/map/CMapBaseObj.hpp"
#include <storm/List.hpp>
#include <tempest/Box.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Sphere.hpp>
#include <tempest/Vector.hpp>

class CM2Model;

class CMapStaticEntity : public CMapBaseObj {
    public:
        // Member variables
        // Which of the five distance bands this one belongs to, and whether the traversal
        // found it inside the frustum this frame.
        uint8_t m_detailLevel = 0;               // +0x24
        uint8_t m_visible = 0;                   // +0x25
        // How far along the camera forward its near edge is, used to sort the frame.
        float m_sortDistance = 0.0f;             // +0x30
        CM2Model* m_model = nullptr;             // +0x34
        // Where it stands and how far it reaches, both in world space and both filled in once
        // its model's own bounds have arrived; until then they sit on the position.
        CAaSphere m_sphere;                      // +0x38
        CAaBox m_bounds;                         // +0x48
        // The centre of the collision box, out in the world. Placement writes it; the queries
        // that ask what a point is standing inside read it.
        C3Vector m_collisionCenter;              // +0x60
        C3Vector m_position = { 0.0f, 0.0f, 0.0f };  // +0x6c
        float m_scale = 1.0f;                    // +0x78
        // How opaque it draws, before any distance fade. The chunk reference walk sets it to
        // one; nothing dims it yet.
        float m_opacity = 1.0f;                  // +0x8c
        // TODO
        CImVector m_ambient = {};
        CImVector m_interiorDirColor = {};
        float m_dirLightScale = 0.0f;
        // Entity state bits (reference +0x7c). Bit 1 (0x2) makes CMap::LinkToMapObjDefGroup put
        // the entity at the head of the group's entity list instead of the tail; bit 10 (0x400)
        // keeps it animating even out of view. The placement and lighting code (FUN_007c23f0,
        // FUN_007c1730 ...) keeps 0x20/0x40/0x80/0x1000/0x2000/0x8000 here. Named by offset
        // until those are ported.
        uint32_t m_flags7c = 0;                  // +0x7c
        // Its place in the distance row the traversal put it in, and the frame it was last
        // reached on, so a thing straddling two chunks is visited once.
        TSLink<CMapStaticEntity> m_rowLink;      // +0xa8
        int32_t m_frameStamp = 0;                // +0xb0
        // Frozen's own, beside the reference's: CMap::ForEachDoodadModel needs a mark of
        // its own so it and the traversal do not answer each other's dedupe.
        int32_t m_walkStamp = 0;                 // diverged
        // The model's collision box, out in the world. Separate from m_bounds, which is the
        // drawn box: a tree's canopy is in one and not the other.
        CAaBox m_collisionBounds;                // +0xc0

        // Member functions
        // Work out where the entity actually is, now that its model has arrived: its sphere,
        // its drawn and collision boxes, and which of the five detail bands its size puts it
        // in. The placement matrix lives on the subclasses in frozen, so it is passed in; the
        // reference reads it at one offset both of them share. ref: FUN_007bdb10
        void Place(const C44Matrix& placement);
};

#endif
