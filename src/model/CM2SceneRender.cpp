#include "model/CM2Lighting.hpp"
#include <tempest/Intersect.hpp>
#include <cmath>
#include "gx/Buffer.hpp"
#include "model/CM2SceneRender.hpp"
#include "gx/shader/CShaderEffect.hpp"
#include "model/CM2Ribbon.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/Shader.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "model/CM2Cache.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2ParticleEmitter.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Types.hpp"
#include <tempest/Math.hpp>

C44Matrix CM2SceneRender::s_identity;

int32_t CM2SceneRender::s_fogModeList[M2BLEND_COUNT] = {
    1,  // M2BLEND_OPAQUE
    1,  // M2BLEND_ALPHA_KEY
    1,  // M2BLEND_ALPHA
    2,  // M2BLEND_NO_ALPHA_ADD
    2,  // M2BLEND_ADD
    3,  // M2BLEND_MOD
    4   // M2BLEND_MOD_2X
};

EGxBlend CM2SceneRender::s_gxBlend[M2PASS_COUNT][M2BLEND_COUNT] = {
    // M2PASS_0
    {
        GxBlend_Opaque,         // M2BLEND_OPAQUE
        GxBlend_AlphaKey,       // M2BLEND_ALPHA_KEY
        GxBlend_Alpha,          // M2BLEND_ALPHA
        GxBlend_NoAlphaAdd,     // M2BLEND_NO_ALPHA_ADD
        GxBlend_Add,            // M2BLEND_ADD
        GxBlend_Mod,            // M2BLEND_MOD
        GxBlend_Mod2x           // M2BLEND_MOD_2X
    },

    // M2PASS_1
    {
        GxBlend_Alpha,          // M2BLEND_OPAQUE
        GxBlend_Alpha,          // M2BLEND_ALPHA_KEY
        GxBlend_Alpha,          // M2BLEND_ALPHA
        GxBlend_NoAlphaAdd,     // M2BLEND_NO_ALPHA_ADD
        GxBlend_Add,            // M2BLEND_ADD
        GxBlend_Mod,            // M2BLEND_MOD
        GxBlend_Mod2x           // M2BLEND_MOD_2X
    },

    // M2PASS_2
    {
        GxBlend_Alpha,          // M2BLEND_OPAQUE
        GxBlend_Alpha,          // M2BLEND_ALPHA_KEY
        GxBlend_Alpha,          // M2BLEND_ALPHA
        GxBlend_NoAlphaAdd,     // M2BLEND_NO_ALPHA_ADD
        GxBlend_Add,            // M2BLEND_ADD
        GxBlend_Mod,            // M2BLEND_MOD
        GxBlend_Mod2x           // M2BLEND_MOD_2X
    }
};

uint32_t CM2SceneRender::s_curElementDword3c = 0;
int32_t CM2SceneRender::s_shadedList[M2BLEND_COUNT] = {
    1,  // M2BLEND_OPAQUE
    1,  // M2BLEND_ALPHA_KEY
    1,  // M2BLEND_ALPHA
    1,  // M2BLEND_NO_ALPHA_ADD
    1,  // M2BLEND_ADD
    0,  // M2BLEND_MOD
    0   // M2BLEND_MOD_2X
};

const C44Matrix* CM2SceneRender::s_shadowCasterRebase = nullptr;
uint32_t CM2SceneRender::s_shadowCasterDrawn = 0;
CShaderEffect* CM2SceneRender::s_shadowCasterEffect = nullptr;

void CM2SceneRender::Draw(M2PASS pass, M2Element* elements, uint32_t* indices, uint32_t count) {

    // Only the OPAQUE pass. Pass 1/2 are the transparent ones, which legitimately run with depth
    // write off -- capturing those was measuring the wrong thing three times running.
    // The shadow-caster sweep also runs as M2PASS_0, rebased into light space with the shadow
    // map's orthographic projection. Capturing that instead of the camera pass has now happened
    // twice; s_shadowCasterRebase is what tells them apart.

    if (!count) {
        return;
    }

    GxRsPush();

    C44Matrix savedView;
    GxXformView(savedView);

    for (int32_t xf = GxXform_Tex0; xf <= GxXform_World; xf++) {
        GxXformPush(static_cast<EGxXform>(xf));
    }

    GxXformSetView(CM2SceneRender::s_identity);
    GxXformSet(GxXform_World, CM2SceneRender::s_identity);

    GxRsSet(GxRs_DepthFunc, 0);
    GxRsSet(GxRs_PolygonOffset, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_MatSpecularExp, 0.0f);

    if (CShaderEffect::s_enableShaders) {
        C44Matrix projNative;
        GxXformProjNative(projNative);
        this->matrix0 = projNative.Inverse(projNative.Determinant()).Transpose();

        CShaderEffect::UpdateProjMatrix();

        // TODO
        // CShadowCache::SetShadowMapGenericGlobal();
    }

    this->m_curPass = pass;

    for (int32_t i = 0; i < count; i++) {
        auto element = &elements[indices[i]];


        if (element->type == 2 || element->type == 4 || !element->model->m_flag2000) {
            this->m_curElement = element;
            this->m_curType = element->type;
            this->m_curModel = element->model;
            this->m_curShared = element->model->m_shared;
            this->m_curShaded = 1;
            this->m_curFogMode = 1;
            this->m_curBatch = nullptr;
            this->m_curSkinSection = nullptr;
            this->m_curLighting = element->model->m_currentLighting;
            // The reference takes the ADDRESS of its embedded block at +0xb8 (0x823a51,
            // `leal 0xb8(%esi), %edx`), not its value as the old note here said. The batch path
            // below overwrites this pointer with the real material out of the model data; what is
            // left pointing here is every element type that has no material of its own.
            //
            // Not cosmetic: SetupMaterial dereferences m_curMaterial and DrawParticle calls
            // SetupMaterial, so without this a particle element would read whatever the previous
            // element left -- a real material belonging to another model.
            this->m_curMaterial = &this->m_scratchMaterial;
            this->m_data = element->model->m_shared->m_data;

            this->m_cache->TouchGeometry(this->m_curShared);

            // TYPE 4 IS NOW REACHABLE as of 2026-09-24: CM2Scene::AddParticleElement builds
            // particle elements and files them in the pass lists, so DrawParticle is a live stub
            // rather than a dead one. It returns 0, which is safe -- `i` is incremented by the
            // loop header and the return only adds an extra skip -- so the elements are visited
            // and nothing is drawn for them yet.
            //
            // The rest still holds, with type 1's gate now understood. Type 0 is the only one
            // that draws. Type 2 needs CM2Model::IsBatchDoodadCompatible, which always returns 0.
            //
            // Type 1 needs TWO things: a batch with flag 0x4, and a non-null
            // CM2Scene::m_projectionCallback -- the field previously called uint104, which is not
            // a number but the projected-decal callback. Nothing calls
            // CM2Scene::SetProjectionCallback yet, so it stays null and the gate stays shut; see
            // docs/ref/parity-shadows.md for the whole chain.
            //
            // Type 5 needs draw-callback elements the gather does not build, so porting
            // DrawCallback still means porting its gate first.
            //
            // Type 3 is no longer in that position as of 2026-09-27: CM2Scene::Animate now has
            // the ribbon gather, transcribed from the reference. It still emits nothing, because
            // CM2Ribbon::IsEmpty is permanently true while no per-frame segment update advances
            // the ring -- so DrawRibbon below is reached zero times today, and the ORDER to port
            // in is the segment update first, then this draw. Filling DrawRibbon before the
            // update would leave it looking correct and never running.
            switch (this->m_curElement->type) {
                case 0: {
                    this->DrawBatch();
                    break;
                }

                case 1: {
                    this->DrawBatchProj();
                    break;
                }

                case 2: {
                    this->DrawBatchDoodad(elements, &indices[i]);
                    i += this->m_curElement->doodadCount - 1;
                    break;
                }

                case 3: {
                    this->DrawRibbon();
                    break;
                }

                case 4: {
                    i += this->DrawParticle(i, elements, indices, count);
                    break;
                }

                case 5: {
                    this->DrawCallback();
                    break;
                }

                default:
                    continue;
            }

            this->m_prevElement = this->m_curElement;
            this->m_prevType = this->m_curType;
            this->m_prevModel = this->m_curModel;
            this->m_prevShared = this->m_curShared;
            // Yes, this one is backwards, and yes it is faithful. Settled 2026-09-23.
            //
            // Every other line in this block copies cur into prev. The reference does the
            // same nine times and then, for the lighting pair alone, copies the other way:
            // its ten adjacent-field copies at 0x00823ad3..0x00823b0f are all low -> high
            // except 0x00823ae5, which is `+0x74 -> +0x70`.
            //
            // Which of those two is `cur` is what settles it, and CM2SceneRender::SetupLighting
            // answers it: at 0x0081fb3e it loads +0x70 and hands it to SetLocalLighting, so
            // +0x70 is the live one and the copy really is cur = prev.
            //
            // It is inert either way. Nothing in the reference or here ever assigns
            // m_prevLighting, so it stays null, this nulls m_curLighting at the end of each
            // element, and the next element reassigns it before anything reads it. The visible
            // consequence is only that SetupLighting's `m_curLighting != m_prevLighting` test
            // always fires, so it redoes its work every batch -- in the reference too.
            //
            // Do not "fix" this into prev = cur. That would be a divergence, and it would
            // silently change how often the lighting path runs.
            this->m_curLighting = this->m_prevLighting;
            this->m_prevShaded = this->m_curShaded;
            this->m_prevFogMode = this->m_curFogMode;
            this->m_prevBatch = this->m_curBatch;
            this->m_prevSkinSection = this->m_curSkinSection;
            this->m_prevMaterial = this->m_curMaterial;
        }
    }

    for (int32_t xf = GxXform_Tex0; xf <= GxXform_World; xf++) {
        GxXformPop(static_cast<EGxXform>(xf));
    }

    GxXformSetView(savedView);

    GxRsPop();
    GxRsSet(GxRs_Fog, 0);

}

