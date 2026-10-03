#ifndef OBJECT_CLIENT_SPELL_VISUALS_HPP
#define OBJECT_CLIENT_SPELL_VISUALS_HPP

#include "gx/Texture.hpp"
#include "util/GUID.hpp"
#include "world/Types.hpp"
#include <storm/Array.hpp>
#include <storm/List.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CEffect;
class CGObject_C;
class CGUnit_C;
class CM2Model;
class SpellChainEffectsRec;
class SpellVisualKitModelAttachRec;

// The reference's SpellVisuals.cpp: the spell visuals that are not one model on one object --
// chain lightning strung between targets, the falling shards of an area spell, and the leap and
// landing a mount transition plays -- and the per-kit table of extra models a visual kit attaches.

// One bolt of a chain: which two targets it joins, when it shows, and the Lightning bolt drawing
// it (0x14 bytes in the reference, RTTI Bolt@LightningObject).
struct LightningBolt {
    uint16_t m_from = 0xFFFF;           // +0x00, an index into the object's targets
    uint16_t m_to = 0xFFFF;             // +0x02
    uint32_t m_startTime = 0;           // +0x04
    uint32_t m_endTime = 0;             // +0x08
    int32_t m_lightning = -1;           // +0x0c, the CLightningSystem index, -1 when none
    int32_t m_param = -1;               // +0x10, handed to StartEffects when the bolt lands
};

// A chain-lightning visual (RTTI LightningObject, 0x60 bytes).
class LightningObject {
    public:
        enum {
            LIGHTNING_PERSISTENT    = 0x1,   // held by an effect; lives until released
            LIGHTNING_FIXED_END     = 0x2,   // the far end is m_endOffset, in world space
            LIGHTNING_WORLD_ATTACH  = 0x4,   // the source attachment is the visual's world one
        };

        TSLink<LightningObject> m_link;                 // +0x00
        TSGrowableArray<WOWGUID> m_targets;             // +0x08, [0] is the caster
        TSGrowableArray<LightningBolt> m_bolts;         // +0x18
        uint32_t m_endTime = 0;                         // +0x28
        HTEXTURE m_texture = nullptr;                   // +0x2c
        int32_t m_spellID = 0;                          // +0x30
        int32_t m_attachment = -1;                      // +0x34
        C3Vector m_startOffset = { 0.0f, 0.0f, 0.0f };  // +0x38
        C3Vector m_endOffset = { 0.0f, 0.0f, 0.0f };    // +0x44
        uint32_t m_flags = 0;                           // +0x50
        CEffect* m_effect = nullptr;                    // +0x54
        const SpellChainEffectsRec* m_rec = nullptr;    // +0x58
        uint32_t m_refCount = 1;                        // +0x5c

        // ref: FUN_007fbc60
        static LightningObject* Create();

        // ref: FUN_007fb6e0
        ~LightningObject();

        // ref: FUN_007fc990
        void Release();
        // ref: FUN_007fa4d0
        void SetChain(const WOWGUID* targets, int32_t count, int32_t fromCaster, int32_t param);
        // ref: FUN_007fae90
        bool Update(uint32_t now);
};

// One shard an area spell drops (RTTI Shard, 0x20 bytes).
struct BlizzardShard {
    TSLink<BlizzardShard> m_link;                       // +0x00
    C3Vector m_position = { 0.0f, 0.0f, 0.0f };         // +0x08
    CM2Model* m_model = nullptr;                        // +0x14
    uint32_t m_startTime = 0;                           // +0x18
    bool m_started = false;                             // +0x1c
};

// An area spell's falling shards -- Blizzard is the one it is named for (RTTI BlizzardObject,
// 0x188 bytes). Shards spawn at the record's rate inside the radius, each one dropped onto the
// ground beneath it.
class BlizzardObject {
    public:
        TSLink<BlizzardObject> m_link;                  // +0x00
        char m_modelName[0x104] = {};                   // +0x08
        HWORLDOBJECT m_worldObject = 0;           // +0x10c
        C3Vector m_position = { 0.0f, 0.0f, 0.0f };     // +0x110
        float m_radius = 0.0f;                          // +0x11c
        float m_accumulator = 0.0f;                     // +0x120
        float m_rate = 0.0f;                            // +0x124
        STORM_EXPLICIT_LIST(BlizzardShard, m_link) m_shards;    // +0x138
        C44Matrix m_transport;                          // +0x144
        int32_t m_hasTransport = 0;                     // +0x184

        // ref: FUN_007fb4b0
        BlizzardObject();
        // ref: FUN_007fc0f0
        ~BlizzardObject();

        // ref: FUN_007fbd70
        static BlizzardObject* Create(const C3Vector& position, float radius, int32_t areaModelIndex, float rate);

