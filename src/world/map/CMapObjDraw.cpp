// The render state a map object sets before its batches go out: which batches the frustum
// keeps, the fog, the lighting, the material colour, and the shader permutation. Reference
// module MapObj.cpp.

#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjGroup.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldScene.hpp"
#include "world/CWFrustum.hpp"
#include "world/ShadowMap.hpp"
#include "world/map/CMap.hpp"
#include "world/map/CMapLight.hpp"
#include "world/DayNightLight.hpp"
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
        CShaderEffect::LocalLights lights;

        // The reference clears the block before filling it, and it matters -- see LocalLights.
        lights.Clear();

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
// Bit 1 of `state` picks the DayNight block's outdoor fog set (+0x8c) over the final, blended
// one the world is under (+0xa0), and bit 2 forces the fog colour black; zero turns fog off.
// Only changes are pushed.
void CMapObj::SetupFog(uint32_t state) {
    if (CMapObj::s_fogState == state) {
        return;
    }

    CMapObj::s_fogState = state;

    if (!state) {
        CShaderEffect::SetFogEnabled(0);

        return;
    }

    auto block = DayNightGetBlock();
    bool outdoor = (state & 0x2) != 0;

    CImVector color = outdoor ? block->fogColor : block->finalFogColor;
    float start = outdoor ? block->fogStart : block->finalFogStart;
    float end = outdoor ? block->fogEnd : block->finalFogEnd;
    float rate = outdoor ? block->fogRate : block->finalFogRate;

    if (state & 0x4) {
        color.value = 0xff000000;
    }

    CShaderEffect::SetFogParams(start, end, rate, color);
    CShaderEffect::SetFogEnabled(1);
}

