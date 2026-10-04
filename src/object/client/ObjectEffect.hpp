// ObjectEffect.cpp: the effect packages a creature or game object display carries.
//
// A package (ObjectEffectPackage.dbc) names groups (ObjectEffectGroup.dbc) by the object state that
// turns each on (ObjectEffectPackageElem.dbc); a group holds effects (ObjectEffect.dbc) by when they
// fire -- entering the state, while in it, leaving it, or on an event. The only effect kind the
// client plays is a sound kit (type 1), placed at an attachment, with an optional modifier
// (ObjectEffectModifier.dbc) that maps the object's speed onto the sound's pitch.
//
// Each object with a package owns a CObjectEffect (CGObject_C +0xcc). Its 0x52 states are switched
// by the unit's animations and movement and by a game object's animation and transport progress;
// the instances it starts live on two lists, the ones held by a state and the ones finishing.
//
// The reference keeps the definitions in Storm hash tables (OBJ_EFFECT_*_LOOKUP) and the per-object
// bookkeeping in two more; frozen keeps the same relations in standard containers.
#ifndef OBJECT_CLIENT_OBJECT_EFFECT_HPP
#define OBJECT_CLIENT_OBJECT_EFFECT_HPP

#include "sound/SOUNDKITOBJECT.hpp"
#include "util/BitArray.hpp"
#include <storm/List.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>

class CGObject_C;
class CObjectEffect;

// An ObjectEffectModifier row, kept only when its input, mapping and output are all set (0x18).
struct CObjectEffectModifier {
    int32_t m_inputType = 0;        // +0x00: 1 the object's speed (CGObject_C slot 0xf8)
    int32_t m_mapType = 0;          // +0x04: 1 a ramp from (param 0, param 2) to (param 1, param 3)
    int32_t m_outputType = 0;       // +0x08: 1 the sound's pitch
    float* m_params = nullptr;      // +0x0c
    int32_t m_paramCount = 0;       // +0x10: 4 for the ramp
    int32_t m_ID = 0;               // +0x14
};

// One ObjectEffect row (the reference's entry +0x18).
struct CObjectEffectDef {
    int32_t m_ID = 0;               // +0x08
    int32_t m_effectRecType = 0;    // +0x0c: 1 a sound kit
    int32_t m_effectRecID = 0;      // +0x10
    int32_t m_attachment = 0;       // +0x14
    C3Vector m_offset;              // +0x18
    CObjectEffectModifier* m_modifier = nullptr; // +0x24
};

// One ObjectEffectGroup row and its effects by trigger type (+0x10, +0x1c, +0x28, +0x34).
struct CObjectEffectGroupDef {
    int32_t m_ID = 0;               // +0x08
    std::vector<CObjectEffectDef*> m_effects[4];
};

// One ObjectEffectPackage row: its groups by object state (+0x0c) and by event (+0x34).
struct CObjectEffectPackageDef {
    int32_t m_ID = 0;
    std::unordered_map<int32_t, std::vector<CObjectEffectGroupDef*>> m_states;
    std::unordered_map<int32_t, std::vector<CObjectEffectGroupDef*>> m_events;
};

// One running effect (0x2c).
class CObjectEffectInstance {
    public:
        TSLink<CObjectEffectInstance> m_link;   // +0x00
        SOUNDKITOBJECT m_sound;                 // +0x08
        const CObjectEffectDef* m_def = nullptr; // +0x1c
        CObjectEffect* m_manager = nullptr;     // +0x20
        int32_t m_plays = 0;                    // +0x24
        int32_t m_refs = 0;                     // +0x28

        C3Vector GetPosition() const;
        void Play();
        void Start();
        void Update(CObjectEffect* manager);
        void Destroy();
};

// An object's effect manager (0x80).
class CObjectEffect {
    public:
        static const uint32_t NUM_STATES = 0x52;

        int32_t m_packageID = 0;                            // +0x00
        BitArray m_states;                                  // +0x04
        CGObject_C* m_owner = nullptr;                      // +0x10
        const CObjectEffectPackageDef* m_package = nullptr; // +0x14
        // +0x18: for each group a state has on, its running effects by effect id.
        std::unordered_map<int32_t, std::unordered_map<int32_t, CObjectEffectInstance*>> m_groups;
        // +0x40: the running effects whose modifier reads each input.
        std::unordered_map<int32_t, std::vector<CObjectEffectInstance*>> m_inputs;
        // +0x68: the effects a state holds; +0x74 the ones finishing.
        STORM_EXPLICIT_LIST(CObjectEffectInstance, m_link) m_active;
        STORM_EXPLICIT_LIST(CObjectEffectInstance, m_link) m_finishing;

        CObjectEffect();
        ~CObjectEffect();
        int32_t Init(int32_t packageID, CGObject_C* owner);
        void Release();
        void SetState(uint32_t state, int32_t playEnter, int32_t onlyIfSet);
        void ClearState(uint32_t state, int32_t playExit);
        void PlayOneShot(const CObjectEffectDef* def);
        void StartPersistent(const CObjectEffectDef* def, int32_t groupID);
        void ApplyInput(int32_t inputType);
        void Update();
};

// The state an animation puts an object in, for its body (`variation` 0) or its mount (1).
int32_t ObjectEffectGetAnimState(uint32_t animID, uint32_t variation);

// The package, group and effect definitions, built once from the DBCs.
void ObjectEffectInitialize();

#endif
