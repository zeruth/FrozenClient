#include "ui/simple/CSimpleAnimGroup.hpp"
#include "ui/simple/CSimpleAnim.hpp"
#include "ui/simple/CSimpleAnimTypes.hpp"
#include "ui/simple/CSimpleAnimScript.hpp"
#include "ui/CScriptRegion.hpp"
#include "ui/FrameScript.hpp"
#include "util/CStatus.hpp"
#include "util/Lua.hpp"
#include <common/XML.hpp>
#include <storm/String.hpp>
#include <storm/Memory.hpp>
#include <cstdint>

int32_t CSimpleAnimGroup::s_metatable;
int32_t CSimpleAnimGroup::s_objectType;
const char* CSimpleAnimGroup::s_objectTypeName = "AnimationGroup";

// The largest step the driver will take in one tick: _DAT_009ec218, which is 60 seconds. It is a
// sanity bound against a stall or a load spike handing the driver an absurd delta, NOT a smoothing
// cap -- a small value here would make every animation crawl after any hitch.
static const float ANIM_MAX_STEP = 60.0f;

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

void CSimpleAnimGroup::CollectOrder(int32_t order, TSGrowableArray<CSimpleAnim*>& out) const {
    out.SetCount(0);

    for (uint32_t i = 0; i < this->m_animations.Count(); i++) {
        CSimpleAnim* anim = this->m_animations[i];

        if (anim->m_order + 1 == order) {
            out.Add(1, &anim);
        }
    }
}

float CSimpleAnimGroup::OrderDuration(int32_t order) const {
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

    return longest;
}

// ref: FUN_0049b470
// Every animation of the current order has finished. Step to the next one, or loop, or end.
//
// The remaining time is carried in rather than discarded, which is what lets a group of short
// orders get through several of them in one frame.
void CSimpleAnimGroup::AdvanceOrder(float remaining) {
    bool forward = this->m_loopState != ANIM_LOOPSTATE_REVERSE;
    int32_t maxOrder = this->GetMaxOrder();

    int32_t step = forward ? 1 : -1;
    int32_t past = forward ? maxOrder : -1;
    int32_t restart = forward ? 0 : maxOrder - 1;

    this->m_currentOrder += step;

    if (this->m_currentOrder != past) {
        // Still inside the group: start the animations of the order just reached.
        TSGrowableArray<CSimpleAnim*> order;
        this->CollectOrder(this->m_currentOrder, order);

        this->m_orderDuration = this->OrderDuration(this->m_currentOrder);

        for (uint32_t i = 0; i < order.Count(); i++) {
            CSimpleAnim* anim = order[i];

            if (this->Play() && !anim->m_playing) {
                anim->m_playing = true;
                anim->m_paused = false;

                if (anim->m_onPlay.luaRef) {
                    anim->RunScript(anim->m_onPlay, 0, nullptr);
                }
            }
        }

        return;
    }

    // Ran off the end. Either finish, or loop back round.
    if (this->m_looping == ANIM_LOOPTYPE_NONE || this->m_pendingFinish) {
        auto L = FrameScript_GetContext();

        if (this->m_onUpdate.luaRef) {
            lua_pushnumber(L, remaining);
            this->RunScript(this->m_onUpdate, 1, nullptr);
        }

        if (this->m_onFinished.luaRef) {
            // Unlike an animation's, the group's OnFinished is told whether the finish was asked
            // for -- Finish() sets that, a natural end does not.
            lua_pushboolean(L, this->m_pendingFinish);
            this->RunScript(this->m_onFinished, 1, nullptr);
        }

        if (this->m_region) {
            this->m_region->NotifyAnimEnd(this);
        }

        this->m_playing = false;
        this->m_paused = false;
        this->m_pendingFinish = false;
        this->m_elapsed = 0.0f;
        this->m_progress = 0.0f;
        this->m_loopState = ANIM_LOOPSTATE_NONE;
        this->m_currentOrder = -1;

        return;
    }

    if (forward && remaining > 0.0001f) {
        this->m_unapplyPending = true;
    }

    if (this->m_looping == ANIM_LOOPTYPE_BOUNCE) {
        // Turn round rather than jumping back to the start, and step off the end we just hit.
        this->m_loopState = forward ? ANIM_LOOPSTATE_REVERSE : ANIM_LOOPSTATE_FORWARD;
        this->m_currentOrder = past - step;
    } else {
        this->m_currentOrder = restart;
    }

    this->m_elapsed = 0.0f;
    this->m_progress = 0.0f;
    this->m_orderDuration = this->OrderDuration(this->m_currentOrder);

    // Every animation in the group is rewound, not just the current order's: on the next lap the
    // earlier orders have to run again from nothing.
    for (uint32_t i = 0; i < this->m_animations.Count(); i++) {
        CSimpleAnim* anim = this->m_animations[i];

        anim->m_loopState = this->m_loopState;
        anim->m_elapsed = 0.0f;
        anim->m_progressWithDelay = 0.0f;
        anim->m_progress = 0.0f;
        anim->m_playing = false;
        anim->m_paused = false;

        if (anim->m_order + 1 != this->m_currentOrder) {
            continue;
        }

        if (this->Play() && !anim->m_playing) {
            anim->m_playing = true;

            if (anim->m_onPlay.luaRef) {
                anim->RunScript(anim->m_onPlay, 0, nullptr);
            }
        }
    }

    if (this->m_onLoop.luaRef) {
        auto L = FrameScript_GetContext();
        lua_pushstring(L, AnimLoopStateName(this->m_loopState));
        this->RunScript(this->m_onLoop, 1, nullptr);
    }
}