// ref: FUN_007a8b10
// The light a map object's geometry draws under. Mode 1 is the exterior set, mode 2 the
// interior one, mode 3 the building's own declared ambient with no diffuse at all, and
// mode 0 no lighting. Only changes are pushed.
//
// Modes 1 and 2 are the DayNight block's packed colours: the sun's diffuse and ambient
// (+0x1a8 / +0x1ac), or the same two blended half and half (+0x1b0 / +0x1b4), which is what
// interiors take. Mode 3 is the MOHD's own ambient with no diffuse.
void CMapObj::SetupLighting(CMapObjGroup* group, int32_t mode) {
    if (CMapObj::s_lightingMode == mode) {
        return;
    }

    CMapObj::s_lightingMode = mode;

    C3Vector ambient = { 0.0f, 0.0f, 0.0f };
    C3Vector diffuse = { 0.0f, 0.0f, 0.0f };

    auto block = DayNightGetBlock();
    const float k = 1.0f / 255.0f;
    auto rgb = [k](const CImVector& c) {
        return C3Vector { c.r * k, c.g * k, c.b * k };
    };

    if (mode == 1) {
        diffuse = rgb(block->diffuse);
        ambient = rgb(block->ambient);
    } else if (mode == 2) {
        diffuse = rgb(block->diffuseHalf);
        ambient = rgb(block->ambientHalf);
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
    // normal without a matrix of its own: the outdoor light's own direction, rotated by the view
    // and not negated (TransformDirection at 0x007a8d4f). The fourth float the reference sends
    // is whatever follows its three on the stack; the program reads only xyz.
    C44Matrix view;
    GxXformView(view);

    const C3Vector& sun = CMap::s_outdoorLight->m_light.m_dir;

    float dirConst[4] = {
        view.a0 * sun.x + view.b0 * sun.y + view.c0 * sun.z,
        view.a1 * sun.x + view.b1 * sun.y + view.c1 * sun.z,
        view.a2 * sun.x + view.b2 * sun.y + view.c2 * sun.z,
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

                // State 2 is SetupFog's outdoor set: the DayNight block's +0x8c group.
                auto block = DayNightGetBlock();
                CShaderEffect::SetFogParams(block->fogStart, block->fogEnd, block->fogRate, block->fogColor);
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

C3Vector CMapObj::s_localCameraPos;
C3Vector CMapObj::s_localCameraTarget;
C4Plane CMapObj::s_localViewPlane;

// ref: FUN_007a6e00
// Set up one instance for the portal walk: the device draws it in its own space with the
// camera translation folded out, and the camera itself is brought into that space so every
// portal plane can be tested against it without leaving it.
void CMapObj::SetupPortalContext(const C44Matrix& placement, const C44Matrix& inversePlacement,
                                 const C3Vector& cameraPos, const C3Vector& cameraTarget) {
    C44Matrix toCamera;
    toCamera.Identity();

    C3Vector back = { -cameraPos.x, -cameraPos.y, -cameraPos.z };
    toCamera.Translate(back);

    GxXformSet(GxXform_World, placement * toCamera);

    CMapObj::s_localCameraPos = cameraPos * inversePlacement;
    CMapObj::s_localCameraTarget = cameraTarget * inversePlacement;

    C3Vector dir = {
        CMapObj::s_localCameraTarget.x - CMapObj::s_localCameraPos.x,
        CMapObj::s_localCameraTarget.y - CMapObj::s_localCameraPos.y,
        CMapObj::s_localCameraTarget.z - CMapObj::s_localCameraPos.z
    };

    float lengthSq = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;

    // A camera sitting on its own target leaves the direction unnormalised, as the reference
    // leaves it.
    if (lengthSq > 0.00000011920929f) {
        float inv = 1.0f / sqrtf(lengthSq);

        dir.x *= inv;
        dir.y *= inv;
        dir.z *= inv;
    }

    CMapObj::s_localViewPlane.n = dir;
    CMapObj::s_localViewPlane.d = -(dir.x * CMapObj::s_localCameraPos.x
                                  + dir.y * CMapObj::s_localCameraPos.y
                                  + dir.z * CMapObj::s_localCameraPos.z);

    // TODO FUN_00682130(&DAT_00adfe90) and the second matrix block after it: the projection
    // the walk measures a portal's screen rectangle with. The rectangle itself
    // (FUN_007a9090 -> FUN_007a85e0) is not ported either, so neither is used yet.
}

// ref: FUN_007a7210
// A camera within a centimetre of the portal's plane, and inside the doorway itself, sees
// straight through: the walk then treats the opening as covering the whole screen rather
// than a rectangle of it.
void CMapObj::TestCameraInPortal(CMapObj* mapObj, const SMOPortal* portal, PortalRect* rect) {
    const C4Plane& plane = portal->plane;
    const C3Vector& camera = CMapObj::s_localCameraPos;

    float distance = plane.n.x * camera.x + plane.n.y * camera.y + plane.n.z * camera.z + plane.d;

    if (distance <= -0.01f || distance >= 0.01f) {
        return;
    }

    auto vertices = &mapObj->m_mopv[portal->startVertex];
    uint32_t axis = DominantAxis(plane.n);

    if (PointInPolygon(camera, vertices, portal->count, axis)) {
        rect->flags |= 0x2;
    }
}

// ref: FUN_007a85e0
// A doorway's outline, measured against the screen. It is taken out to world space, clipped to
// the camera's own frustum, and each surviving corner projected; a corner behind the eye would
// divide by a vanishing w, so w is held at a ten-thousandth.
uint32_t CMapObj::ProjectPortal(const SMOPortal* portal, const C3Vector* vertices,
                                const C3Vector& offset, const C44Matrix& placement,
                                C3Vector* screen) {
    uint32_t count = portal->count;

    if (count > CLIP_POLYGON_MAX) {
        count = CLIP_POLYGON_MAX;
    }

    if (!count) {
        return 0;
    }

    C3Vector world[CLIP_POLYGON_MAX];

    for (uint32_t i = 0; i < count; i++) {
        C3Vector moved = {
            vertices[i].x + offset.x,
            vertices[i].y + offset.y,
            vertices[i].z + offset.z
        };

        world[i] = moved * placement;
    }

    // The four sides and the far plane. The near one is left out: a doorway straddling the eye
    // is handled by the camera-in-doorway test instead of by cutting it.
    C3Vector clipped[CLIP_POLYGON_MAX];
    uint32_t clippedCount = ClipPolygonToPlanes(CWorldScene::s_clipFrustum.planes, 5,
                                                world, count, clipped);

    if (clippedCount < 3) {
        return 0;
    }

    C44Matrix view;
    C44Matrix proj;

    GxXformView(view);
    GxXformProjection(proj);

    C44Matrix viewProj = view * proj;

    for (uint32_t i = 0; i < clippedCount; i++) {
        C4Vector v = {
            clipped[i].x - CWorldScene::s_cameraPos.x,
            clipped[i].y - CWorldScene::s_cameraPos.y,
            clipped[i].z - CWorldScene::s_cameraPos.z,
            1.0f
        };

        C4Vector p;
        p.x = view.a0 * v.x + view.b0 * v.y + view.c0 * v.z + view.d0 * v.w;
        p.y = view.a1 * v.x + view.b1 * v.y + view.c1 * v.z + view.d1 * v.w;
        p.z = view.a2 * v.x + view.b2 * v.y + view.c2 * v.z + view.d2 * v.w;
        p.w = view.a3 * v.x + view.b3 * v.y + view.c3 * v.z + view.d3 * v.w;

        C4Vector c;
        c.x = proj.a0 * p.x + proj.b0 * p.y + proj.c0 * p.z + proj.d0 * p.w;
        c.y = proj.a1 * p.x + proj.b1 * p.y + proj.c1 * p.z + proj.d1 * p.w;
        c.z = proj.a2 * p.x + proj.b2 * p.y + proj.c2 * p.z + proj.d2 * p.w;
        c.w = proj.a3 * p.x + proj.b3 * p.y + proj.c3 * p.z + proj.d3 * p.w;

        float w = c.w < 0.0001f ? 0.0001f : c.w;
        float inv = 1.0f / w;

        screen[i].x = c.x * inv;
        screen[i].y = c.y * inv;
        screen[i].z = c.z;
    }

    return clippedCount;
}

// ref: FUN_007a6b90
// The reference unrolls this four corners at a time; the comparisons per corner are the same.
void CMapObj::ScreenBounds(PortalRect* rect, const C3Vector* points, uint32_t count) {
    rect->minY = 3.4028235e+38f;
    rect->minX = 3.4028235e+38f;
    rect->maxY = -3.4028235e+38f;
    rect->maxX = -3.4028235e+38f;

    for (uint32_t i = 0; i < count; i++) {
        if (points[i].x < rect->minX) {
            rect->minX = points[i].x;
        }

        if (rect->maxX < points[i].x) {
            rect->maxX = points[i].x;
        }

        if (points[i].y < rect->minY) {
            rect->minY = points[i].y;
        }

        if (rect->maxY < points[i].y) {
            rect->maxY = points[i].y;
        }
    }
}

// ref: FUN_007a9090
// A doorway is one of three things: the camera is standing in it, so it opens onto the whole
// screen; nothing of it survives the clip, so it opens onto nothing; or it covers a rectangle,
// which is what the walk narrows the view to before stepping through.
void CMapObj::MeasurePortal(CMapObj* mapObj, const SMOPortal* portal, PortalRect* rect,
                            const C44Matrix& placement) {
    CMapObj::TestCameraInPortal(mapObj, portal, rect);

    C3Vector screen[CLIP_POLYGON_MAX];
    uint32_t count = 0;

    if (!(rect->flags & 0x2)) {
        C3Vector noOffset = { 0.0f, 0.0f, 0.0f };

        count = CMapObj::ProjectPortal(portal, &mapObj->m_mopv[portal->startVertex],
                                       noOffset, placement, screen);

        if (!count) {
            rect->flags |= 0x1;
        }
    }

    if (rect->flags & 0x2) {
        // Standing in the doorway: the whole screen.
        rect->minY = -3.4028235e+38f;
        rect->minX = -3.4028235e+38f;
        rect->maxY = 1.0f;
        rect->maxX = 1.0f;

        return;
    }

    if (rect->flags & 0x1) {
        // Nothing of it is in view: an empty rectangle nothing can overlap.
        rect->minY = 3.4028235e+38f;
        rect->minX = 3.4028235e+38f;
        rect->maxY = -3.4028235e+38f;
        rect->maxX = -3.4028235e+38f;

        return;
    }

    CMapObj::ScreenBounds(rect, screen, count);
}

C44Matrix CMapObj::s_portalPlacement;
int32_t CMapObj::s_portalStamp;

// ref: FUN_007ac060
// One room, then every room its doorways open onto. A doorway facing away from the camera is
// skipped, one covering none of the remaining view is skipped, and what is left narrows the
// view for the room beyond it.
void CMapObj::WalkPortals(uint32_t groupIndex, uint32_t fromGroup, const float* window,
                          uint32_t depth, int32_t interior) {
    if (depth > CMapObj::PORTAL_DEPTH_MAX) {
        return;
    }

    auto group = this->GetGroup(groupIndex, 0);

    if (!group) {
        return;
    }

    // A group that draws on its own is not walked into; it was already handled.
    if (group->m_flags & 0x10000) {
        return;
    }

    // Stepping out of a room into daylight ends the interior run.
    if (interior && (group->m_flags & 0x48)) {
        interior = 0;
    }

    CMapObj::s_interiorFog = interior;

    if (CMapObj::s_insideBuilding && (group->m_flags & 0x40000)) {
        CWorldScene::s_mapObjSkybox = this->m_mosb;
    }

    if (CMapObj::s_visibleCallback) {
        CMapObj::s_visibleCallback(groupIndex, CMapObj::s_visibleCallbackArg);
    }

    if (!group->m_portalCount || !this->m_mopr || !this->m_mopt) {
        return;
    }

    if (CWorldScene::s_frustumDepth + 1 >= static_cast<int32_t>(CWorldScene::FRUSTUM_DEPTH_MAX)) {
        return;
    }

    for (uint32_t i = 0; i < group->m_portalCount; i++) {
        auto ref = &this->m_mopr[group->m_portalStart + i];

        if (ref->groupIndex == 0xffff || ref->groupIndex == fromGroup) {
            continue;
        }

        if (ref->portalIndex >= this->m_portalRects.Count()) {
            continue;
        }

        auto portal = &this->m_mopt[ref->portalIndex];
        auto rect = &this->m_portalRects[ref->portalIndex];

        // Measured at most once a frame: a doorway reached twice covers the same rectangle.
        if (rect->stamp != CMapObj::s_portalStamp) {
            rect->stamp = CMapObj::s_portalStamp;
            rect->flags = 0;

            uint32_t targetFlags = this->GroupFlags(ref->groupIndex);

            // Crossing between lit and unlit is marked so the room beyond knows.
            if (!(targetFlags & 0x8) && !(group->m_flags & 0x8)) {
                rect->flags = 0x10;
            }

            CMapObj::MeasurePortal(this, portal, rect, CMapObj::s_portalPlacement);
        }

        // Which face of the doorway the camera is on; a doorway seen from behind leads nowhere.
        const C4Plane& plane = portal->plane;
        const C3Vector& camera = CMapObj::s_localCameraPos;

        float side = plane.n.x * camera.x + plane.n.y * camera.y + plane.n.z * camera.z + plane.d;

        if (ref->side < 0) {
            side = -side;
        }

        if (side < 0.0f) {
            continue;
        }

        // Nothing of it in view.
        if (!(rect->flags & 0x2) && (rect->flags & 0x1)) {
            continue;
        }

        // Overlap with the window, both ordered vertical-first ({minY, minX, maxY, maxX}): the
        // reference tests +8 against window[3], window[1] against +0x10, +4 against window[2]
        // and window[0] against +0xc (0x007ac2f0). This used to compare each rect edge with the
        // window edge of the OTHER axis, so a doorway was dropped whenever its vertical span
        // missed the window's horizontal one -- which rooms showed through a door depended on
        // the view angle.
        if (rect->minX > window[3] || window[1] > rect->maxX
            || rect->minY > window[2] || window[0] > rect->maxY) {
            continue;
        }

        // The doorway narrowed to what is left of the view. The reference clamps three of the
        // four edges and repeats one of them instead of clamping the fourth; kept, because a
        // doorway is measured against this same window on the way in, so the unclamped edge
        // cannot exceed it in practice.
        float sub[4];
        sub[0] = rect->minY < window[0] ? window[0] : rect->minY;
        sub[1] = rect->minX < window[1] ? window[1] : rect->minX;
        sub[2] = rect->maxY;
        sub[3] = window[3] < rect->maxX ? window[3] : rect->maxX;

        // A doorway edge-on covers no area and leads nowhere.
        if (NearlyEqual(sub[1], sub[3], 0.001f) || NearlyEqual(sub[0], sub[2], 0.001f)) {
            continue;
        }

        // The traversal's windows run 0 to 1; a projected rectangle runs -1 to 1.
        CWorldScene::ViewWindow narrowed;
        narrowed.minX = (sub[0] + 1.0f) * 0.5f;
        narrowed.minY = (sub[1] + 1.0f) * 0.5f;
        narrowed.maxX = (sub[2] + 1.0f) * 0.5f;
        narrowed.maxY = (sub[3] + 1.0f) * 0.5f;
        narrowed.depth = -1.0f;
        narrowed.points = nullptr;
        narrowed.pointCount = 0;

        CWorldScene::s_frustumDepth++;
        CWorldScene::s_frustums[CWorldScene::s_frustumDepth] =
            CWorldScene::s_frustums[CWorldScene::s_frustumDepth - 1];
        CWorldScene::SubFrustum(CWorldScene::s_frustumCorners, &narrowed);

        this->WalkPortals(ref->groupIndex, groupIndex, sub, depth + 1, interior);

        CWorldScene::s_frustumDepth--;
    }
}

CMapObjDef* CMapObj::s_walkDef;
CMapObjDef* CMapObj::s_lastWalkDef;
int32_t CMapObj::s_insideBuilding;
int32_t CMapObj::s_sawExterior;

namespace {

// The doorway measurements are cached per frame per building, so the stamp advances when the
// walk moves to a different building rather than on every group of the same one.
void AdvancePortalStamp() {
    if (CMapObj::s_walkDef != CMapObj::s_lastWalkDef) {
        CMapObj::s_portalStamp++;
        CMapObj::s_lastWalkDef = CMapObj::s_walkDef;
    }
}

}

// ref: FUN_007ad350
void CMapObj::WalkFromOutside(const C44Matrix& placement, const C44Matrix& inversePlacement,
                              const C3Vector& cameraPos, const C3Vector& cameraTarget,
                              const float* window, uint32_t groupIndex) {
    CMapObj::SetupPortalContext(placement, inversePlacement, cameraPos, cameraTarget);

    CMapObj::s_insideBuilding = 0;

    AdvancePortalStamp();

    this->WalkPortals(groupIndex, 0xffff, window, 0, 0);
}

// ref: FUN_007ad1f0
// The camera is standing inside the building, possibly in more than one group at once where
// they overlap. Each of those starts its own walk with the whole screen to work with.
void CMapObj::WalkFromInside(const C44Matrix& placement, const C44Matrix& inversePlacement,
                             const C3Vector& cameraPos, const C3Vector& cameraTarget,
                             const uint32_t* groups, uint32_t groupCount) {
    CMapObj::SetupPortalContext(placement, inversePlacement, cameraPos, cameraTarget);

    CMapObj::s_insideBuilding = 1;
    CMapObj::s_sawExterior = 0;

    AdvancePortalStamp();

    if (CWorldScene::s_frustumDepth + 1 >= static_cast<int32_t>(CWorldScene::FRUSTUM_DEPTH_MAX)) {
        return;
    }

    CWorldScene::s_frustumDepth++;
    CWorldScene::s_frustums[CWorldScene::s_frustumDepth] =
        CWorldScene::s_frustums[CWorldScene::s_frustumDepth - 1];

    float window[4] = { -1.0f, -1.0f, 1.0f, 1.0f };

    for (uint32_t i = 0; i < groupCount; i++) {
        auto group = this->GetGroup(groups[i], 0);

        if (!group) {
            continue;
        }

        // Standing in a group that is open to the sky means the walk can see daylight.
        if (!(group->m_flags & 0x48)) {
            CMapObj::s_sawExterior = 1;
        }

        this->WalkPortals(groups[i], 0xffff, window, 0, 1);
    }

    CWorldScene::s_frustumDepth--;

    // The walk only reaches what the portals connect. A group the root flags always-draw is
    // reported anyway, on nothing but its own box being in view -- that is how a building's
    // outer shell stays drawn while the camera is inside one of its rooms.
    //
    // Unexercised so far: across 215 placed buildings and 2759 groups on map 609, not one
    // carries MOGI bit 16, so this loop has never had anything to report. What that run did
    // confirm is the reading -- every one of those 2759 boxes came out well formed, so the
    // stride and the field offsets are right.
    for (uint32_t i = 0; i < this->m_groupCount; i++) {
        if (!(this->m_mogi[i].flags & 0x10000)) {
            continue;
        }

        CAaBox box = TransformBox(this->m_mogi[i].bounds, placement);

        if (CWorldScene::BoxOutsideFrustum(box)) {
            continue;
        }

        this->ReportVisible(i);
    }
}

// ref: FUN_007b3b20
// The camera is inside this building: point the walk's reports at the frame's visible list and
// start from the groups it is standing in.
void CMapObj::EnterPortalWalk(CMapObjDef* def, const uint32_t* groups, uint32_t groupCount) {
    CMapObj::SetVisibleCallback(&CWorldScene::MarkMapObjGroupVisible, def);

    CMapObj::s_walkDef = def;
    CMapObj::s_portalPlacement = def->m_placement;

    def->m_mapObj->WalkFromInside(def->m_placement, def->m_inversePlacement,
                                  CWorldScene::s_cameraPos, CWorldScene::s_cameraTarget,
                                  groups, groupCount);
}

// ref: FUN_007ab760
void MapObjDrawShadowCasters(CMapObjGroup* const* groups, uint32_t count, const C44Matrix* const* placements,
                             const C44Matrix& toCamera, const CWFrustum& frustum) {
    GxRsPush();

    // Pixel c2: white, with the alpha reference in w.
    float color[4] = { 1.0f, 0.0f, 0.0f, 0.0f };

    for (uint32_t i = 0; i < count; i++) {
        CMapObjGroup* group = groups[i];
        const C44Matrix& placement = *placements[i];
        CMapObj* mapObj = group->m_mapObj;

        GxXformSet(GxXform_World, placement * toCamera);

        CAaBox box = TransformBox(group->m_mogpBounds, placement);

        if (group->m_batchCount == 0 || !AaBoxVsPlanes6(frustum.planes, box)) {
            continue;
        }

        group->m_bufferIdleTime = 0.0f;
        group->CreateBuffers();
        group->BindIndexStream();
        group->BindVertexStream();
        CShaderEffect::SetWorldViewConstants();

        if (!(group->m_state & 0x4)) {
            CShaderEffect::SetAlphaRef(0.8784313797950745f);
            CShaderEffect::SetShadersForGeometry(0);

            for (uint32_t b = 0; b < group->m_batchCount; b++) {
                const SMOBatch& batch = group->m_batches[b];
                const SMOMaterial& material = mapObj->m_materials[batch.materialId];
                HTEXTURE texture = mapObj->m_materialTextures[batch.materialId].texture1;
                CGxTex* gxTex = texture ? TextureGetGxTex(texture, 0, nullptr) : nullptr;

                if (!gxTex) {
                    continue;
                }

                g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);

                color[3] = material.blendMode == 1 ? 0.8784313797950745f : 0.0f;
                g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 2, color, 1);

                CGxBatch gxBatch;
                gxBatch.m_primType = GxPrim_Triangles;
                gxBatch.m_start = batch.startIndex;
                gxBatch.m_count = batch.count;
                gxBatch.m_minIndex = batch.minVertex;
                gxBatch.m_maxIndex = batch.maxVertex;

                g_theGxDevicePtr->Draw(&gxBatch, 1);
            }
        } else {
            CShaderEffect::SetAlphaRef(0.0f);
            CShaderEffect::SetShadersForGeometry(0);

            g_theGxDevicePtr->RsSet(GxRs_Texture0, static_cast<CGxTex*>(nullptr));

            color[3] = 0.0f;
            g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 2, color, 1);

            CGxBatch gxBatch;
            gxBatch.m_primType = GxPrim_Triangles;
            gxBatch.m_start = group->m_minIndex;
            gxBatch.m_count = group->m_maxIndex - group->m_minIndex + 1;
            gxBatch.m_minIndex = group->m_minVertex;
            gxBatch.m_maxIndex = group->m_maxVertex;

            g_theGxDevicePtr->Draw(&gxBatch, 1);
        }
    }

    GxRsPop();
}

