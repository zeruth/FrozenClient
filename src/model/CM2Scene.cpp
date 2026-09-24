#include <cstdio>
#include "model/CM2Scene.hpp"
#include "gx/shader/CShaderEffect.hpp"
#include "gx/shader/CShaderEffectManager.hpp"
#include "gx/Shader.hpp"
#include "gx/Transform.hpp"
#include "model/CM2Cache.hpp"
#include "model/CM2Light.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2SceneRender.hpp"
#include "model/CM2ParticleEmitter.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Internal.hpp"
#include "model/M2Sort.hpp"
#include <algorithm>
#include <cassert>
#include <tempest/Math.hpp>

uint32_t CM2Scene::s_optFlags = 0xFFFFFFFF;

void CM2Scene::AnimateThread(void* arg) {
    // TODO
}

// ref: FUN_0081f1d0
void CM2Scene::ComputeElementShaders(M2Element* element) {
    auto model = element->model;
    auto batch = element->batch;
    auto material = &model->m_shared->m_data->materials[batch->materialIndex];
    auto lighting = model->m_currentLighting;

    int32_t shaded;
    int32_t lightCount;

    if (material->flags & 0x1 || CM2SceneRender::s_shadedList[material->blendMode] == 0) {
        shaded = 0;
        lightCount = material->flags & 0x1 ? 0 : lighting->m_lightCount;
    } else {
        shaded = 1;
        lightCount = lighting->m_lightCount;
    }

    int32_t boneInfluences = element->skinSection->boneInfluences;

    int32_t v18;
    if (material->blendMode == M2BLEND_OPAQUE) {
        v18 = 0;
    } else if (material->blendMode == M2BLEND_ALPHA_KEY) {
        v18 = CMath::fuint(element->alpha * 224.0f);
    } else {
        v18 = 1;
    }

    int32_t v8 = 0;
    if (!(material->flags & 0x1) && !(material->flags & 0x100) && lighting->m_flags & 0x10) {
        // TODO
        // v8 = Sub873FF0();

        if (v8) {
            if (lighting->m_flags & 0x8) {
                v8 = 1;
            }

            if (element->type == 1) {
                v8 = 0;
            }
        }
    }

    int32_t v9 = v18 && (/* TODO !GxCaps().dword130 ||*/ v8);
    int32_t v10 = std::min(boneInfluences, 2);
    int32_t v11 = std::min(v8, 2);

    element->vertexPermute = shaded + 2 * (v11 + v10 + 2 * v11 + lightCount + 4 * (v11 + v10 + 2 * v11));
    element->pixelPermute = v8 + 4 * (CShaderEffect::s_usePcfFiltering + 2 * v9);

    // TODO
    // element->dword3C = v8;
}

int32_t CM2Scene::SortOpaque(uint32_t a, uint32_t b, const void* userArg) {
    auto elements = static_cast<const CM2Scene*>(userArg)->m_elements.Ptr();
    auto elementA = const_cast<M2Element*>(&elements[a]);
    auto elementB = const_cast<M2Element*>(&elements[b]);

    if (elementA->type < elementB->type) {
        return -1;
    }

    if (elementA->type > elementB->type) {
        return 1;
    }

    switch (elementA->type) {
        case 0:
        case 1:
            return CM2Scene::SortOpaqueGeoBatches(elementA, elementB);

        case 3:
            return CM2Scene::SortOpaqueRibbons(elementA, elementB);

        case 4:
            return CM2Scene::SortOpaqueParticles(elementA, elementB);

        default:
            return 0;
    }
}

