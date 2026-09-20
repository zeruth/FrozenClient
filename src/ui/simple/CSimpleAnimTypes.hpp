#ifndef UI_SIMPLE_C_SIMPLE_ANIM_TYPES_HPP
#define UI_SIMPLE_C_SIMPLE_ANIM_TYPES_HPP

#include "ui/simple/CSimpleAnim.hpp"
#include "ui/Types.hpp"
#include <storm/array/TSGrowableArray.hpp>
#include <cstdint>

// The six animation subclasses. They are one file rather than six pairs because each is two to six
// small accessors over two or three fields; the class names are the ones ScriptMethods.cpp has
// carried commented out since before any of this existed, and the reference's own are the same.
//
// CSimpleControlPoint is the odd one: it is not an animation and has no CreateAnimation branch,
// because control points belong to a Path and are made by Path:CreateControlPoint.

class CSimplePathAnim;
class CStatus;
class XMLNode;

// Only two curve types, from the reference's table at 00a440d4.
enum ANIM_CURVE {
    ANIM_CURVE_NONE     = 0,
    ANIM_CURVE_SMOOTH   = 1,
    NUM_ANIM_CURVES     = 2
};

const char* AnimCurveName(ANIM_CURVE curve);
bool AnimCurveFromName(const char* name, ANIM_CURVE& curve);

// ---------------------------------------------------------------------------- Translation

class CSimpleTranslationAnim : public CSimpleAnim {
    public:
        static int32_t s_metatable;
        static int32_t s_objectType;
        static const char* s_objectTypeName;

        static void CreateScriptMetaTable();
        static int32_t GetObjectType();
        static void RegisterScriptMethods(lua_State* L);

        float m_offsetX = 0.0f;
        float m_offsetY = 0.0f;

        virtual int32_t GetScriptMetaTable();
        virtual bool IsA(int32_t type);
        virtual bool IsA(const char* typeName);
        virtual const char* GetObjectTypeName();
        virtual void LoadXML(const XMLNode* node, CStatus* status);
        virtual void OnApply(float amount);

        CSimpleTranslationAnim(CSimpleAnimGroup* group) : CSimpleAnim(group) {}
};

// ---------------------------------------------------------------------------- Rotation

class CSimpleRotationAnim : public CSimpleAnim {
    public:
        static int32_t s_metatable;
        static int32_t s_objectType;
        static const char* s_objectTypeName;

        static void CreateScriptMetaTable();
        static int32_t GetObjectType();
        static void RegisterScriptMethods(lua_State* L);

        FRAMEPOINT m_originPoint = FRAMEPOINT_CENTER;
        float m_originX = 0.0f;
        float m_originY = 0.0f;

        // Stored in RADIANS. SetDegrees converts on the way in and GetDegrees on the way out, which
        // is why setting degrees and reading radians agrees in both clients.
        float m_radians = 0.0f;

        virtual int32_t GetScriptMetaTable();
        virtual bool IsA(int32_t type);
        virtual bool IsA(const char* typeName);
        virtual const char* GetObjectTypeName();
        virtual void LoadXML(const XMLNode* node, CStatus* status);
        virtual void OnApply(float amount);

        CSimpleRotationAnim(CSimpleAnimGroup* group) : CSimpleAnim(group) {}
};

// ---------------------------------------------------------------------------- Scale

class CSimpleScaleAnim : public CSimpleAnim {
    public:
        static int32_t s_metatable;
        static int32_t s_objectType;
        static const char* s_objectTypeName;

        static void CreateScriptMetaTable();
        static int32_t GetObjectType();
        static void RegisterScriptMethods(lua_State* L);

        FRAMEPOINT m_originPoint = FRAMEPOINT_CENTER;
        float m_originX = 0.0f;
        float m_originY = 0.0f;

        float m_scaleX = 1.0f;
        float m_scaleY = 1.0f;

        virtual int32_t GetScriptMetaTable();
        virtual bool IsA(int32_t type);
        virtual bool IsA(const char* typeName);
        virtual const char* GetObjectTypeName();
        virtual void LoadXML(const XMLNode* node, CStatus* status);
        virtual void OnApply(float amount);
        virtual void OnUnapply(float amount);

