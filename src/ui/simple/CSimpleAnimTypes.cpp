#include "ui/simple/CSimpleAnimTypes.hpp"
#include "ui/simple/CSimpleAnimGroup.hpp"
#include "ui/simple/CSimpleAnimTypesScript.hpp"
#include "ui/FrameScript.hpp"
#include <storm/String.hpp>
#include <storm/Memory.hpp>
#include <cstdint>

static const char* s_curveNames[NUM_ANIM_CURVES] = { "NONE", "SMOOTH" };

const char* AnimCurveName(ANIM_CURVE curve) {
    if (curve < 0 || curve >= NUM_ANIM_CURVES) {
        return "UNKNOWN";
    }

    return s_curveNames[curve];
}

bool AnimCurveFromName(const char* name, ANIM_CURVE& curve) {
    for (int32_t i = 0; i < NUM_ANIM_CURVES; i++) {
        if (!SStrCmpI(name, s_curveNames[i], 0x7FFFFFFF)) {
            curve = static_cast<ANIM_CURVE>(i);

            return true;
        }
    }

    return false;
}

// Each subclass repeats the same five-part shape: two statics, the metatable, the type id, the
// method-table registration, and an IsA pair that answers for itself and then defers upward.
#define WHOA_ANIM_SUBCLASS(cls, typeName, methods, count)                                   \
    int32_t cls::s_metatable;                                                               \
    int32_t cls::s_objectType;                                                              \
    const char* cls::s_objectTypeName = typeName;                                           \
                                                                                            \
    void cls::CreateScriptMetaTable() {                                                     \
        cls::s_metatable = FrameScript_Object::CreateScriptMetaTable(                       \
            FrameScript_GetContext(), &cls::RegisterScriptMethods                           \
        );                                                                                  \
    }                                                                                       \
                                                                                            \
    int32_t cls::GetObjectType() {                                                          \
        if (!cls::s_objectType) {                                                           \
            cls::s_objectType = ++FrameScript_Object::s_objectTypes;                        \
        }                                                                                   \
                                                                                            \
        return cls::s_objectType;                                                           \
    }                                                                                       \
                                                                                            \
    void cls::RegisterScriptMethods(lua_State* L) {                                         \
        FrameScript_Object::FillScriptMethodTable(L, methods, count);                       \
    }                                                                                       \
                                                                                            \
    int32_t cls::GetScriptMetaTable() {                                                     \
        return cls::s_metatable;                                                            \
    }                                                                                       \
                                                                                            \
    const char* cls::GetObjectTypeName() {                                                  \
        return cls::s_objectTypeName;                                                       \
    }

WHOA_ANIM_SUBCLASS(CSimpleTranslationAnim, "Translation", SimpleTranslationAnimMethods,
                   NUM_SIMPLE_TRANSLATION_ANIM_SCRIPT_METHODS)
WHOA_ANIM_SUBCLASS(CSimpleRotationAnim, "Rotation", SimpleRotationAnimMethods,
                   NUM_SIMPLE_ROTATION_ANIM_SCRIPT_METHODS)
WHOA_ANIM_SUBCLASS(CSimpleScaleAnim, "Scale", SimpleScaleAnimMethods,
                   NUM_SIMPLE_SCALE_ANIM_SCRIPT_METHODS)
WHOA_ANIM_SUBCLASS(CSimpleAlphaAnim, "Alpha", SimpleAlphaAnimMethods,
                   NUM_SIMPLE_ALPHA_ANIM_SCRIPT_METHODS)
WHOA_ANIM_SUBCLASS(CSimplePathAnim, "Path", SimplePathAnimMethods,
                   NUM_SIMPLE_PATH_ANIM_SCRIPT_METHODS)
WHOA_ANIM_SUBCLASS(CSimpleControlPoint, "ControlPoint", SimpleControlPointMethods,
                   NUM_SIMPLE_CONTROL_POINT_SCRIPT_METHODS)

#undef WHOA_ANIM_SUBCLASS