int32_t CM2Scene::SortOpaqueGeoBatches(M2Element* elementA, M2Element* elementB) {
    auto modelA = elementA->model;
    auto dataA = modelA->m_shared->m_data;
    auto batchA = elementA->batch;
    auto modelB = elementB->model;
    auto dataB = modelB->m_shared->m_data;
    auto batchB = elementB->batch;

    if (elementA->type == 0) {
        if (batchA->materialLayer < batchB->materialLayer) {
            return -1;
        }

        if (batchA->materialLayer > batchB->materialLayer) {
            return 1;
        }

        if (elementA->effect && elementB->effect) {
            auto effectA = elementA->effect;
            auto effectB = elementB->effect;
            auto vertexShaderA = effectA->m_vertexShaders[elementA->vertexPermute];
            auto pixelShaderA = effectA->m_pixelShaders[elementA->pixelPermute];
            auto vertexShaderB = effectB->m_vertexShaders[elementB->vertexPermute];
            auto pixelShaderB = effectB->m_pixelShaders[elementB->pixelPermute];

            if (vertexShaderA < vertexShaderB) {
                return -1;
            }

            if (vertexShaderA > vertexShaderB) {
                return 1;
            }

            if (pixelShaderA < pixelShaderB) {
                return -1;
            }

            if (pixelShaderA > pixelShaderB) {
                return 1;
            }
        }

        if (modelA->m_shared < modelB->m_shared) {
            return -1;
        }

        if (modelA->m_shared > modelB->m_shared) {
            return 1;
        }

        if ((elementA->flags & 0x4) < (elementB->flags & 0x4)) {
            return -1;
        }

        if ((elementA->flags & 0x4) > (elementB->flags & 0x4)) {
            return 1;
        }

        if (modelA < modelB) {
            return -1;
        }

        if (modelA > modelB) {
            return 1;
        }

        if (elementA->skinSection->boneComboIndex < elementB->skinSection->boneComboIndex) {
            return -1;
        }

        if (elementA->skinSection->boneComboIndex > elementB->skinSection->boneComboIndex) {
            return 1;
        }
    }

    auto materialA = &dataA->materials[batchA->materialIndex];
    auto materialB = &dataB->materials[batchB->materialIndex];

    if (materialA->blendMode < materialB->blendMode) {
        return -1;
    }

    if (materialA->blendMode > materialB->blendMode) {
        return 1;
    }

    if ((materialA->flags & 0x1F) < (materialB->flags & 0x1F)) {
        return -1;
    }

    if ((materialA->flags & 0x1F) > (materialB->flags & 0x1F)) {
        return 1;
    }

    if (batchA->textureCount > 0 && batchB->textureCount > 0) {
        for (int32_t i = 0; i < std::min(batchA->textureCount, batchB->textureCount); i++) {
            auto textureIndexA = dataA->textureCombos[batchA->textureComboIndex];
            auto textureA = textureIndexA >= dataA->textures.Count() ? 0 : reinterpret_cast<intptr_t>(modelA->m_textures[textureIndexA]);
            auto textureIndexB = dataB->textureCombos[batchB->textureComboIndex];
            auto textureB = textureIndexB >= dataB->textures.Count() ? 0 : reinterpret_cast<intptr_t>(modelB->m_textures[textureIndexB]);

            if ((textureA - textureB) / sizeof(void*) < 0) {
                return -1;
            }

            if ((textureA - textureB) / sizeof(void*) > 0) {
                return 1;
            }
        }
    }

    if (batchA->textureCount < batchB->textureCount) {
        return -1;
    }

    if (batchA->textureCount > batchB->textureCount) {
        return 1;
    }

    if (batchA < batchB)  {
        return -1;
    }

    return batchA > batchB;
}

int32_t CM2Scene::SortOpaqueParticles(M2Element* elementA, M2Element* elementB) {
    // TODO
    return 0;
}

int32_t CM2Scene::SortOpaqueRibbons(M2Element* elementA, M2Element* elementB) {
    // TODO
    return 0;
}