        // ref: FUN_007f9dc0
        C3Vector& GetPosition(C3Vector& out);
        // ref: FUN_007fa1b0
        void SetTransport(const C44Matrix* transport);
        // ref: FUN_007fb2d0
        void UpdateShards(uint32_t visible);
        // ref: FUN_007fb5b0
        void Initialize(const C3Vector& position, const char* modelName, float radius, float rate);
        // ref: FUN_007fc220
        bool Update();
};

// A mount's leap on and landing (RTTI MountTransitionObject, 0x88 bytes): the rider travels from
// the mount point to the ground and the mount turns to the slope it lands on.
class MountTransitionObject {
    public:
        enum {
            MOUNT_INITIALIZED   = 0x1,
            MOUNT_DONE          = 0x2,
            MOUNT_LANDED        = 0x4,
        };

        TSLink<MountTransitionObject> m_link;           // +0x00
        WOWGUID m_unit = 0;                             // +0x08
        float m_progress = 0.0f;                        // +0x10
        float m_jumpRate = 0.0f;                        // +0x14
        float m_jumpStart = 0.0f;                       // +0x18
        float m_jumpEnd = 0.0f;                         // +0x1c
        C3Vector m_start = { 0.0f, 0.0f, 0.0f };        // +0x20
        C3Vector m_delta = { 0.0f, 0.0f, 0.0f };        // +0x2c
        float m_fallStart = 0.0f;                       // +0x38
        float m_fallEnd = 0.0f;                         // +0x3c
        float m_fallRate = 0.0f;                        // +0x40
        float m_vertical = 0.0f;                        // +0x44
        C3Vector m_position = { 0.0f, 0.0f, 0.0f };     // +0x48
        C3Vector m_up = { 0.0f, 0.0f, 0.0f };           // +0x54
        uint32_t m_startTime = 0;                       // +0x60
        float m_rate = 0.0f;                            // +0x64
        C3Vector m_attachPosition = { 0.0f, 0.0f, 0.0f };   // +0x68
        float m_facing = 0.0f;                          // +0x74
        uint32_t m_flags = 0;                           // +0x78
        float m_landing = 0.0f;                         // +0x7c
        uint32_t m_lastTime = 0;                        // +0x80

        // ref: FUN_007fbe00
        static MountTransitionObject* Create(CGUnit_C* unit);
        // ref: FUN_007fc9d0
        static void Release(MountTransitionObject* object);

        // ref: FUN_007f9f00
        float GetProgress() const { return this->m_progress; }
        // ref: FUN_007f9f10
        float GetLanding() const { return this->m_landing; }
        // ref: FUN_007f9f20
        void GetState(C3Vector& position, C3Vector& up, float& facing) const;
        // ref: FUN_007f9f60
        void SetDone() { this->m_flags |= MOUNT_DONE; }
        // ref: FUN_007f9f70
        uint32_t IsDone() const { return (this->m_flags >> 1) & 1; }
        // ref: FUN_007fa6a0
        bool Initialize();
        // ref: FUN_007fa930
        void GetRiderOffset(C3Vector& out) const;
        // ref: FUN_007fb7f0
        bool Update();
};

// ref: FUN_007fc5a0
void SpellVisualsInitialize();

// The object effects' shared random stream (0x00b2fa30): spell shards, ripples, blood.
class CRndSeed;
CRndSeed& ObjectRandomSeed();
// ref: FUN_007fcbc0
void SpellVisualsShutdown();
// ref: FUN_007fc9f0
void SpellVisualsReleaseObjects();
// ref: FUN_007fca30
// Every chain, shard field and mount transition, once a frame.
void SpellVisualsUpdate();
// ref: FUN_007f9ec0
// The chain lightning, in the active camera's space.
void SpellVisualsDraw();

// ref: FUN_007fc5f0 (through FUN_007fc950)
// The chain-lightning visual for SpellChainEffects row `chainID`: one object per row of the
// record's combo, each strung from `caster` through `targets`. A persistent chain is handed back in
// `out` for the effect that owns it.
void SpellVisualsCreateChain(int32_t chainID, CGObject_C* caster, CEffect* effect, const WOWGUID* targets,
                             int32_t count, int32_t spellID, bool persistent, int32_t fromCaster,
                             LightningObject** out, int32_t outCount, const C3Vector* position,
                             int32_t param);

// ref: FUN_007fa620
// Play the camera shakes of SpellEffectCameraShakes row `id` on the active camera.
void SpellVisualsShakeCamera(int32_t id, const C3Vector& position);

// ref: FUN_007fbe90
// Index SpellVisualKitModelAttach by the kit that owns each row.
void SpellVisualsBuildModelAttachTable();
// ref: FUN_007fbb80
void SpellVisualsFreeModelAttachTable();
// ref: FUN_007fa9f0
int32_t SpellVisualsGetModelAttachCount(int32_t kitID);
// ref: FUN_007faa20
const SpellVisualKitModelAttachRec* SpellVisualsGetModelAttach(int32_t kitID, int32_t index);

#endif