void CM2SceneRender::DrawBatch() {
    auto element = this->m_curElement;

    this->m_curBatch = element->batch;
    this->m_curSkinSection = element->skinSection;
    this->m_curMaterial = &this->m_data->materials[element->batch->materialIndex];

    // Casting into the shadow map needs geometry and nothing else: no lighting, no material, no
    // textures. The effect is swapped rather than the draw reimplemented so that vertex streams,
    // index buffers and bone constants all follow exactly the path the visible pass uses.
    if (CM2SceneRender::s_shadowCasterEffect) {
        CM2SceneRender::s_shadowCasterEffect->SetCurrent();
    } else {
        element->effect->SetCurrent();
        this->SetupLighting();
        this->SetupMaterial();
        this->SetupTextures();
    }

    if (
        CShaderEffect::s_enableShaders
        && (
            CM2SceneRender::s_shadowCasterRebase
            || this->m_curType != this->m_prevType
            || this->m_curModel != this->m_prevModel
            || this->m_curSkinSection->boneComboIndex != this->m_prevSkinSection->boneComboIndex
        )
    ) {
        C4Vector* constants = reinterpret_cast<C4Vector*>(GxShaderConstantsLock(GxSh_Vertex));

        for (int32_t i = 0; i < this->m_curSkinSection->boneCount; i++) {
            auto& boneMatrix = this->m_curModel->m_boneMatrices[this->m_data->boneCombos[this->m_curSkinSection->boneComboIndex + i]];


            // In caster mode the bone is lifted out of the camera's view and into the light's. The
            // cached-upload shortcut above is disabled in that mode because the same bone yields a
            // different matrix here than it did in the visible pass.
            C44Matrix bone = CM2SceneRender::s_shadowCasterRebase
                ? boneMatrix * *CM2SceneRender::s_shadowCasterRebase
                : boneMatrix;

            constants[31 + (i * 3) + 0] = { bone.a0, bone.b0, bone.c0, bone.d0 };
            constants[31 + (i * 3) + 1] = { bone.a1, bone.b1, bone.c1, bone.d1 };
            constants[31 + (i * 3) + 2] = { bone.a2, bone.b2, bone.c2, bone.d2 };
        }

        GxShaderConstantsUnlock(GxSh_Vertex, 31, this->m_curSkinSection->boneCount * 3);
    }

    if (this->m_curElement->flags & 0x4) {
        if (
            this->m_curType != this->m_prevType
            || this->m_curModel != this->m_prevModel
        ) {
            this->m_curModel->SetIndices();
        }
    } else if (
        this->m_curType != this->m_prevType
        || this->m_curShared != this->m_prevShared
        || this->m_prevElement->flags & 0x4
    ) {
        this->m_curShared->SetIndices();
    }

    int32_t v9 = this->m_curModel->m_shared->m_data->bones.count == 1 && this->m_cache->m_flags & 0x40;
    this->SetBatchVertices(v9);

    if (CM2SceneRender::s_shadowCasterEffect) {
        // THE CASTER PASS SELECTS ITS SHADER BY GEOMETRY, NOT BY THE MATERIAL. The element's
        // vertexPermute and pixelPermute were computed for the effect it draws with in the visible
        // pass -- Model2 -- and using them here indexes the SHADOW MAP library at those numbers,
        // which are different programs. Both libraries have permutations at those indices, so
        // nothing fails and nothing complains: 2332 of 2332 batches reached GxDraw and the map came
        // back the white it was cleared to.
        //
        // The reference never does this. FUN_0082da40 calls CShaderEffect::SetShadersForGeometry(0)
        // for each of the two caster lists, and FUN_00829ba0 calls it again per batch, so the
        // permutation comes from bone influences, light state and shadow mode -- never from the
        // material. Passing 0 is the reference's own argument at both sites.
        CShaderEffect::SetShadersForGeometry(0);
    } else {
        CShaderEffect::SetShaders(this->m_curElement->vertexPermute, this->m_curElement->pixelPermute);
    }

    if (CShaderEffect::s_enableShaders) {
        auto skinSection = this->m_curSkinSection;

        CGxBatch batch;

        batch.m_primType = GxPrim_Triangles;
        batch.m_start = skinSection->indexStart;
        batch.m_count = skinSection->indexCount;
        batch.m_minIndex = skinSection->vertexStart;
        batch.m_maxIndex = skinSection->vertexStart + skinSection->vertexCount - 1;


        // Counted so that DrawShadowCasters can say how many submitted batches actually reached
        // a draw. A caster pass that submits thousands and draws none reads identically in the
        // log to one that works, and that is exactly the state this was in until 2026-09-28.
        if (CM2SceneRender::s_shadowCasterEffect) {
            CM2SceneRender::s_shadowCasterDrawn++;
        }

        GxDraw(&batch, 1);
    } else if (v9) {
        // TODO
    } else {
        // TODO
    }
}

