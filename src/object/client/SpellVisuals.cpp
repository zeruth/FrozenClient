#include "object/client/SpellVisuals.hpp"
#include "console/CVar.hpp"
#include "db/Db.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2Scene.hpp"
#include "model/CM2Shared.hpp"
#include "object/client/CEffect.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "sound/SI2.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/game/CGCamera.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "world/CWorld.hpp"
#include "world/Lightning.hpp"
#include <common/Handle.hpp>
#include <common/Time.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <tempest/random/CRandom.hpp>
#include <cmath>
#include <cstring>

// CGCamera.cpp
float ClampRange(float value, float low, float high);

const SpellVisualRec* GetSpellVisual(const SpellRec* spell);

namespace {

// The three object lists (0x00af50a8, 0x00af509c, 0x00af50b4).
STORM_EXPLICIT_LIST(LightningObject, m_link) s_lightningObjects;
STORM_EXPLICIT_LIST(BlizzardObject, m_link) s_blizzardObjects;
STORM_EXPLICIT_LIST(MountTransitionObject, m_link) s_mountTransitions;

// The previous update's clock (0x00af5090), -1 before the first.
uint32_t s_lastUpdate = 0xFFFFFFFF;

// spellEffectLevel (0x00d39754), which thins the shards an area spell drops.
CVar* s_spellEffectLevel;

// The shards' random stream (0x00b2fa30).
CRndSeed s_shardSeed(0);

// The per-kit model attach lists (0x00d39758): for each SpellVisualKit id, the
// SpellVisualKitModelAttach rows whose parent it is.
TSGrowableArray<TSGrowableArray<const SpellVisualKitModelAttachRec*>*>* s_modelAttach;

// Every eighth anim event, a shard's sound (FUN_007f9d80 counts in 0x00d397ac).
uint32_t s_shardEventCount;

float RandomUnit(CRndSeed& seed) {
    uint32_t r = CRandom::uint32(seed);
    uint32_t bits = (r & 0x7FFFFF) | 0x3F800000;
    float f;
    memcpy(&f, &bits, 4);

    return f - 1.0f;
}

// ref: FUN_009a8ce0
// A combo entry is four characters of the record's combo string, decoded to a chain id.
uint32_t DecodeComboEntry(uint32_t v) {
    uint32_t x = v & 0xFEFEFEFE;

    return ((((((x >> 7) & 0x200000) | (v & 0x20000000)) >> 3 | (v & 0x2000000)) >> 4 | (v & 0x40000000)) >> 3
        | (v & 0x4000000)) >> 0xb
        | ((((x >> 4) & 0xFF800000) | x) & 0xFFFFFF);
}

// ref: FUN_007f9d80
void ShardAnimEvent(CM2Model* model, uint32_t boneId, uint32_t eventId, uint32_t eventData,
                    const C3Vector* position, uint32_t a6, WOWGUID owner) {
    s_shardEventCount++;

    if ((s_shardEventCount & 3) == 0 && eventId == 0x444E5324) {
        SI2::PlaySoundKit(static_cast<int32_t>(eventData), position, nullptr, nullptr, 0, nullptr, 1, 0);
    }
}

// ref: FUN_007fc010
void ShardRelease(BlizzardShard* shard) {
    if (shard->m_model) {
        shard->m_model->Release();
    }

    shard->m_link.Unlink();
    shard->~BlizzardShard();
    SMemFree(shard, __FILE__, __LINE__, 0);
}

// ref: FUN_007fc200
void ShardSequenceDone(CM2Model* model, uint32_t boneId, uint32_t animId, int32_t a4, int32_t a5, WOWGUID owner) {
    if (a4 == 0) {
        ShardRelease(reinterpret_cast<BlizzardShard*>(static_cast<uintptr_t>(owner)));
    }
}

// ref: FUN_007fb590
int32_t BlizzardWorldHandler(void* param, int32_t flags, uint32_t guidLow, uint32_t guidHigh, uint32_t param32) {
    static_cast<BlizzardObject*>(param)->UpdateShards(static_cast<uint32_t>(flags) & 1);

    return 1;
}

// ref: FUN_007faa40
// Where a chain leaves its caster: the visual's attachment when the model has it, the model's
// $CSL event when it has that, else the offset in the unit's space, at three quarters of its
// height. The reference passes the unit in ESI and the result in EBX.
C3Vector ChainSourcePosition(CGUnit_C* unit, int32_t attachment, const C3Vector& offset, int32_t worldAttach) {
    auto model = unit->m_model;
    C3Vector base = unit->m_vehiclePassenger ? unit->GetModelWorldPosition() : unit->GetPosition();

    C3Vector pos = { base.x, base.y, unit->m_scale * unit->m_height * 0.75f + base.z };

    if (model && model->IsLoaded(0, 0) && model->m_flag8000) {
        if (attachment != -1 && unit->HasSpellAttachment(attachment, worldAttach)) {
            C3Vector out;
            unit->GetSpellAttachmentWorldPosition(out, attachment, offset, worldAttach);

            return out;
        }

        if (model->HasEvent(0x4C534324)) {
            C44Matrix event;
            model->GetEventWorldTransform(event, 0x4C534324);

            return offset * event;
        }

        if (offset.x != 0.0f || offset.y != 0.0f || offset.z != 0.0f) {
            C44Matrix world;

            if (unit->m_vehiclePassenger) {
                unit->GetModelWorldMatrix(world);
            } else {
                unit->GetWorldMatrix(world);
            }

            C3Vector p = offset * world;
            pos = { p.x, p.y, unit->m_scale * unit->m_height * 0.75f + p.z };
        }
    }

    return pos;
}

// ref: FUN_007fabf0
// Where a chain reaches a unit: the spell visual's impact attachment, else the impact offset in
// the unit's space at three quarters of its collision height. A spell with no row lands on
// attachment 0x22 when the model carries it.
C3Vector ChainTargetPosition(CGUnit_C* unit, int32_t spellID) {
    auto model = unit->m_model;
    C3Vector base = unit->m_vehiclePassenger ? unit->GetModelWorldPosition() : unit->GetPosition();

    C3Vector pos = { base.x, base.y, unit->GetModelHeight() * 0.75f + base.z };

    auto spell = g_spellDB.GetRecord(spellID);

    if (!spell) {
        if (model && model->IsLoaded(0, 0) && model->m_flag8000 && model->HasAttachment(0x22)) {
            return model->GetAttachmentWorldPosition(0x22);
        }

        return pos;
    }

    auto visual = unit->IsA(TYPE_UNIT) ? static_cast<const SpellVisualRec*>(unit->GetSpellVisualRec(spell)) : GetSpellVisual(spell);

    if (!visual) {
        return pos;
    }

    int32_t worldAttach = visual->m_flags & 0x200;

    if (model->IsLoaded(0, 0) && model->m_flag8000
        && unit->HasSpellAttachment(visual->m_missileDestinationAttachment, worldAttach)) {
        C3Vector off = { visual->m_missileImpactOffset[0], -visual->m_missileImpactOffset[1], visual->m_missileImpactOffset[2] };
        C3Vector out;
        unit->GetSpellAttachmentWorldPosition(out, visual->m_missileDestinationAttachment, off, worldAttach);

        return out;
    }

    if (visual->m_missileImpactOffset[0] != 0.0f || visual->m_missileImpactOffset[1] != 0.0f
        || visual->m_missileImpactOffset[2] != 0.0f) {
        C44Matrix world;

        if (unit->m_vehiclePassenger) {
            unit->GetModelWorldMatrix(world);
        } else {
            unit->GetWorldMatrix(world);
        }

        world.d0 = 0.0f;
        world.d1 = 0.0f;
        world.d2 = 0.0f;

        C3Vector off = { visual->m_missileImpactOffset[0], -visual->m_missileImpactOffset[1], visual->m_missileImpactOffset[2] };
        C3Vector p = off * world;

        pos.x += p.x;
        pos.y += p.y;
        pos.z += p.z;
    }

    return pos;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// LightningObject
// ------------------------------------------------------------------------------------------------

// ref: FUN_007fbc60
LightningObject* LightningObject::Create() {
    void* mem = SMemAlloc(sizeof(LightningObject), "LightningObject", -2, 0x8);
    auto object = mem ? new (mem) LightningObject() : nullptr;

    if (object) {
        s_lightningObjects.LinkToTail(object);
    }

    return object;
}

// ref: FUN_007fb6e0
LightningObject::~LightningObject() {
    for (uint32_t i = 0; i < this->m_bolts.Count(); i++) {
        if (this->m_bolts[i].m_lightning != -1) {
            g_lightningSystem->Destroy(this->m_bolts[i].m_lightning);
        }
    }

    this->m_bolts.SetCount(0);
    this->m_targets.SetCount(0);

    if (this->m_texture) {
        HandleClose(this->m_texture);
        this->m_texture = nullptr;
    }

    if (this->m_effect) {
        this->m_effect->ReleaseUse();
        this->m_effect->Release();
        this->m_effect = nullptr;
    }

    this->m_link.Unlink();
}

// ref: FUN_007fc990
void LightningObject::Release() {
    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());

    this->m_flags &= ~LIGHTNING_PERSISTENT;
    this->m_refCount--;
    this->m_endTime = now;

    if (this->m_refCount == 0) {
        this->~LightningObject();
        SMemFree(this, __FILE__, __LINE__, 0);
    }
}

// ref: FUN_007fa4d0
// One bolt per target, the targets taken in reverse, each starting the record's delay after the
// last. A chain that does not run from the caster each time runs bolt i from target i.
void LightningObject::SetChain(const WOWGUID* targets, int32_t count, int32_t fromCaster, int32_t param) {
    uint32_t t = static_cast<uint32_t>(OsGetAsyncTimeMs()) + static_cast<uint32_t>(this->m_rec->m_segDelay);

    this->m_endTime = t;

    for (int32_t i = 0; i < count; i++) {
        auto& bolt = this->m_bolts[i];

        bolt.m_to = 0xFFFF;
        bolt.m_from = 0xFFFF;
        bolt.m_lightning = -1;

        this->m_targets[i + 1] = targets[count - i - 1];

        bolt.m_startTime = t;
        bolt.m_endTime = static_cast<uint32_t>(this->m_rec->m_segDuration) + t;
        bolt.m_from = fromCaster ? 0 : static_cast<uint16_t>(i);
        bolt.m_to = static_cast<uint16_t>(i + 1);

        if (static_cast<int32_t>(bolt.m_endTime - this->m_endTime) >= 0) {
            this->m_endTime = bolt.m_endTime;
        }

        t += static_cast<uint32_t>(lrintf(this->m_rec->m_delayBetweenEffects));

        bolt.m_param = i == 0 ? param : -1;

        WOWGUID target = this->m_targets[i + 1];

        if (target) {
            auto object = ClntObjMgrObjectPtr(target, TYPE_OBJECT, ".\\SpellVisuals.cpp", 0x35a);

            if (object) {
                for (auto effect = object->m_effects; effect; effect = effect->m_linkNext) {
                    if (effect->m_spellID == this->m_spellID && (effect->m_flags & CEffect::EFFECT_EMISSION_COUNTDOWN)) {
                        effect->Stop();
                    }
                }
            }
        }
    }
}

// ref: FUN_007fae90
bool LightningObject::Update(uint32_t now) {
    for (uint32_t i = 0; i < this->m_bolts.Count(); i++) {
        auto& bolt = this->m_bolts[i];

        if (bolt.m_from == 0xFFFF || bolt.m_to == 0xFFFF) {
            continue;
        }

        auto src = ClntObjMgrObjectPtr(this->m_targets[bolt.m_from], TYPE_OBJECT, ".\\SpellVisuals.cpp", 0x223);
        auto dst = ClntObjMgrObjectPtr(this->m_targets[bolt.m_to], TYPE_OBJECT, ".\\SpellVisuals.cpp", 0x224);

        auto srcUnit = src && src->IsA(TYPE_UNIT) ? static_cast<CGUnit_C*>(src) : nullptr;
        auto dstUnit = dst && dst->IsA(TYPE_UNIT) ? static_cast<CGUnit_C*>(dst) : nullptr;

        if ((this->m_flags & LIGHTNING_PERSISTENT) || (bolt.m_startTime <= now && now < bolt.m_endTime)) {
            C3Vector start = { 0.0f, 0.0f, 0.0f };

            if (!srcUnit) {
                if (src) {
                    C44Matrix world;
                    src->GetWorldMatrix(world);
                    start = this->m_startOffset * world;
                }
            } else if (bolt.m_from == 0) {
                start = ChainSourcePosition(srcUnit, this->m_attachment, this->m_startOffset,
                                            this->m_flags & LIGHTNING_WORLD_ATTACH);
            } else {
                start = ChainTargetPosition(srcUnit, this->m_spellID);
            }

            C3Vector end = { 0.0f, 0.0f, 0.0f };
            bool unreached = false;

            if (!dstUnit) {
                if (!dst) {
                    if (this->m_flags & LIGHTNING_FIXED_END) {
                        end = this->m_endOffset;
                    } else {
                        unreached = true;
                    }
                } else {
                    C44Matrix world;
                    dst->GetWorldMatrix(world);
                    end = this->m_endOffset * world;
                }
            } else {
                end = ChainTargetPosition(dstUnit, this->m_spellID);
            }

            if (bolt.m_lightning == -1) {
                bolt.m_lightning = g_lightningSystem->Create(nullptr, nullptr, this->m_rec);

                auto lightning = g_lightningSystem->Get(bolt.m_lightning);
                lightning->m_start = start;
                lightning->m_end = end;
                lightning->SetTexture(this->m_texture);
            } else {
                g_lightningSystem->SetEnds(bolt.m_lightning, &start, &end);
            }

            g_lightningSystem->SetVisible(bolt.m_lightning, src && !unreached ? 1 : 0);

            auto lightning = g_lightningSystem->Get(bolt.m_lightning);

            if (src && lightning) {
                if (lightning->m_flags & 0x10) {
                    src->StartEffects(this->m_spellID, -1);
                }

                if (!dst) {
                    if (lightning->m_flags & 0x20) {
                        src->StartEffects(this->m_spellID, bolt.m_param);
                    }
                } else if (lightning->m_flags & 0x20) {
                    dst->StartEffects(this->m_spellID, -1);
                }
            }
        }

        if ((this->m_flags & LIGHTNING_PERSISTENT) == 0 && bolt.m_endTime <= now && bolt.m_lightning != -1) {
            g_lightningSystem->Destroy(bolt.m_lightning);
            bolt.m_lightning = -1;
        }
    }

    return (this->m_flags & LIGHTNING_PERSISTENT) || now < this->m_endTime;
}

// ------------------------------------------------------------------------------------------------
// BlizzardObject
// ------------------------------------------------------------------------------------------------

// ref: FUN_007fb4b0
BlizzardObject::BlizzardObject() {
    this->m_transport = C44Matrix();
}

// ref: FUN_007fc0f0
BlizzardObject::~BlizzardObject() {
    while (auto shard = this->m_shards.Head()) {
        ShardRelease(shard);
    }

    if (this->m_worldObject) {
        CWorld::RemoveObject(this->m_worldObject);
    }

    this->m_link.Unlink();
}

// ref: FUN_007fbd70
BlizzardObject* BlizzardObject::Create(const C3Vector& position, float radius, int32_t areaModelIndex, float rate) {
    const char* modelName;
    auto area = areaModelIndex >= 0 && areaModelIndex < g_spellVisualKitAreaModelDB.GetNumRecords()
        ? g_spellVisualKitAreaModelDB.GetRecordByIndex(areaModelIndex)
        : nullptr;

    if (!area && g_spellVisualKitAreaModelDB.GetNumRecords() > 0) {
        area = g_spellVisualKitAreaModelDB.GetRecordByIndex(0);
    }

    modelName = area ? area->m_modelName : "Spells\\Blizzard_Impact_Base.mdx";

    void* mem = SMemAlloc(sizeof(BlizzardObject), "BlizzardObject", -2, 0);
    auto object = mem ? new (mem) BlizzardObject() : nullptr;

    if (object) {
        s_blizzardObjects.LinkToTail(object);
        object->Initialize(position, modelName, radius, rate);
    }

    return object;
}

// ref: FUN_007f9dc0
C3Vector& BlizzardObject::GetPosition(C3Vector& out) {
    if (!this->m_hasTransport) {
        out = this->m_position;
    } else {
        out = this->m_position * this->m_transport;
    }

    return out;
}

// ref: FUN_007fa1b0
void BlizzardObject::SetTransport(const C44Matrix* transport) {
    this->m_hasTransport = transport != nullptr;

    if (!transport) {
        return;
    }

    this->m_transport = *transport;

    C44Matrix matrix;
    C3Vector pos;
    this->GetPosition(pos);
    matrix.d0 = pos.x;
    matrix.d1 = pos.y;
    matrix.d2 = pos.z;

    CAaSphere sphere = { { 0.0f, 0.0f, 0.0f }, this->m_radius };
    CAaBox box;
    SphereToBox(sphere, box);
    C3Vector collision = { 0.0f, 0.0f, 0.0f };

    CWorld::UpdateObject(this->m_worldObject, matrix, box, sphere, collision, 0, 0xFFFFFFFF);
}

// ref: FUN_007fb2d0
// Every shard whose time has come starts its animation once, and is placed and shown or hidden
// with the field.
void BlizzardObject::UpdateShards(uint32_t visible) {
    for (auto shard = this->m_shards.Head(); shard; ) {
        auto next = this->m_shards.Next(shard);

        if (shard->m_startTime <= CWorld::GetCurTimeMs()) {
            if (!shard->m_started) {
                shard->m_started = true;
                shard->m_model->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);
            }

            shard->m_model->SetAnimating(1);

            uint32_t bit = visible & 1;

            if (!shard->m_model->m_attachParent) {
                shard->m_model->m_flag8 = bit;
                shard->m_model->m_flag10000 = bit;
            } else {
                shard->m_model->m_flag80 = bit;
                shard->m_model->m_flag20000 = bit;
            }

            C3Vector pos = shard->m_position;

            if (this->m_hasTransport) {
                pos = pos * this->m_transport;
            }

            C44Matrix world;
            world.d0 = pos.x;
            world.d1 = pos.y;
            world.d2 = pos.z;

            shard->m_model->m_flag8000 = 1;
            shard->m_model->matrixB4 = world;
        }

        shard = next;
    }
}

