// The render state a map object sets before its batches go out: which batches the frustum
// keeps, the fog, the lighting, the material colour, and the shader permutation. Reference
// module MapObj.cpp.

#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjGroup.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldScene.hpp"
#include "world/ShadowMap.hpp"
#include "world/map/CMap.hpp"
#include "world/map/CMapObjDefGroup.hpp"

#include "gx/CGxBatch.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Texture.hpp"
#include "gx/Device.hpp"
#include "gx/Gx.hpp"
#include "gx/RenderState.hpp"
#include "gx/Transform.hpp"
#include "gx/Shader.hpp"
#include "gx/shader/CShaderEffect.hpp"
#include "model/CM2Lighting.hpp"

#include <tempest/Box.hpp>
#include <tempest/Intersect.hpp>
#include <tempest/Ray.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Sphere.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>

// The state the draw only touches when it changes. The reference keeps them as MapObj.cpp
// file statics; every one is reset to its "nothing set" value when a pass starts.
uint32_t CMapObj::s_fogState = 0xffffffff;          // DAT_00cfbeb0
int32_t CMapObj::s_lightingMode = -1;               // DAT_00cfbeac
uint32_t CMapObj::s_materialColor = 0xffffffff;     // DAT_00d1bef8
int32_t CMapObj::s_shadowState = -1;                // DAT_00cfbea8
uint32_t CMapObj::s_vertexPermuteBase = 0;          // DAT_00cfbeb4
uint32_t CMapObj::s_shadowMode = 0;                 // DAT_00d43010
CImVector CMapObj::s_instanceColor = { 0 };         // DAT_00d1befc
int32_t CMapObj::s_interiorFog = 0;                 // DAT_00cfbeb8

namespace {

void ImVectorToFloats(float* out, const CImVector& color) {
    out[0] = color.r / 255.0f;
    out[1] = color.g / 255.0f;
    out[2] = color.b / 255.0f;
    out[3] = color.a / 255.0f;
}

}

// ref: FUN_007a8320
// One instance's transform, with the camera translation already folded in: it goes to the
// device as the world matrix and, transposed, to the vertex constants the map object programs
// read their transform from.
void CMapObj::SetInstanceTransform(const C44Matrix& world) {
    if (CShaderEffect::s_enableShaders) {
        // The constants carry the whole way from the building's own space to view space; the
        // device keeps only the world half, the way the terrain pass does.
        C44Matrix view;
        GxXformView(view);

        C44Matrix worldView = (world * view).Transpose();

        GxShaderConstantsSet(GxSh_Vertex, 0x1f, reinterpret_cast<const float*>(&worldView), 4);
    }

    GxXformSet(GxXform_World, world);
}