        CSimpleScaleAnim(CSimpleAnimGroup* group) : CSimpleAnim(group) {}

        void GetScale(float& x, float& y) const;
};

// ---------------------------------------------------------------------------- Alpha

class CSimpleAlphaAnim : public CSimpleAnim {
    public:
        static int32_t s_metatable;
        static int32_t s_objectType;
        static const char* s_objectTypeName;

        static void CreateScriptMetaTable();
        static int32_t GetObjectType();
        static void RegisterScriptMethods(lua_State* L);

        // The DELTA applied over the animation, not a target alpha: -255 fades fully out, +255
        // fully in.
        //
        // Stored as the reference stores it -- a signed 16-bit value already scaled by 255 -- and
        // NOT as the [-1, 1] float the API speaks in. The conversion sits at three edges:
        // SetChange and the XML loader multiply by 255 on the way in, GetChange divides on the way
        // out, and the apply passes the scaled integer straight through. Keeping a float here
        // would round-trip differently, because the reference truncates on the way in.
        //
        // The two inbound paths differ on a value outside [-1, 1]: SetChange CLAMPS it, while the
        // XML loader reports "Value must be between -1 and 1, inclusive" and then stores it anyway.
        int16_t m_change = 0;

        virtual int32_t GetScriptMetaTable();
        virtual bool IsA(int32_t type);
        virtual bool IsA(const char* typeName);
        virtual const char* GetObjectTypeName();
        virtual void LoadXML(const XMLNode* node, CStatus* status);
        virtual void OnApply(float amount);

        CSimpleAlphaAnim(CSimpleAnimGroup* group) : CSimpleAnim(group) {}

        // ref: FUN_004982e0
        // Clamps to [-1, 1] and scales by 255. The Lua setter is a thin wrapper over this; the XML
        // loader deliberately is not, because it reports an out-of-range value instead of clamping.
        void SetChange(float change);
};

// ---------------------------------------------------------------------------- ControlPoint

class CSimpleControlPoint : public CScriptObject {
    public:
        static int32_t s_metatable;
        static int32_t s_objectType;
        static const char* s_objectTypeName;

        static void CreateScriptMetaTable();
        static int32_t GetObjectType();
        static void RegisterScriptMethods(lua_State* L);

        CSimplePathAnim* m_path = nullptr;

        float m_offsetX = 0.0f;
        float m_offsetY = 0.0f;

        // Zero-based like an animation's, behind a one-based Lua API.
        int8_t m_order = -1;

        virtual ~CSimpleControlPoint();
        virtual int32_t GetScriptMetaTable();
        virtual bool IsA(int32_t type);
        virtual bool IsA(const char* typeName);
        virtual const char* GetObjectTypeName();
        virtual CScriptObject* GetScriptObjectParent();
        virtual void LoadXML(const XMLNode* node, CStatus* status);

        CSimpleControlPoint(CSimplePathAnim* path);

        void SetParentPath(CSimplePathAnim* path);
};

// ---------------------------------------------------------------------------- Path

class CSimplePathAnim : public CSimpleAnim {
    public:
        static int32_t s_metatable;
        static int32_t s_objectType;
        static const char* s_objectTypeName;

        static void CreateScriptMetaTable();
        static int32_t GetObjectType();
        static void RegisterScriptMethods(lua_State* L);

        ANIM_CURVE m_curve = ANIM_CURVE_NONE;

        // Owned, the way a group owns its animations.
        TSGrowableArray<CSimpleControlPoint*> m_controlPoints;

        virtual ~CSimplePathAnim();
        virtual int32_t GetScriptMetaTable();
        virtual bool IsA(int32_t type);
        virtual bool IsA(const char* typeName);
        virtual const char* GetObjectTypeName();
        virtual void LoadXML(const XMLNode* node, CStatus* status);

        CSimplePathAnim(CSimpleAnimGroup* group) : CSimpleAnim(group) {}

        int32_t GetMaxOrder() const;
        CSimpleControlPoint* CreateControlPoint(const char* name);
        void AddControlPoint(CSimpleControlPoint* point);
        void RemoveControlPoint(CSimpleControlPoint* point);
};

#endif