// ref: FUN_007fb5b0
void BlizzardObject::Initialize(const C3Vector& position, const char* modelName, float radius, float rate) {
    this->m_position = position;
    this->m_radius = radius;

    if (s_spellEffectLevel) {
        rate = (static_cast<float>(s_spellEffectLevel->GetInt()) * 0.1111111119389534f * 0.8999999761581421f
            + 0.10000000149011612f) * rate;
    }

    this->m_rate = rate;

    SStrCopy(this->m_modelName, modelName, sizeof(this->m_modelName));

    C44Matrix matrix;
    matrix.d0 = position.x;
    matrix.d1 = position.y;
    matrix.d2 = position.z;

    this->m_worldObject = CWorld::AddObject(nullptr, reinterpret_cast<void*>(&BlizzardWorldHandler), this, 0, 0, 0xE);

    CAaSphere sphere = { { 0.0f, 0.0f, 0.0f }, radius };
    CAaBox box;
    SphereToBox(sphere, box);
    C3Vector collision = { 0.0f, 0.0f, 0.0f };

    CWorld::UpdateObject(this->m_worldObject, matrix, box, sphere, collision, 0, 0xFFFFFFFF);
}

// ref: FUN_007fc220
// Spawn shards at the field's rate: each at a random point in the circle, dropped onto what lies
// below it, with its own model and a random start up to half a second away.
bool BlizzardObject::Update() {
    float frame = CWorld::GetTickTimeSec();

    if (0.06666667014360428f < frame) {
        frame = 0.06666667014360428f;
    }

    this->m_accumulator = frame * this->m_rate + this->m_accumulator;

    C3Vector center;

    if (!this->m_hasTransport) {
        center = this->m_position;
    } else {
        center = this->m_position * this->m_transport;
    }

    while (this->m_accumulator > 1.0f) {
        void* mem = SMemAlloc(sizeof(BlizzardShard), "Shard", -2, 0);
        auto shard = mem ? new (mem) BlizzardShard() : nullptr;

        this->m_shards.LinkToTail(shard);

        float distance = RandomUnit(s_shardSeed);
        float angle = RandomUnit(s_shardSeed) * 6.2831854820251465f;
        C2Vector dir = { cosf(angle), sinf(angle) };

        C3Vector top = {
            distance * dir.x * this->m_radius + center.x,
            this->m_radius * dir.y * distance + center.y,
            16.66666603088379f + center.z
        };

        C3Vector from = { center.x, center.y, center.z + 0.1666666716337204f };
        C3Vector hit = { 0.0f, 0.0f, 0.0f };
        float t = 1.0f;
        C3Vector drop = top;

        if (WorldQuerySegment(from, top, &hit, &t, 0x120111, nullptr)) {
            float f = t * 0.949999988079071f;
            drop = {
                (top.x - from.x) * f + from.x,
                from.y + (top.y - from.y) * f,
                f * (top.z - from.z) + from.z
            };
        }

        C3Vector bottom = { drop.x, drop.y, drop.z - 533.3333129882812f };
        float t2 = 1.0f;

        if (!WorldQuerySegment(drop, bottom, &shard->m_position, &t2, 0x120111, nullptr)) {
            shard->m_position = this->m_position;
        } else if (this->m_hasTransport) {
            C44Matrix inverse = this->m_transport.AffineInverse();
            shard->m_position = shard->m_position * inverse;
        }

        shard->m_model = CWorld::GetM2Scene()->CreateModel(this->m_modelName, 0);

        if (shard->m_model) {
            shard->m_model->SetAnimEventCallback(&ShardAnimEvent, 0);
            shard->m_model->SetSequenceDoneCallback(&ShardSequenceDone, static_cast<WOWGUID>(reinterpret_cast<uintptr_t>(shard)));
        }

        float r = RandomUnit(s_shardSeed) * 255.0f;
        uint32_t start = CWorld::GetCurTimeMs() + (static_cast<uint32_t>(lrintf(r)) & 0xFF) * 2;
        shard->m_startTime = start ? start : 1;

        this->m_accumulator -= 1.0f;
    }

    return !(this->m_rate == 0.0f && this->m_shards.Head() == nullptr);
}