int32_t CM2Scene::SortTransparent(uint32_t a, uint32_t b, const void* userArg) {
    auto elements = static_cast<const CM2Scene*>(userArg)->m_elements.Ptr();
    auto elementA = const_cast<M2Element*>(&elements[a]);
    auto elementB = const_cast<M2Element*>(&elements[b]);

    if (elementA->float10 > elementB->float10) {
        return -1;
    }

    if (elementA->float10 < elementB->float10) {
        return 1;
    }

    if ((elementA->flags & 0x1) > (elementB->flags & 0x1)) {
        return -1;
    }

    if ((elementA->flags & 0x1) < (elementB->flags & 0x1)) {
        return 1;
    }

    if (elementA->priorityPlane < elementB->priorityPlane) {
        return -1;
    }

    if (elementA->priorityPlane > elementB->priorityPlane) {
        return 1;
    }

    if (elementA->float14 > elementB->float14) {
        return -1;
    }

    if (elementA->float14 < elementB->float14) {
        return 1;
    }

    if ((CM2Scene::s_optFlags & 0x4000)
        && (elementA->type != elementB->type || elementA->model != elementB->model)
        && elementA->effect
        && elementB->effect
    ) {
        auto effectA = elementA->effect;
        auto effectB = elementB->effect;
        auto vertexShaderA = effectA->m_vertexShaders[elementA->vertexPermute];
        auto pixelShaderA = effectA->m_pixelShaders[elementA->pixelPermute];
        auto vertexShaderB = effectB->m_vertexShaders[elementB->vertexPermute];
        auto pixelShaderB = effectB->m_pixelShaders[elementB->pixelPermute];

        if (vertexShaderA < vertexShaderB) {
            return -1;
        }

        if (vertexShaderA > vertexShaderB) {
            return 1;
        }

        if (pixelShaderA < pixelShaderB) {
            return -1;
        }

        if (pixelShaderA > pixelShaderB) {
            return 1;
        }
    }

    if (elementA->model < elementB->model) {
        return -1;
    }

    if (elementA->model > elementB->model) {
        return 1;
    }

    if (elementA->type < elementB->type) {
        return -1;
    }

    if (elementA->type > elementB->type) {
        return 1;
    }

    if (elementA->type <= 2) {
        if (elementA->batch->materialLayer < elementB->batch->materialLayer) {
            return -1;
        }

        if (elementA->batch->materialLayer > elementB->batch->materialLayer) {
            return 1;
        }
    }

    if (!(CM2Scene::s_optFlags & 0x4000) || !elementA->effect || !elementB->effect) {
        return CM2Scene::SortOpaque(a, b, userArg);
    }

    auto effectA = elementA->effect;
    auto effectB = elementB->effect;
    auto vertexShaderA = effectA->m_vertexShaders[elementA->vertexPermute];
    auto pixelShaderA = effectA->m_pixelShaders[elementA->pixelPermute];
    auto vertexShaderB = effectB->m_vertexShaders[elementB->vertexPermute];
    auto pixelShaderB = effectB->m_pixelShaders[elementB->pixelPermute];

    if (vertexShaderA < vertexShaderB) {
        return -1;
    }

    if (vertexShaderA > vertexShaderB) {
        return 1;
    }

    if (pixelShaderA < pixelShaderB) {
        return -1;
    }

    if (pixelShaderA > pixelShaderB) {
        return 1;
    }

    return CM2Scene::SortOpaque(a, b, userArg);
}

void CM2Scene::AdvanceTime(uint32_t a2) {
    this->m_time += a2;

    this->m_cache->UpdateShared();
    this->m_cache->GarbageCollect(0);

    this->m_flags |= 0x4;
    this->uint10 = a2;

    if (a2) {
        for (auto model = this->m_animateList; model; model = model->m_animateNext) {
            model->ProcessCallbacksRecursive();
        }
    }

    this->m_flags &= ~0x4;
}

