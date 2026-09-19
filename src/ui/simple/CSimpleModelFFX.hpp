#ifndef UI_SIMPLE_C_SIMPLE_MODEL_FFX_HPP
#define UI_SIMPLE_C_SIMPLE_MODEL_FFX_HPP

#include "model/CM2Light.hpp"
#include "ui/simple/CSimpleModel.hpp"

class CSimpleFrame;

class CSimpleModelFFX : public CSimpleModel {
    public:
        // Types

        // One set of scripted lights. The reference keeps three groups of two sets each laid out
        // back to back from +0x368 to +0xDDC, one group per Add* binding (AddLight,
        // AddCharacterLight, AddPetLight), each set being { uint32_t count; Light lights[4];
        // bool dirty; } on a 0x1C0 stride. The count saturates at four: a fifth AddLight on the
        // same set is dropped. The flag is raised whenever a light is appended and cleared by
        // ResetLights.
        //
        // The element type is a guess: the reference light struct is 108 bytes and is built and
        // torn down by FUN_00834a40 / FUN_00834ab0, neither of which has been identified, so the
        // only thing proven about it is its size. CM2Light is used here because it is the type
        // the rest of Frozen drives model lighting with; revisit once the shared light-argument
        // parser FUN_00960a10 has been decompiled and the three Add* bindings can be ported.
        struct LightSet {
            uint32_t m_count = 0;
            CM2Light m_lights[4];
            bool m_dirty = false;
        };

        // Static variables
        static int32_t s_metatable;
        static int32_t s_objectType;

        // Static functions
        static CSimpleFrame* Create(CSimpleFrame* parent);
        static void CreateScriptMetaTable();
        static int32_t GetObjectType();
        static void RegisterScriptMethods(lua_State* L);
        static void Render(void* arg);

        // Member variables
        LightSet m_lights[2];
        LightSet m_characterLights[2];
        LightSet m_petLights[2];

        // Virtual member functions
        virtual int32_t GetScriptMetaTable();
        virtual bool IsA(int32_t type);
        virtual void OnFrameRender(CRenderBatch* batch, uint32_t layer);

        // Member functions
        CSimpleModelFFX(CSimpleFrame* parent);
};

#endif