// The IsA pairs are written out rather than macro'd: each defers to a different base, and
// ControlPoint's base is CScriptObject where the others' is CSimpleAnim.
#define WHOA_ANIM_ISA(cls, base)                                                            \
    bool cls::IsA(int32_t type) {                                                           \
        return type == cls::GetObjectType() || this->base::IsA(type);                       \
    }                                                                                       \
                                                                                            \
    bool cls::IsA(const char* typeName) {                                                   \
        return !SStrCmpI(typeName, cls::s_objectTypeName, 0x7FFFFFFF)                        \
            || this->base::IsA(typeName);                                                   \
    }

WHOA_ANIM_ISA(CSimpleTranslationAnim, CSimpleAnim)
WHOA_ANIM_ISA(CSimpleRotationAnim, CSimpleAnim)
WHOA_ANIM_ISA(CSimpleScaleAnim, CSimpleAnim)
WHOA_ANIM_ISA(CSimpleAlphaAnim, CSimpleAnim)
WHOA_ANIM_ISA(CSimplePathAnim, CSimpleAnim)
WHOA_ANIM_ISA(CSimpleControlPoint, CScriptObject)

#undef WHOA_ANIM_ISA

// ---------------------------------------------------------------------------- ControlPoint

CSimpleControlPoint::CSimpleControlPoint(CSimplePathAnim* path) : CScriptObject() {
    this->m_path = path;
}

CSimpleControlPoint::~CSimpleControlPoint() {
    if (this->m_path) {
        this->m_path->RemoveControlPoint(this);
        this->m_path = nullptr;
    }
}

CScriptObject* CSimpleControlPoint::GetScriptObjectParent() {
    return this->m_path;
}

void CSimpleControlPoint::SetParentPath(CSimplePathAnim* path) {
    if (this->m_path == path) {
        return;
    }

    if (this->m_path) {
        this->m_path->RemoveControlPoint(this);
    }

    this->m_path = path;

    if (path) {
        path->AddControlPoint(this);
    }
}

// ---------------------------------------------------------------------------- Path

CSimplePathAnim::~CSimplePathAnim() {
    for (uint32_t i = 0; i < this->m_controlPoints.Count(); i++) {
        CSimpleControlPoint* point = this->m_controlPoints[i];

        if (point) {
            point->m_path = nullptr;
            point->~CSimpleControlPoint();
            SMemFree(point, __FILE__, __LINE__, 0x0);
        }
    }

    this->m_controlPoints.SetCount(0);
}

int32_t CSimplePathAnim::GetMaxOrder() const {
    int32_t max = 0;

    for (uint32_t i = 0; i < this->m_controlPoints.Count(); i++) {
        int32_t order = this->m_controlPoints[i]->m_order + 1;

        if (order > max) {
            max = order;
        }
    }

    return max;
}

CSimpleControlPoint* CSimplePathAnim::CreateControlPoint(const char* name) {
    void* m = SMemAlloc(sizeof(CSimpleControlPoint), __FILE__, __LINE__, 0x0);
    auto point = new (m) CSimpleControlPoint(this);

    if (name && *name) {
        point->SetName(name);
    }

    this->AddControlPoint(point);

    return point;
}

void CSimplePathAnim::AddControlPoint(CSimpleControlPoint* point) {
    if (!point) {
        return;
    }

    for (uint32_t i = 0; i < this->m_controlPoints.Count(); i++) {
        if (this->m_controlPoints[i] == point) {
            return;
        }
    }

    this->m_controlPoints.Add(1, &point);
}

void CSimplePathAnim::RemoveControlPoint(CSimpleControlPoint* point) {
    uint32_t count = this->m_controlPoints.Count();

    for (uint32_t i = 0; i < count; i++) {
        if (this->m_controlPoints[i] != point) {
            continue;
        }

        for (uint32_t j = i; j + 1 < count; j++) {
            this->m_controlPoints[j] = this->m_controlPoints[j + 1];
        }

        this->m_controlPoints.SetCount(count - 1);

        return;
    }
}