// ------------------------------------------------------------------------------------------------
// MountTransitionObject
// ------------------------------------------------------------------------------------------------

// ref: FUN_007fbe00
MountTransitionObject* MountTransitionObject::Create(CGUnit_C* unit) {
    void* mem = SMemAlloc(sizeof(MountTransitionObject), "MountTransitionObject", -2, 0x8);
    auto object = mem ? new (mem) MountTransitionObject() : nullptr;

    if (!object) {
        return nullptr;
    }

    s_mountTransitions.LinkToTail(object);

    object->m_flags = 0;
    object->m_unit = unit->GetGUID();
    object->m_up = unit->m_localMove.GetWorldUp();
    object->m_startTime = static_cast<uint32_t>(OsGetAsyncTimeMs());
    object->m_start = { 0.0f, 0.0f, 0.0f };
    object->m_landing = 0.0f;
    object->m_lastTime = static_cast<uint32_t>(OsGetAsyncTimeMs());

    return object;
}

// ref: FUN_007fc9d0 -> FUN_007fc070
void MountTransitionObject::Release(MountTransitionObject* object) {
    if (!object) {
        return;
    }

    object->m_link.Unlink();
    object->~MountTransitionObject();
    SMemFree(object, "MountTransitionObject", -2, 0);
}

// ref: FUN_007f9f20
void MountTransitionObject::GetState(C3Vector& position, C3Vector& up, float& facing) const {
    position = this->m_position;
    up = this->m_up;
    facing = this->m_facing;
}

