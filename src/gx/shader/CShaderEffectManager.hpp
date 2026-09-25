#ifndef GX_SHADER_C_SHADER_EFFECT_MANAGER_HPP
#define GX_SHADER_C_SHADER_EFFECT_MANAGER_HPP

#include <cstdint>
#include <storm/Hash.hpp>

class CShaderEffect;

// The blends an effect file's ColorOp/AlphaOp lines name, in the order the reference's
// name table lists them (PTR_DAT_00b1d5c8 against DAT_00a4d678). A name it does not know
// reads as Mod.
enum EShaderEffectOp {
    ShaderEffectOp_Mod = 0,
    ShaderEffectOp_Mod2x = 1,
    ShaderEffectOp_Add = 2,
    ShaderEffectOp_PassThru = 3,
    ShaderEffectOp_Decal = 4,
    ShaderEffectOp_Fade = 5,
    ShaderEffectOps_Last = 6
};

// One Effect(...) block, as the parser fills it before handing it to the registry. The
// reference builds this on the stack of its file walk and it is exactly 228 bytes there,
// which is room for one FixedFunc pass and one Shader pass; a file carrying two of either
// would run past the end. The shipped .wfx files carry one of each.
struct SShaderEffectDef {
    // One FixedFunc Pass(n): n is a stage count, not an index, and the two ops per stage
    // sit in their own runs so the registry can hand each run to the effect whole.
    struct FixedFuncPass {
        int32_t stages;                         // +0x00
        uint32_t colorOps[2];                   // +0x04
        int32_t unused0;                        // +0x0c
        uint32_t alphaOps[2];                   // +0x10
        int32_t unused1;                        // +0x18
    };

    struct ShaderPass {
        char vertexShader[0x40];                // +0x00
        char pixelShader[0x40];                 // +0x40
    };

    char name[0x40];                            // +0x00
    uint32_t fixedFuncPassCount;                // +0x40
    FixedFuncPass fixedFuncPasses[1];           // +0x44
    uint32_t shaderPassCount;                   // +0x60
    ShaderPass shaderPasses[1];                 // +0x64
};

static_assert(sizeof(SShaderEffectDef) == 0xe4, "SShaderEffectDef must match the reference's 228 bytes");

class CShaderEffectManager {
    public:
        // Static variables
        static TSHashTable<CShaderEffect, HASHKEY_STRI> s_shaderList;

        // Static functions
        static CShaderEffect* CreateEffect(const char* effectKey);
        static CShaderEffect* GetEffect(const char* effectKey);

        // Read Shaders\Effects\<fileName> out of the archives and register every effect it
        // describes under its own name. ref: FUN_00876d90
        static void LoadEffectFile(const char* fileName);

        // ref: FUN_00876ca0
        static void RegisterEffect(const SShaderEffectDef* def, void* arg);
};

#endif
