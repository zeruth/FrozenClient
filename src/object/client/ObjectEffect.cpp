#include "object/client/ObjectEffect.hpp"
#include "db/Db.hpp"
#include "model/CM2Model.hpp"
#include "object/client/CGObject_C.hpp"
#include "sound/SI2.hpp"
#include "sound/SOUNDKITDEF.hpp"
#include "sound/SoundKitProperties.hpp"
#include <tempest/Matrix.hpp>
#include <algorithm>
#include <cstring>

namespace {

// ---- the definitions (FUN_006f66b0) ----------------------------------------------------------

std::unordered_map<int32_t, CObjectEffectPackageDef*> s_packages;   // 0x00ca04dc
std::unordered_map<int32_t, CObjectEffectGroupDef*> s_groups;       // 0x00ca04b4
std::unordered_map<int32_t, CObjectEffectDef*> s_effects;           // 0x00ca048c

// The anim table at 0x00c9ecd0 (FUN_006f1820): for each of the 0x1fa animations, the state its
// body puts the object in and the one a mount playing it does.
struct ANIM_STATE {
    int32_t animID;
    int32_t state;
    int32_t mountState;
};

ANIM_STATE s_animStates[0x1fa];

// ref: FUN_006f1820
void InitAnimStates() {
    for (auto& entry : s_animStates) {
        entry.animID = 0x1fa;
        entry.state = 0;
    }

    static const ANIM_STATE s_set[] = {
        { 0, 0x13, 0x29 }, { 1, 0x0f, 0 }, { 4, 0x0d, 0 }, { 5, 0x0c, 0 }, { 6, 0x51, 0 },
        { 8, 0x11, 0 }, { 9, 0x0e, 0 }, { 10, 0x27, 0 }, { 11, 0x14, 0 }, { 12, 0x15, 0 },
        { 13, 0x16, 0 }, { 16, 0x28, 0 }, { 25, 0x12, 0 }, { 37, 0x17, 0 }, { 38, 0x24, 0 },
        { 39, 0x18, 0 }, { 40, 0x19, 0 }, { 41, 0x1a, 0x2a }, { 42, 0x1b, 0 }, { 43, 0x1c, 0 },
        { 44, 0x1d, 0 }, { 45, 0x1e, 0 }, { 53, 0x10, 0 }, { 92, 0x20, 0 }, { 93, 0x1f, 0 },
        { 96, 0x2d, 0 }, { 97, 0x2b, 0 }, { 98, 0x2c, 0 }, { 107, 0x08, 0 }, { 108, 0x0b, 0 },
        { 111, 0x09, 0 }, { 112, 0x0a, 0 }, { 135, 0x21, 0 }, { 143, 0x22, 0 }, { 146, 0x49, 0 },
        { 147, 0x4a, 0 }, { 148, 0x47, 0 }, { 149, 0x48, 0 }, { 150, 0x4b, 0 }, { 151, 0x4c, 0 },
        { 153, 0x4d, 0 }, { 154, 0x4e, 0 }, { 155, 0x4f, 0 }, { 156, 0x50, 0 }, { 162, 0x44, 0 },
        { 163, 0x45, 0 }, { 164, 0x46, 0 }, { 187, 0x23, 0 }, { 193, 0x2e, 0 }, { 226, 0x43, 0 },
    };

    for (const auto& set : s_set) {
        s_animStates[set.animID] = set;
    }
}

// The modifier's three stages (0x00ada37c, 0x00ada384, 0x00ada38c).

// ref: FUN_006f1d50
float InputSpeed(CObjectEffect* manager) {
    return manager->m_owner->Virtual0F8();
}

float ReadInput(int32_t type, CObjectEffect* manager) {
    switch (type) {
        case 1:
            return InputSpeed(manager);

        default:
            // 0x006f1700
            return -1.0f;
    }
}

// ref: FUN_006f1720
// Below the first parameter the third, above the second the fourth, and a straight line between.
float MapRamp(float value, const float* params) {
    if ((value < params[0]) != (value == params[0])) {
        return params[2];
    }

    if (params[1] <= value) {
        return params[3];
    }

    return (params[3] - params[2]) * ((value - params[0]) / (params[1] - params[0])) + params[2];
}

float MapInput(int32_t type, float value, const float* params) {
    switch (type) {
        case 1:
            return MapRamp(value, params);

        default:
            // 0x006f1710
            return value;
    }
}

// ref: FUN_006f1770
void OutputPitch(CObjectEffectInstance* instance, float value) {
    instance->m_sound.SetFrequencyScale(value);
}

void WriteOutput(int32_t type, CObjectEffectInstance* instance, float value) {
    if (type == 1) {
        OutputPitch(instance, value);
    }
}

// ref: FUN_006f1790
void ApplyModifier(const CObjectEffectModifier* modifier, CObjectEffect* manager, CObjectEffectInstance* instance) {
    if (0 < modifier->m_mapType) {
        float value = ReadInput(modifier->m_inputType, manager);
        value = MapInput(modifier->m_mapType, value, modifier->m_params);
        WriteOutput(modifier->m_outputType, instance, value);
    }
}

int32_t InputKey(const CObjectEffectDef* def) {
    return def->m_modifier ? def->m_modifier->m_inputType : 0;
}

// FUN_004c5c10: how a kit's advanced row uses it, 2 when it has none.
int32_t GetSoundKitUsage(int32_t id) {
    auto def = SI2::GetSoundKitDef(id);

    if (def && def->advanced) {
        return def->advanced->m_usage;
    }

    return 2;
}

void SetPropertyFloat(uint32_t& field, float value) {
    std::memcpy(&field, &value, sizeof(field));
}

} // namespace

