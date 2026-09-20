#include "ui/simple/CSimpleAnim.hpp"
#include "ui/simple/CSimpleAnimGroup.hpp"
#include "ui/simple/CSimpleAnimScript.hpp"
#include "ui/FrameScript.hpp"
#include "ui/LoadXML.hpp"
#include "gx/Coordinate.hpp"
#include "util/CStatus.hpp"
#include "util/Lua.hpp"
#include <common/XML.hpp>
#include <storm/String.hpp>
#include <cstdint>

int32_t CSimpleAnim::s_metatable;
int32_t CSimpleAnim::s_objectType;
const char* CSimpleAnim::s_objectTypeName = "Animation";

// The reference's table at 00a44068: five rows of {easeIn, easeOut, name}. The pair is what the
// object actually stores; the name is recovered by scanning for the row whose pair matches within
// an epsilon. Frozen keeps the enum instead and converts at the edge, which cannot reproduce the
// scan's one visible quirk on its own -- see the header, and AnimSmoothingName below.
static const char* s_smoothingNames[NUM_ANIM_SMOOTHINGS] = {
    "NONE", "IN", "OUT", "IN_OUT", "OUT_IN"
};

const char* AnimSmoothingName(ANIM_SMOOTHING smoothing) {
    if (smoothing < 0 || smoothing >= NUM_ANIM_SMOOTHINGS) {
        return "UNKNOWN";
    }

    // OUT_IN reports as IN_OUT. The reference stores smoothing as the {1, 1} weight pair both
    // names share and finds IN_OUT first, so the name never comes back out. Kept because Lua can
    // see it: SetSmoothing("OUT_IN") followed by GetSmoothing() answers "IN_OUT" in both clients.
    if (smoothing == ANIM_SMOOTHING_OUT_IN) {
        return s_smoothingNames[ANIM_SMOOTHING_IN_OUT];
    }

    return s_smoothingNames[smoothing];
}

bool AnimSmoothingFromName(const char* name, ANIM_SMOOTHING& smoothing) {
    for (int32_t i = 0; i < NUM_ANIM_SMOOTHINGS; i++) {
        if (!SStrCmpI(name, s_smoothingNames[i], 0x7FFFFFFF)) {
            smoothing = static_cast<ANIM_SMOOTHING>(i);

            return true;
        }
    }

    return false;
}

void CSimpleAnim::CreateScriptMetaTable() {
    lua_State* L = FrameScript_GetContext();
    CSimpleAnim::s_metatable = FrameScript_Object::CreateScriptMetaTable(
        L, &CSimpleAnim::RegisterScriptMethods
    );
}