// ref: FUN_0049ab60
void CSimpleAnimGroup::OnAnimationFinished(CSimpleAnim* anim) {
    if (!this->m_playing || this->m_stopping) {
        return;
    }

    for (uint32_t i = 0; i < this->m_animations.Count(); i++) {
        CSimpleAnim* other = this->m_animations[i];

        if (other != anim && other->m_playing) {
            return;
        }
    }

    this->m_stopping = true;

    for (uint32_t i = 0; i < this->m_animations.Count(); i++) {
        if (this->m_animations[i] != anim) {
            this->m_animations[i]->Stop();
        }
    }

    if (this->m_onStop.luaRef) {
        auto L = FrameScript_GetContext();
        lua_pushboolean(L, 0);
        this->RunScript(this->m_onStop, 1, nullptr);
    }

    if (this->m_region) {
        this->m_region->NotifyAnimEnd(this);
    }

    this->m_playing = false;
    this->m_paused = false;
    this->m_elapsed = 0.0f;
    this->m_progress = 0.0f;
    this->m_loopState = ANIM_LOOPSTATE_NONE;
    this->m_currentOrder = -1;
    this->m_stopping = false;
}

// ref: FUN_00497920
// Whether the tick's loop must stop after this pass. Without it the loop does not terminate: a
// looping group whose animations have zero duration finishes every pass, consumes no time, and
// comes straight back round. The reference's first test is exactly that guard.
//
// The second half only matters for a group that is NOT looping, where the loop also ends once the
// group's own progress passes 1.
bool CSimpleAnimGroup::ShouldStopStepping() const {
    if (this->m_orderDuration <= 0.0001f) {
        return true;
    }

    if (this->m_looping == ANIM_LOOPTYPE_REPEAT || this->m_looping == ANIM_LOOPTYPE_BOUNCE) {
        return false;
    }

    if (this->m_loopState == ANIM_LOOPSTATE_FORWARD
        || this->m_loopState == ANIM_LOOPSTATE_REVERSE) {
        return false;
    }

    return this->m_progress > 1.0f;
}