// ref: FUN_007fa6a0
// Read the leap from the mount's model: where the rider sits (attachment 0), how long the mount
// sequence runs, and the $BTS/$ETS marks that bound the jump and the fall.
bool MountTransitionObject::Initialize() {
    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_unit, TYPE_UNIT, ".\\SpellVisuals.cpp", 0x48a));

    if (!unit) {
        return false;
    }

    CM2Model* mount = unit->m_mountModel;

    if (!unit->m_model || !unit->m_model->IsLoaded(0, 0) || !mount || !mount->IsLoaded(0, 0)
        || !unit->m_model->HasAttachment(0)) {
        return false;
    }

    unit->m_model->GetAttachmentPosition(&this->m_attachPosition, 0);

    M2BoneSequenceState state;
    unit->m_model->GetBoneSequenceState(0xFFFFFFFF, &state);

    int32_t span = static_cast<int32_t>(state.endTime - state.startTime);
    float rate = 1.0f / static_cast<float>(span);
    this->m_rate = rate;

    uint32_t anim = unit->GetCurrentAnimationId();
    int32_t jumpStart = unit->m_model->GetEventTimestamp(anim, 0x42545324);
    int32_t jumpEnd = unit->m_model->GetEventTimestamp(anim, 0x45545324);

    this->m_jumpStart = static_cast<float>(jumpStart) * rate;
    this->m_jumpEnd = static_cast<float>(jumpEnd) * rate;
    this->m_jumpRate = 1.0f / (static_cast<float>(jumpEnd) * rate - static_cast<float>(jumpStart) * rate);

    if (unit->m_model->HasEvent(0x42545324)) {
        unit->m_model->GetEventPosition(this->m_start, 0x42545324);
    }

    C3Vector world;
    TransformPointInPlace(world, this->m_start, mount->matrixB4);
    this->m_start = world;

    C3Vector attach = this->m_attachPosition;
    C3Vector attachWorld;
    TransformPointInPlace(attachWorld, attach, mount->matrixB4);

    this->m_delta = { attachWorld.x - this->m_start.x, attachWorld.y - this->m_start.y, attachWorld.z - this->m_start.z };

    int32_t fallStart = mount->GetEventTimestamp(0x7F, 0x42545324);
    int32_t fallEnd = mount->GetEventTimestamp(0x7F, 0x45545324);

    this->m_fallStart = static_cast<float>(fallStart) * rate;
    this->m_fallEnd = static_cast<float>(fallEnd) * rate;

    float fall = static_cast<float>(fallEnd) * rate - static_cast<float>(fallStart) * rate;
    this->m_fallRate = fall;

    if (!(0.0f < fall)) {
        return false;
    }

    this->m_fallRate = 1.0f / fall;
    this->m_facing = unit->GetRenderFacing();
    this->m_vertical = 0.0f;
    this->m_flags |= MOUNT_INITIALIZED;

    return true;
}

