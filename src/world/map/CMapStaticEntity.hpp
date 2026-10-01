#ifndef WORLD_MAP_C_MAP_STATIC_ENTITY_HPP
#define WORLD_MAP_C_MAP_STATIC_ENTITY_HPP

#include "world/map/CMapBaseObj.hpp"
#include <storm/List.hpp>
#include <tempest/Box.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Sphere.hpp>
#include <tempest/Vector.hpp>

class CM2Model;
class CMapObjDef;
class CMapObjDefGroup;
class CMapChunk;

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
        // The floor light, as CMapDoodadDef::FloorLight leaves it: the directional half at
        // +0x88 (whose ALPHA byte, +0x8b, keeps the MOCV alpha the blend used) and the
        // ambient half at +0x84.
        //
        // m_ambient is the CURRENT ambient, and SelectLights is what says so: it reads three
        // bytes at +0x84 and hands them to CM2Lighting::AddAmbient. A doodad is placed once
        // and writes it straight; a unit writes CMapEntity::m_ambientTarget (+0xc0) instead
        // and is moved toward it, which is why the two FloorLight overrides do not share the
        // field even though they share this class.
        CImVector m_ambient = {};              // +0x84
        CImVector m_interiorDirColor = {};     // +0x88
        // TODO: no reader yet.
        float m_dirLightScale = 0.0f;
        // Entity state bits (reference +0x7c). Bit 1 (0x2) makes CMap::LinkToMapObjDefGroup put
        // the entity at the head of the group's entity list instead of the tail; bit 10 (0x400)
        // keeps it animating even out of view. The placement and lighting code (FUN_007c23f0,
        // FUN_007c1730 ...) keeps 0x20/0x40/0x80/0x1000/0x2000/0x8000 here. Named by offset
        // until those are ported.
        uint32_t m_flags7c = 0;                  // +0x7c
        // +0x80: the height of the liquid surface over this entity, in world z, once
        // SelectUnderwater has looked (m_flags7c 0x80 says it has, 0x20 that there is liquid).
        float m_liquidHeight = 0.0f;             // +0x80

        // Apply the cached water side to a model's lighting: above only (0x20), below only
        // (0x40), or straddling the surface (both, and the plane z = m_liquidHeight).
        // ref: FUN_007c10c0
        void ApplyWaterSide(CM2Lighting* lighting) const;
        // Its place in the distance row the traversal put it in, and the frame it was last
        // reached on, so a thing straddling two chunks is visited once.
        TSLink<CMapStaticEntity> m_rowLink;      // +0xa8
        int32_t m_frameStamp = 0;                // +0xb0
        // What the entity is standing on, as a GroundEffectTexture terrain type when it is on
        // open terrain or a WMO material's ground type when it is on a building. 0xFFFFFFFF
        // until placement resolves it, and only resolved at all for a Type_Entity. The map's
        // own segment query branches on this, so an entity whose placement never ran reads as
        // standing on nothing rather than on the default surface.
        int32_t m_groundType = -1;               // +0xb8
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

        // VTABLE SLOT 3, and pure here exactly as in the reference: the base's slot at
        // 0x00a3fd80 holds _purecall (0x0040baa5), while CMapEntity (0x00a3fd90) and
        // CMapDoodadDef (0x00a40318) each supply a body. CMapBaseObj declares the three
        // virtuals above this one, so adding it here is what puts it at slot 3.
        //
        // The light the entity takes from the floor of the building it stands in. `def` is
        // that building's placement -- the CMapObj comes from def->m_mapObj, not from a
        // parameter -- and `face` being null is how the caller asks for a probe instead of
        // naming a face it already found, in which case `point` is where to sample.
        virtual void FloorLight(CMapObjDef* def, uint32_t groupIndex, const uint16_t* face,
                                const C3Vector* point) = 0;
};

// One hit from a segment query over the placed buildings. Sixteen bytes in the reference, and
// the queries keep them in PAIRS -- a collision hit and a render hit -- which is why their
// callers offset by 0x10 to pick one.
//
// A face of 0xffff does not mean 'no face': it means the segment left through a PORTAL rather
// than landing on a polygon, and the distance is the portal crossing.
struct SMapObjHit {
    CMapObjDef* def = nullptr;              // +0x00
    CMapObjDefGroup* defGroup = nullptr;    // +0x04
    float distance = 0.0f;                  // +0x08
    uint16_t face = 0;                      // +0x0c
    uint16_t interior = 0;                  // +0x0e: the group is a room, not open air
};

// NO SIZE ASSERT, and deliberately: the reference packs this into 16 bytes with two 32-bit
// pointers, and frozen is a 64-bit build, so it measures 24. Nothing reads the record from a file
// or indexes it by a baked stride -- the reference's own callers keep PAIRS of it and step by
// sizeof, which is why the width can differ without changing behaviour. The offsets in the comments
// above are the reference's, for reading its disassembly against this.

// One placed building group's answer to a segment: the geometry it hits, and the portal it
// leaves through. Both records are updated in place. ref: FUN_007c1dc0
void QueryDefGroupSegment(CMapObjDef* def, CMapObjDefGroup* defGroup, const C3Vector& start,
                          const C3Vector& end, SMapObjHit* collision, SMapObjHit* render);

// One placed building's answer to a segment, over all of its groups. The two record arrays
// hold TWO entries each and this picks which by a flag on the def. False only when the def has
// no root yet. ref: FUN_007c25d0
bool QueryDefSegment(CMapObjDef* def, const C3Vector& start, const C3Vector& end,
                     const C3Vector& point, SMapObjHit* collision, SMapObjHit* render);

// The whole segment query over placed buildings: one terrain chunk's worth when a chunk is
// given, every placed building when it is null. Both arrays must hold TWO records. False when
// nothing was hit at all. ref: FUN_007c2700
bool QueryMapObjDefSegment(const C3Vector& start, const C3Vector& end, const C3Vector& point,
                           SMapObjHit* collision, SMapObjHit* render, CMapChunk* chunk);

// What a placed entity is standing in, if anything: runs the building query for it, discards a
// building the terrain sits in front of, and falls back to a straight probe from the entity's
// collision centre when nothing answered. `start` and `end` are REWRITTEN on that fallback.
// outInterior receives the winning record's interior flag, outHit whether anything was found.
// ref: FUN_007c28f0
void QueryEntityMapObj(CMapStaticEntity* entity, C3Vector* start, C3Vector* end,
                       const C3Vector& point, uint32_t* outInterior, uint32_t* outHit,
                       SMapObjHit* collision, SMapObjHit* render);

// Link the entity to every terrain chunk its box covers, so each chunk can reach the things
// resting on it. ref: FUN_007c2040
bool LinkEntityToChunks(CMapStaticEntity* entity);

// Work out what a placed entity is standing on and hook it up to it: its ground type, the
// building groups it is inside, and its floor light. ref: FUN_007c2a70
void ResolveEntityGround(CMapStaticEntity* entity);

// Which side of a building's wall a placed entity ended up on, and its floor light if it
// ended up inside. A free function in the reference too -- it returns with a plain `ret`,
// so the caller cleans the stack and there is no `this`. ref: FUN_007c15f0
void ClassifyEntityInterior(CMapStaticEntity* entity, CMapObjDef* def,
                            CMapObjDefGroup* defGroup, uint16_t face, const C3Vector& point);

#endif
