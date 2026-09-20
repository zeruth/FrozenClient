#include "ui/simple/CSimpleAnimTypes.hpp"
#include "ui/simple/CSimpleAnimGroup.hpp"
#include "ui/simple/CSimpleAnimTypesScript.hpp"
#include "ui/CScriptRegion.hpp"
#include "ui/FrameScript.hpp"
#include "ui/LoadXML.hpp"
#include "util/CStatus.hpp"
#include <common/XML.hpp>
#include <tempest/Vector.hpp>
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

// ---------------------------------------------------------------------------- Scale

// ref: FUN_004980d0
// DIVERGENCE in representation. The reference stores 1 - scale and this function is how the scale
// comes back out, which is why its body reads {1 - x, 1 - y} and frozen's is a plain copy. The
// storage choice shows through nowhere else: the reference's constructor zeroes the pair, which is
// a scale of 1, and frozen defaults the same pair to 1 directly.
//
// This exists as its own method rather than inline in the Lua thunk so the reference function has
// a counterpart to point at. It previously had none, and the matcher filled the gap with
// AnimThisOf -- a template helper -- on callgraph evidence alone.
void CSimpleScaleAnim::GetScale(float& x, float& y) const {
    x = this->m_scaleX;
    y = this->m_scaleY;
}

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

// ---------------------------------------------------------------------------- Alpha

// ref: FUN_004982e0
// Decoded from the instruction bytes: the value is compared against -1.0 and 1.0 and clamped to
// them, then multiplied by 255 and truncated into the signed 16-bit field.
//
// This existing as its own method rather than sitting inline in the Lua thunk is not tidiness. The
// recomp order matcher had claimed this address for CSimpleAnim::GetScriptObjectParent, purely
// because it sits between two anchored functions and frozen happened to define that method at the
// matching position. Writing the real counterpart displaces the guess.
void CSimpleAlphaAnim::SetChange(float change) {
    if (change < -1.0f) {
        change = -1.0f;
    } else if (change > 1.0f) {
        change = 1.0f;
    }

    this->m_change = static_cast<int16_t>(change * 255.0f);
}

// ---------------------------------------------------------------------------- Apply

// Every apply reaches the region the same way: through the group, never directly. An animation
// whose group has no region contributes nothing rather than crashing, which is the reference's
// own null check and not defensive padding.
static CScriptRegion* AnimTargetRegion(CSimpleAnim* anim) {
    return anim->m_group ? anim->m_group->m_region : nullptr;
}

// ref: FUN_00498040
void CSimpleTranslationAnim::OnApply(float amount) {
    auto region = AnimTargetRegion(this);

    if (!region) {
        return;
    }

    C2Vector offset;
    offset.x = this->m_offsetX * amount;
    offset.y = this->m_offsetY * amount;

    region->AddAnimTranslation(region, offset);
}

// ref: FUN_00498090
void CSimpleRotationAnim::OnApply(float amount) {
    auto region = AnimTargetRegion(this);

    if (!region) {
        return;
    }

    C2Vector origin;
    origin.x = this->m_originX;
    origin.y = this->m_originY;

    // Only the angle scales with the amount. The origin is a fixed anchor, so it is passed
    // through unscaled -- scaling it would drag the pivot across the screen as the animation ran.
    region->AddAnimRotation(region, this->m_originPoint, origin, this->m_radians * amount);
}

// ref: FUN_004980f0
// DIVERGENCE follows from the storage choice recorded against FUN_004980d0: the reference holds
// 1 - scale and passes (1 - scale) * amount, so what the region accumulates is the COMPLEMENT
// scaled by progress, not the scale. Frozen holds the scale, so it forms the complement here to
// hand the region the same number the reference does.
void CSimpleScaleAnim::OnApply(float amount) {
    auto region = AnimTargetRegion(this);

    if (!region) {
        return;
    }

    C2Vector origin;
    origin.x = this->m_originX;
    origin.y = this->m_originY;

    C2Vector scale;
    scale.x = (1.0f - this->m_scaleX) * amount;
    scale.y = (1.0f - this->m_scaleY) * amount;

    region->AddAnimScale(region, this->m_originPoint, origin, scale);
}

