#include "ui/simple/CSimpleAnimGroup.hpp"
#include "ui/simple/CSimpleAnim.hpp"
#include "ui/simple/CSimpleAnimTypes.hpp"
#include "ui/simple/CSimpleAnimScript.hpp"
#include "ui/CScriptRegion.hpp"
#include "ui/FrameScript.hpp"
#include <storm/String.hpp>
#include <storm/Memory.hpp>
#include <cstdint>

int32_t CSimpleAnimGroup::s_metatable;
int32_t CSimpleAnimGroup::s_objectType;
const char* CSimpleAnimGroup::s_objectTypeName = "AnimationGroup";

static const char* s_loopTypeNames[NUM_ANIM_LOOPTYPES] = { "NONE", "REPEAT", "BOUNCE" };
static const char* s_loopStateNames[NUM_ANIM_LOOPSTATES] = { "NONE", "FORWARD", "REVERSE" };

const char* AnimLoopTypeName(ANIM_LOOPTYPE loopType) {
    // The reference's lookup is a scan of three rows that answers "UNKNOWN" when none matches,
    // which is reachable only if the stored value is out of range.
    if (loopType < 0 || loopType >= NUM_ANIM_LOOPTYPES) {
        return "UNKNOWN";
    }

    return s_loopTypeNames[loopType];
}

bool AnimLoopTypeFromName(const char* name, ANIM_LOOPTYPE& loopType) {
    for (int32_t i = 0; i < NUM_ANIM_LOOPTYPES; i++) {
        if (!SStrCmpI(name, s_loopTypeNames[i], 0x7FFFFFFF)) {
            loopType = static_cast<ANIM_LOOPTYPE>(i);

            return true;
        }
    }

    return false;
}

const char* AnimLoopStateName(ANIM_LOOPSTATE loopState) {
    if (loopState < 0 || loopState >= NUM_ANIM_LOOPSTATES) {
        return "UNKNOWN";
    }

    return s_loopStateNames[loopState];
}

void CSimpleAnimGroup::CreateScriptMetaTable() {
    lua_State* L = FrameScript_GetContext();
    CSimpleAnimGroup::s_metatable = FrameScript_Object::CreateScriptMetaTable(
        L, &CSimpleAnimGroup::RegisterScriptMethods
    );
}

