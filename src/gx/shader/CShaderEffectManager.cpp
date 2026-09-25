#include "gx/shader/CShaderEffectManager.hpp"
#include "gx/shader/CShaderEffect.hpp"
#include "util/SFile.hpp"

#include <cstring>
#include <cstdlib>
#include <storm/Memory.hpp>
#include <storm/String.hpp>

TSHashTable<CShaderEffect, HASHKEY_STRI> CShaderEffectManager::s_shaderList;

namespace {

// The largest argument list the parser will read out of one Name(...) call. The reference
// gives its walk a kilobyte of stack for this, which is sixteen slots.
static const int32_t EFFECT_ARG_MAX = 16;
static const size_t EFFECT_ARG_SIZE = 0x40;

typedef char EffectArgs[EFFECT_ARG_MAX][EFFECT_ARG_SIZE];

// The six blend names, in the order that gives them their values.
static const char* const s_opNames[ShaderEffectOps_Last] = {
    "Mod",
    "Mod2x",
    "Add",
    "PassThru",
    "Decal",
    "Fade"
};

// ref: FUN_00876f70
uint32_t OpFromName(const char* name) {
    for (uint32_t i = 0; i < ShaderEffectOps_Last; i++) {
        if (!SStrCmpI(name, s_opNames[i], STORM_MAX_STR)) {
            return i;
        }
    }

    return ShaderEffectOp_Mod;
}

// ref: FUN_00876e30
// Read one `Name(a,b)` call at `text`. On a match the arguments land one per slot, the
// count comes back in `argCount`, and `pos` advances past the closing bracket.
//
// The reference steps two characters past a comma rather than one, so an argument list
// written without a space after the comma loses its first character. Kept: the shipped
// effect files pass one argument to everything, and a silent fix here would hide a real
// difference if a file ever did use two.
int32_t MatchToken(const char* text, const char* name, EffectArgs args, int32_t* argCount, uint32_t* pos) {
    size_t len = strlen(name);

    if (strncmp(text, name, len) || text[len] != '(') {
        return 0;
    }

    if (text[len + 1] == ')') {
        *argCount = 0;
        *pos += len + 2;

        return 1;
    }

    *argCount = 1;
    memset(args[0], 0, EFFECT_ARG_SIZE);

    int32_t at = 0;
    char c = text[len + 1];

    while (c != ')') {
        size_t next = len + 1;

        if (c == ',') {
            *argCount += 1;
            memset(args[*argCount - 1], 0, EFFECT_ARG_SIZE);
            at = 0;
            next = len + 2;
        } else {
            args[*argCount - 1][at] = c;
            at++;
        }

        len = next;
        c = text[len + 1];
    }

    *pos += len + 2;

    return 1;
}

// ref: FUN_00876f10
// The brace pair that opens at or after `start`, with everything nested inside it skipped.
int32_t FindBraces(const char* text, uint32_t start, uint32_t end, uint32_t* open, uint32_t* close) {
    int32_t depth = 0;

    while (start < end) {
        if (text[start] == '{') {
            if (!depth) {
                *open = start;
            }

            depth++;
        } else if (text[start] == '}') {
            depth--;

            if (!depth) {
                *close = start;

                return 1;
            }
        }

        start++;
    }

    return 0;
}

// ref: FUN_00876fb0
// A FixedFunc block: each Pass(n) inside it names a stage count and then the blend each
// stage applies.
void ParseFixedFunc(const char* text, uint32_t size, uint32_t* pos, EffectArgs args, int32_t argCount, SShaderEffectDef* def) {
    uint32_t open = 0;
    uint32_t close = 0;

    if (!FindBraces(text, *pos, size, &open, &close)) {
        return;
    }

    *pos = open + 1;

    while (*pos < close) {
        if (MatchToken(text + *pos, "Pass", args, &argCount, pos)) {
            auto pass = &def->fixedFuncPasses[def->fixedFuncPassCount];

            char* end = nullptr;
            pass->stages = strtol(args[0], &end, 10);

            uint32_t passOpen = 0;
            uint32_t passClose = 0;
            FindBraces(text, *pos, size, &passOpen, &passClose);

            pass->colorOps[0] = 0;
            pass->colorOps[1] = 0;
            pass->unused0 = 0;
            pass->alphaOps[0] = 0;
            pass->alphaOps[1] = 0;
            pass->unused1 = 0;

            while (*pos < passClose) {
                if (MatchToken(text + *pos, "ColorOp0", args, &argCount, pos)) {
                    pass->colorOps[0] = OpFromName(args[0]);
                } else if (MatchToken(text + *pos, "ColorOp1", args, &argCount, pos)) {
                    pass->colorOps[1] = OpFromName(args[0]);
                } else if (MatchToken(text + *pos, "AlphaOp0", args, &argCount, pos)) {
                    pass->alphaOps[0] = OpFromName(args[0]);
                } else if (MatchToken(text + *pos, "AlphaOp1", args, &argCount, pos)) {
                    pass->alphaOps[1] = OpFromName(args[0]);
                }

                *pos += 1;
            }

            def->fixedFuncPassCount++;
            *pos = passClose + 1;
        }

        *pos += 1;
    }

    *pos = close + 1;
}

// ref: FUN_00877150
// A Shader block: each Pass() inside it names the vertex and pixel program to draw with.
void ParseShaderBlock(const char* text, uint32_t size, uint32_t* pos, EffectArgs args, int32_t argCount, SShaderEffectDef* def) {
    uint32_t open = 0;
    uint32_t close = 0;

    if (!FindBraces(text, *pos, size, &open, &close)) {
        return;
    }

    *pos = open + 1;

    while (*pos < close) {
        if (MatchToken(text + *pos, "Pass", args, &argCount, pos)) {
            auto pass = &def->shaderPasses[def->shaderPassCount];

            uint32_t passOpen = 0;
            uint32_t passClose = 0;
            FindBraces(text, *pos, size, &passOpen, &passClose);

            while (*pos < passClose) {
                if (MatchToken(text + *pos, "VertexShader", args, &argCount, pos)) {
                    SStrCopy(pass->vertexShader, args[0], sizeof(pass->vertexShader));
                } else if (MatchToken(text + *pos, "PixelShader", args, &argCount, pos)) {
                    SStrCopy(pass->pixelShader, args[0], sizeof(pass->pixelShader));
                }

                *pos += 1;
            }

            def->shaderPassCount++;
            *pos = passClose + 1;
        }

        *pos += 1;
    }

    *pos = close + 1;
}

// ref: FUN_00877290
// One Effect(name) block, which holds a FixedFunc description and a Shader one.
void ParseEffect(const char* text, uint32_t size, uint32_t* pos, EffectArgs args, int32_t argCount, SShaderEffectDef* def) {
    SStrCopy(def->name, args[0], sizeof(def->name));

    def->fixedFuncPassCount = 0;
    def->shaderPassCount = 0;

    uint32_t open = 0;
    uint32_t close = 0;

    if (!FindBraces(text, *pos, size, &open, &close)) {
        return;
    }

    *pos = open + 1;

    while (*pos < close) {
        if (MatchToken(text + *pos, "FixedFunc", args, &argCount, pos)) {
            ParseFixedFunc(text, size, pos, args, argCount, def);
        } else if (MatchToken(text + *pos, "Shader", args, &argCount, pos)) {
            ParseShaderBlock(text, size, pos, args, argCount, def);
        }

        *pos += 1;
    }
}

// ref: FUN_00877360
// Walk a whole effect file, handing each Effect(...) it finds to `callback`.
void ParseEffects(const char* text, uint32_t size, void (*callback)(const SShaderEffectDef*, void*), void* arg) {
    if (!size) {
        return;
    }

    EffectArgs args;
    SShaderEffectDef def;
    int32_t argCount = 0;
    uint32_t pos = 0;

    while (pos < size) {
        if (MatchToken(text + pos, "Effect", args, &argCount, &pos)) {
            ParseEffect(text, size, &pos, args, argCount, &def);
            callback(&def, arg);
        }

        pos++;
    }
}

}