// ref: FUN_00498150
// Scale is the one subclass that cannot be undone by negating, because scale composes by
// multiplying rather than adding. The reference sends the reciprocal instead, guarding each axis
// against a division by something indistinguishable from zero and passing 1 in that case.
void CSimpleScaleAnim::OnUnapply(float amount) {
    auto region = AnimTargetRegion(this);

    if (!region) {
        return;
    }

    C2Vector origin;
    origin.x = this->m_originX;
    origin.y = this->m_originY;

    float x = (1.0f - this->m_scaleX) * amount;
    float y = (1.0f - this->m_scaleY) * amount;

    float magnitudeX = x < 0.0f ? -x : x;
    float magnitudeY = y < 0.0f ? -y : y;

    C2Vector inverse;
    inverse.x = 1.0f - (magnitudeX >= 2.384185791015625e-07f ? 1.0f / x : 1.0f);
    inverse.y = 1.0f - (magnitudeY >= 2.384185791015625e-07f ? 1.0f / y : 1.0f);

    region->AddAnimScale(region, this->m_originPoint, origin, inverse);
}

// ref: FUN_00498330
// No 255 here: m_change is already scaled, so the apply is the stored integer times the amount,
// truncated. Decoded from the instruction bytes -- Ghidra lost the whole float expression feeding
// the truncation and showed only a bare call, which would have made this look like it passed the
// change through untouched.
void CSimpleAlphaAnim::OnApply(float amount) {
    auto region = AnimTargetRegion(this);

    if (!region) {
        return;
    }

    region->AddAnimAlpha(region, static_cast<int16_t>(this->m_change * amount));
}

// ---------------------------------------------------------------------------- XML

// The four that take an <Origin> child share this. A missing or malformed point leaves the
// defaults in place and reports; the element is never a hard failure.
static void LoadOriginChild(CScriptObject* object, const XMLNode* child, FRAMEPOINT& point,
                            float& originX, float& originY, CStatus* status) {
    if (!LoadXML_AnimOrigin(child, point, originX, originY, status)) {
        status->Add(STATUS_WARNING, "%s %s: Error loading Origin element",
                    object->GetObjectTypeName(),
                    object->GetName() ? object->GetName() : "<unnamed>");
    }
}

// ref: FUN_0049bb00
void CSimpleTranslationAnim::LoadXML(const XMLNode* node, CStatus* status) {
    this->CSimpleAnim::LoadXML(node, status);

    const char* offsetXAttr = node->GetAttributeByName("offsetX");

    if (offsetXAttr && *offsetXAttr) {
        this->m_offsetX = AnimXmlOffset(offsetXAttr);
    }

    const char* offsetYAttr = node->GetAttributeByName("offsetY");

    if (offsetYAttr && *offsetYAttr) {
        this->m_offsetY = AnimXmlOffset(offsetYAttr);
    }
}

// ref: FUN_0049bc10
// degrees and radians both write the same field, so whichever appears LAST in the element wins.
void CSimpleRotationAnim::LoadXML(const XMLNode* node, CStatus* status) {
    this->CSimpleAnim::LoadXML(node, status);

    const char* degreesAttr = node->GetAttributeByName("degrees");

    if (degreesAttr && *degreesAttr) {
        this->m_radians = SStrToFloat(degreesAttr) * 0.017453292519943295f;
    }

    const char* radiansAttr = node->GetAttributeByName("radians");

    if (radiansAttr && *radiansAttr) {
        this->m_radians = SStrToFloat(radiansAttr);
    }

    for (auto child = node->GetChild(); child; child = child->GetSibling()) {
        if (!SStrCmpI(child->GetName(), "Origin", 0x7FFFFFFF)) {
            LoadOriginChild(this, child, this->m_originPoint, this->m_originX, this->m_originY,
                            status);
        }
    }
}

