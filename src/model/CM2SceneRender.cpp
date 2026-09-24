#include "model/CM2SceneRender.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/Shader.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "model/CM2Cache.hpp"
#include "model/CM2Model.hpp"
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
            // TODO
            // this->m_curMaterial = this->dwordB8;
            this->m_data = element->model->m_shared->m_data;

            // TODO
            // this->m_cache->LinkToSharedUpdateList(this->m_curShared);

            // TYPE 4 IS NOW REACHABLE as of 2026-09-24: CM2Scene::AddParticleElement builds
            // particle elements and files them in the pass lists, so DrawParticle is a live stub
            // rather than a dead one. It returns 0, which is safe -- `i` is incremented by the
            // loop header and the return only adds an extra skip -- so the elements are visited
            // and nothing is drawn for them yet.
            //
            // The rest still holds. Type 0 is the only one that draws: type 2 needs
            // CM2Model::IsBatchDoodadCompatible, which always returns 0, and type 1 needs
            // CM2Scene::uint104, which is never written, so both fall through to 0. Types 3 and 5
            // need ribbon and draw-callback elements the gather does not build, so porting either
            // of those draws still means porting its gate first.
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
                    // TODO
                    // i += this->m_curElement->dword1C - 1;
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

    CShaderEffect::SetShaders(this->m_curElement->vertexPermute, this->m_curElement->pixelPermute);

    if (CShaderEffect::s_enableShaders) {
        auto skinSection = this->m_curSkinSection;

        CGxBatch batch;

        batch.m_primType = GxPrim_Triangles;
        batch.m_start = skinSection->indexStart;
        batch.m_count = skinSection->indexCount;
        batch.m_minIndex = skinSection->vertexStart;
        batch.m_maxIndex = skinSection->vertexStart + skinSection->vertexCount - 1;


        GxDraw(&batch, 1);
    } else if (v9) {
        // TODO
    } else {
        // TODO
    }
}

// The other half of the doodad-batch trap. This draws type 2 elements, and nothing produces them
// today because CM2Model::IsBatchDoodadCompatible returns 0 -- see the note there. If that changes
// while this is still empty, the merged batches stop drawing with nothing in the log.
void CM2SceneRender::DrawBatchDoodad(M2Element* elements, uint32_t* a3) {
    // TODO
}

void CM2SceneRender::DrawBatchProj() {
    // TODO
}

void CM2SceneRender::DrawCallback() {
    // TODO
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
// LIVE STUB as of 2026-09-24: the emission above now builds type-4 elements, so this is reached.
// Returning 0 is safe -- Draw increments its index in the loop header and this return only adds an
// extra skip -- so the elements are visited and nothing draws for them.
//
// Its own shape, read 2026-09-24 (it is FUN_008214e0, already linked through overrides.json):
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
// So the geometry lives in CM2ParticleEmitter, not here; this is the state around it. Porting it
// means the GX-layer helpers in step 4 first, which is a chain of its own -- and FUN_0097ea60
// would be dead until this calls it, so the two go together the way the emission and its builder
// did.
int32_t CM2SceneRender::DrawParticle(uint32_t a2, M2Element* elements, uint32_t* a4, uint32_t a5) {
    // TODO -- see the map above. Reached, draws nothing.
    return 0;
}

void CM2SceneRender::DrawRibbon() {
    // TODO
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

    // TODO
    // dwordD43010 = this->m_curElement->dword3C;

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
        if (this->m_curElement->flags & 0x2) {
            // TODO
            // - enable clip plane mask for liquid plane
        } else  {
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
            GxRsSet(static_cast<EGxRenderState>(GxRs_Texture0 + i), 0);
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

    for (int32_t i = 0; i < 2; i++) {
        if (i >= textureCount) {
            GxRsSet(static_cast<EGxRenderState>(GxRs_Texture0 + i), static_cast<CGxTex*>(nullptr));
            continue;
        }

        auto textureIndex = this->m_data->textureCombos[this->m_curBatch->textureComboIndex + i];
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

        auto textureTransformIndex = this->m_data->textureTransformCombos[this->m_curBatch->textureTransformComboIndex + i];
        auto stageShift = M2COMBINER_STAGE_SHIFT * (2 - (i + 1));

        uint32_t v21 = v19 == 0 ? i : 1;

        if (this->m_curBatch->shader & 0x8000 || !((this->m_curBatch->shader >> stageShift) & M2COMBINER_ENVMAP)) {
            if (textureTransformIndex >= this->m_data->textureTransforms.Count()) {
                CShaderEffect::SetTexMtx_Identity(v21);
            } else {
                CShaderEffect::SetTexMtx(this->m_curModel->m_textureMatrices[textureTransformIndex], v21);
            }
        } else {
            CShaderEffect::SetTexMtx_SphereMap(v21);
        }
    }
}