// ref: FUN_00820ae0
// A group of doodad elements drawn as instances of the current one: the doodad list holds the
// group's members from `indices` on, and the head carries how many. The instances go in chunks of
// what CM2Shared::ReserveInstances could make room for. Without shaders each chunk's vertices are
// skinned on the CPU by the section's packer into a stream buffer; with them, the shared instance
// vertex buffer is bound once and each chunk uploads every instance's bone matrices at c31.
void CM2SceneRender::DrawBatchDoodad(M2Element* elements, uint32_t* indices) {
    auto element = this->m_curElement;

    this->m_curBatch = element->batch;
    this->m_curSkinSection = element->skinSection;
    this->m_curMaterial = &this->m_data->materials[element->batch->materialIndex];

    element->effect->SetCurrent();
    this->SetupLighting();
    this->SetupMaterial();
    this->SetupTextures();

    uint32_t total = element->doodadCount;
    uint32_t chunk = this->m_curShared->ReserveInstances(total);

    if (chunk > total) {
        chunk = total;
    }

    this->m_curShared->SetIndices();

    auto section = this->m_curSkinSection;

    CGxBatch batch;
    batch.m_primType = GxPrim_Triangles;
    batch.m_start = section->indexStart;
    batch.m_minIndex = 0;

    if (!CShaderEffect::s_enableShaders) {
        for (uint32_t done = 0; done < total; done += chunk) {
            if (done + chunk > total) {
                chunk = total - done;
            }

            auto buffer = GxBufStream(GxPoolTarget_Vertex, 0x20, section->vertexCount * chunk);
            auto vertices = GxBufLock(buffer);

            if (!vertices) {
                return;
            }

            // Normals need renormalising after a blend, or after one bone that scales.
            uint32_t normalize = section->boneCount != 1;
            auto pack = M2GetPackBatchVerticesFn(section->boneInfluences);

            for (uint32_t i = 0; i < chunk; i++) {
                CM2Model* model = elements[indices[done + i]].model;

                pack(model, section, vertices, 0);
                vertices += section->vertexCount * 0x20;

                if (!normalize) {
                    const C44Matrix& bone = model->m_boneMatrices[this->m_data->boneCombos[section->boneComboIndex]];
                    float lengthSquared = bone.a0 * bone.a0 + bone.a1 * bone.a1 + bone.a2 * bone.a2;

                    normalize = std::fabs(lengthSquared - 1.0f) > 0.001f;
                }
            }

            GxBufUnlock(buffer, 0);
            GxPrimVertexPtr(buffer, GxVBF_PNT);
            GxRsSet(GxRs_NormalizeNormals, static_cast<int32_t>(normalize));

            batch.m_count = section->indexCount * chunk;
            batch.m_maxIndex = section->vertexCount * chunk - 1;

            GxDraw(&batch, 1);
        }

        return;
    }

    this->m_curShared->SetVertices(0);

    for (uint32_t done = 0; done < total; done += chunk) {
        if (done + chunk > total) {
            chunk = total - done;
        }

        C4Vector* constants = reinterpret_cast<C4Vector*>(GxShaderConstantsLock(GxSh_Vertex)) + 31;
        uint32_t bones = 0;

        for (uint32_t i = 0; i < chunk; i++) {
            CM2Model* model = elements[indices[done + i]].model;

            for (uint32_t j = 0; j < section->boneCount; j++) {
                const C44Matrix& bone = model->m_boneMatrices[this->m_data->boneCombos[section->boneComboIndex + j]];

                constants[0] = { bone.a0, bone.b0, bone.c0, bone.d0 };
                constants[1] = { bone.a1, bone.b1, bone.c1, bone.d1 };
                constants[2] = { bone.a2, bone.b2, bone.c2, bone.d2 };
                constants += 3;
                bones++;
            }
        }

        GxShaderConstantsUnlock(GxSh_Vertex, 31, bones * 3);
        CShaderEffect::SetShaders(element->vertexPermute, element->pixelPermute);

        batch.m_count = section->indexCount * chunk;
        batch.m_maxIndex = this->m_curShared->skinProfile->vertices.Count() * chunk - 1;

        GxDraw(&batch, 1);
    }
}

// ref: FUN_00820720
// A batch flagged for projection (0x4) is not drawn as geometry: its section's world bounds and
// texture mapping are handed to the scene's projection callback, which draws the decal onto
// whatever lies under it. Texture stage 0 maps camera-relative positions to the batch's texture
// coordinates, stage 1 maps height across the bounds (widened by 2 each way) to u. The colour is
// the model's diffuse by the batch colour, plus its emissive, with the element's alpha.
void CM2SceneRender::DrawBatchProj() {
    auto element = this->m_curElement;

    this->m_curBatch = element->batch;
    this->m_curSkinSection = element->skinSection;
    this->m_curMaterial = &this->m_data->materials[element->batch->materialIndex];

    CAaBox bounds;
    C44Matrix texMatrix;

    if (!this->m_curModel->ComputeProjection(this->m_curSkinSection, bounds, texMatrix)) {
        return;
    }

    bounds.b.z -= 2.0f;
    bounds.t.z += 2.0f;

    const C44Matrix& viewInv = this->m_scene->m_viewInv;
    float heightScale = 1.0f / (bounds.t.z - bounds.b.z);

    C44Matrix heightMatrix;
    heightMatrix.a0 = 0.0f; heightMatrix.a1 = 0.0f; heightMatrix.a2 = 0.0f; heightMatrix.a3 = 0.0f;
    heightMatrix.b0 = 0.0f; heightMatrix.b1 = 0.0f; heightMatrix.b2 = 0.0f; heightMatrix.b3 = 0.0f;
    heightMatrix.c0 = heightScale; heightMatrix.c1 = 0.0f; heightMatrix.c2 = 0.0f; heightMatrix.c3 = 0.0f;
    heightMatrix.d0 = heightScale * (viewInv.d2 - bounds.b.z);
    heightMatrix.d1 = 0.5f;
    heightMatrix.d2 = 0.0f;
    heightMatrix.d3 = 1.0f;

    auto model = this->m_curModel;
    C3Vector color = model->m_currentDiffuse;

    if (this->m_curBatch->colorIndex < this->m_data->colors.Count()) {
        const C3Vector& batchColor = model->m_colors[this->m_curBatch->colorIndex].colorTrack.currentValue;

        color.y = batchColor.y * color.y;
        color.z = batchColor.z * color.z;
        color.x = batchColor.x * color.x;
    }

    color.x += model->m_currentEmissive.x;
    color.y += model->m_currentEmissive.y;
    color.z += model->m_currentEmissive.z;

    auto channel = [](float value) {
        float clamped = value < 0.0f ? 0.0f : value < 1.0f ? value : 1.0f;
        return static_cast<uint8_t>(lrintf(clamped * 255.0f));
    };

    CImVector decalColor;
    decalColor.r = channel(color.x);
    decalColor.g = channel(color.y);
    decalColor.b = channel(color.z);
    decalColor.a = channel(element->alpha);

    this->SetupTextures();
    this->SetupLighting();
    this->SetupMaterial();
    element->effect->SetCurrent();

    C3Vector cameraPosition = { viewInv.d0, viewInv.d1, viewInv.d2 };
    this->SetupParticleTransform(cameraPosition);

    CShaderEffect::SetTexMtx_EyeSpace(2, texMatrix, 0);
    CShaderEffect::SetTexMtx_EyeSpace(2, heightMatrix, 1);

    using ProjectionCallback = void (*)(const CAaBox& bounds, const CImVector& color, uint32_t shaded, void* context, uint32_t flag4);
    reinterpret_cast<ProjectionCallback>(this->m_scene->m_projectionCallback)(
        bounds, decalColor, this->m_curShaded, this->m_scene->m_projectionContext, model->m_flag4);

    GxXformSet(GxXform_World, CM2SceneRender::s_identity);
}

// ref: FUN_00823070
// The model's own textures, lighting and material, no shaders, the particle transform, and the
// states pushed so the owner's draw cannot leak them.
void CM2SceneRender::BeginCallbackDraw(const C3Vector& cameraPosition) {
    this->SetupTextures();
    this->SetupLighting();
    this->SetupMaterial();
    GxRsSet(GxRs_VertexShader, static_cast<CGxShader*>(nullptr));
    GxRsSet(GxRs_PixelShader, static_cast<CGxShader*>(nullptr));
    this->SetupParticleTransform(cameraPosition);
    GxRsPush();
}

