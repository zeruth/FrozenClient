// The render state a map object sets before its batches go out: which batches the frustum
// keeps, the fog, the lighting, the material colour, and the shader permutation. Reference
// module MapObj.cpp.

#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjGroup.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldScene.hpp"

#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Gx.hpp"
#include "gx/RenderState.hpp"
#include "gx/Transform.hpp"
#include "gx/Shader.hpp"
#include "gx/shader/CShaderEffect.hpp"

#include <tempest/Box.hpp>
#include <tempest/Matrix.hpp>

// The state the draw only touches when it changes. The reference keeps them as MapObj.cpp
// file statics; every one is reset to its "nothing set" value when a pass starts.
uint32_t CMapObj::s_fogState = 0xffffffff;          // DAT_00cfbeb0
int32_t CMapObj::s_lightingMode = -1;               // DAT_00cfbeac
uint32_t CMapObj::s_materialColor = 0xffffffff;     // DAT_00d1bef8
int32_t CMapObj::s_shadowState = -1;                // DAT_00cfbea8
uint32_t CMapObj::s_vertexPermuteBase = 0;          // DAT_00cfbeb4
uint32_t CMapObj::s_shadowMode = 0;                 // DAT_00d43010

namespace {

void ImVectorToFloats(float* out, const CImVector& color) {
    out[0] = color.r / 255.0f;
    out[1] = color.g / 255.0f;
    out[2] = color.b / 255.0f;
    out[3] = color.a / 255.0f;
}

}

// ref: FUN_007a7630
// A batch carries its own box as six integers in group space. The traversal has already
// narrowed the frustum to this group, so testing each batch against it is cheap and throws
// away most of a large building's geometry.
bool CMapObjGroup::BatchOutsideFrustum(const SMOBatch* batch) {
    CAaBox box;
    box.b.x = static_cast<float>(batch->bounds[0]);
    box.b.y = static_cast<float>(batch->bounds[1]);
    box.b.z = static_cast<float>(batch->bounds[2]);
    box.t.x = static_cast<float>(batch->bounds[3]);
    box.t.y = static_cast<float>(batch->bounds[4]);
    box.t.z = static_cast<float>(batch->bounds[5]);

    return CWorldScene::BoxOutsideFrustum(box) != 0;
}

// ref: FUN_007a8440
// Bit 1 of `state` picks which of the light's two fog sets to use and bit 2 forces the fog
// colour black; zero turns fog off. Only changes are pushed.
//
// Diverged: the reference reads two distinct fog sets out of the light block (+0x8c and
// +0xa0). frozen's CWorld carries one, so both selections read the same start, end, rate
// and colour. The second set is what a map object under liquid would fog with.
void CMapObj::SetupFog(uint32_t state) {
    if (CMapObj::s_fogState == state) {
        return;
    }

    CMapObj::s_fogState = state;

    if (!state) {
        CShaderEffect::SetFogEnabled(0);

        return;
    }

    const C3Vector& fog = CWorld::GetFogColor();

    CImVector color;

    if (state & 0x4) {
        color.b = 0;
        color.g = 0;
        color.r = 0;
        color.a = 0xff;
    } else {
        color.b = static_cast<uint8_t>(fog.z * 255.0f);
        color.g = static_cast<uint8_t>(fog.y * 255.0f);
        color.r = static_cast<uint8_t>(fog.x * 255.0f);
        color.a = 0xff;
    }

    CShaderEffect::SetFogParams(CWorld::GetFogStart(), CWorld::GetFogEnd(), CWorld::GetFogRate(), color);
    CShaderEffect::SetFogEnabled(1);
}