void CM2Scene::Animate(const C3Vector& cameraPos) {
    this->uint14++;

    uint32_t optFlags = this->m_cache->m_flags & 0xE000;
    if (CM2Scene::s_optFlags != optFlags) {
        CM2Scene::s_optFlags = optFlags;
    }

    GxXformView(this->m_view);
    C3Vector invCameraPos = { -cameraPos.x, -cameraPos.y, -cameraPos.z };
    this->m_view.Translate(invCameraPos);
    this->m_viewInv = this->m_view.Inverse(this->m_view.Determinant());

    // This branch is unreachable today and must stay that way until CM2Cache::BeginThread is real.
    // The interleave below is not an optimisation that degrades gracefully: with no second thread,
    // walking two at a time simply leaves every other model un-animated. CM2Cache::Initialize
    // refuses to propagate the M2UseThreads CVar into this bit for that reason.
    if (this->m_cache->m_flags & 0x4) {
        // In multithreaded mode, iteration over the animate list is interleaved:
        // - the current thread animates entries 0, 2, 4, ...
        // - the newly created thread animates entries 1, 3, 5, ...

        this->m_cache->BeginThread(CM2Scene::AnimateThread, this);

        CM2Model* nextModel;
        for (auto model = this->m_animateList; model; model = nextModel->m_animateNext) {
            if (!model->m_attachParent) {
                if (model->m_flag1000) {
                    C3Vector v222 = { 0.0f, 0.0f, 0.0f };
                    C3Vector v218 = { 1.0f, 1.0f, 1.0f };

                    model->AnimateMTSimple(&this->m_view, v218, v222, 1.0f, 1.0f);
                } else {
                    C3Vector v220 = { 0.0f, 0.0f, 0.0f };
                    C3Vector v221 = { 1.0f, 1.0f, 1.0f };

                    model->AnimateMT(&this->m_view, v221, v220, 1.0f, 1.0f);
                }
            }

            nextModel = model->m_animateNext;
            if (!nextModel) {
                break;
            }
        }

        this->m_cache->WaitThread();
    } else {
        for (auto model = this->m_animateList; model; model = model->m_animateNext) {
            if (!model->m_attachParent) {
                if (model->m_flag1000 != 0) {
                    C3Vector v222 = { 0.0f, 0.0f, 0.0f };
                    C3Vector v218 = { 1.0f, 1.0f, 1.0f };

                    model->AnimateMTSimple(&this->m_view, v218, v222, 1.0f, 1.0f);
                } else {
                    C3Vector v220 = { 0.0f, 0.0f, 0.0f };
                    C3Vector v221 = { 1.0f, 1.0f, 1.0f };

                    model->AnimateMT(&this->m_view, v221, v220, 1.0f, 1.0f);
                }
            }
        }
    }

    for (auto model = this->m_animateList; model; model = model->m_animateNext) {
        if (!model->m_attachParent) {
            model->AnimateST();
        }
    }

    while (this->m_animateList) {
        // TODO
        // - this is clearing out the animate list; why? something must reattach things to it...
        auto model = this->m_animateList;
        this->m_animateList = model->m_animateNext;
        model->m_animatePrev = nullptr;
        model->m_animateNext = nullptr;

        model->SetupLighting();
    }

    this->array44.SetCount(0);
    for (int32_t i = 0; i < M2PASS_COUNT; i++) {
        this->array54[i].SetCount(0);
    }

    this->m_elements.SetCount(0);
    int32_t elementIndex = 0;

    // How many particle elements use an additive blend. WRITE-ONLY here for now, and that is
    // faithful rather than an oversight: the reference's builder only increments it too.
    //
    // What consumes it is the tail this function still marks TODO, read 2026-09-24. That tail is
    // not a sort at all -- it walks a FOURTH element list (the container at scene+0x44, count at
    // +0x48) and groups its entries through a 251-entry open-addressing hash table at 0x00d40da0,
    // memset to 0xff and keyed by FUN_0081cc50 of the element. Additive blending is
    // order-independent, so grouping by material beats sorting by depth.
    //
    // Frozen has no container at +0x44 and nothing fills one, so the count has nothing to size
    // yet. Do not delete it to silence the warning -- it is the reference's own bookkeeping, and
    // removing it would have to be put back.
    uint32_t additiveCount = 0;

    while (this->m_drawList) {
        auto model = this->m_drawList;
        this->m_drawList = model->m_drawNext;

        model->m_flag8 = 0;
        model->m_flag10000 = 0;
        model->m_drawPrev = nullptr;
        model->m_drawNext = nullptr;

        if (!model->IsDrawable(0, 0) || model->m_flag4000) {
            continue;
        }

        auto v19 = model->m_currentLighting;
        auto data = model->m_shared->m_data;
        auto v21 = v19->m_flags & 0x20;
        auto v22 = v19->m_flags & 0x40;

        // The liquid plane test, ported 2026-09-24 -- found from the particle emission, whose
        // pass-selection flag is this same v21.
        //
        // A model whose bounding sphere sits entirely below the water surface has its transparent
        // batches routed to the other pass. That is where the transparent block's under-liquid
        // order flip is actually decided, and it is PER MODEL rather than per camera, which the
        // render inventory's description from the draw side does not make obvious.
        //
        // Scope worth noticing: v21 feeds the routing for every transparent batch registered
        // below, not only for particles.
        //
        // The reference tests only when both bits are set, and the outcome is to clear v21; v22 is
        // left alone.
        if (v21 && v22) {
            const CAaBox& extent = data->bounds.extent;

            C3Vector centre = { (extent.t.x + extent.b.x) * 0.5f,
                                (extent.t.y + extent.b.y) * 0.5f,
                                (extent.t.z + extent.b.z) * 0.5f };

            // The radius is the authored one SCALED by the length of the placement's first row,
            // which is how a scaled model gets a correspondingly scaled bound. matrixF4 is the
            // same matrix the centre is transformed by below.
            const C44Matrix& placement = model->matrixF4;

            float scale = sqrtf(placement.a0 * placement.a0
                + placement.a1 * placement.a1
                + placement.a2 * placement.a2);

            float radius = scale * data->bounds.radius;

            C3Vector world = centre * placement;

            const C4Plane& plane = v19->m_liquidPlane;

            float distance = plane.n.x * world.x + plane.n.y * world.y + plane.n.z * world.z
                + plane.d;

            if (distance <= -radius) {
                v21 = 0;
            }
        }

        auto skinProfile = model->m_shared->skinProfile;
        auto v17 = (this->m_cache->m_flags & 0x1) == 0;

        int32_t v229;
        if (v17 || (model->m_flags & 0x1) != 0 || (v17 = (model->m_flag40) == 0, v229 = 1, v17)) {
            v229 = 0;
        }

        uint32_t batchCount;
        if (model->ptr2D0) {
            // TODO
            // batchCount = (model->ptr2D0 + 4);

            assert(false);
        } else {
            batchCount = skinProfile->batches.Count();
        }

        for (int32_t batchIndex = 0; batchIndex < batchCount; batchIndex++) {
            M2Batch* batch;
            M2SkinSection* skinSection;
            CShaderEffect* effect;
            int32_t v221;
            int32_t v222;

            if (model->ptr2D0) {
                // TODO
                // batch = &model->m_optGeo->batches[batchIndex];
                // skinSection = model->m_optGeo->skinSections[batch->skinSectionIndex];

                assert(false);
            } else {
                batch = &skinProfile->batches[batchIndex];
                skinSection = &model->m_shared->m_skinSections[batch->skinSectionIndex];

                // Skip if skin section isn't currently visible
                if (!model->m_skinSections[batch->skinSectionIndex]) {
                    continue;
                }
            }

            if (batch->shader == 0x8000) {
                continue;
            }

            float alpha = model->alpha19C;

            if (batch->colorIndex < data->colors.Count()) {
                auto& color = model->m_colors[batch->colorIndex];
                alpha *= color.alphaTrack.currentValue;
            }

            if (batch->textureCount) {
                auto& textureWeight = model->m_textureWeights[data->textureWeightCombos[batch->textureWeightComboIndex]];
                alpha *= textureWeight.weightTrack.currentValue;
            }

            if (alpha < 0.000099999997f) {
                continue;
            }

            M2Material* material = &data->materials[batch->materialIndex];

            auto v17 = (batch->flags & 0x4) == 0;
            if (v17 || (v17 = this->uint104 == 0, v222 = 1, v17)) {
                v222 = 0;
            }

            M2Material* layerMaterial = batch->materialLayer
                ? &data->materials[batch->materialIndex - batch->materialLayer]
                : &data->materials[batch->materialIndex];

            if (layerMaterial->blendMode > 1 || (v221 = 0, alpha < 0.99998999f)) {
                v221 = 1;
            }

            if (model->ptr2D0) {
                // TODO
                // effect = model->m_optGeo->effects[batchIndex];

                assert(false);
            } else {
                effect = model->m_shared->m_batchShaders[batchIndex];
            }

            if (!effect) {
                continue;
            }

            auto element = this->m_elements.New();

            if (v222) {
                element->type = 1;
            } else if (!model->IsBatchDoodadCompatible(batch) || v221) {
                element->type = 0;
            } else {
                element->type = 2;
            }

            element->model = model;

            element->flags = 0x0;
            if (v221 == 1 && v21 && v22 && !v222) {
                element->flags |= 0x2;
            }
            if (model->ptr2D0) {
                element->flags |= 0x4;
            }

            element->alpha = alpha;
            element->index = batchIndex;
            element->priorityPlane = batch->priorityPlane;
            element->batch = batch;
            element->skinSection = skinSection;
            element->effect = effect;

            CM2Scene::ComputeElementShaders(element);

            float v58;

            if (v221 < 1) {
                element->float14 = model->float88;
                v58 = model->float88;
            } else if (data->flags & 0x10) {
                element->float14 = (skinSection->sortCenterPosition * model->m_boneMatrices[skinSection->centerBoneIndex]).SquaredMag();
                v58 = model->float88;
            } else {
                // TODO other sort position logic

                v58 = model->float88;
            }

            element->float10 = v58;

            if (element->type == 2) {
                // TODO
            } else if (v221 == 1) {
                if (v222) {
                    if (v22) {
                        *this->array54[2].New() = elementIndex;
                    } else {
                        *this->array54[1].New() = elementIndex;
                    }
                } else {
                    if (v21) {
                        *this->array54[1].New() = elementIndex;
                    }

                    if (v22) {
                        *this->array54[2].New() = elementIndex;
                    }
                }
            } else {
                *this->array54[v221].New() = elementIndex;
            }

            elementIndex++;

            // The alpha-tested DEPTH PREPASS, ported from FUN_00821a20 at 0x008224f7. The gate was
            // already the reference's, condition for condition; only the body was missing, so
            // frozen laid no depth for alpha-tested geometry such as hair and foliage.
            //
            // It costs one extra draw per eligible batch and cannot darken anything: the gate
            // already excludes materials carrying the depth-write-disable bit, so the shaded
            // element that follows writes the same depth either way, and this pass writes no
            // colour. **Built, not seen running.**
            //
            // What the reference does here, read from 0x0082257f on 2026-09-23:
            //
            //     grow the element array by one
            //     copy elements[elementIndex - 1] into the new slot   (0x44 bytes, rep movsl x 0x11)
            //     newElement->flags |= 0x1
            //     if (<a>) *array54[1].New() = elementIndex;
            //     if (<b>) *array54[2].New() = elementIndex;
            //     elementIndex++;
            //
            // So the prepass element is a verbatim duplicate distinguished only by flag 0x1.
            // CM2SceneRender::SetupMaterial already does the rest: that flag selects alpha-key
            // blending with colour writes off. GxRs_ColorWrite reaches D3D as of 2026-09-23, so the
            // draw side is ready and this gather is the only thing still missing.
            //
            // Copy through the array rather than through a saved pointer: New() can reallocate, and
            // the reference re-reads the base for exactly that reason.
            //
            // Which list the duplicate joins is the reference's water-side pair, and those default
            // to the two lighting bits read above. The reference seeds them with v21 and v22 at
            // 0x00821cab and only refines them -- by testing the model's bounding sphere against
            // m_currentLighting->m_liquidPlane -- when BOTH are set. That test is no longer a
            // TODO; it was ported 2026-09-24. It still does not fire, because
            // CM2Lighting::Initialize sets 0x20 and nothing sets 0x40 or writes m_liquidPlane, so
            // the pair is (true, false) for every model and this reduces to array54[1]. That is
            // exactly what the main registration above does for v221 == 1, so the two agree today
            // by construction rather than by luck.
            if (v229 && !v222 && v221 >= 1 && !(material->flags & 0x10)) {
                auto prepass = this->m_elements.New();

                // Through the array, not through a saved pointer: New() can reallocate, which is
                // why the reference re-reads the base before its own copy at 0x0082258f.
                *prepass = this->m_elements[elementIndex - 1];
                prepass->flags |= 0x1;

                if (v21) {
                    *this->array54[1].New() = elementIndex;
                }

                if (v22) {
                    *this->array54[2].New() = elementIndex;
                }

                elementIndex++;
            }
        }

        // The particle elements. The reference runs this immediately after the batch loop and
        // before the ribbons, which is where it sits here.
        for (int32_t i = 0; i < data->particles.Count(); i++) {
            CM2ParticleEmitter* emitter = model->m_particleEmitters
                ? model->m_particleEmitters[i]
                : nullptr;

            // Frozen-only: null for an emitter type frozen does not build. The reference's
            // factory always builds something, so it dereferences unconditionally.
            if (!emitter) {
                continue;
            }

            // Four gates, in the reference's order.
            if (model->m_flag2000 && (emitter->m_flags & 0x200)) {
                continue;
            }

            if (emitter->m_flags & 0x2000000) {
                continue;
            }

            if (!model->m_particles[i].active) {
                continue;
            }

            if (!(model->float198 > 0.0001f)) {
                continue;
            }

            const M2Particle& file = data->particles[i];

            // NOT a camera distance, whatever the element field is called: the emitter's own
            // position in the model's space, squared. Transcribed; see the note at DrawParticle.
            C3Vector local = file.position * model->m_boneMatrices[file.boneIndex];

            float distance = local.x * local.x + local.y * local.y + local.z * local.z;

            this->AddParticleElement(emitter, model, distance, model->float198, v21,
                                     elementIndex, additiveCount);

            // Each child gets its own element, with the parent's distance and alpha.
            for (uint32_t c = 0; c < emitter->m_childCount; c++) {
                this->AddParticleElement(emitter->m_children[c], model, distance, model->float198,
                                         v21, elementIndex, additiveCount);
            }
        }

        // TODO
        // - ribbons

        // TODO
        // - draw callbacks
    }

    M2HeapSort(CM2Scene::SortOpaque, this->array54[0].Ptr(), this->array54[0].Count(), this);
    M2HeapSort(CM2Scene::SortTransparent, this->array54[1].Ptr(), this->array54[1].Count(), this);
    M2HeapSort(CM2Scene::SortTransparent, this->array54[2].Ptr(), this->array54[2].Count(), this);

    // TODO sort additive particles
}