CShaderEffect* CShaderEffectManager::CreateEffect(const char* effectKey) {
    return CShaderEffectManager::s_shaderList.New(effectKey, 0, 0);
}

CShaderEffect* CShaderEffectManager::GetEffect(const char* effectKey) {
    return CShaderEffectManager::s_shaderList.Ptr(effectKey);
}

// ref: FUN_00876ca0
// An effect the file described becomes one the rest of the client can ask for by name. The
// reference registers unconditionally rather than looking first, so loading a file twice
// leaves two entries under the same name; kept.
void CShaderEffectManager::RegisterEffect(const SShaderEffectDef* def, void* arg) {
    auto effect = CShaderEffectManager::CreateEffect(def->name);

    effect->InitEffect(def->shaderPasses[0].vertexShader, def->shaderPasses[0].pixelShader);

    for (uint32_t i = 0; i < def->fixedFuncPassCount; i++) {
        auto pass = &def->fixedFuncPasses[i];

        effect->SetFixedFunc(pass->colorOps, pass->alphaOps, pass->stages);
    }
}

// ref: FUN_00876d90
void CShaderEffectManager::LoadEffectFile(const char* fileName) {
    char path[256];
    SStrPrintf(path, sizeof(path), "Shaders\\Effects\\%s", fileName);

    SFile* file = nullptr;
    SFile::Open(path, &file);

    if (!file) {
        return;
    }

    size_t size = SFile::GetFileSize(file, nullptr);
    auto text = static_cast<char*>(STORM_ALLOC(size + 1));

    SFile::Read(file, text, size, nullptr, nullptr, nullptr);
    SFile::Close(file);

    // The reference allocates the extra byte and leaves it uninitialized. The walk is
    // bounded by `size` either way; terminating costs nothing and keeps the argument scan
    // from reading past the buffer on a truncated file.
    text[size] = '\0';

    ParseEffects(text, size, &CShaderEffectManager::RegisterEffect, nullptr);

    STORM_FREE(text);
}