// ref: FUN_0049bd20
// scaleX and scaleY are bounded BELOW at 0.001, reported and then clamped rather than rejected.
//
// DIVERGENCE in storage, not in behaviour: the reference keeps 1 - scale in the object and
// recovers the scale on the way out (FUN_004980d0 returns {1 - x, 1 - y}). Frozen keeps the scale
// itself, so its GetScale is a plain read. The two agree at every observable point, including the
// zeroed constructor, since 1 - 1.0 is 0.
void CSimpleScaleAnim::LoadXML(const XMLNode* node, CStatus* status) {
    this->CSimpleAnim::LoadXML(node, status);

    const char* scaleXAttr = node->GetAttributeByName("scaleX");

    if (scaleXAttr && *scaleXAttr) {
        float value = SStrToFloat(scaleXAttr);

        if (value < 0.001f) {
            // The reference passes something here that renders as a nonsense integer for the %d;
            // the bound it is describing is 0.001, so that is what frozen prints.
            status->Add(STATUS_WARNING,
                        "%s: Invalid scaleX value: %s. Value must be at least %g.",
                        this->GetName() ? this->GetName() : "<unnamed>", scaleXAttr, 0.001);

            value = 0.001f;
        }

        this->m_scaleX = value;
    }

    const char* scaleYAttr = node->GetAttributeByName("scaleY");

    if (scaleYAttr && *scaleYAttr) {
        float value = SStrToFloat(scaleYAttr);

        if (value < 0.001f) {
            status->Add(STATUS_WARNING,
                        "%s: Invalid scaleY value: %s. Value must be at least %g.",
                        this->GetName() ? this->GetName() : "<unnamed>", scaleYAttr, 0.001);

            value = 0.001f;
        }

        this->m_scaleY = value;
    }

    for (auto child = node->GetChild(); child; child = child->GetSibling()) {
        if (!SStrCmpI(child->GetName(), "Origin", 0x7FFFFFFF)) {
            LoadOriginChild(this, child, this->m_originPoint, this->m_originX, this->m_originY,
                            status);
        }
    }
}

// ref: FUN_0049c170
void CSimpleAlphaAnim::LoadXML(const XMLNode* node, CStatus* status) {
    this->CSimpleAnim::LoadXML(node, status);

    const char* changeAttr = node->GetAttributeByName("change");

    if (changeAttr && *changeAttr) {
        float value = SStrToFloat(changeAttr);

        if (value < -1.0f || value > 1.0f) {
            status->Add(STATUS_WARNING,
                        "%s: Invalid change value: %s. Value must be between %d and %d, inclusive.",
                        this->GetName() ? this->GetName() : "<unnamed>", changeAttr, -1, 1);
        }

        // Reported but not clamped: the reference stores whatever it was given.
        this->m_change = static_cast<int16_t>(value * 255.0f);
    }
}

// ref: FUN_0049bf00
// Control points live in a <ControlPoints> wrapper, one level deeper than the animations in an
// <AnimationGroup>, and anything else in there is reported by name.
void CSimplePathAnim::LoadXML(const XMLNode* node, CStatus* status) {
    this->CSimpleAnim::LoadXML(node, status);

    const char* curveAttr = node->GetAttributeByName("curve");

    if (curveAttr && *curveAttr) {
        ANIM_CURVE curve;

        if (AnimCurveFromName(curveAttr, curve)) {
            this->m_curve = curve;
        } else {
            status->Add(STATUS_WARNING, "%s %s: Invalid curve value: %s",
                        this->GetObjectTypeName(),
                        this->GetName() ? this->GetName() : "<unnamed>", curveAttr);
        }
    }

    auto points = node->GetChildByName("ControlPoints");

    if (!points) {
        return;
    }

    for (auto child = points->GetChild(); child; child = child->GetSibling()) {
        if (!SStrCmpI(child->GetName(), "ControlPoint", 0x7FFFFFFF)) {
            CSimpleControlPoint* point = this->CreateControlPoint(nullptr);

            if (point) {
                point->LoadXML(child, status);
            }
        } else {
            status->Add(STATUS_WARNING, "%s %s: Unknown child node in %s element: %s",
                        this->GetObjectTypeName(),
                        this->GetName() ? this->GetName() : "<unnamed>",
                        points->GetName(), child->GetName());
        }
    }
}

// ref: FUN_004987e0
// A control point is not an animation, so this does NOT chain to CSimpleAnim::LoadXML -- it reads
// its own inherits and parentKey through PreLoadXML and then just the two offsets.
void CSimpleControlPoint::LoadXML(const XMLNode* node, CStatus* status) {
    this->PreLoadXML(node, status);

    const char* offsetXAttr = node->GetAttributeByName("offsetX");

    if (offsetXAttr && *offsetXAttr) {
        this->m_offsetX = AnimXmlOffset(offsetXAttr);
    }

    const char* offsetYAttr = node->GetAttributeByName("offsetY");

    if (offsetYAttr && *offsetYAttr) {
        this->m_offsetY = AnimXmlOffset(offsetYAttr);
    }
}