// Register one emitter's particles as a draw element.
//
// Sets exactly the fields the reference sets and no more. priorityPlane, batch, skinSection,
// vertexPermute and the last dword are left ALONE, as they are there: the element allocator does
// not zero, so a recycled slot keeps the previous frame's values. That is faithful and harmless
// while DrawParticle is a stub, but it is the first thing to check when that stub is filled.
//
// The reference also caches emitter->[0x18] into the element's +0x24. What +0x18 holds is not
// identified, and frozen carries the emitter pointer in the element anyway, so that cache is not
// reproduced rather than filled with a guess.
//
// ref: FUN_00821930
void CM2Scene::AddParticleElement(CM2ParticleEmitter* emitter, CM2Model* model, float distance,
                                  float alpha, int32_t aboveLiquid, int32_t& elementIndex,
                                  uint32_t& additiveCount) {
    // Nothing alive anywhere in the subtree means nothing to draw.
    if (!emitter->HasLiveParticles()) {
        return;
    }

    // Emitters carrying spawned models draw through those models, not as a particle element.
    if (emitter->m_particleKind == 1) {
        return;
    }

    M2Element* element = this->m_elements.New();

    if (!element) {
        return;
    }

    element->type = 4;
    element->model = model;
    element->flags = 0x0;
    element->alpha = alpha;
    element->emitter = emitter;
    element->float10 = model->float88;
    element->float14 = distance;
    element->pixelPermute = 0;
    element->dword34 = 0xFFFFFFFF;
    element->dword38 = 0xFFFFFFFF;
    element->dword3c = 0;

    // Counted for the additive sort the tail of Animate still owes.
    if (emitter->m_blendMode == 10 || emitter->m_blendMode == 3) {
        additiveCount++;
    }

    // Pass 0 only for a blend that does not need sorting AND an alpha that is effectively 1
    // (0x00a45528 is 0.99999). Everything else is transparent, and the water side picks which of
    // the two transparent passes.
    if (emitter->m_blendMode <= 1 && alpha >= 0.9999899864196777f) {
        *this->array54[0].New() = elementIndex;
    } else if (!aboveLiquid || (emitter->m_flags & 0x40000)) {
        *this->array54[2].New() = elementIndex;
    } else {
        *this->array54[1].New() = elementIndex;
    }

    elementIndex++;
}