// ---- instances -------------------------------------------------------------------------------

// ref: FUN_006f1d70
// Where the effect sounds: its offset through the attachment of the owner's model when that is
// ready, through the model itself without the attachment, else through the owner's matrix.
C3Vector CObjectEffectInstance::GetPosition() const {
    auto owner = this->m_manager->m_owner;
    auto model = owner->GetObjectModel();

    C44Matrix matrix;

    if (model && model->IsLoaded(0, 0) && model->m_flag8000) {
        if (model->HasAttachment(static_cast<uint32_t>(this->m_def->m_attachment))) {
            matrix = model->GetAttachmentWorldTransform(static_cast<uint32_t>(this->m_def->m_attachment));
        } else {
            matrix = model->AnimateAndGetWorldMatrix();
        }
    } else {
        owner->GetWorldMatrix(matrix);
    }

    return this->m_def->m_offset * matrix;
}

// ref: FUN_006f1e70
// A one-shot: a sound kit plays once, where the effect is.
void CObjectEffectInstance::Play() {
    C3Vector position = this->GetPosition();

    if (this->m_def->m_effectRecType == 1) {
        SoundKitProperties properties;
        properties.ResetToDefaults();
        properties.uint1c = 2;

        if (!SI2::IsPlaying(&this->m_sound)) {
            SI2::PlaySoundKit(this->m_def->m_effectRecID, &position, &this->m_sound, &properties, 0, nullptr, 0, 0);
        }
    }

    this->m_plays++;
}

// ref: FUN_006f1ef0
// A held effect's first start: its sound fades in (0.7 s both ways), or one still fading out comes
// back.
void CObjectEffectInstance::Start() {
    if (0 < this->m_plays) {
        return;
    }

    C3Vector position = this->GetPosition();

    if (this->m_def->m_effectRecType == 1) {
        if (!SI2::IsPlaying(&this->m_sound)) {
            int32_t usage = GetSoundKitUsage(this->m_def->m_effectRecID);

            SoundKitProperties properties;
            properties.ResetToDefaults();
            SetPropertyFloat(properties.uint10, 0.7f);
            SetPropertyFloat(properties.uint14, 0.7f);
            properties.uint1c = usage != 1 ? 1 : 0;

            SI2::PlaySoundKit(this->m_def->m_effectRecID, &position, &this->m_sound, &properties, 0, nullptr, 0, 0);
            this->m_plays++;

            return;
        }

        this->m_sound.m_sound.BeginFadeIn();
    }

    this->m_plays++;
}