int32_t CSimpleAnimGroup::GetObjectType() {
    if (!CSimpleAnimGroup::s_objectType) {
        CSimpleAnimGroup::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CSimpleAnimGroup::s_objectType;
}

void CSimpleAnimGroup::RegisterScriptMethods(lua_State* L) {
    FrameScript_Object::FillScriptMethodTable(
        L, SimpleAnimGroupMethods, NUM_SIMPLE_ANIM_GROUP_SCRIPT_METHODS
    );
}

CSimpleAnimGroup::CSimpleAnimGroup(CScriptRegion* region) : CScriptObject() {
    this->m_region = region;
}

CSimpleAnimGroup::~CSimpleAnimGroup() {
    // Clear each animation's back-pointer before destroying it, so ~CSimpleAnim does not try to
    // remove itself from an array that is already going away.
    for (uint32_t i = 0; i < this->m_animations.Count(); i++) {
        CSimpleAnim* anim = this->m_animations[i];

        if (anim) {
            anim->m_group = nullptr;
            anim->~CSimpleAnim();
            SMemFree(anim, __FILE__, __LINE__, 0x0);
        }
    }

    this->m_animations.SetCount(0);
}

// ref: FUN_00497800
// Seven handlers, in this order. Read out of the reference rather than assumed: OnLoad was missed
// on the first pass, and OnLoad, OnPlay and OnPause deliberately set NO wrapper where the other
// four do -- they take only self, so the reference leaves whatever the base put in `data`.
FrameScript_Object::ScriptIx* CSimpleAnimGroup::GetScriptByName(const char* name, ScriptData& data) {
    auto parentScript = CScriptObject::GetScriptByName(name, data);

    if (parentScript) {
        return parentScript;
    }

    if (!SStrCmpI(name, "OnLoad", STORM_MAX_STR)) {
        return &this->m_onLoad;
    }

    if (!SStrCmpI(name, "OnPlay", STORM_MAX_STR)) {
        return &this->m_onPlay;
    }

    if (!SStrCmpI(name, "OnPause", STORM_MAX_STR)) {
        return &this->m_onPause;
    }

    if (!SStrCmpI(name, "OnStop", STORM_MAX_STR)) {
        data.wrapper = "return function(self,requested) %s end";
        return &this->m_onStop;
    }

    if (!SStrCmpI(name, "OnFinished", STORM_MAX_STR)) {
        data.wrapper = "return function(self,requested) %s end";
        return &this->m_onFinished;
    }

    if (!SStrCmpI(name, "OnUpdate", STORM_MAX_STR)) {
        data.wrapper = "return function(self,elapsed) %s end";
        return &this->m_onUpdate;
    }

    if (!SStrCmpI(name, "OnLoop", STORM_MAX_STR)) {
        data.wrapper = "return function(self,loopState) %s end";
        return &this->m_onLoop;
    }

    return nullptr;
}

int32_t CSimpleAnimGroup::GetScriptMetaTable() {
    return CSimpleAnimGroup::s_metatable;
}

bool CSimpleAnimGroup::IsA(int32_t type) {
    return type == CSimpleAnimGroup::GetObjectType() || this->CScriptObject::IsA(type);
}

bool CSimpleAnimGroup::IsA(const char* typeName) {
    return !SStrCmpI(typeName, CSimpleAnimGroup::s_objectTypeName, 0x7FFFFFFF)
        || this->CScriptObject::IsA(typeName);
}

const char* CSimpleAnimGroup::GetObjectTypeName() {
    return CSimpleAnimGroup::s_objectTypeName;
}

CScriptObject* CSimpleAnimGroup::GetScriptObjectParent() {
    return this->m_region;
}

// ref: FUN_0049a8f0
// Refuses an empty group outright, which is the reference's first test. Everything else it does --
// walking the animations of the current order and starting each, resetting the loop state when the
// group was finished, notifying the parent region -- needs the driver, so this sets the state and
// starts the animations and stops there. See stage 4 in docs/ref/parity-animations.md.
bool CSimpleAnimGroup::Play() {
    if (this->m_animations.Count() == 0) {
        return false;
    }

    if (this->m_pendingFinish) {
        this->m_pendingFinish = false;
        this->m_progress = 0.0f;
        this->m_elapsed = 0.0f;
        this->m_loopState = ANIM_LOOPSTATE_NONE;
    }

    this->m_playing = true;
    this->m_paused = false;

    return true;
}

void CSimpleAnimGroup::Pause() {
    if (this->m_playing) {
        this->m_paused = true;

        for (uint32_t i = 0; i < this->m_animations.Count(); i++) {
            this->m_animations[i]->Pause();
        }
    }
}

void CSimpleAnimGroup::Stop() {
    this->m_playing = false;
    this->m_paused = false;
    this->m_pendingFinish = false;
    this->m_elapsed = 0.0f;
    this->m_progress = 0.0f;
    this->m_loopState = ANIM_LOOPSTATE_NONE;

    for (uint32_t i = 0; i < this->m_animations.Count(); i++) {
        this->m_animations[i]->Stop();
    }
}

// Finish() asks the group to stop at the END of the current loop rather than immediately, which is
// what separates it from Stop(). With no driver there is no later loop boundary to reach, so the
// flag is set and the group is left playing -- IsPendingFinish reports it, as the reference's does.
void CSimpleAnimGroup::Finish() {
    if (this->m_playing) {
        this->m_pendingFinish = true;
    }
}

bool CSimpleAnimGroup::IsDone() const {
    if (this->m_animations.Count() == 0) {
        return false;
    }

    for (uint32_t i = 0; i < this->m_animations.Count(); i++) {
        if (!this->m_animations[i]->IsDone()) {
            return false;
        }
    }

    return true;
}

int32_t CSimpleAnimGroup::GetMaxOrder() const {
    int32_t max = 0;

    for (uint32_t i = 0; i < this->m_animations.Count(); i++) {
        // m_order is stored zero-based; the Lua-facing order is one more. An animation left at the
        // -1 sentinel has no order yet and does not raise the maximum.
        int32_t order = this->m_animations[i]->m_order + 1;

        if (order > max) {
            max = order;
        }
    }

    return max;
}

// The group runs its animations grouped by order, one order after another, so its duration is the
// sum over orders of the longest animation in each -- not the sum of every animation.
float CSimpleAnimGroup::GetDuration() const {
    float total = 0.0f;
    int32_t maxOrder = this->GetMaxOrder();

    for (int32_t order = 1; order <= maxOrder; order++) {
        float longest = 0.0f;

        for (uint32_t i = 0; i < this->m_animations.Count(); i++) {
            CSimpleAnim* anim = this->m_animations[i];

            if (anim->m_order + 1 != order) {
                continue;
            }

            float span = anim->m_startDelay + anim->m_duration + anim->m_endDelay;

            if (span > longest) {
                longest = span;
            }
        }

        total += longest;
    }

    return total;
}

// The type dispatch out of the reference's FUN_004a7e00, which allocates a different size and
// calls a different constructor per name. Comparison is SStrCmpI there, so the names are
// case-insensitive, and an unrecognised one falls through to the base Animation rather than
// failing -- that fall-through is the reference's behaviour, not a shortcut here.
#define WHOA_ANIM_BRANCH(typeName, cls)                                     \
    if (!SStrCmpI(type, typeName, 0x7FFFFFFF)) {                            \
        void* m = SMemAlloc(sizeof(cls), __FILE__, __LINE__, 0x0);          \
        anim = new (m) cls(this);                                           \
    } else

CSimpleAnim* CSimpleAnimGroup::CreateAnimation(const char* type, const char* name) {
    CSimpleAnim* anim = nullptr;

    if (!type) {
        type = "Animation";
    }

    WHOA_ANIM_BRANCH("Translation", CSimpleTranslationAnim)
    WHOA_ANIM_BRANCH("Rotation", CSimpleRotationAnim)
    WHOA_ANIM_BRANCH("Scale", CSimpleScaleAnim)
    WHOA_ANIM_BRANCH("Path", CSimplePathAnim)
    WHOA_ANIM_BRANCH("Alpha", CSimpleAlphaAnim)
    {
        void* m = SMemAlloc(sizeof(CSimpleAnim), __FILE__, __LINE__, 0x0);
        anim = new (m) CSimpleAnim(this);
    }

    if (name && *name) {
        anim->SetName(name);
    }

    this->AddAnimation(anim);

    return anim;
}

#undef WHOA_ANIM_BRANCH

void CSimpleAnimGroup::AddAnimation(CSimpleAnim* anim) {
    if (!anim) {
        return;
    }

    for (uint32_t i = 0; i < this->m_animations.Count(); i++) {
        if (this->m_animations[i] == anim) {
            return;
        }
    }

    this->m_animations.Add(1, &anim);
}

void CSimpleAnimGroup::RemoveAnimation(CSimpleAnim* anim) {
    uint32_t count = this->m_animations.Count();

    for (uint32_t i = 0; i < count; i++) {
        if (this->m_animations[i] != anim) {
            continue;
        }

        for (uint32_t j = i; j + 1 < count; j++) {
            this->m_animations[j] = this->m_animations[j + 1];
        }

        this->m_animations.SetCount(count - 1);

        return;
    }
}