// ref: FUN_0081f8f0
CM2Model* CM2Scene::CreateModel(const char* file, uint32_t a3) {
    if (!file) {
        return nullptr;
    }

    CM2Shared* shared = this->m_cache->CreateShared(file, a3);
    if (!shared) {
        shared = this->m_cache->CreateShared("Spells\\ErrorCube.mdx", 0);
    }

    CM2Model* model = nullptr;

    if (shared) {
        model = CM2Model::AllocModel(g_modelPool);

        if (model) {
            if (!model->Initialize(this, shared, nullptr, a3)) {
                // TODO
            }
        }

        shared->Release();
    }

    return model;
}

int32_t CM2Scene::Draw(M2PASS pass) {
    // TODO
    // - conditional check on this->dword144

    if (CM2Scene::s_optFlags != (this->m_cache->m_flags & 0xE000)) {
        CM2Scene::s_optFlags = this->m_cache->m_flags & 0xE000;
    }

    CM2SceneRender render(this);


    render.Draw(pass, this->m_elements.m_data, this->array54[pass].m_data, this->array54[pass].Count());

    if (pass == M2PASS_0) {
        render.Draw(pass, this->m_elements.m_data, this->array44.m_data, this->array44.Count());
    }

    return 1;
}

int32_t CM2Scene::DrawShadowCasters(const C44Matrix& lightView) {
    // The reference loads this pair through the effect ShadowMapRenderSL, declared in the archive
    // file Shaders/Effects/ShadowMap.wfx as VertexShader(ShadowMap) + PixelShader(ShadowMapSL).
    // Frozen has no .wfx parser, so the indirection is resolved here and the two shader libraries are
    // requested by name directly; both ship in the reference archives with the same 90 / 16
    // permutation counts InitEffect already asks for, so the element's own permutation indices
    // select the matching program.
    static CShaderEffect* effect = nullptr;
    static bool tried = false;

    if (!tried) {
        tried = true;
        effect = CShaderEffectManager::GetEffect("ShadowMapShadowMapSL");

        if (!effect) {
            effect = CShaderEffectManager::CreateEffect("ShadowMapShadowMapSL");
            effect->InitEffect("ShadowMap", "ShadowMapSL");
        }
    }

    if (!effect || !CShaderEffect::s_enableShaders) {
        return 0;
    }

    C44Matrix rebase = this->m_viewInv * lightView;

    // The shadow map pixel shader multiplies the light-space z it receives by c0.w, which is the
    // reciprocal of the far plane; writer and reader have to agree on that constant.
    C4Vector depthScale = { 0.0f, 0.0f, 0.0f, 1.0f / 4000.0f };
    GxShaderConstantsSet(GxSh_Pixel, 0, reinterpret_cast<float*>(&depthScale), 1);

    CM2SceneRender::s_shadowCasterEffect = effect;
    CM2SceneRender::s_shadowCasterRebase = &rebase;

    CM2SceneRender render(this);
    uint32_t casters = this->array54[M2PASS_0].Count();

    // Whether the map has any content at all is answerable without reading the texture back: if
    // nothing is submitted, the map is the white it was cleared to. Reported once so a run says
    // plainly whether shadows are being cast.
    static bool reported = false;

    if (!reported) {
        reported = true;
        fprintf(stderr, "MapShadow: caster pass submitting %u opaque model batches\n", casters);
    }

    render.Draw(M2PASS_0, this->m_elements.m_data, this->array54[M2PASS_0].m_data, casters);

    CM2SceneRender::s_shadowCasterEffect = nullptr;
    CM2SceneRender::s_shadowCasterRebase = nullptr;

    return 1;
}