// ref: FUN_007a9160
// The point lights near this instance, uploaded for its geometry to read. The permutation
// base the shader selection adds to is the number of them.
void CMapObj::SetupLocalLights(CM2Lighting* lighting, const C3Vector& cameraPos) {
    if (!CShaderEffect::s_enableShaders) {
        lighting->SetupGxLights(&cameraPos);

        CMapObj::s_instanceColor.value = 0;

        return;
    }

    CMapObj::s_vertexPermuteBase = lighting->m_lightCount;

    if (CMapObj::s_vertexPermuteBase) {
        // TODO FUN_007a8a60: the reference refreshes its own light block first.

        CShaderEffect::LocalLights lights;
        CShaderEffect::ComputeLocalLights(
            &lights,
            CMapObj::s_vertexPermuteBase,
            lighting->m_lights,
            &cameraPos
        );

        GxShaderConstantsSet(GxSh_Vertex, 0x11, lights.float0, 0xb);
    }

    CMapObj::s_instanceColor.value = 0;
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
        // Diverged: the reference reads two LightParams columns per zone that frozen has not
        // ported. Take the sun the same way the terrain pass does instead, including its
        // fallback -- without that fallback the world's own ambient and diffuse are both zero
        // and every building draws black, which is exactly what happened the first time this
        // ran.
        C3Vector sunDir = { 0.0f, 0.0f, 0.0f };
        CM2Lighting lighting;
        CAaSphere origin = { sunDir, 0.0f };
        lighting.Initialize(nullptr, origin);

        lighting.AddAmbient(CWorld::GetOutdoorAmbient());
        lighting.AddDiffuse(CWorld::GetOutdoorDiffuse(), CWorld::GetOutdoorDirection());

        C3Vector specular = { 0.0f, 0.0f, 0.0f };

        if (!lighting.GetSunlight(&sunDir, &ambient, &diffuse, &specular)) {
            ambient = { 1.0f, 1.0f, 1.0f };
            diffuse = { 0.0f, 0.0f, 0.0f };
        }
    } else if (mode == 3) {
        const CImVector& color = group->m_mapObj->m_mohd->ambColor;

        ambient.x = color.r / 255.0f;
        ambient.y = color.g / 255.0f;
        ambient.z = color.b / 255.0f;
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
    // normal without a matrix of its own. Negated and rotated exactly as the terrain pass does
    // it, from the same world-level direction.
    C44Matrix view;
    GxXformView(view);

    const C3Vector& sun = CWorld::GetOutdoorDirection();
    float x = -sun.x;
    float y = -sun.y;
    float z = -sun.z;

    float dirConst[4] = {
        view.a0 * x + view.b0 * y + view.c0 * z,
        view.a1 * x + view.b1 * y + view.c1 * z,
        view.a2 * x + view.b2 * y + view.c2 * z,
        1.0f
    };

    // The map object vertex program (disassembled from MapObjDiffuse_T1) reads c10 as the term
    // it adds and c11 as the one it scales by the sun angle, so the ambient goes to 9 and 10 and
    // the diffuse to 11. The reference writes the same registers from the same pair.
    GxShaderConstantsSet(GxSh_Vertex, 9, ambientConst, 1);
    GxShaderConstantsSet(GxSh_Vertex, 10, ambientConst, 1);
    GxShaderConstantsSet(GxSh_Vertex, 11, diffuseConst, 1);
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

// ref: FUN_00873ee0
// The alpha reference the current blend mode calls for, as the shader path wants it: a
// fraction rather than the 0..255 the fixed-function state takes.
void CMapObj::SetAlphaRefForBlendMode() {
    int32_t blendMode = 0;
    g_theGxDevicePtr->RsGet(GxRs_BlendingMode, blendMode);

    CShaderEffect::SetAlphaRef(CGxDevice::s_alphaRef[blendMode] / 255.0f);
}

namespace {

// The per-byte average the reference computes with a carry trick: each channel of the two
// colours halved and added, with the alpha taken from the sum unhalved.
CImVector AverageColor(const CImVector& a, const CImVector& b) {
    CImVector out;
    out.b = static_cast<uint8_t>((a.b + b.b) >> 1);
    out.g = static_cast<uint8_t>((a.g + b.g) >> 1);
    out.r = static_cast<uint8_t>((a.r + b.r) >> 1);
    out.a = static_cast<uint8_t>(a.a + b.a);

    return out;
}

}

// ref: FUN_007ac6a0
// One group's batches, drawn. `record` is which frustum record of the portal walk this pass
// is for; on the first the batches shed last frame's drawn marks, and after that a batch
// reached through a second doorway is skipped rather than drawn twice.
void CMapObjGroup::DrawBatches(int32_t record) {
    auto mapObj = this->m_mapObj;

    if (mapObj->m_mohd->flags & 0x2) {
        this->DrawBatchesOutdoor(record);

        return;
    }

    // Drawing resets how long the buffers have gone unused, so the ageing pass leaves them.
    this->m_bufferIdleTime = 0.0f;

    this->CreateBuffers();
    this->BindIndexStream();
    this->BindVertexStream();

    GxRsPush();

    CMapObj::s_fogState = 0xffffffff;
    CMapObj::s_lightingMode = -1;
    CMapObj::s_materialColor = 0xffffffff;
    CMapObj::s_shadowState = -1;

    // What a material whose own texture has not arrived draws with: the flat 0xff808080
    // texture the scene makes at startup, which is the reference's own fallback.
    CGxTex* fallback = CWorldScene::s_solidTexture
        ? TextureGetGxTex(CWorldScene::s_solidTexture, 1, nullptr)
        : nullptr;

    for (uint32_t i = 0; i < this->m_batchCountC; i++) {
        auto batch = &this->m_batches[i];

        if (!record) {
            batch->flags &= 0x0f;
        }

        if ((batch->flags & 0xf0) || CMapObjGroup::BatchOutsideFrustum(batch)) {
            continue;
        }

        batch->flags |= 0xf0;

        // Diverged, same reason as the shader id below: the material id is file data and the
        // reference trusts it.
        if (batch->materialId >= mapObj->m_materialCount) {
            continue;
        }

        auto material = &mapObj->m_materials[batch->materialId];
        auto textures = &mapObj->m_materialTextures[batch->materialId];

        CGxTex* tex0 = TextureGetGxTex(textures->texture1, 0, nullptr);

        if (!tex0) {
            if (!fallback) {
                continue;
            }

            tex0 = fallback;
        }

        CGxTex* tex1 = fallback;

        if (textures->texture2) {
            tex1 = TextureGetGxTex(textures->texture2, 0, nullptr);

            if (!tex1) {
                if (!fallback) {
                    continue;
                }

                tex1 = fallback;
            }
        }

        // A diffuse material with no blending whose texture carries no alpha is really an
        // opaque one, and the opaque program is cheaper.
        uint32_t shader = material->shader;

        if (!shader && !material->blendMode && !TextureHasAlpha(textures->texture1)) {
            shader = 4;
        }

        // Diverged: the shader id is file data and the reference indexes its table with it
        // unchecked. A WMO naming a shader the table has no room for would read past the end,
        // so it falls back to the plain diffuse one here.
        if (shader >= CMapObj::SHADER_COUNT) {
            shader = 0;
        }

        CMapObj::SetupFog(~material->flags & 0x2);
        CMapObj::SetupLighting(this, ~material->flags & 0x1);

        // A group flagged unlit or unfogged takes no terrain shadow either.
        int32_t shadowed = (this->m_flags & 0x48) ? 0 : 1;

        if (CMapObj::s_shadowState != shadowed) {
            CMapObj::s_shadowState = shadowed;

            ShadowMapBindMapObj(shadowed);

            CMapObj::s_shadowMode = shadowed
                ? (ShadowMapGetShaderLevel() ? 1 : 0)
                : ShadowMapGetShaderLevel();
        }

        GxRsSet(GxRs_Culling, ~(material->flags >> 2) & 0x1);

        CImVector sidn;
        sidn.value = (material->flags & 0x10) ? material->frameSidnColor : 0;

        CMapObj::SetMaterialColor(AverageColor(CMapObj::s_instanceColor, sidn));

        GxRsSet(GxRs_BlendingMode, static_cast<int32_t>(material->blendMode));

        CMapObj::SetAlphaRefForBlendMode();

        GxTexSetWrap(
            tex0,
            static_cast<EGxTexWrapMode>(~(material->flags >> 6) & 0x1),
            static_cast<EGxTexWrapMode>(~(material->flags >> 7) & 0x1)
        );

        GxRsSet(GxRs_Texture0, tex0);
        GxRsSet(GxRs_Texture1, tex1);

        auto effect = CMapObj::s_effects[shader];

        if (effect) {
            effect->SetCurrent();
        }

        CMapObj::SelectShaders();

        CGxBatch gxBatch;
        gxBatch.m_primType = GxPrim_Triangles;
        gxBatch.m_start = batch->startIndex;
        gxBatch.m_count = batch->count;
        gxBatch.m_minIndex = batch->minVertex;
        gxBatch.m_maxIndex = batch->maxVertex;

        g_theGxDevicePtr->Draw(&gxBatch, 1);
    }

    GxRsPop();
}

// ref: FUN_007ac9f0
// The draw a group with vertex colours takes. MOBA is partitioned into three runs -- the
// transition batches, then the interior ones, then the exterior -- and each run is lit
// differently: interior geometry draws unlit off its baked colours, exterior geometry under
// the sun, and a transition batch draws twice, once each way, so a doorway fades between
// them.
void CMapObjGroup::DrawBatchesSplit(int32_t record) {
    auto mapObj = this->m_mapObj;

    if (mapObj->m_mohd->flags & 0x2) {
        this->DrawBatchesOutdoor(record);

        return;
    }

    this->m_bufferIdleTime = 0.0f;

    this->CreateBuffers();
    this->BindIndexStream();
    this->BindVertexStream();

    GxRsPush();

    CMapObj::s_fogState = 0xffffffff;
    CMapObj::s_lightingMode = -1;
    CMapObj::s_materialColor = 0xffffffff;
    CMapObj::s_shadowState = -1;

    // Which of the light's two fog sets this instance asked for.
    uint32_t fogSet = CMapObj::s_interiorFog ? 1 : 2;

    CGxTex* fallback = CWorldScene::s_solidTexture
        ? TextureGetGxTex(CWorldScene::s_solidTexture, 1, nullptr)
        : nullptr;

    uint32_t interiorEnd = static_cast<uint32_t>(this->m_batchCountA) + this->m_batchCountB;

    for (uint32_t i = 0; i < this->m_batchCount; i++) {
        auto batch = &this->m_batches[i];

        if (!record) {
            batch->flags &= 0x0f;
        }

        if ((batch->flags & 0xf0) || CMapObjGroup::BatchOutsideFrustum(batch)) {
            continue;
        }

        batch->flags |= 0xf0;

        // Diverged, same reason as the shader id below: the material id is file data and the
        // reference trusts it.
        if (batch->materialId >= mapObj->m_materialCount) {
            continue;
        }

        auto material = &mapObj->m_materials[batch->materialId];
        auto textures = &mapObj->m_materialTextures[batch->materialId];

        CGxTex* tex0 = TextureGetGxTex(textures->texture1, 0, nullptr);

        if (!tex0) {
            if (!fallback) {
                continue;
            }

            tex0 = fallback;
        }

        CGxTex* tex1 = nullptr;

        if (textures->texture2) {
            tex1 = TextureGetGxTex(textures->texture2, 0, nullptr);

            if (!tex1) {
                if (!fallback) {
                    continue;
                }

                tex1 = fallback;
            }
        }

        uint32_t shader = material->shader;

        if (!shader && !material->blendMode && !TextureHasAlpha(textures->texture1)) {
            shader = 4;
        }

        // Diverged: the shader id is file data and the reference indexes its table with it
        // unchecked. A WMO naming a shader the table has no room for would read past the end,
        // so it falls back to the plain diffuse one here.
        if (shader >= CMapObj::SHADER_COUNT) {
            shader = 0;
        }

        GxRsSet(GxRs_Culling, ~(material->flags >> 2) & 0x1);

        CImVector sidn;
        sidn.value = (material->flags & 0x10) ? material->frameSidnColor : 0;

        CMapObj::SetMaterialColor(AverageColor(CMapObj::s_instanceColor, sidn));

        GxTexSetWrap(
            tex0,
            static_cast<EGxTexWrapMode>(~(material->flags >> 6) & 0x1),
            static_cast<EGxTexWrapMode>(~(material->flags >> 7) & 0x1)
        );

        GxRsSet(GxRs_Texture0, tex0);
        GxRsSet(GxRs_Texture1, tex1);

        auto effect = CMapObj::s_effects[shader];

        if (effect) {
            effect->SetCurrent();
        }

        // Which way this material wants the exterior light: bit 5 asks for the interior set.
        int32_t exteriorMode = (material->flags & 0x20) ? 2 : 1;

        CGxBatch gxBatch;
        gxBatch.m_primType = GxPrim_Triangles;
        gxBatch.m_start = batch->startIndex;
        gxBatch.m_count = batch->count;
        gxBatch.m_minIndex = batch->minVertex;
        gxBatch.m_maxIndex = batch->maxVertex;

        if (i < this->m_batchCountA) {
            // A transition batch, drawn twice. First lit, writing where its alpha says the
            // exterior wins.
            if (CMapObj::s_shadowState != 0) {
                CMapObj::s_shadowState = 0;

                ShadowMapBindMapObj(0);

                CMapObj::s_shadowMode = ShadowMapGetShaderLevel();
            }

            CMapObj::SetupLighting(this, exteriorMode);
            CMapObj::SetupFog(~material->flags & 0x2);

            GxRsSet(GxRs_BlendingMode, GxBlend_SrcAlphaOpaque);

            CMapObj::SetAlphaRefForBlendMode();
            CMapObj::SelectShaders();

        g_theGxDevicePtr->Draw(&gxBatch, 1);

            // Then unlit, adding the interior colours back through the inverse alpha.
            CMapObj::SetupLighting(this, 0);
            CMapObj::SetupFog((material->flags & 0x2) ? 0 : fogSet);

            if (CMapObj::s_shadowState != 1) {
                CMapObj::s_shadowState = 1;

                ShadowMapBindMapObj(1);

                CMapObj::s_shadowMode = ShadowMapGetShaderLevel() ? 1 : 0;
            }

            GxRsSet(GxRs_BlendingMode, GxBlend_InvSrcAlphaAdd);

            CMapObj::SetAlphaRefForBlendMode();
            CMapObj::SelectShaders();

        g_theGxDevicePtr->Draw(&gxBatch, 1);

            continue;
        }

        CMapObj::SetupFog((material->flags & 0x2) ? 0 : fogSet);

        // Interior geometry carries its light in its vertex colours; exterior geometry takes
        // the sun.
        CMapObj::SetupLighting(this, i < interiorEnd ? 0 : exteriorMode);

        if (CMapObj::s_shadowState != 1) {
            CMapObj::s_shadowState = 1;

            ShadowMapBindMapObj(1);

            CMapObj::s_shadowMode = ShadowMapGetShaderLevel() ? 1 : 0;
        }

        GxRsSet(GxRs_BlendingMode, static_cast<int32_t>(material->blendMode));

        CMapObj::SetAlphaRefForBlendMode();
        CMapObj::SelectShaders();

        g_theGxDevicePtr->Draw(&gxBatch, 1);
    }

    GxRsPop();
}

// ref: FUN_007abf50
// One group of one building, drawn once per record the traversal left on it. A group whose
// vertex colours are baked takes the three-range draw; one without them takes the flat one.
void CMapObj::Render(uint32_t groupIndex, const C44Matrix& inversePlacement, CMapObjDefGroup* defGroup) {
    auto group = this->GetGroup(groupIndex, 0);

    if (!group) {
        return;
    }

    if (!(group->m_state & 0x2)) {
        group->BakePortalLight();
    }

    // TODO FUN_00872e40: refresh the sun from the shared material block at 0x00ce04a8 + 0x58.
    CShaderEffect::UpdateProjMatrix();

    int32_t index = 0;

    for (auto record = defGroup->m_frustums.Head(); record; record = defGroup->m_frustums.Next(record)) {
        auto frustum = &CWorldScene::s_frustums[CWorldScene::s_frustumDepth];
        *frustum = *record;

        // Into the building's own space, where the batch boxes already are. Without this the
        // per-batch cull would be comparing group coordinates against a world-space volume.
        frustum->Transform(inversePlacement);

        if (group->m_colors) {
            group->DrawBatchesSplit(index);
        } else {
            group->DrawBatches(index);
        }

        index++;
    }

    // TODO the two debug passes the world enables gate: bounding volumes (0x40000000) and
    // portals (0x1000).
}

// ref: FUN_007a9380
// The draw a group under an outdoor root takes: a bridge or a ruin, lit by the sky rather
// than by a room. Its transition batches blend between the sun and the building's own
// declared ambient, and geometry flagged unlit or unfogged falls back to the second fog set.
//
// The reference writes this out beside the other two draws rather than sharing their body,
// and it is kept that way here.
void CMapObjGroup::DrawBatchesOutdoor(int32_t record) {
    auto mapObj = this->m_mapObj;

    this->m_bufferIdleTime = 0.0f;

    this->CreateBuffers();
    this->BindIndexStream();
    this->BindVertexStream();

    GxRsPush();

    CMapObj::s_fogState = 0xffffffff;
    CMapObj::s_lightingMode = -1;
    CMapObj::s_materialColor = 0xffffffff;
    CMapObj::s_shadowState = -1;

    uint32_t fogSet = CMapObj::s_interiorFog ? 1 : 2;

    CGxTex* fallback = CWorldScene::s_solidTexture
        ? TextureGetGxTex(CWorldScene::s_solidTexture, 1, nullptr)
        : nullptr;

    for (uint32_t i = 0; i < this->m_batchCount; i++) {
        auto batch = &this->m_batches[i];

        if (!record) {
            batch->flags &= 0x0f;
        }

        if ((batch->flags & 0xf0) || CMapObjGroup::BatchOutsideFrustum(batch)) {
            continue;
        }

        batch->flags |= 0xf0;

        // Diverged, same reason as the shader id below: the material id is file data and the
        // reference trusts it.
        if (batch->materialId >= mapObj->m_materialCount) {
            continue;
        }

        auto material = &mapObj->m_materials[batch->materialId];
        auto textures = &mapObj->m_materialTextures[batch->materialId];

        CGxTex* tex0 = TextureGetGxTex(textures->texture1, 0, nullptr);

        if (!tex0) {
            if (!fallback) {
                continue;
            }

            tex0 = fallback;
        }

        CGxTex* tex1 = nullptr;

        if (textures->texture2) {
            tex1 = TextureGetGxTex(textures->texture2, 0, nullptr);

            if (!tex1) {
                if (!fallback) {
                    continue;
                }

                tex1 = fallback;
            }
        }

        uint32_t shader = material->shader;

        if (!shader && !material->blendMode && !TextureHasAlpha(textures->texture1)) {
            shader = 4;
        }

        // Diverged: the shader id is file data and the reference indexes its table with it
        // unchecked. A WMO naming a shader the table has no room for would read past the end,
        // so it falls back to the plain diffuse one here.
        if (shader >= CMapObj::SHADER_COUNT) {
            shader = 0;
        }

        GxRsSet(GxRs_Culling, ~(material->flags >> 2) & 0x1);

        CImVector sidn;
        sidn.value = (material->flags & 0x10) ? material->frameSidnColor : 0;

        CMapObj::SetMaterialColor(AverageColor(CMapObj::s_instanceColor, sidn));

        GxTexSetWrap(
            tex0,
            static_cast<EGxTexWrapMode>(~(material->flags >> 6) & 0x1),
            static_cast<EGxTexWrapMode>(~(material->flags >> 7) & 0x1)
        );

        GxRsSet(GxRs_Texture0, tex0);
        GxRsSet(GxRs_Texture1, tex1);

        auto effect = CMapObj::s_effects[shader];

        if (effect) {
            effect->SetCurrent();
        }

        CGxBatch gxBatch;
        gxBatch.m_primType = GxPrim_Triangles;
        gxBatch.m_start = batch->startIndex;
        gxBatch.m_count = batch->count;
        gxBatch.m_minIndex = batch->minVertex;
        gxBatch.m_maxIndex = batch->maxVertex;

        if (i < this->m_batchCountA) {
            // A transition batch: once under the sky, once under the building's own ambient.
            CMapObj::SetupFog((material->flags & 0x2) ? 0 : fogSet);

            if (CMapObj::s_shadowState != 0) {
                CMapObj::s_shadowState = 0;

                ShadowMapBindMapObj(0);

                CMapObj::s_shadowMode = ShadowMapGetShaderLevel();
            }

            int32_t skyMode = (material->flags & 0x1)
                ? 0
                : ((material->flags & 0x20) ? 2 : 1);

            CMapObj::SetupLighting(this, skyMode);
            CMapObj::SetupFog(~material->flags & 0x2);

            GxRsSet(GxRs_BlendingMode, GxBlend_SrcAlphaOpaque);

            CMapObj::SetAlphaRefForBlendMode();
            CMapObj::SelectShaders();

        g_theGxDevicePtr->Draw(&gxBatch, 1);

            CMapObj::SetupLighting(this, 3);
            CMapObj::SetupFog((material->flags & 0x2) ? 0 : fogSet);

            if (CMapObj::s_shadowState != 1) {
                CMapObj::s_shadowState = 1;

                ShadowMapBindMapObj(1);

                CMapObj::s_shadowMode = ShadowMapGetShaderLevel() ? 1 : 0;
            }

            GxRsSet(GxRs_BlendingMode, GxBlend_InvSrcAlphaAdd);

            CMapObj::SetAlphaRefForBlendMode();
            CMapObj::SelectShaders();

        g_theGxDevicePtr->Draw(&gxBatch, 1);

            continue;
        }

        if (!(this->m_flags & 0x48)) {
            CMapObj::SetupLighting(this, (material->flags & 0x20) ? 2 : 3);

            if (CMapObj::s_shadowState != 1) {
                CMapObj::s_shadowState = 1;

                ShadowMapBindMapObj(1);

                CMapObj::s_shadowMode = ShadowMapGetShaderLevel() ? 1 : 0;
            }

            CMapObj::SetupFog(fogSet);
        } else {
            CMapObj::SetupLighting(this, ~material->flags & 0x1);

            if (CMapObj::s_shadowState != 0) {
                CMapObj::s_shadowState = 0;

                ShadowMapBindMapObj(0);

                CMapObj::s_shadowMode = ShadowMapGetShaderLevel();
            }

            // Straight to the second fog set, past the cache's usual selection.
            if (CMapObj::s_fogState != 2) {
                CMapObj::s_fogState = 2;

                // Diverged with CMapObj::SetupFog: frozen carries one fog set, so this reads
                // the same start, end, rate and colour that set does.
                const C3Vector& fog = CWorld::GetFogColor();

                CImVector color;
                color.b = static_cast<uint8_t>(fog.z * 255.0f);
                color.g = static_cast<uint8_t>(fog.y * 255.0f);
                color.r = static_cast<uint8_t>(fog.x * 255.0f);
                color.a = 0xff;

                CShaderEffect::SetFogParams(
                    CWorld::GetFogStart(),
                    CWorld::GetFogEnd(),
                    CWorld::GetFogRate(),
                    color
                );
                CShaderEffect::SetFogEnabled(1);
            }
        }

        GxRsSet(GxRs_BlendingMode, static_cast<int32_t>(material->blendMode));

        CMapObj::SetAlphaRefForBlendMode();
        CMapObj::SelectShaders();

        g_theGxDevicePtr->Draw(&gxBatch, 1);
    }

    GxRsPop();
}

// ref: FUN_007d78c0
// Daylight through a doorway. For every vertex of the group's transition batches, how near it
// is to each portal it owns decides how far its baked colour is pulled toward mid grey, and
// the alpha it ends up with is that amount. A vertex right in a doorway whose far side is a
// lit room gets nothing, because the room lights it already.
//
// Runs once per group: CMapObj::Render sets the state bit whether or not this ran.
void CMapObjGroup::BakePortalLight() {
    this->m_state |= 0x2;

    if (!this->m_batchCountA || !this->m_colors || !this->m_vertices) {
        return;
    }

    auto mapObj = this->m_mapObj;
    uint32_t lastVertex = this->m_batches[this->m_batchCountA - 1].maxVertex;

    for (uint32_t v = 0; v <= lastVertex; v++) {
        const C3Vector& position = this->m_vertices[v];
        float total = 0.0f;
        bool inDoorway = false;

        for (uint32_t p = 0; p < this->m_portalCount; p++) {
            auto ref = &mapObj->m_mopr[this->m_portalStart + p];
            auto portal = &mapObj->m_mopt[ref->portalIndex];
            const C4Plane& plane = portal->plane;

            float planeDistance = plane.n.x * position.x
                                + plane.n.y * position.y
                                + plane.n.z * position.z
                                + plane.d;

            // Where the vertex lands on the portal's own plane. A vertex already on it stays
            // put; otherwise it steps along the normal, whichever way it has to.
            C3Vector hit = position;

            if (planeDistance > 0.001f || planeDistance < -0.001f) {
                C3Vector toward;

                if (planeDistance > 0.001f) {
                    toward.x = position.x - plane.n.x;
                    toward.y = position.y - plane.n.y;
                    toward.z = position.z - plane.n.z;
                } else {
                    toward.x = position.x + plane.n.x;
                    toward.y = position.y + plane.n.y;
                    toward.z = position.z + plane.n.z;
                }

                C3Ray ray;
                RayFromPoints(ray, position, toward, false);
                IntersectRayPlane(ray, plane, nullptr, &hit, 0.01f);
            }

            auto vertices = &mapObj->m_mopv[portal->startVertex];
            uint32_t axis = DominantAxis(plane.n);

            float reach;

            if (PointInPolygon(hit, vertices, portal->count, axis)) {
                // Straight through the opening: how far back from it the vertex is, signed so
                // the room's own side comes out negative.
                reach = planeDistance;

                if (ref->side != 1) {
                    reach = -reach;
                }
            } else {
                // Off to the side of the opening: how far round the frame it has to go.
                reach = DistancePointPolygon(position, vertices, portal->count);
            }

            if (!(mapObj->m_mogi[ref->groupIndex].flags & 0x48)) {
                // The far side is a lit room, so a vertex in the doorway takes its light from
                // there and this bake leaves it alone.
                if (reach > -1.0f && reach < 1.0f) {
                    inDoorway = true;

                    break;
                }
            } else {
                if (reach < 0.0f) {
                    reach = 0.0f;
                }

                float share = 1.0f - reach * 0.15f;

                if (share > 0.001f) {
                    total += share;
                }
            }
        }

        if (inDoorway || total <= 0.001f) {
            total = 0.0f;
        } else if (total > 1.0f) {
            total = 1.0f;
        }

        CImVector& color = this->m_colors[v];

        color.r = static_cast<uint8_t>(lroundf(color.r + (127.0f - color.r) * total));
        color.g = static_cast<uint8_t>(lroundf(color.g + (127.0f - color.g) * total));
        color.b = static_cast<uint8_t>(lroundf(color.b + (127.0f - color.b) * total));
        color.a = static_cast<uint8_t>(lroundf(total * 255.0f));
    }
}

void (*CMapObj::s_visibleCallback)(uint32_t groupIndex, CMapObjDef* def);
CMapObjDef* CMapObj::s_visibleCallbackArg;

// ref: FUN_007a6b40
void CMapObj::SetVisibleCallback(void (*callback)(uint32_t, CMapObjDef*), CMapObjDef* def) {
    CMapObj::s_visibleCallback = callback;
    CMapObj::s_visibleCallbackArg = def;
}

// ref: FUN_007a6b60
void CMapObj::ReportVisible(uint32_t groupIndex) {
    if (!this->GetGroup(groupIndex, 0)) {
        return;
    }

    if (!CMapObj::s_visibleCallback) {
        return;
    }

    CMapObj::s_visibleCallback(groupIndex, CMapObj::s_visibleCallbackArg);
}