// ref: FUN_006f1fb0
// Each frame a held sound keeps playing, follows its modifier and moves with the object.
void CObjectEffectInstance::Update(CObjectEffect* manager) {
    if (this->m_def->m_effectRecType != 1) {
        return;
    }

    C3Vector position = this->GetPosition();

    if (!SI2::IsPlaying(&this->m_sound)) {
        this->Start();
    } else {
        this->m_sound.m_sound.BeginFadeIn();
    }

    if (this->m_def->m_modifier) {
        ApplyModifier(this->m_def->m_modifier, manager, this);
    }

    this->m_sound.SetPosition(position);
}

// ref: FUN_006f2ac0
// The effect ends: its sound stops and it leaves its list.
void CObjectEffectInstance::Destroy() {
    if (this->m_def->m_effectRecType == 1) {
        SI2::StopOrFadeOut(&this->m_sound, 0, -1.0f, 1);
    }

    this->m_link.Unlink();
}

// ---- the manager -----------------------------------------------------------------------------

// ref: FUN_006f5900
CObjectEffect::CObjectEffect() {
    // 0xb bytes, cleared (the reference's SMemAlloc flag 8).
    this->m_states.Assign(new uint8_t[(CObjectEffect::NUM_STATES + 7) / 8](), CObjectEffect::NUM_STATES, true);
}

// ref: FUN_006f7370
CObjectEffect::~CObjectEffect() {
    this->Release();
}

// ref: FUN_006f7420
// The object starts in the package's base states: 1, and 0x26 (standing still).
int32_t CObjectEffect::Init(int32_t packageID, CGObject_C* owner) {
    this->m_packageID = packageID;

    auto it = s_packages.find(packageID);

    if (it == s_packages.end()) {
        return 0;
    }

    this->m_package = it->second;
    this->m_owner = owner;

    this->SetState(1, 1, 0);
    this->SetState(0x26, 1, 0);

    return 1;
}

// ref: FUN_006f70b0
// Every state goes off without its exit effects, and every effect is dropped.
void CObjectEffect::Release() {
    for (uint32_t state = 0; state < CObjectEffect::NUM_STATES; state++) {
        this->ClearState(state, 0);
    }

    this->m_inputs.clear();
    this->m_groups.clear();

    while (auto instance = this->m_active.Head()) {
        instance->Destroy();
        delete instance;
    }

    while (auto instance = this->m_finishing.Head()) {
        instance->Destroy();
        delete instance;
    }
}

// ref: FUN_006f7270
// A state goes on (or, with `onlyIfSet`, is re-entered while on): its groups' entry effects play
// once when `playEnter`, and their held effects start.
void CObjectEffect::SetState(uint32_t state, int32_t playEnter, int32_t onlyIfSet) {
    if (!onlyIfSet) {
        if (this->m_states.IsSet(state)) {
            return;
        }

        this->m_states.Set(state, true);
    } else if (!this->m_states.IsSet(state)) {
        return;
    }

    auto it = this->m_package->m_states.find(static_cast<int32_t>(state));

    if (it == this->m_package->m_states.end()) {
        return;
    }

    for (auto group : it->second) {
        if (playEnter) {
            for (auto def : group->m_effects[0]) {
                this->PlayOneShot(def);
            }
        }

        for (auto def : group->m_effects[1]) {
            this->StartPersistent(def, group->m_ID);
        }
    }
}

// ref: FUN_006f61d0
// A state goes off: its groups' exit effects play once when `playExit`, and each held effect loses
// the state's hold; one nothing else holds starts finishing (a sound fades out). A group whose
// effects all finished forgets them.
void CObjectEffect::ClearState(uint32_t state, int32_t playExit) {
    if (!this->m_states.IsSet(state)) {
        return;
    }

    this->m_states.Set(state, false);

    auto it = this->m_package->m_states.find(static_cast<int32_t>(state));

    if (it == this->m_package->m_states.end()) {
        return;
    }

    for (auto group : it->second) {
        if (playExit) {
            for (auto def : group->m_effects[2]) {
                this->PlayOneShot(def);
            }
        }

        auto running = this->m_groups.find(group->m_ID);

        if (running == this->m_groups.end()) {
            continue;
        }

        bool finished = true;

        for (auto& entry : running->second) {
            auto instance = entry.second;

            if (--instance->m_refs == 0) {
                instance->m_link.Unlink();
                this->m_finishing.LinkToTail(instance);

                if (instance->m_def->m_effectRecType == 1) {
                    SI2::StopOrFadeOut(&instance->m_sound, 0, -1.0f, 1);
                }
            } else {
                finished = false;
            }
        }

        if (finished) {
            running->second.clear();
        }
    }
}