// ref: FUN_0081f6a0
void CM2SceneRender::EndCallbackDraw() {
    GxRsPop();
    GxXformSetView(CM2SceneRender::s_identity);
    GxXformSet(GxXform_World, CM2SceneRender::s_identity);
}

// ref: FUN_008230d0
// An element whose model draws itself: set up as for a particle, hand over to the owner's
// callback, restore.
void CM2SceneRender::DrawCallback() {
    C3Vector origin = { 0.0f, 0.0f, 0.0f };
    this->BeginCallbackDraw(origin);

    auto model = this->m_curModel;
    model->m_drawCallback(model, model->m_currentLighting, model->m_drawCallbackArg);

    CM2SceneRender::EndCallbackDraw();
    CShaderEffect::UpdateProjMatrix();
}

// DEAD STUB, and the chain above it is what has to land first.
//
// This has a real caller -- element type 4 in Draw -- but CM2Scene::Animate emits only types 0, 1
// and 2, so nothing reaches here. Porting this before the emission would give frozen a function
// that can never run.
//
// The emission was mapped 2026-09-24. Two functions:
//
// CM2Scene::Animate's loop at 0x822be0 walks m_particleEmitters and skips an emitter when any of
// four gates fail: the model's flag 0x2000 together with the emitter's 0x200; the emitter's
// 0x2000000; the runtime block's `active` byte being clear; or the model's float198 at or below
// 1e-4 (0x009e8cd0). It then transforms the M2Particle's position by the emitter's bone matrix
// (FUN_004c21b0, C3Vector * C44Matrix), derives a camera distance, and calls the builder once for
// the emitter and once for each of its children.
//
// FUN_00821930 is that builder, and 0x821977 is where the `type = 4` store actually lives:
//
//     if (!emitter->HasLiveParticles()) return;      // FUN_0097b9e0, ported
//     if (emitter->m_particleKind == 1) return;      // model-carrying emitters make no element
//     element = scene->m_elements.New();             // FUN_00821670, the container at scene+0x34
//     element->type = 4; element->model = model; element->flags = 0;
//     element->alpha = <the alpha passed in>;  element->float10 = model->[0x88];
//     element->float14 = <the camera distance>;
//     element->[0x18] = emitter;                     // the EMITTER rides in the element
//     element->[0x24] = emitter->[0x18];
//     element->[0x30] = 0; element->[0x34] = -1; element->[0x38] = -1; element->[0x3c] = 0;
//     then blend-mode bookkeeping off emitter->[0xd0] into two of the scene's counters.
//
// The element layout blocker is gone: M2Element is 17 dwords and frozen's struct now matches, and
// it carries the emitter in a field of its own rather than overloading `index` the way the
// reference does (a 64-bit pointer does not fit that 32-bit slot).
//
// The builder's SIGNATURE is settled too -- seven dwords of arguments, which `retl $0x1c`
// confirms:
//
//     AddParticleElement(CM2ParticleEmitter* emitter, CM2Model* model,
//                        float distance, float alpha,
//                        int32_t flag, uint32_t* elementIndex, uint32_t* counter)
//
// `elementIndex` is a POINTER to the scene's running element count: the pass list gets the current
// value appended and the count is then incremented, so the lists hold element indices. `counter`
// is bumped whenever the blend is GxBlend_Add or GxBlend_NoAlphaAdd.
//
// Pass routing at the tail, onto frozen's existing array54[3]:
//     blend <= 1 and alpha >= 0.99999 (0x00a45528)      -> array54[0]
//     else if flag == 0 or emitter->m_flags & 0x40000   -> array54[2]
//     else                                              -> array54[1]
//
// The two arguments the loop computes, traced 2026-09-24:
//
//   ALPHA is just model->float198, stashed at 0x822c2c by the same read that gates the emitter on
//   it being above 1e-4. Nothing more.
//
//   DISTANCE is NOT a camera distance, which is what the name suggests. It is the SQUARED length
//   of `file.position * m_boneMatrices[file.boneIndex]` -- the emitter's own position in the
//   model's space, squared, with no camera involved (0x822c70..0x822c98). Transcribe it; do not
//   "fix" it into a camera distance.
//
// And the FLAG, which selects pass 1 against pass 2, is a LIQUID PLANE TEST. It reads an object at
// model+0x2a8, which the reference points either at the model's own block at +0x1d4 or at its
// parent's (0x828a49) -- that is frozen's m_currentLighting / m_lighting pair exactly. The fields
// it uses land on CM2Lighting as frozen already has it: +0x14 is m_flags, and +0xc4 is
// m_liquidPlane. So:
//
//     flag = lighting->m_flags & 0x20;
//     if (flag && (lighting->m_flags & 0x40)) {
//         centre = (data->boundsMax + data->boundsMin) * 0.5;      // data +0xa0..+0xb4
//         radius = length(matrixF4.row0) * data->[0xb8];
//         if (dot(m_liquidPlane, centre * matrixF4) <= -radius) flag = 0;
//     }
//
// which is why the transparent block flips its order under liquid: a model entirely below the
// water plane has its particles routed to the other pass.
//
// ONE FIELD REMAINS UNIDENTIFIED and it is small: the reference caches emitter->[0x18] into the
// element's +0x24 slot. It is set by neither the constructor nor SetTextureGrid. Frozen carries
// the emitter in the element anyway, so the cache is probably redundant here -- but that cannot be
// asserted until the field is known, so do not silently omit it without checking.
// Put the view into camera-relative space for a particle draw.
//
// Translating the view matrix by the camera position cancels the camera translation, and the world
// matrix goes to identity -- so particle geometry is built and submitted relative to the camera
// rather than in absolute world coordinates. That is the arrangement CLAUDE.md's third priority
// is about, arrived at here from the reference rather than from the depth symptoms.
//
// The caller passes `scene + 0xf4`, which is m_viewInv's translation row: the camera position.
// The same value the particle driver hands to CM2ParticleEmitter::Update.
//
// NOT a divergence, though an earlier version of this note called it one. The reference writes
// the device's transform-stack top at device+0x18c8 directly, which looks like reaching into
// internals -- but CGxDevice::XformSet indexes `device + 0x1008 + xf * 0x118`, and for
// GxXform_World (8) that is exactly 0x18c8. The reference is calling XformSet INLINED, so the
// GxXformSet line below is the faithful form rather than a convenient substitute. The identity it
// copies is the global at 0x00af58a8, read out of the PE and confirmed exactly identity.
//
// ref: FUN_0081f620
void CM2SceneRender::SetupParticleTransform(const C3Vector& cameraPosition) {
    C44Matrix view = this->m_scene->m_view;

    view.Translate(cameraPosition);

    GxXformSetView(view);
    GxXformSet(GxXform_World, C44Matrix());
}