// ref: FUN_007a8b10
// The light a map object's geometry draws under. Mode 1 is the exterior set, mode 2 the
// interior one, mode 3 the building's own declared ambient with no diffuse at all, and
// mode 0 no lighting. Only changes are pushed.
//
// Diverged: modes 1 and 2 take their ambient and diffuse from two LightParams columns the
// reference resolves per zone and frozen has not ported; both read the outdoor ambient and
// the sun colour here instead. Mode 3 is faithful -- that colour is the MOHD's own.
void CMapObj::SetupLighting(CMapObjGroup* group, int32_t mode) {
    if (CMapObj::s_lightingMode == mode) {
        return;
    }

    CMapObj::s_lightingMode = mode;

    C3Vector ambient = { 0.0f, 0.0f, 0.0f };
    C3Vector diffuse = { 0.0f, 0.0f, 0.0f };

    if (mode == 1 || mode == 2) {
        ambient = CWorld::GetOutdoorAmbient();
        diffuse = CShaderEffect::s_sunDiffuse;
    } else if (mode == 3) {
        const CImVector& color = group->m_mapObj->m_mohd->ambColor;

        diffuse.x = color.r / 255.0f;
        diffuse.y = color.g / 255.0f;
        diffuse.z = color.b / 255.0f;
    }

    if (!CShaderEffect::s_enableShaders) {
        // TODO the fixed-function path sets GxRs_Lighting and drives the device's own light
        // 0 through the material block. frozen's map object draw is shader-only.
        GxRsSet(GxRs_Lighting, mode != 0 ? 1 : 0);

        return;
    }

    if (!mode) {
        // Unlit geometry still needs the ambient register to hold something; the reference
        // parks a half there once and leaves it.
        float unlit[4] = { 0.0f, 0.0f, 0.0f, 0.5f };

        GxShaderConstantsSet(GxSh_Vertex, 11, unlit, 1);

        return;
    }

    float diffuseConst[4] = { diffuse.x, diffuse.y, diffuse.z, 0.0f };
    float ambientConst[4] = { ambient.x, ambient.y, ambient.z, 0.0f };

    // The sun arrives in view space, so the vertex program can dot it against a view-space
    // normal without a matrix of its own.
    C44Matrix view;
    GxXformView(view);

    C3Vector sun = CShaderEffect::s_sunDir;
    C3Vector viewSun;
    TransformDirection(viewSun, sun, view);

    float dirConst[4] = { viewSun.x, viewSun.y, viewSun.z, 0.0f };

    GxShaderConstantsSet(GxSh_Vertex, 9, diffuseConst, 1);
    GxShaderConstantsSet(GxSh_Vertex, 10, diffuseConst, 1);
    GxShaderConstantsSet(GxSh_Vertex, 11, ambientConst, 1);
    GxShaderConstantsSet(GxSh_Vertex, 12, dirConst, 1);
}

// ref: FUN_007a8940
// A material's own colour, which the self-illuminated ones change every frame. Only changes
// are pushed.
void CMapObj::SetMaterialColor(const CImVector& color) {
    if (CMapObj::s_materialColor == color.value) {
        return;
    }

    CMapObj::s_materialColor = color.value;

    CImVector emissive;
    emissive.b = 0x7f;
    emissive.g = 0x7f;
    emissive.r = 0x7f;
    emissive.a = 0xff;

    if (!CShaderEffect::s_enableShaders) {
        GxRsSet(GxRs_MatDiffuse, emissive.value);
        GxRsSet(GxRs_MatEmissive, color.value);

        return;
    }

    float constants[8];
    ImVectorToFloats(&constants[0], emissive);
    ImVectorToFloats(&constants[4], color);

    GxShaderConstantsSet(GxSh_Vertex, 0x1c, constants, 2);

    // TODO the reference also refreshes the shared material block at 0x00ce04a8 + 0xa0 into
    // vertex constant 13 here. frozen has no port of that block.
}

// ref: FUN_007a84d0
// Which vertex and pixel program the next batch draws with: the base the draw picked for
// this material, doubled and offset by whether the geometry is lit, plus fifteen per level
// of shadowing.
void CMapObj::SelectShaders() {
    if (!CShaderEffect::s_enableShaders) {
        return;
    }

    uint32_t shadow = CMapObj::s_shadowMode > 2 ? 2 : CMapObj::s_shadowMode;
    uint32_t base = shadow * 15 + CMapObj::s_vertexPermuteBase;
    uint32_t lit = CMapObj::s_lightingMode != 0 ? 1 : 0;

    CShaderEffect::SetShaders(base * 2 + lit, CShaderEffect::PixelPermute());
}