// Feeds CM2Lighting with the lights that affect one model: the list walk below covers DIRECTIONAL
// lights and the hash-grid sweep after it covers POINT lights. It is worth writing down where the
// whole chain stands, because the pieces were ported from the wrong end.
//
// Local lights on a model take six steps to reach CM2Lighting, and all six are ported:
//
//   1. CM2Light::Initialize        stamps a new light one frame behind, so it reads as stale.
//   2. CM2Model's per-frame update positions each point light through its bone and m_viewInv,
//                                  and stamps it with the scene's counter.
//   3. CM2Light::Link              files it into the hash grid below; SetPosition re-files it.
//   4. CM2Scene::SelectLights      this function: sweeps the cells the model's sphere covers.
//   5. CM2Lighting::AddLight       keeps the four nearest, sorted.
//   6. CM2Lighting::CameraSpace    puts their positions in camera space.
//
// From there the lights leave by TWO separate doors, and both are now open:
//
//   shader          CShaderEffect::ComputeLocalLights packs them into eleven vertex constants
//                   at c17 -- colour, camera-space position and the three attenuation rows.
//   fixed function  CM2Lighting::SetupGxLights loads the device's four light slots, sun in slot
//                   0 as a directional light and up to three point lights after it, and
//                   CGxDeviceD3d::IStateSyncLights sends whatever changed to D3D. That door was
//                   walled up until 2026-09-23: CGxDevice had no light state of any kind, so
//                   SetupGxLights had nowhere to put anything and stayed a stub.
//
// Steps 5 and 6 landed first and sat inert for two cycles because 1 through 4 were empty branches.
// None of it has been seen running.
// ref: FUN_0081e400
void CM2Scene::SelectLights(CM2Lighting* lighting) {
    for (auto light = this->m_lightList; light; light = light->m_lightNext) {
        lighting->AddLight(light);
    }

    // Then the point lights, by sweeping every grid cell the model's bounding sphere touches. The
    // bounds are computed the reference's way -- the low edge takes minus a half and the high edge
    // plus a half BEFORE truncation, which widens the range by a cell on each side rather than
    // rounding to the nearest.
    const C3Vector& c = lighting->sphere4.c;
    float r = lighting->sphere4.r;

    int32_t xMin = static_cast<int32_t>((c.x - r) * 0.05f - 0.5f) & 0x3f;
    int32_t xMax = static_cast<int32_t>((c.x + r) * 0.05f + 0.5f) & 0x3f;
    int32_t yMin = static_cast<int32_t>((c.y - r) * 0.05f - 0.5f) & 0x3f;
    int32_t yMax = static_cast<int32_t>((c.y + r) * 0.05f + 0.5f) & 0x3f;

    // Both loops are do-while and both wrap, so a sphere straddling the fold still sweeps the
    // cells on each side of it instead of walking the whole grid backwards.
    int32_t x = xMin;

    for (;;) {
        int32_t y = yMin;

        for (;;) {
            CM2Light* light = this->m_lightGrid[(y << 6) + x];

            while (light) {
                // Taken before the test: switching a light off unlinks it and clears its next
                // pointer, so reading it afterwards would walk into a cleared node.
                CM2Light* next = light->m_lightNext;

                if (!light->m_scene || light->m_updateStamp == this->uint14) {
                    lighting->AddLight(light);
                } else {
                    // Nobody drove this light this frame, so it belongs to a model that stopped
                    // animating. The reference culls it here rather than anywhere else.
                    light->SetVisible(0);
                }

                light = next;
            }

            if (y == yMax) {
                break;
            }

            y = (y + 1) & 0x3f;
        }

        if (x == xMax) {
            break;
        }

        x = (x + 1) & 0x3f;
    }
}