// ref: FUN_0049c350
// One frame. The loop is the interesting part: an order that finishes part-way through the frame
// hands what is left to the next order, so several short orders can complete in one tick.
//
// The incoming time is clamped before it is used, so a single enormous frame -- a stall, a load --
// cannot skip an animation entirely.
void CSimpleAnimGroup::OnUpdate(float elapsedSec) {
    if (!this->m_playing || this->m_paused || this->m_currentOrder < 0) {
        return;
    }

    float step = elapsedSec;

    if (step < 0.0f) {
        step = 0.0f;
    } else if (step > ANIM_MAX_STEP) {
        step = ANIM_MAX_STEP;
    }

    TSGrowableArray<CSimpleAnim*> order;

    while (true) {
        this->m_elapsed += step;
        this->m_progress = this->m_orderDuration > 0.0f
            ? this->m_elapsed / this->m_orderDuration
            : 1.0f;

        if (this->m_progress > 1.0f) {
            this->m_progress = 1.0f;
        }

        this->CollectOrder(this->m_currentOrder, order);

        float consumed = 0.0f;
        bool allDone = true;

        for (uint32_t i = 0; i < order.Count(); i++) {
            float used = 0.0f;

            if (!order[i]->OnUpdate(step, used)) {
                allDone = false;
            }

            if (used > consumed) {
                consumed = used;
            }

            // A handler can stop the group from underneath us. The reference re-tests this after
            // every animation, not just once per pass.
            if (!this->m_playing) {
                return;
            }
        }

        step -= consumed;

        if (allDone) {
            this->AdvanceOrder(step);
        }

        if (step < 0.0001f || this->ShouldStopStepping() || !this->m_playing || this->m_paused
            || this->m_currentOrder < 0) {
            return;
        }
    }
}

// ref: FUN_0049a060
// NOT CreateAnimation, which an earlier cycle tagged this address as on the strength of the two
// inherited-node strings it shares with it. This is the group's XML loader: it recurses into the
// inherited node, then reads looping and the initial offsets.
void CSimpleAnimGroup::LoadXML(const XMLNode* node, CStatus* status) {
    this->PreLoadXML(node, status);

    const char* loopingAttr = node->GetAttributeByName("looping");

    if (loopingAttr && *loopingAttr) {
        ANIM_LOOPTYPE loopType;

        if (AnimLoopTypeFromName(loopingAttr, loopType)) {
            this->m_looping = loopType;
        } else {
            status->Add(STATUS_WARNING, "%s %s: Invalid looping value: %s",
                        this->GetObjectTypeName(),
                        this->GetName() ? this->GetName() : "<unnamed>", loopingAttr);
        }
    }

    const char* offsetXAttr = node->GetAttributeByName("initialOffsetX");

    if (offsetXAttr && *offsetXAttr) {
        this->m_initialOffsetX = AnimXmlOffset(offsetXAttr);
    }

    const char* offsetYAttr = node->GetAttributeByName("initialOffsetY");

    if (offsetYAttr && *offsetYAttr) {
        this->m_initialOffsetY = AnimXmlOffset(offsetYAttr);
    }

    for (auto child = node->GetChild(); child; child = child->GetSibling()) {
        if (!SStrCmpI(child->GetName(), "Scripts", 0x7FFFFFFF)) {
            AnimLoadXML_Scripts(this, child, status);

            continue;
        }

        // Every other child is an animation, named by its type. CreateAnimation already maps an
        // unrecognised name onto the base Animation, so a typo yields an inert animation rather
        // than a dropped one -- the reference behaves the same way.
        CSimpleAnim* anim = this->CreateAnimation(child->GetName(), nullptr);

        if (anim) {
            anim->LoadXML(child, status);
        }
    }
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

    // Seed the order machine. Without this the tick has no current order and does nothing, which
    // is the difference between a group that plays and one that merely reports playing.
    if (this->m_currentOrder < 0) {
        this->m_currentOrder = 0;
        this->m_orderDuration = this->OrderDuration(0);
        this->m_elapsed = 0.0f;
        this->m_progress = 0.0f;
    }

    this->m_playing = true;
    this->m_paused = false;

    if (this->m_region) {
        this->m_region->NotifyAnimBegin(this);
    }

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
    // Held across the loop below so each animation's Stop, which reports back through
    // OnAnimationFinished, does not start a second teardown inside this one.
    this->m_stopping = true;

    this->m_playing = false;
    this->m_paused = false;
    this->m_pendingFinish = false;
    this->m_elapsed = 0.0f;
    this->m_progress = 0.0f;
    this->m_loopState = ANIM_LOOPSTATE_NONE;

    this->m_currentOrder = -1;

    for (uint32_t i = 0; i < this->m_animations.Count(); i++) {
        this->m_animations[i]->Stop();
    }

    this->m_stopping = false;
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