// ref: FUN_007fa930
void MountTransitionObject::GetRiderOffset(C3Vector& out) const {
    out = this->m_delta;

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_unit, TYPE_UNIT, ".\\SpellVisuals.cpp", 0x4ea));

    if (unit && unit->m_mountTransitionEffect && unit->m_mountTransitionEffect->m_model) {
        auto model = unit->m_mountTransitionEffect->m_model;
        C3Vector pos = model->GetPosition();
        C3Vector attach = model->GetAttachmentWorldPosition(0);

        out = {
            (attach.x - pos.x) * this->m_progress,
            (attach.y - pos.y) * this->m_progress,
            this->m_progress * (attach.z - pos.z)
        };
    }
}

// ref: FUN_007fb7f0
// PHASE4(World): the landing slope comes from the point facet query FUN_00783a40, which is not
// ported; without it the mount keeps the up vector it started with when it lands.
bool MountTransitionObject::Update() {
    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());
    float dt = static_cast<float>(now - this->m_lastTime) * 0.0010000000474974513f;
    this->m_lastTime = now;

    auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_unit, TYPE_UNIT, ".\\SpellVisuals.cpp", 0x432));

    if (!unit) {
        return false;
    }

    if ((this->m_flags & MOUNT_INITIALIZED) == 0 && !this->Initialize()) {
        return true;
    }

    float frac = static_cast<float>(static_cast<uint32_t>(OsGetAsyncTimeMs()) - this->m_startTime) * this->m_rate;

    if (0.0f < this->m_jumpStart && this->m_jumpStart < frac) {
        this->m_facing = unit->GetRenderFacing();
        this->m_progress = (frac - this->m_jumpStart) * this->m_jumpRate;

        if (this->m_progress > 1.0f) {
            this->m_flags |= MOUNT_DONE;
            this->m_progress = 1.0f;
        }
    }

    C3Vector up = unit->m_localMove.GetWorldUp();

    if (frac <= this->m_fallEnd) {
        if (this->m_fallStart < frac) {
            float t = (frac - this->m_fallStart) * this->m_fallRate;

            if (1.0f < t) {
                t = 1.0f;
            }

            float h = sqrtf(1.0f - t) * 10.0f;

            this->m_position = {
                this->m_start.x + this->m_delta.x * t,
                this->m_start.y + this->m_delta.y * t,
                this->m_start.z + this->m_delta.z * t
            };

            C3Vector top = { this->m_position.x, this->m_position.y, this->m_position.z + h };
            C3Vector bottom = { this->m_position.x, this->m_position.y, this->m_position.z - h };
            C3Vector hit = { 0.0f, 0.0f, 0.0f };
            float f = 1.0f;

            if (WorldQuerySegment(top, bottom, &hit, &f, 0x110, nullptr)) {
                this->m_flags |= MOUNT_LANDED;
                this->m_vertical = h - (f * h + f * h);
            }

            this->m_vertical = ClampRange(this->m_vertical, -h, h);
            this->m_position.z += this->m_vertical;
        }
    } else {
        this->m_position = unit->GetPosition();
    }

    // The up vector eases a quarter of the way to the ground's each update.
    this->m_up.x = (up.x - this->m_up.x) * 0.25f + this->m_up.x;
    this->m_up.y = (up.y - this->m_up.y) * 0.25f + this->m_up.y;
    this->m_up.z = 0.25f * (up.z - this->m_up.z) + this->m_up.z;

    float inv = 1.0f / sqrtf(this->m_up.x * this->m_up.x + this->m_up.y * this->m_up.y + this->m_up.z * this->m_up.z);
    this->m_up.x *= inv;
    this->m_up.y *= inv;
    this->m_up.z *= inv;

    if (this->m_flags & MOUNT_LANDED) {
        this->m_landing = dt + dt + this->m_landing;

        if (this->m_landing >= 1.0f) {
            this->m_landing = 1.0f;
        }
    }

    return true;
}