// ref: FUN_006f60e0
// An effect that plays once and finishes on its own.
void CObjectEffect::PlayOneShot(const CObjectEffectDef* def) {
    auto instance = new CObjectEffectInstance();
    instance->m_manager = this;
    instance->m_def = def;

    this->m_finishing.LinkToTail(instance);
    this->m_inputs[InputKey(def)].push_back(instance);

    instance->Play();

    if (def->m_modifier) {
        ApplyModifier(def->m_modifier, this, instance);
    }
}

// ref: FUN_006f6420
// An effect a state holds for group `groupID`: one still finishing is taken back (a sound fades
// in again), one the group already runs gains a hold, otherwise a new one starts.
void CObjectEffect::StartPersistent(const CObjectEffectDef* def, int32_t groupID) {
    auto& group = this->m_groups[groupID];

    for (auto instance = this->m_finishing.Head(); instance; instance = this->m_finishing.Next(instance)) {
        if (instance->m_def != def) {
            continue;
        }

        group[def->m_ID] = instance;

        instance->m_link.Unlink();
        this->m_active.LinkToTail(instance);

        if (def->m_effectRecType != 1) {
            return;
        }

        instance->m_sound.m_sound.BeginFadeIn();
        instance->m_refs++;

        return;
    }

    auto held = group.find(def->m_ID);

    if (held != group.end()) {
        held->second->m_refs++;
        return;
    }

    auto instance = new CObjectEffectInstance();
    group[def->m_ID] = instance;
    instance->m_manager = this;
    instance->m_def = def;

    this->m_active.LinkToTail(instance);
    instance->m_refs++;

    this->m_inputs[InputKey(def)].push_back(instance);

    instance->Start();

    if (def->m_modifier) {
        ApplyModifier(def->m_modifier, this, instance);
    }
}

// ref: FUN_006f3910
// The input `inputType` changed (a transport's speed): every effect reading it retakes its value.
void CObjectEffect::ApplyInput(int32_t inputType) {
    auto it = this->m_inputs.find(inputType);

    if (it == this->m_inputs.end()) {
        return;
    }

    for (auto instance : it->second) {
        auto modifier = instance->m_def->m_modifier;

        if (modifier && 0 < modifier->m_mapType) {
            ApplyModifier(modifier, this, instance);
        }
    }
}

// ref: FUN_006f39b0
// Each frame: held effects keep up with the object; a finishing sound follows it until it stops,
// and then -- as at once for any other kind but 2 -- the effect is dropped.
void CObjectEffect::Update() {
    for (auto instance = this->m_active.Head(); instance; instance = this->m_active.Next(instance)) {
        instance->Update(this);
    }

    auto instance = this->m_finishing.Head();

    while (instance) {
        auto type = instance->m_def->m_effectRecType;

        if (type == 1) {
            C3Vector position = instance->GetPosition();

            if (SI2::IsPlaying(&instance->m_sound)) {
                instance->m_sound.SetPosition(position);
                instance = this->m_finishing.Next(instance);
                continue;
            }
        } else if (type == 2) {
            instance = this->m_finishing.Next(instance);
            continue;
        }

        auto refs = this->m_inputs.find(InputKey(instance->m_def));

        if (refs != this->m_inputs.end()) {
            auto& list = refs->second;
            list.erase(std::remove(list.begin(), list.end(), instance), list.end());
        }

        instance->m_plays--;

        auto next = this->m_finishing.Next(instance);

        instance->Destroy();
        delete instance;

        instance = next;
    }
}