// PORTED 2026-09-24, from the map below plus the disassembly at 0x8214e0 -- which carries three
// register arguments the decompilation does not. NOT VERIFIED: the geometry stage inside
// CM2ParticleEmitter::Draw is still FUN_0097e730, so this sets up a draw that emits no quads yet.
//
// The shape, kept because every step below is now a line of code and the map is how to check it:
//
//   1. Build a 16-bit material key from the EMITTER'S MATERIAL FLAGS:
//          key = (matFlags & 0x1) ? 4 : 5
//          if (!(matFlags & 0x2)) key |= 0x2
//          if (!(matFlags & 0x4)) key |= 0x10
//          key |= <FUN_0081ca20()> << 16
//      then `m_curMaterial = &key` -- which is the field the Draw loop still marks TODO.
//
//      Worth noting as corroboration: those are exactly the three bits CM2Model's factory
//      derives when it builds the emitter's material, and they were mapped from the
//      CONSTRUCTION side. The draw side reading the same three is independent confirmation.
//
//   2. If `this->[0x44]->[0x4] & 0x80`, hand off to FUN_00821100 and return its value instead.
//   3. Otherwise resolve the emitter's texture (+0x128) through FUN_004b6cb0 and bail if null.
//   4. Set up device and shader state -- 0x685f50, 0x685970 (both through the global at
//      0x00c5df88), 0x872f90, 0x81fb10, 0x81fe90, 0x81f620 -- then call
//      **FUN_0097ea60, which is the EMITTER'S OWN DRAW** and where the quads are actually built.
//   5. A virtual through the device, then 0x57c450.
//
// The geometry lives in CM2ParticleEmitter, not here; this is the state around it. Every helper
// step 4 names turned out to exist already -- 0x685f50 is CGxDevice::RsSet, 0x685970 IRsDirty,
// 0x872f90 CShaderEffect::SetCurrent, 0x81fb10 SetupLighting, 0x81fe90 SetupMaterial, 0x81f620
// SetupParticleTransform, 0x4b6cb0 TextureGetGxTex -- so the chain that looked like a port of its
// own was one function deep, and this is it.
//
// What is left between here and pixels is exactly one function: FUN_0097e730, the quad builder
// that CM2ParticleEmitter::Draw calls.
int32_t CM2SceneRender::DrawParticle(uint32_t a2, M2Element* elements, uint32_t* a4, uint32_t a5) {
    CM2ParticleEmitter* emitter = this->m_curElement->emitter;

    // The emission builds one element per emitter, but the factory is allowed to produce a null
    // emitter for a type frozen does not have -- see CM2Model::InitializeLoaded.
    if (!emitter) {
        return 0;
    }

    // Particles carry no M2Material in the file, so one is built here out of the emitter's own
    // flags and handed to SetupMaterial through the scratch member. Cross-checked against
    // SetupMaterial, its only consumer: 0x4 is culling, 0x8 depth test, 0x10 depth write. 0x8 is
    // never set here, so a particle always depth-tests.
    uint32_t matFlags = emitter->m_materialFlags;

    // Bit 0 of the emitter's flags is LIT and the M2 material bit is UNLIT, so this inverts. Bit
    // 1 is fogged and bit 2 is depth-write-enabled, inverting the same way. Those are the three
    // bits CM2Model's factory derives from the file record -- read from the construction side
    // before this was, which makes the two readings independent of each other.
    uint16_t flags = (matFlags & 0x1) ? 0x4 : 0x5;

    if (!(matFlags & 0x2)) {
        flags |= 0x2;
    }

    if (!(matFlags & 0x4)) {
        flags |= 0x10;
    }

    this->m_scratchMaterial.flags = flags;
    this->m_scratchMaterial.blendMode =
        static_cast<uint16_t>(M2BlendIndexFromGx(emitter->m_blendMode));

    // The reference points m_curMaterial at a STACK LOCAL here, which dies on return and leaves
    // Draw's epilogue copying a dangling pointer into m_prevMaterial. The scratch member is the
    // recorded divergence; see its declaration.
    this->m_curMaterial = &this->m_scratchMaterial;

    // Forces SetupMaterial to redo its work rather than trust a comparison against whatever the
    // previous element left.
    this->m_prevMaterial = nullptr;

    // M2BatchParticles: adjacent emitters that can share a buffer are drawn as one, and the count
    // they swallowed is returned for Draw to skip.
    if (this->m_cache->m_flags & 0x80) {
        return this->DrawParticleBatch(a2, elements, a4, a5);
    }

    CGxTex* texture = TextureGetGxTex(emitter->m_texture, 0, nullptr);

    if (!texture) {
        return 0;
    }

    // 0x8215a1. The EMITTER's material flag bit 0 chooses between the two particle effects, and
    // the reference stashes the choice in its own incoming a5 parameter slot -- which is why the
    // decompilation shows neither this nor the receiver of SetCurrent below.
    CShaderEffect* effect = (matFlags & 0x1)
        ? this->m_particleEffect
        : this->m_particleUnlitEffect;

    GxRsSet(GxRs_Texture0, texture);

    // Clear texture unit 1 WITHOUT going through GxRsSet, which is what the reference does at
    // 0x8215cf: it marks the state dirty and assigns the value directly, skipping the
    // render-target refresh RsSet(void*) does on the way in. Transcribed rather than simplified,
    // because RsSet would also take the null through that branch and the two are not the same
    // call.
    CGxDevice* device = g_theGxDevicePtr;

    if (device && device->m_context) {
        CGxAppRenderState& rs = device->m_appRenderStates[GxRs_Texture1];

        if (rs.m_value != static_cast<void*>(nullptr)) {
            device->IRsDirty(GxRs_Texture1);
            rs.m_value = static_cast<void*>(nullptr);
        }
    }

    // DIVERGENCE: the reference calls this unconditionally. Frozen's effects come from
    // CShaderEffectManager::GetEffect, which returns null for an effect the shader list has not
    // been given rather than creating one, so a null here is a supported state (see the
    // declarations) and calling through it would fault on a machine missing the shader.
    if (effect) {
        effect->SetCurrent();
    }

    this->SetupLighting();
    this->SetupMaterial();

    // scene + 0xf4 is row 3 of m_viewInv -- the inverse view's translation, i.e. the camera
    // position in world space. The reference passes a pointer straight into the matrix.
    const C44Matrix& viewInv = this->m_scene->m_viewInv;
    C3Vector cameraPosition = { viewInv.d0, viewInv.d1, viewInv.d2 };

    this->SetupParticleTransform(cameraPosition);

    // m_curModel->m_particleRelative is null unless FUN_00824460 has put this model's particles in
    // some other space; see its declaration. The `1` is the batched bit Draw folds into
    // m_drawFlags, and it is 1 on this, the UNbatched path, because the bit means "the caller owns
    // the buffer" -- which is true here too.
    emitter->Draw(this->m_curModel->m_particleRelative, nullptr, 1);

    // Restore: the view and world transforms both go back to identity. SetupParticleTransform put
    // the view into camera-relative space, and leaving it there would carry into the next element.
    GxXformSetView(CM2SceneRender::s_identity);
    GxXformSet(GxXform_World, CM2SceneRender::s_identity);

    return 0;
}

// The sign of `(a - b) >> 2` over two handles (FUN_0047bf20); see M2CompareHandles in CM2Scene.cpp.
static int32_t M2CompareTextures(const void* a, const void* b) {
    return a < b ? -1 : a > b ? 1 : 0;
}

// ref: FUN_0081f800
// Two lightings close enough to share a draw: sun ambient and diffuse colours within 1/255.
static int32_t M2LightingNearlyEqual(const CM2Lighting* a, const CM2Lighting* b) {
    const float epsilon = 1.0f / 255.0f;

    return std::fabs(a->m_sunAmbient.x - b->m_sunAmbient.x) < epsilon
        && std::fabs(a->m_sunAmbient.y - b->m_sunAmbient.y) < epsilon
        && std::fabs(a->m_sunAmbient.z - b->m_sunAmbient.z) < epsilon
        && std::fabs(a->m_sunDiffuse.x - b->m_sunDiffuse.x) < epsilon
        && NearlyEqual(a->m_sunDiffuse.y, b->m_sunDiffuse.y, epsilon)
        && NearlyEqual(a->m_sunDiffuse.z, b->m_sunDiffuse.z, epsilon);
}