// ------------------------------------------------------------------------------------------------
// The module
// ------------------------------------------------------------------------------------------------

// ref: FUN_007fc5a0
CRndSeed& ObjectRandomSeed() {
    return s_shardSeed;
}

void SpellVisualsInitialize() {
    void* mem = SMemAlloc(sizeof(CLightningSystem), ".\\SpellVisuals.cpp", 0x166, 0);
    g_lightningSystem = mem ? new (mem) CLightningSystem() : nullptr;

    s_spellEffectLevel = CVar::Lookup("spellEffectLevel");

    SpellVisualsBuildModelAttachTable();
}

// ref: FUN_007fc9f0
void SpellVisualsReleaseObjects() {
    // FUN_007fbc20
    while (auto object = s_lightningObjects.Head()) {
        object->~LightningObject();
        SMemFree(object, "LightningObject", -2, 0);
    }

    while (auto object = s_blizzardObjects.Head()) {
        object->~BlizzardObject();
        SMemFree(object, "BlizzardObject", -2, 0);
    }
}

// ref: FUN_007fcbc0
void SpellVisualsShutdown() {
    SpellVisualsReleaseObjects();

    if (g_lightningSystem) {
        g_lightningSystem->~CLightningSystem();
        SMemFree(g_lightningSystem, __FILE__, __LINE__, 0);
    }

    g_lightningSystem = nullptr;
    s_lastUpdate = 0xFFFFFFFF;
    s_spellEffectLevel = nullptr;

    SpellVisualsFreeModelAttachTable();
}