int32_t CSimpleAnim::GetObjectType() {
    if (!CSimpleAnim::s_objectType) {
        CSimpleAnim::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CSimpleAnim::s_objectType;
}

void CSimpleAnim::RegisterScriptMethods(lua_State* L) {
    FrameScript_Object::FillScriptMethodTable(L, SimpleAnimMethods, NUM_SIMPLE_ANIM_SCRIPT_METHODS);
}

CSimpleAnim::CSimpleAnim(CSimpleAnimGroup* group) : CScriptObject() {
    this->m_group = group;
}

CSimpleAnim::~CSimpleAnim() {
    if (this->m_group) {
        this->m_group->RemoveAnimation(this);
        this->m_group = nullptr;
    }
}

// Modelled on the group's FUN_00497800, whose wrappers these match. The ANIMATION's own
// GetScriptByName has not been located in the reference yet, so whether it also carries OnLoad is
// unconfirmed; the five here are the ones an animation is documented to take.
FrameScript_Object::ScriptIx* CSimpleAnim::GetScriptByName(const char* name, ScriptData& data) {
    auto parentScript = CScriptObject::GetScriptByName(name, data);

    if (parentScript) {
        return parentScript;
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

    return nullptr;
}

int32_t CSimpleAnim::GetScriptMetaTable() {
    return CSimpleAnim::s_metatable;
}

bool CSimpleAnim::IsA(int32_t type) {
    return type == CSimpleAnim::GetObjectType() || this->CScriptObject::IsA(type);
}

bool CSimpleAnim::IsA(const char* typeName) {
    return !SStrCmpI(typeName, CSimpleAnim::s_objectTypeName, 0x7FFFFFFF)
        || this->CScriptObject::IsA(typeName);
}

const char* CSimpleAnim::GetObjectTypeName() {
    return CSimpleAnim::s_objectTypeName;
}

CScriptObject* CSimpleAnim::GetScriptObjectParent() {
    return this->m_group;
}

// ref: FUN_0049ad80
// An animation does not play by itself: it plays its GROUP, and only marks itself playing if the
// group agreed to start. That is why an animation in an empty or parentless group stays stopped.
bool CSimpleAnim::Play() {
    if (!this->m_group || !this->m_group->Play()) {
        return false;
    }

    if (!this->m_playing) {
        this->m_playing = true;
        this->m_paused = false;
    }

    return true;
}

void CSimpleAnim::Pause() {
    if (this->m_playing) {
        this->m_paused = true;
    }
}

void CSimpleAnim::Stop() {
    this->m_playing = false;
    this->m_paused = false;
    this->m_elapsed = 0.0f;
    this->m_progress = 0.0f;
    this->m_progressWithDelay = 0.0f;
}

// Jump to the end. With no driver the visible effect is only the state change; stage 4 is what
// makes finishing mean anything on screen.
void CSimpleAnim::Finish() {
    this->m_playing = false;
    this->m_paused = false;
    this->m_progress = 1.0f;
    this->m_progressWithDelay = 1.0f;
    this->m_elapsed = this->m_duration;
}

// ref: FUN_004a5000
// The reference tests the delay-inclusive fraction against 1.0, not the plain progress, so an
// animation still inside its end delay is not done.
bool CSimpleAnim::IsDone() const {
    return this->m_progressWithDelay >= 1.0f;
}

bool CSimpleAnim::IsDelaying() const {
    return this->m_playing && this->m_elapsed < this->m_startDelay;
}

bool CSimpleAnim::IsStopped() const {
    return !this->m_playing && !this->m_paused;
}

// ref: FUN_00497fe0
// The region the animation ultimately drives, reached through the group rather than held directly.
// The recomp matcher first paired this reference function with the AnimThis helper in
// CSimpleAnimScript.cpp on callgraph evidence alone, which was wrong -- the reference inlines its
// object resolution and has no such helper. Writing the real counterpart displaces that match.
CScriptRegion* CSimpleAnim::GetRegionParent() {
    return this->m_group ? this->m_group->m_region : nullptr;
}

// An offsetX / offsetY / initialOffset attribute. The reference converts these the same way
// <AbsDimension> is converted -- divide by 1024 scaled by the aspect compensation, then NDC to
// DDC -- rather than taking them as raw pixels. Translation, ControlPoint and AnimationGroup all
// go through it.
float AnimXmlOffset(const char* attr) {
    float value = SStrToFloat(attr);

    return NDCToDDCWidth(value / (CoordinateGetAspectCompensation() * 1024.0f));
}

// ref: FUN_0049b810
// Every attribute the base animation understands. Two of them do not behave the way their names
// suggest and are worth stating:
//
//  - startDelay and endDelay are CLAMPED at zero. A negative value is silently raised to 0, not
//    reported.
//  - order is 1-based in XML and stored 0-based. Out of range is reported AND THEN CLAMPED, so a
//    bad order still yields a usable animation rather than dropping it. The bounds are 1 to 100.
void CSimpleAnim::LoadXML(const XMLNode* node, CStatus* status) {
    this->PreLoadXML(node, status);

    const char* startDelayAttr = node->GetAttributeByName("startDelay");

    if (startDelayAttr && *startDelayAttr) {
        float value = SStrToFloat(startDelayAttr);
        this->m_startDelay = value < 0.0f ? 0.0f : value;
    }

    const char* endDelayAttr = node->GetAttributeByName("endDelay");

    if (endDelayAttr && *endDelayAttr) {
        float value = SStrToFloat(endDelayAttr);
        this->m_endDelay = value < 0.0f ? 0.0f : value;
    }

    const char* durationAttr = node->GetAttributeByName("duration");

    if (durationAttr && *durationAttr) {
        this->m_duration = SStrToFloat(durationAttr);
    }

    const char* maxFramerateAttr = node->GetAttributeByName("maxFramerate");

    if (maxFramerateAttr && *maxFramerateAttr) {
        float value = SStrToFloat(maxFramerateAttr);

        // Against 1e-4, not against zero: the reference's threshold is _DAT_009e8cd0. A rate
        // below it counts as uncapped rather than producing an enormous interval.
        if (value > 0.0001f) {
            this->m_maxFramerate = value;
            this->m_maxFramerateInterval = 1.0f / value;
        } else {
            this->m_maxFramerate = 0.0f;
            this->m_maxFramerateInterval = 0.0f;
        }
    }

    const char* smoothingAttr = node->GetAttributeByName("smoothing");

    if (smoothingAttr && *smoothingAttr) {
        ANIM_SMOOTHING smoothing;

        if (AnimSmoothingFromName(smoothingAttr, smoothing)) {
            this->m_smoothing = smoothing;
        } else {
            status->Add(STATUS_WARNING, "%s: Invalid smoothing value: %s",
                        this->GetName() ? this->GetName() : "<unnamed>", smoothingAttr);
        }
    }

    const char* orderAttr = node->GetAttributeByName("order");

    if (orderAttr && *orderAttr) {
        int32_t order = SStrToInt(orderAttr) - 1;

        if (order < 0 || order > 99) {
            status->Add(STATUS_WARNING,
                        "%s: Invalid order value: %s. Order must be between %d and %d.",
                        this->GetName() ? this->GetName() : "<unnamed>", orderAttr, 1, 100);
        }

        this->m_order = static_cast<int8_t>(order < 0 ? 0 : (order > 99 ? 99 : order));
    }

    for (auto child = node->GetChild(); child; child = child->GetSibling()) {
        if (!SStrCmpI(child->GetName(), "Scripts", 0x7FFFFFFF)) {
            AnimLoadXML_Scripts(this, child, status);
        }
    }
}

void CSimpleAnim::SetParentGroup(CSimpleAnimGroup* group) {
    if (this->m_group == group) {
        return;
    }

    if (this->m_group) {
        this->m_group->RemoveAnimation(this);
    }

    this->m_group = group;

    if (group) {
        group->AddAnimation(this);
    }
}

// ref: FUN_00497c30
void AnimLoadXML_Scripts(CScriptObject* object, const XMLNode* root, CStatus* status) {
    lua_State* L = FrameScript_GetContext();

    for (auto node = root->m_child; node; node = node->m_next) {
        const char* scriptName = node->GetName();

        FrameScript_Object::ScriptData scriptData;
        scriptData.wrapper = "return function(self) %s end";

        auto script = object->GetScriptByName(scriptName, scriptData);

        if (!script) {
            continue;
        }

        if (script->luaRef) {
            luaL_unref(L, LUA_REGISTRYINDEX, script->luaRef);
            script->luaRef = 0;
        }

        const char* functionLookup = node->GetAttributeByName("function");

        if (functionLookup && *functionLookup) {
            lua_pushstring(L, functionLookup);
            lua_rawget(L, LUA_GLOBALSINDEX);

            int32_t luaRef = luaL_ref(L, LUA_REGISTRYINDEX);

            if (luaRef == -1) {
                status->Add(STATUS_WARNING, "%s %s: Unknown function %s in element %s",
                            object->GetObjectTypeName(),
                            object->GetName() ? object->GetName() : "<unnamed>",
                            functionLookup, scriptName);
            } else {
                script->luaRef = luaRef;
            }

            continue;
        }

        const char* scriptBody = node->m_body;

        if (scriptBody && *scriptBody) {
            char compileName[1024];
            SStrPrintf(compileName, sizeof(compileName), "*:%s", scriptName);

            script->luaRef = FrameScript_CompileFunction(
                compileName, scriptData.wrapper, scriptBody, status
            );
        }
    }
}