// ref: FUN_00821040
// One merged particle buffer to the device: the first emitter's texture and effect, the model's
// lighting and material, then the emitter's own submit.
void CM2SceneRender::SubmitParticleBatch(CM2ParticleEmitter* emitter, CGxBuf* buffer, EGxVertexBufferFormat format, uint32_t vertexCount, uint32_t indexCount) {
    CShaderEffect* effect = (emitter->m_materialFlags & 0x1) ? this->m_particleEffect : this->m_particleUnlitEffect;

    GxRsSet(GxRs_Texture0, TextureGetGxTex(emitter->m_texture, 0, nullptr));

    CGxDevice* device = g_theGxDevicePtr;

    if (device && device->m_context) {
        CGxAppRenderState& rs = device->m_appRenderStates[GxRs_Texture1];

        if (rs.m_value != static_cast<void*>(nullptr)) {
            device->IRsDirty(GxRs_Texture1);
            rs.m_value = static_cast<void*>(nullptr);
        }
    }

    if (effect) {
        effect->SetCurrent();
    }

    this->SetupLighting();
    this->SetupMaterial();

    emitter->SubmitDraw(buffer, format, static_cast<uint16_t>(vertexCount), indexCount);
}

// ref: FUN_00821100
// The batched particle draw. From the current element it takes every following particle element
// whose emitter agrees on blend, the three material bits and texture, fits the 1MB vertex and
// 0x20000 index budgets and, when lit, sits in near-equal lighting; draws them all into one
// stream buffer; and answers how many extra elements that consumed.
int32_t CM2SceneRender::DrawParticleBatch(uint32_t first, M2Element* elements, uint32_t* indices, uint32_t count) {
    auto& firstElement = elements[indices[first]];
    auto emitter = firstElement.emitter;

    EGxVertexBufferFormat format = (emitter->m_materialFlags & 0x1) ? GxVBF_PNCT : GxVBF_PCT;
    uint32_t stride = GxVertexBufferFormatSize(format);
    uint32_t maxVertices = 0x100000 / stride;

    uint32_t flags = emitter->m_materialFlags;
    int32_t blend = static_cast<int32_t>(emitter->m_blendMode);
    auto lighting = firstElement.model->m_currentLighting;
    auto texture = emitter->m_texture;

    auto budget = [](CM2ParticleEmitter* e, uint32_t& vertices, uint32_t& indices) {
        uint32_t live = e->m_liveIndices.Count();
        uint32_t fit = 0x4000 / e->m_verticesPerParticle;
        uint32_t drawn = live <= fit ? live : fit;

        vertices = e->m_verticesPerParticle * drawn;
        indices = e->m_indicesPerParticle * drawn;
    };

    uint32_t vertexCount;
    uint32_t indexCount;
    budget(emitter, vertexCount, indexCount);

    uint32_t batched = 1;

    for (uint32_t i = first + 1; i < count; i++) {
        auto& element = elements[indices[i]];

        if (element.type != 4) {
            break;
        }

        auto other = element.emitter;

        uint32_t otherVertices;
        uint32_t otherIndices;
        budget(other, otherVertices, otherIndices);

        if (static_cast<int32_t>(other->m_blendMode) != blend) {
            break;
        }

        uint32_t otherFlags = other->m_materialFlags;
        uint32_t lit = otherFlags & 0x1;

        if (lit != (flags & 0x1) || ((flags ^ otherFlags) & 0x2) || ((flags ^ otherFlags) & 0x4)
            || M2CompareTextures(other->m_texture, texture) != 0
            || maxVertices < otherVertices + vertexCount
            || 0x20000 < otherIndices + indexCount
            || (lit && !M2LightingNearlyEqual(element.model->m_currentLighting, lighting))) {
            break;
        }

        batched++;
        vertexCount += otherVertices;
        indexCount += otherIndices;
    }

    if (!vertexCount || !indexCount) {
        return 0;
    }

    C44Matrix savedView;
    GxXformView(savedView);

    const C44Matrix& viewInv = this->m_scene->m_viewInv;
    C3Vector cameraPosition = { viewInv.d0, viewInv.d1, viewInv.d2 };
    this->SetupParticleTransform(cameraPosition);

    auto buffer = GxBufStream(GxPoolTarget_Vertex, stride, vertexCount);
    auto vertices = GxBufLock(buffer);

    uint32_t submittedVertices = 0;
    uint32_t submittedIndices = 0;
    CM2ParticleEmitter* last = emitter;

    for (uint32_t i = 0; i < batched; i++) {
        last = elements[indices[first + i]].emitter;
        last->Draw(this->m_curModel->m_particleRelative, vertices, i == 0);

        uint32_t drawnVertices = last->m_verticesPerParticle * last->m_drawnCount;
        submittedVertices += drawnVertices;
        vertices += drawnVertices * stride;
        submittedIndices += last->m_indicesPerParticle * last->m_drawnCount;
    }

    GxBufUnlock(buffer, 0);

    if (submittedVertices && submittedIndices) {
        this->SubmitParticleBatch(last, buffer, format, submittedVertices, submittedIndices);
    }

    GxXformSetView(savedView);

    return batched - 1;
}

// ref: FUN_00820f40
// One ribbon element. Short, because the geometry and all the render state live in
// CM2Ribbon::Draw -- this only sets up what the shared Setup* helpers need and then hands over.
//
// The effect is the particle UNLIT one, unconditionally, where DrawParticle chooses between the
// lit and unlit pair on its emitter's material flag. A ribbon does not choose: its own per-material
// bit 0 drives emissive and the lighting state inside CM2Ribbon::Draw, so the effect it draws
// through is always the unlit variant. Read off the receiver at 0x820f8f, which the decompilation
// drops because it is a register argument.
//
// NOT PORTED from it: the leading SysMsgPrintf. Its target FUN_005eeb70 is a bare `retl` in the
// reference -- a trace hook compiled out -- so there is nothing behind it to reproduce.
void CM2SceneRender::DrawRibbon() {
    uint32_t index = this->m_curElement->index;

    if (index >= static_cast<uint32_t>(this->m_data->ribbons.Count())
            || !this->m_curModel->m_ribbonEmitters) {
        return;
    }

    const M2Ribbon& file = this->m_data->ribbons[index];

    // The FIRST material pass decides what the shared Setup* helpers see. CM2Ribbon::Draw walks
    // all of them itself afterwards; this one is only for the setup.
    //
    // Gated on Count() because element 0 of an EMPTY M2Array is a wild pointer, not null.
    if (!file.materialIndices.Count()) {
        return;
    }

    this->m_curMaterial = &this->m_data->materials[file.materialIndices[0]];

    // DIVERGENCE, the same one DrawParticle carries: the reference calls SetCurrent
    // unconditionally, and frozen's effects come from a lookup that returns null for an effect the
    // shader list has not been given.
    if (this->m_particleUnlitEffect) {
        this->m_particleUnlitEffect->SetCurrent();
    }

    this->SetupTextures();
    this->SetupLighting();
    this->SetupMaterial();

    // A ZERO camera position, not the real one -- the reference builds three zeroes on the stack
    // and passes those. The ribbon's geometry is already world-space and its own Draw subtracts the
    // origin it is stored relative to, so there is nothing for this to re-centre.
    C3Vector cameraPosition = { 0.0f, 0.0f, 0.0f };

    this->SetupParticleTransform(cameraPosition);
    CShaderEffect::SetTexMtx_Identity(0);

    this->m_curModel->m_ribbonEmitters[index]->Draw(this->m_curModel->m_particleRelative);

    // Put the world transform back to identity. CM2Ribbon::Draw pushed and popped its own, and this
    // SETS the level the pop returned to -- the reference's tail at 0x820ff0 is a device XformSet
    // plus the same dirty-flag and level bookkeeping GxXformSet does, against the identity matrix
    // at 0x00af58a8. frozen already spells this exact pair elsewhere in this file.
    GxXformSet(GxXform_World, CM2SceneRender::s_identity);
}

