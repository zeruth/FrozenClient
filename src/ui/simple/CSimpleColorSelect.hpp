#ifndef UI_SIMPLE_C_SIMPLE_COLOR_SELECT_HPP
#define UI_SIMPLE_C_SIMPLE_COLOR_SELECT_HPP

#include "ui/simple/CSimpleFrame.hpp"

class CSimpleTexture;

// The colour picker's wheel. FrameXML's ColorPickerFrame is built on one of these, and with the
// factory returning nullptr the whole frame failed to create -- so the colour picker did not exist
// and anything opening it threw on a nil global.
//
// The frame owns four textures supplied by XML (the hue/saturation wheel and its thumb, the value
// strip and its thumb) and carries the selected colour as HSV, which is the form the wheel works in.
class CSimpleColorSelect : public CSimpleFrame {
    public:
        // Static variables
        static int32_t s_metatable;
        static int32_t s_objectType;

        // Static functions
        static void CreateScriptMetaTable();
        static int32_t GetObjectType();
        static void RegisterScriptMethods(lua_State* L);

        // Member variables
        CSimpleTexture* m_wheelTexture = nullptr;
        CSimpleTexture* m_wheelThumbTexture = nullptr;
        CSimpleTexture* m_valueTexture = nullptr;
        CSimpleTexture* m_valueThumbTexture = nullptr;
        float m_hue = 0.0f;         // degrees, [0, 360)
        float m_saturation = 0.0f;  // [0, 1]
        float m_value = 1.0f;       // [0, 1]
        ScriptIx m_onColorSelect;

        // Virtual member functions
        virtual bool IsA(int32_t type);
        virtual int32_t GetScriptMetaTable();
        virtual void LoadXML(const XMLNode* node, CStatus* status);
        virtual ScriptIx* GetScriptByName(const char* name, ScriptData& data);

        // Member functions
        CSimpleColorSelect(CSimpleFrame* parent);
        void GetColorRGB(float& r, float& g, float& b);
        void SetColorRGB(float r, float g, float b);
        void SetColorHSV(float hue, float saturation, float value);
        void RunOnColorSelectScript();
};

#endif