// ref: FUN_006f17f0
int32_t ObjectEffectGetAnimState(uint32_t animID, uint32_t variation) {
    if (animID >= sizeof(s_animStates) / sizeof(s_animStates[0])) {
        return 0;
    }

    return variation == 1 ? s_animStates[animID].mountState : s_animStates[animID].state;
}

// ref: FUN_006f66b0
// The definitions: packages and groups by id; each package element files a group under a state;
// each effect joins its group by trigger type with its modifier; then each package files the
// groups with event effects under each event.
void ObjectEffectInitialize() {
    InitAnimStates();

    for (int32_t i = 0; i < g_objectEffectPackageDB.GetNumRecords(); i++) {
        auto rec = g_objectEffectPackageDB.GetRecordByIndex(i);
        auto package = new CObjectEffectPackageDef();
        package->m_ID = rec->m_ID;
        s_packages[rec->m_ID] = package;
    }

    for (int32_t i = 0; i < g_objectEffectGroupDB.GetNumRecords(); i++) {
        auto rec = g_objectEffectGroupDB.GetRecordByIndex(i);
        auto group = new CObjectEffectGroupDef();
        group->m_ID = rec->m_ID;
        s_groups[rec->m_ID] = group;
    }

    for (int32_t i = 0; i < g_objectEffectPackageElemDB.GetNumRecords(); i++) {
        auto rec = g_objectEffectPackageElemDB.GetRecordByIndex(i);
        auto package = s_packages.find(rec->m_objectEffectPackageID);

        if (package == s_packages.end()) {
            continue;
        }

        auto group = s_groups.find(rec->m_objectEffectGroupID);

        package->second->m_states[rec->m_stateType].push_back(group != s_groups.end() ? group->second : nullptr);
    }

    for (int32_t i = 0; i < g_objectEffectDB.GetNumRecords(); i++) {
        auto rec = g_objectEffectDB.GetRecordByIndex(i);
        auto def = new CObjectEffectDef();

        def->m_ID = rec->m_ID;
        def->m_effectRecType = rec->m_effectRecType;
        def->m_effectRecID = rec->m_effectRecID;
        def->m_attachment = rec->m_attachment;
        def->m_offset = { rec->m_offset[0], rec->m_offset[1], rec->m_offset[2] };

        auto modifierRec = rec->m_objectEffectModifierID ? g_objectEffectModifierDB.GetRecord(rec->m_objectEffectModifierID) : nullptr;

        if (modifierRec && 0 < modifierRec->m_inputType && 0 < modifierRec->m_mapType && 0 < modifierRec->m_outputType) {
            auto modifier = new CObjectEffectModifier();
            modifier->m_inputType = modifierRec->m_inputType;
            modifier->m_outputType = modifierRec->m_outputType;
            modifier->m_mapType = modifierRec->m_mapType;
            modifier->m_ID = modifierRec->m_ID;
            modifier->m_paramCount = modifier->m_mapType == 1 ? 4 : 0;

            if (modifier->m_paramCount) {
                modifier->m_params = new float[modifier->m_paramCount];

                for (int32_t p = 0; p < modifier->m_paramCount; p++) {
                    modifier->m_params[p] = modifierRec->m_param[p];
                }
            }

            def->m_modifier = modifier;
        }

        s_effects[rec->m_ID] = def;

        auto group = s_groups.find(rec->m_objectEffectGroupID);

        if (group != s_groups.end() && 1 <= rec->m_triggerType && rec->m_triggerType <= 4) {
            group->second->m_effects[rec->m_triggerType - 1].push_back(def);
        }
    }

    for (auto& package : s_packages) {
        for (auto& state : package.second->m_states) {
            for (auto group : state.second) {
                if (!group) {
                    continue;
                }

                for (auto def : group->m_effects[3]) {
                    auto rec = g_objectEffectDB.GetRecord(def->m_ID);
                    auto& list = package.second->m_events[rec ? rec->m_eventType : 0];

                    if (std::find(list.begin(), list.end(), group) == list.end()) {
                        list.push_back(group);
                    }
                }
            }
        }
    }
}