// ref: FUN_007abac0
void MapObjDrawGroupsFlat(CMapObjGroup* const* groups, uint32_t count, const C44Matrix* const* placements,
                          const C44Matrix& toCamera, CImVector color) {
    CImVector vertexColor = color;

    // A device that reads colours red first gets the bytes the other way round.
    if (g_theGxDevicePtr->Caps().m_colorFormat == GxCF_rgba) {
        vertexColor.r = color.b;
        vertexColor.b = color.r;
    }

    GxRsPush();
    GxRsSet(GxRs_BlendingMode, GxBlend_Opaque);
    GxRsSetAlphaRef();
    GxRsSet(GxRs_VertexShader, static_cast<CGxShader*>(nullptr));
    GxRsSet(GxRs_PixelShader, static_cast<CGxShader*>(nullptr));

    for (uint32_t i = 0; i < count; i++) {
        CMapObjGroup* group = groups[i];
        CMapObj* mapObj = group->m_mapObj;

        GxXformSet(GxXform_World, *placements[i] * toCamera);

        if (!group->m_batchCount) {
            continue;
        }

        CGxBuf* vertices = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x1c, group->m_vertexCount);
        uint8_t* out = reinterpret_cast<uint8_t*>(g_theGxDevicePtr->BufLock(vertices));

        for (uint32_t v = 0; v < group->m_vertexCount; v++) {
            memcpy(out, &group->m_vertices[v], sizeof(C3Vector));
            memcpy(out + 0xc, &group->m_normals[v], sizeof(C3Vector));
            memcpy(out + 0x18, &vertexColor, sizeof(CImVector));
            out += 0x1c;
        }

        g_theGxDevicePtr->BufUnlock(vertices, 0);
        vertices->unk1C = 1;
        GxPrimVertexPtr(vertices, GxVBF_PNC);

        CGxBuf* indices = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, group->m_indexCount);
        void* indexOut = g_theGxDevicePtr->BufLock(indices);
        memcpy(indexOut, group->m_indices, group->m_indexCount * 2);
        g_theGxDevicePtr->BufUnlock(indices, 0);
        indices->unk1C = 1;
        g_theGxDevicePtr->PrimIndexPtr(indices);

        if (!(group->m_state & 0x4)) {
            for (uint32_t b = 0; b < group->m_batchCount; b++) {
                const SMOBatch& batch = group->m_batches[b];
                HTEXTURE texture = mapObj->m_materialTextures[batch.materialId].texture1;
                CGxTex* gxTex = texture ? TextureGetGxTex(texture, 0, nullptr) : nullptr;

                if (!gxTex) {
                    continue;
                }

                g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);

                CGxBatch gxBatch;
                gxBatch.m_primType = GxPrim_Triangles;
                gxBatch.m_start = batch.startIndex;
                gxBatch.m_count = batch.count;
                gxBatch.m_minIndex = batch.minVertex;
                gxBatch.m_maxIndex = batch.maxVertex;
                g_theGxDevicePtr->Draw(&gxBatch, 1);
            }
        } else {
            GxRsSet(GxRs_Texture0, static_cast<CGxTex*>(nullptr));

            CGxBatch gxBatch;
            gxBatch.m_primType = GxPrim_Triangles;
            gxBatch.m_start = group->m_minIndex;
            gxBatch.m_count = group->m_maxIndex - group->m_minIndex + 1;
            gxBatch.m_minIndex = group->m_minVertex;
            gxBatch.m_maxIndex = group->m_maxVertex;
            g_theGxDevicePtr->Draw(&gxBatch, 1);
        }
    }

    GxRsPop();
}