// ref: FUN_0081f700
void CM2SceneRender::SetBatchVertices(int32_t a2) {
    if (CShaderEffect::s_enableShaders) {
        if (this->m_curType != this->m_prevType || this->m_curShared != this->m_prevShared) {
            this->m_curShared->SetVertices(0);
        }
    } else {
        // TODO
        // - non-shader code path
    }
}

// Its 56% call-order recall is one missing block, not scatter: the reference ends this function by
// setting a USER CLIP PLANE, and frozen does not. Written down 2026-09-23 with the whole chain, so
// the next attempt starts at the top of it rather than the bottom.
//
// The reference, from 0x0081fd5e:
//
//     if (m_curElement->flags & 0x2) {            // M2UseClipPlanes
//         C4Plane p = m_curLighting->m_liquidPlane;
//         if (<device +0x1b4> && <global 0xd43020>)  // a transform step, not decoded
//             ...
//         if (m_curPass == 2)                     // negate all four components
//             p = -p;
//         GxDevice->ClipPlaneSet(0, &p);
//         if (<device +0xf58>) GxRsSet(GxRs_ClipPlaneMask, 1);
//     } else {
//         if (<device +0xf58>) GxRsSet(GxRs_ClipPlaneMask, 0);   // 0x0081fe4d
//     }
//
// Four pieces, and only two exist. CGxDevice::ClipPlaneSet and IStateSyncClipPlanes landed
// 2026-09-23, and GxRs_ClipPlaneMask reaches D3DRS_CLIPPLANEENABLE as of the same day. This block
// is the third.
//
// The fourth is the reason not to write this block yet: CM2Lighting::m_liquidPlane is DECLARED AND
// NEVER WRITTEN. The liquid-plane work that would fill it is a TODO in CM2Scene::Animate -- see the
// note there about CM2Lighting flag 0x40 never being set -- so porting this would clip every
// flagged element against a plane of all zeros. Start at Animate.
//
// Also unidentified: the device fields at +0x1b4 and +0xf58 that gate the transform and the mask.
void CM2SceneRender::SetupLighting() {
    if (this->m_curMaterial->flags & 0x1) {
        this->m_curShaded = 0;
    } else {
        this->m_curShaded = CM2SceneRender::s_shadedList[this->m_curMaterial->blendMode];
    }

    CShaderEffect::SetLocalLighting(this->m_curLighting, this->m_curShaded, 0);

    CM2SceneRender::s_curElementDword3c = this->m_curElement->dword3c;

    if ((this->m_curMaterial->flags & 0x2) || this->m_curLighting->m_fogScale <= 0.0f) {
        this->m_curFogMode = 0;
    } else {
        this->m_curFogMode = CM2SceneRender::s_fogModeList[this->m_curMaterial->blendMode];
    }

    if (this->m_curFogMode == 0) {
        CShaderEffect::SetFogEnabled(0);
    } else {
        CImVector fogColor;

        switch (this->m_curFogMode) {
            case 1: {
                float x = this->m_curLighting->m_fogColor.x;
                float y = this->m_curLighting->m_fogColor.y;
                float z = this->m_curLighting->m_fogColor.z;

                fogColor.b = z <= 0.0f ? 0x00 : z >= 1.0f ? 0xFF : CMath::fuint_n(z * 255.0f);
                fogColor.g = y <= 0.0f ? 0x00 : y >= 1.0f ? 0xFF : CMath::fuint_n(y * 255.0f);
                fogColor.r = x <= 0.0f ? 0x00 : x >= 1.0f ? 0xFF : CMath::fuint_n(x * 255.0f);
                fogColor.a = 0xFF;

                break;
            }

            case 2: {
                fogColor = { 0x00, 0x00, 0x00, 0x00 };
                break;
            }

            case 3: {
                fogColor = { 0xFF, 0xFF, 0xFF, 0x00 };
                break;
            }

            case 4: {
                fogColor = { 0x80, 0x80, 0x80, 0x00 };
                break;
            }
        }

        CShaderEffect::SetFogParams(
            this->m_curLighting->m_fogStart,
            this->m_curLighting->m_fogEnd,
            this->m_curLighting->m_fogDensity,
            fogColor
        );

        CShaderEffect::SetFogEnabled(1);
    }

    if (
        this->m_curType != this->m_prevType
        || this->m_curModel != this->m_prevModel
        || this->m_curLighting != this->m_prevLighting
        || ((this->m_curElement->flags & 0x2) != (this->m_prevElement->flags & 0x2))
    ) {
        // 0x0081fd5e..0x0081fe74. An element flagged 0x2 belongs to a model straddling a water
        // surface: it is clipped against the lighting's liquid plane, already in camera space
        // (CM2Lighting::CameraSpace). With shaders on, a D3D-convention device takes user clip
        // planes in CLIP space, so the plane goes through matrix0 -- the inverse-transpose of the
        // projection, set by Draw -- first; the reference gates that on the device's +0x1b4, a
        // second copy of the API id that is 0 only for the legacy OpenGL device, and on
        // DAT_00d43020, the shaders flag. The pass that draws what is under the surface (2)
        // keeps the other side, so the plane is negated there.
        if (this->m_curElement->flags & 0x2) {
            C4Vector plane = {
                this->m_curLighting->m_liquidPlane.n.x,
                this->m_curLighting->m_liquidPlane.n.y,
                this->m_curLighting->m_liquidPlane.n.z,
                this->m_curLighting->m_liquidPlane.d
            };

            if (g_theGxDevicePtr->m_api != GxApi_OpenGl && CShaderEffect::s_enableShaders) {
                C4Vector clip;
                TransformVector4(&clip, plane, this->matrix0);
                plane = clip;
            }

            if (this->m_curPass == 2) {
                plane.x = -plane.x;
                plane.y = -plane.y;
                plane.z = -plane.z;
                plane.w = -plane.w;
            }

            C4Plane clipPlane;
            clipPlane.n = { plane.x, plane.y, plane.z };
            clipPlane.d = plane.w;

            g_theGxDevicePtr->ClipPlaneSet(0, &clipPlane);
            GxRsSet(GxRs_ClipPlaneMask, 1);
        } else {
            GxRsSet(GxRs_ClipPlaneMask, 0);
        }
    }
}