// ref: FUN_007fca30
void SpellVisualsUpdate() {
    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());

    for (auto object = s_lightningObjects.Head(); object; ) {
        auto next = s_lightningObjects.Next(object);

        if (!object->Update(now)) {
            object->m_link.Unlink();
            object->m_refCount--;

            if (object->m_refCount == 0) {
                object->~LightningObject();
                SMemFree(object, "LightningObject", -2, 0);
            }
        }

        object = next;
    }

    float elapsed = 0.0f;

    if (s_lastUpdate != 0xFFFFFFFF) {
        elapsed = static_cast<float>(static_cast<int32_t>(now - s_lastUpdate)) * 0.0010000000474974513f;
    }

    s_lastUpdate = now;

    if (g_lightningSystem) {
        g_lightningSystem->Update(elapsed);
    }

    SpellVisualsDraw();

    for (auto object = s_blizzardObjects.Head(); object; ) {
        auto next = s_blizzardObjects.Next(object);

        if (!object->Update()) {
            object->~BlizzardObject();
            SMemFree(object, "BlizzardObject", -2, 0);
        }

        object = next;
    }

    for (auto object = s_mountTransitions.Head(); object; ) {
        auto next = s_mountTransitions.Next(object);

        if (!object->Update()) {
            MountTransitionObject::Release(object);
        }

        object = next;
    }
}

// ref: FUN_007f9ec0
void SpellVisualsDraw() {
    auto camera = CGWorldFrame::GetActiveCamera();

    if (!camera || !g_lightningSystem) {
        return;
    }

    g_lightningSystem->Draw(camera->Position());
}

// ref: FUN_007fc5f0
void SpellVisualsCreateChain(int32_t chainID, CGObject_C* caster, CEffect* effect, const WOWGUID* targets,
                             int32_t count, int32_t spellID, bool persistent, int32_t fromCaster,
                             LightningObject** out, int32_t outCount, const C3Vector* position,
                             int32_t param) {
    if (!caster || !spellID) {
        return;
    }

    auto rec = g_spellChainEffectsDB.GetRecord(chainID);

    if (!rec) {
        return;
    }

    auto first = rec;

    C3Vector startOffset = { 0.0f, 0.0f, 0.0f };
    C3Vector endOffset = { 0.0f, 0.0f, 0.0f };
    int32_t attachment = -1;
    uint32_t worldAttach = 0;

    auto spell = g_spellDB.GetRecord(spellID);
    auto visual = spell ? GetSpellVisual(spell) : nullptr;

    if (visual) {
        startOffset = { visual->m_missileCastOffset[0], -visual->m_missileCastOffset[1], visual->m_missileCastOffset[2] };
        attachment = visual->m_missileAttachment;
        endOffset = { visual->m_missileImpactOffset[0], -visual->m_missileImpactOffset[1], visual->m_missileImpactOffset[2] };
        worldAttach = visual->m_flags & 0x200;
    }

    WOWGUID none = 0;
    bool single = false;

    if (count == 0) {
        if (!position) {
            return;
        }

        targets = &none;
        count = 1;
        single = true;
    }

    int32_t outIndex = 0;
    uint32_t comboOffset = 0;

    for (;;) {
        auto object = LightningObject::Create();

        object->m_targets.SetCount(count + 1);
        object->m_bolts.SetCount(count);
        object->m_targets[0] = caster->GetGUID();
        object->m_flags = 0;

        CStatus status;
        object->m_texture = TextureCreate(rec->m_texture, CGxTexFlags(GxTex_Linear, 1, 0, 0, 0, 0, 1), &status, 0);

        object->m_startOffset = startOffset;
        object->m_spellID = spellID;
        object->m_attachment = attachment;
        object->m_rec = rec;

        if (!effect || persistent) {
            object->m_effect = nullptr;
        } else {
            object->m_effect = effect;
            effect->m_uses++;
            effect->m_refCount++;
        }

        if (!single) {
            object->m_endOffset = endOffset;
        } else {
            C44Matrix world;

            if (caster->IsA(TYPE_UNIT) && static_cast<CGUnit_C*>(caster)->m_vehiclePassenger) {
                caster->GetModelWorldMatrix(world);
            } else {
                caster->GetWorldMatrix(world);
            }

            world.d0 = 0.0f;
            world.d1 = 0.0f;
            world.d2 = 0.0f;

            C3Vector p = endOffset * world;

            object->m_flags |= LightningObject::LIGHTNING_FIXED_END;
            object->m_endOffset = { p.x + position->x, p.y + position->y, p.z + position->z };
        }

        if (persistent) {
            object->m_flags |= LightningObject::LIGHTNING_PERSISTENT;
        }

        if (worldAttach) {
            object->m_flags |= LightningObject::LIGHTNING_WORLD_ATTACH;
        }

        object->SetChain(targets, count, fromCaster, param);

        if (persistent) {
            object->m_refCount++;

            if (out) {
                out[outIndex++] = object;
            }
        }

        if (outIndex >= outCount || !first->m_combo || comboOffset >= SStrLen(first->m_combo)) {
            break;
        }

        uint32_t entry;
        memcpy(&entry, first->m_combo + comboOffset, 4);

        uint32_t next = DecodeComboEntry(entry);

        if (!next) {
            break;
        }

        rec = g_spellChainEffectsDB.GetRecord(static_cast<int32_t>(next));

        if (!rec) {
            break;
        }

        comboOffset += 4;

        if (comboOffset >= 0x30) {
            break;
        }
    }
}

