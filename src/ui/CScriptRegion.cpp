#include "ui/CScriptRegion.hpp"
#include "ui/simple/CSimpleAnimGroup.hpp"
#include "ui/CScriptObject.hpp"
#include "ui/CScriptRegionScript.hpp"
#include "ui/simple/CSimpleTop.hpp"
#include "util/Lua.hpp"
#include "util/CStatus.hpp"
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <common/XML.hpp>

int32_t CScriptRegion::s_objectType;
const char* CScriptRegion::s_objectTypeName = "Region";

int32_t CScriptRegion::GetObjectType() {
    if (!CScriptRegion::s_objectType) {
        CScriptRegion::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CScriptRegion::s_objectType;
}

void CScriptRegion::RegisterScriptMethods(lua_State* L) {
    CScriptObject::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, ScriptRegionMethods, NUM_SCRIPT_REGION_SCRIPT_METHODS);
}

// TODO verify return type
CLayoutFrame* CScriptRegion::GetLayoutParent() {
    if (!this->m_parent || this->m_parent->m_layoutScale == 0.0f) {
        return CSimpleTop::s_instance;
    } else {
        return (CLayoutFrame*)this->m_parent;
    }
}

CLayoutFrame* CScriptRegion::GetLayoutFrameByName(const char* name) {
    char fullName[1024];
    this->CreateName(name, fullName, 1024);

    int32_t type = CScriptRegion::GetObjectType();
    CScriptRegion* object = static_cast<CScriptRegion*>(this->GetScriptObjectByName(fullName, type));

    return static_cast<CLayoutFrame*>(object);
}

const char* CScriptRegion::GetObjectTypeName() {
    return CScriptRegion::s_objectTypeName;
}

// TODO verify return type
CScriptObject* CScriptRegion::GetScriptObjectParent() {
    return (CScriptObject*)(this->m_parent);
}

bool CScriptRegion::IsA(int32_t type) {
    return type == CScriptRegion::s_objectType
        || type == CScriptObject::s_objectType;
}

bool CScriptRegion::IsA(const char* typeName) {
    return !SStrCmpI(typeName, CScriptRegion::s_objectTypeName, 0x7FFFFFFF)
        || !SStrCmpI(typeName, CScriptObject::s_objectTypeName, 0x7FFFFFFF);
}

bool CScriptRegion::IsDragging() {
    // TODO
    return false;
}

void CScriptRegion::LoadXML(const XMLNode* node, CStatus* status) {
    CLayoutFrame::LoadXML(node, status);

    const char* parentKey = node->GetAttributeByName("parentKey");

    if (parentKey && *parentKey) {
        lua_State* L = FrameScript_GetContext();

        CScriptObject* parent = this->GetScriptObjectParent();

        if (parent) {
            if (!parent->lua_registered) {
                parent->RegisterScriptObject(0);
            }

            lua_rawgeti(L, LUA_REGISTRYINDEX, parent->lua_objectRef);
            lua_pushstring(L, parentKey);

            if (!this->lua_registered) {
                this->RegisterScriptObject(0);
            }

            lua_rawgeti(L, LUA_REGISTRYINDEX, this->lua_objectRef);
            lua_settable(L, -3);
            lua_settop(L, -2);
        }
    }

    this->LoadXML_Animations(node, status);
}

// ref: FUN_004883f0
// The <Animations> block on a region: a flat list of <AnimationGroup> elements, each of which
// owns its animations. Anything else in there is reported by name and skipped.
void CScriptRegion::LoadXML_Animations(const XMLNode* node, CStatus* status) {
    auto animations = node->GetChildByName("Animations");

    if (!animations) {
        return;
    }

    for (auto child = animations->GetChild(); child; child = child->GetSibling()) {
        if (SStrCmpI(child->GetName(), "AnimationGroup", 0x7FFFFFFF)) {
            status->Add(STATUS_WARNING, "%s %s: Unknown child node in %s element: %s",
                        this->GetObjectTypeName(),
                        this->GetName() ? this->GetName() : "<unnamed>",
                        animations->GetName(), child->GetName());

            continue;
        }

        void* m = SMemAlloc(sizeof(CSimpleAnimGroup), __FILE__, __LINE__, 0x0);
        auto group = new (m) CSimpleAnimGroup(this);

        this->m_animGroups.Add(1, &group);

        group->LoadXML(child, status);
    }
}

// Frozen ticks every region unconditionally, so there is nothing to switch on here yet. The
// reference uses this pair to add and remove the region from an active list; that becomes worth
// porting when the per-frame cost matters, and is noted rather than faked.
void CScriptRegion::NotifyAnimBegin(CSimpleAnimGroup* animGroup) {
    // TODO stage 4b: join the reference's active-region list.
}

void CScriptRegion::NotifyAnimEnd(CSimpleAnimGroup* animGroup) {
    // TODO
}

// Stage 4a of docs/ref/parity-animations.md. The call site already existed --
// CSimpleFrame::OnLayerUpdate has always run this every frame on the frame and each of its
// regions -- it was simply empty, so nothing an animation did ever advanced.
//
// Iterating a copy of the index rather than caching Count(): a handler fired from inside a tick
// can call CreateAnimationGroup or StopAnimating on this very region, and the array can move
// underneath the loop.
void CScriptRegion::OnLayerUpdate(float elapsedSec) {
    for (uint32_t i = 0; i < this->m_animGroups.Count(); i++) {
        this->m_animGroups[i]->OnUpdate(elapsedSec);
    }
}

bool CScriptRegion::ProtectedFunctionsAllowed() {
    // TODO

    return true;
}

// ref: FUN_004883a0
void CScriptRegion::SetParent(CSimpleFrame* parent) {
    this->m_parent = parent;
}

void CScriptRegion::StopAnimating() {
    // TODO
}