void CM2SceneRender::SetupMaterial() {
    if (
        this->m_curType != this->m_prevType
        || this->m_curMaterial != this->m_prevMaterial
        || (this->m_curElement->flags & 0x1) != (this->m_prevElement->flags & 0x1)
        || this->m_curElement->alpha != this->m_prevElement->alpha
    ) {
        EGxBlend blendingMode = this->m_curElement->flags & 0x1
            ? GxBlend_AlphaKey
            : CM2SceneRender::s_gxBlend[this->m_curPass][this->m_curMaterial->blendMode];

        int32_t colorWrite = (this->m_curElement->flags & 0x1)
            ? 0
            : 15;

        GxRsSet(GxRs_ColorWrite, colorWrite);
        GxRsSet(GxRs_BlendingMode, blendingMode);

        float alphaRef;
        if (this->m_curMaterial->blendMode == 0) {
            alphaRef = 0.0f;
        } else if (this->m_curMaterial->blendMode == 1) {
            alphaRef = this->m_curElement->alpha * 0.87843138f;
        } else {
            alphaRef = 0.0039215689f;
        }

        CShaderEffect::SetAlphaRef(alphaRef);
    }

    if (
        this->m_curType != this->m_prevType
        || this->m_curMaterial != this->m_prevMaterial
    ) {
        int32_t culling = (this->m_curMaterial->flags & 0x4) == 0;
        GxRsSet(GxRs_Culling, culling);

        int32_t depthTest = (this->m_curMaterial->flags & 0x8) == 0;
        GxRsSet(GxRs_DepthTest, depthTest);

        int32_t depthWrite = (this->m_curMaterial->flags & 0x10) == 0;
        GxRsSet(GxRs_DepthWrite, depthWrite);
    }

    if (!CShaderEffect::s_enableShaders) {
        // TODO
    }

    if (this->m_curElement->type > 2) {
        if (
            this->m_curType != this->m_prevType
            || this->m_curElement->alpha != this->m_prevElement->alpha
        ) {
            C4Vector diffuse = { 1.0f, 1.0f, 1.0f, this->m_curElement->alpha };
            CShaderEffect::SetDiffuse(diffuse);

            C4Vector emissive = { 0.0f, 0.0f, 0.0f, 0.0f };
            CShaderEffect::SetEmissive(emissive);
        }
    } else if (
        this->m_curType != this->m_prevType
        || this->m_curShared != this->m_prevShared
        || this->m_curShaded != this->m_prevShaded
        || this->m_curMaterial->blendMode != this->m_prevMaterial->blendMode
        || this->m_prevBatch == nullptr
        || this->m_curBatch->colorIndex != this->m_prevBatch->colorIndex
        || this->m_prevElement->alpha != this->m_curElement->alpha
        || this->m_curModel->m_currentDiffuse != this->m_prevModel->m_currentDiffuse
        || this->m_curModel->m_currentEmissive != this->m_prevModel->m_currentEmissive
    ) {
        if (this->m_curMaterial->blendMode == M2BLEND_MOD) {
            C4Vector diffuse = { 0.0f, 0.0f, 0.0f, this->m_curElement->alpha };
            CShaderEffect::SetDiffuse(diffuse);

            C4Vector emissive = { 1.0f, 1.0f, 1.0f, 0.0f };
            CShaderEffect::SetEmissive(emissive);
        } else if (this->m_curMaterial->blendMode == M2BLEND_MOD_2X) {
            C4Vector diffuse = { 0.0f, 0.0f, 0.0f, this->m_curElement->alpha };
            CShaderEffect::SetDiffuse(diffuse);

            C4Vector emissive = { 0.5f, 0.5f, 0.5f, 0.0f };
            CShaderEffect::SetEmissive(emissive);
        } else {
            auto modelDiffuse = this->m_curModel->m_currentDiffuse;
            auto modelEmissive = this->m_curModel->m_currentEmissive;

            if (this->m_curBatch->colorIndex < this->m_data->colors.Count()) {
                auto& modelColor = this->m_curModel->m_colors[this->m_curBatch->colorIndex];


                modelDiffuse.x *= modelColor.colorTrack.currentValue.x;
                modelDiffuse.y *= modelColor.colorTrack.currentValue.y;
                modelDiffuse.z *= modelColor.colorTrack.currentValue.z;
            }

            if (!this->m_curShaded) {
                modelEmissive.x += modelDiffuse.x;
                modelEmissive.y += modelDiffuse.y;
                modelEmissive.z += modelDiffuse.z;

                modelDiffuse.x = 0.0f;
                modelDiffuse.y = 0.0f;
                modelDiffuse.z = 0.0f;
            }

            C4Vector diffuse = { modelDiffuse.x, modelDiffuse.y, modelDiffuse.z, this->m_curElement->alpha };
            CShaderEffect::SetDiffuse(diffuse);

            C4Vector emissive = { modelEmissive.x, modelEmissive.y, modelEmissive.z, 0.0f };
            CShaderEffect::SetEmissive(emissive);
        }
    }
}

void CM2SceneRender::SetupTextures() {
    if (this->m_curType > 2) {
        for (int32_t i = 0; i < 2; i++) {
            // THE CAST IS LOAD-BEARING and its absence was a crash. A bare 0 picks the int32_t
            // overload of GxRsSet, and CGxStateBom's value is a union of `int32_t i[3]` with a
            // `void* p` -- so assigning an int writes FOUR bytes and leaves bytes 4 through 7 of
            // an eight-byte pointer exactly as they were.
            //
            // A texture slot that had held a real CGxTex* therefore kept its HIGH half and lost
            // its low half: 0x00000211777C01D0 became 0x0000021100000000. The device faulted on
            // that in ITexMarkAsUpdated the next time it synced the stage. Found 2026-09-27 from
            // tools/crashstack.py, whose registers showed exactly that pair of values.
            //
            // This branch is the one every element type above 2 takes -- ribbons and particles --
            // and it clears BOTH stages while those draws bind only stage 0, so stage 1 kept the
            // corrupt pointer. The loop at the bottom of this function always spelled the cast
            // out; only this one did not.
            GxRsSet(static_cast<EGxRenderState>(GxRs_Texture0 + i), static_cast<CGxTex*>(nullptr));
            CShaderEffect::SetTexMtx_Identity(i);
        }

        return;
    }

    uint32_t textureCount = this->m_curBatch->textureCount;
    int32_t v19 = 1;

    if (!CShaderEffect::s_enableShaders) {
        // Without shaders these batches collapse to a single texture stage
        if (this->m_curBatch->shader & 0x8000) {
            textureCount = 1;
        }

        v19 = 0;
    } else if (!(this->m_curBatch->shader & 0x4000) || textureCount != 1) {
        v19 = 0;
    }

    // Hoisted out of the loop: one lookup per batch rather than per stage. See
    // CM2Shared::TextureCombos for why these do not come straight off m_data any more.
    uint32_t comboCount = 0;
    uint32_t transformComboCount = 0;

    uint16_t* textureCombos = this->m_curModel->m_shared->TextureCombos(&comboCount);
    uint16_t* textureTransformCombos =
        this->m_curModel->m_shared->TextureTransformCombos(&transformComboCount);

    // The reference's shape (0x0081f4c9..0x0081f60b): every stage the batch NAMES, however many
    // that is, then the stages below two left empty. Frozen walked exactly two and blanked the
    // extras inside the loop, which would also have dropped a third or fourth texture.
    uint32_t i = 0;

    for (; i < textureCount; i++) {
        auto textureIndex = textureCombos[this->m_curBatch->textureComboIndex + i];
        auto textureHandle = textureIndex < this->m_data->textures.Count()
            ? this->m_curModel->m_textures[textureIndex]
            : nullptr;
        auto texture = textureHandle
            ? TextureGetGxTex(textureHandle, 1, nullptr)
            : nullptr;

        if (texture) {
            uint16_t textureFlags = this->m_data->textures[textureIndex].flags;

            EGxTexWrapMode wrapU = textureFlags & 0x1 ? GxTex_Wrap : GxTex_Clamp;
            EGxTexWrapMode wrapV = textureFlags & 0x2 ? GxTex_Wrap : GxTex_Clamp;

            GxTexSetWrap(texture, wrapU, wrapV);
        }

        GxRsSet(static_cast<EGxRenderState>(GxRs_Texture0 + i), texture);

        auto textureTransformIndex = textureTransformCombos[this->m_curBatch->textureTransformComboIndex + i];

        // The stage's combiner nibble: shift 4 for the first stage, 0 for the second. The
        // reference keeps the shift in a byte it decrements by four, masked to five bits.
        uint32_t stageShift = (4u - 4u * i) & 0x1f;

        uint32_t v21 = v19 == 0 ? i : 1;

        if (!(this->m_curBatch->shader & 0x8000) && ((this->m_curBatch->shader >> stageShift) & M2COMBINER_ENVMAP)) {
            CShaderEffect::SetTexMtx_SphereMap(v21);
        } else if (textureTransformIndex < this->m_data->textureTransforms.Count()) {
            CShaderEffect::SetTexMtx(this->m_curModel->m_textureMatrices[textureTransformIndex], v21);
        } else {
            CShaderEffect::SetTexMtx_Identity(v21);
        }
    }

    for (; i < 2; i++) {
        GxRsSet(static_cast<EGxRenderState>(GxRs_Texture0 + i), static_cast<CGxTex*>(nullptr));
    }
}