// ref: FUN_007fa620
void SpellVisualsShakeCamera(int32_t id, const C3Vector& position) {
    auto rec = g_spellEffectCameraShakesDB.GetRecord(id);

    if (!rec) {
        return;
    }

    for (int32_t i = 0; i < 3; i++) {
        if (rec->m_cameraShake[i] != 0) {
            CGWorldFrame::GetActiveCamera()->AddShakeByID(rec->m_cameraShake[i], position);
        }
    }
}

// ref: FUN_007fbe90
void SpellVisualsBuildModelAttachTable() {
    SpellVisualsFreeModelAttachTable();

    int32_t maxKit = g_spellVisualKitDB.GetNumRecords() > 0 ? g_spellVisualKitDB.m_maxID : 0;

    void* mem = SMemAlloc(sizeof(*s_modelAttach), ".\\SpellVisuals.cpp", 0x534, 0);
    s_modelAttach = mem ? new (mem) TSGrowableArray<TSGrowableArray<const SpellVisualKitModelAttachRec*>*>() : nullptr;

    if (!s_modelAttach) {
        return;
    }

    s_modelAttach->SetCount(static_cast<uint32_t>(maxKit) + 1);

    for (uint32_t i = 0; i < s_modelAttach->Count(); i++) {
        (*s_modelAttach)[i] = nullptr;
    }

    for (int32_t i = 0; i < g_spellVisualKitModelAttachDB.GetNumRecords(); i++) {
        auto attach = g_spellVisualKitModelAttachDB.GetRecordByIndex(i);

        if (!attach || attach->m_parentSpellVisualKitID == 0 || attach->m_parentSpellVisualKitID > maxKit) {
            continue;
        }

        auto& slot = (*s_modelAttach)[attach->m_parentSpellVisualKitID];

        if (!slot) {
            void* listMem = SMemAlloc(sizeof(*slot), ".\\SpellVisuals.cpp", 0x547, 0);
            slot = listMem ? new (listMem) TSGrowableArray<const SpellVisualKitModelAttachRec*>() : nullptr;
        }

        if (slot) {
            slot->Add(1, &attach);
        }
    }
}

// ref: FUN_007fbb80
void SpellVisualsFreeModelAttachTable() {
    if (!s_modelAttach) {
        return;
    }

    for (uint32_t i = 0; i < s_modelAttach->Count(); i++) {
        auto slot = (*s_modelAttach)[i];

        if (slot) {
            slot->~TSGrowableArray();
            SMemFree(slot, __FILE__, __LINE__, 0);
        }
    }

    s_modelAttach->~TSGrowableArray();
    SMemFree(s_modelAttach, __FILE__, __LINE__, 0);
    s_modelAttach = nullptr;
}

// ref: FUN_007fa9f0
int32_t SpellVisualsGetModelAttachCount(int32_t kitID) {
    if (!s_modelAttach || kitID < 0 || static_cast<uint32_t>(kitID) >= s_modelAttach->Count()) {
        return 0;
    }

    auto slot = (*s_modelAttach)[kitID];

    return slot ? static_cast<int32_t>(slot->Count()) : 0;
}

// ref: FUN_007faa20
const SpellVisualKitModelAttachRec* SpellVisualsGetModelAttach(int32_t kitID, int32_t index) {
    return (*(*s_modelAttach)[kitID])[index];
}
