#include <cstdio>
#include "model/CM2Scene.hpp"
#include <common/ObjectAlloc.hpp>
#include "util/Log.hpp"
#include "model/M2Model.hpp"
#include "gx/shader/CShaderEffect.hpp"
#include "gx/shader/CShaderEffectManager.hpp"
#include "gx/Shader.hpp"
#include "gx/Transform.hpp"
#include "model/CM2Cache.hpp"
#include "model/CM2Light.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2SceneRender.hpp"
#include "model/CM2ParticleEmitter.hpp"
#include "model/CM2Ribbon.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Internal.hpp"
#include "model/M2Sort.hpp"
#include "gx/buffer/Types.hpp"
#include <storm/Memory.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>
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
            uint32_t comboCountA = 0;
            auto textureIndexA = modelA->m_shared->TextureCombos(&comboCountA)[batchA->textureComboIndex];
            auto textureA = textureIndexA >= dataA->textures.Count() ? 0 : reinterpret_cast<intptr_t>(modelA->m_textures[textureIndexA]);
            uint32_t comboCountB = 0;
            auto textureIndexB = modelB->m_shared->TextureCombos(&comboCountB)[batchB->textureComboIndex];
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

// The blend each M2 blend index draws with, in a TRANSPARENT list. DAT_00a453cc.
//
// It is not the same map as M2ParticleBlendToGx: this one sends 0, 1 and 2 all to GxBlend_Alpha
// where that one distinguishes opaque, alpha-key and alpha. In a transparent list the distinction
// cannot arise -- an opaque element was filed in the opaque list -- so the collapse is free, and
// the only question ever asked of this table is whether the entry is additive.
//
// The reference's table repeats this run of seven at indices 7..13. Nothing here indexes past 6,
// because an M2 blend mode is 0..6, and what the second half is for is not established.
static const uint32_t M2_TRANSPARENT_BLEND[7] = {
    GxBlend_Alpha, GxBlend_Alpha, GxBlend_Alpha, GxBlend_NoAlphaAdd,
    GxBlend_Add, GxBlend_Mod, GxBlend_Mod2x
};

static bool M2BlendIsAdditive(uint32_t blendIndex) {
    if (blendIndex >= 7) {
        return false;
    }

    uint32_t gxBlend = M2_TRANSPARENT_BLEND[blendIndex];

    return gxBlend == GxBlend_Add || gxBlend == GxBlend_NoAlphaAdd;
}

// ref: FUN_0081ca80
// An ordering class for a particle emitter's material, built from the three low flag bits. Each
// bit contributes only when it is CLEAR, which is why the result reads inverted: the base is 5
// unless bit 0 is set, and bits 1 and 2 add 2 and 0x10 by being absent.
//
// It is called on the emitter's MATERIAL rather than on the emitter: the reference reads its
// argument's +4, and the call site hands it the {blend, flags} pair at emitter +0xd0, so the
// field it lands on is m_materialFlags at +0xd4. That pairing is what identifies the argument --
// the comparator has just finished reading +0xd0 one line above.
//
// What the three bits MEAN is not established here and is not guessed at; the function only has
// to be a stable key, and it is reproduced exactly so that two emitters compare the way the
// reference compares them.
static uint32_t M2ParticleMaterialClass(uint32_t materialFlags) {
    uint32_t key = 4;

    if (!(materialFlags & 1)) {
        key = 5;
    }

    if (!(materialFlags & 2)) {
        key |= 2;
    }

    if (!(materialFlags & 4)) {
        key |= 0x10;
    }

    return key;
}

// ref: FUN_0081f0e0
// The comparator the reference sorts its two TRANSPARENT element lists with, and the reason
// KeyAndSortElementList below numbers additive runs at all: the run number is this one's FIRST
// key, so a run that shares a number stays contiguous no matter what the finer keys say.
//
// With the runs equal it splits three ways. A particle sorts ahead of anything that is not one.
// Two non-particles fall through to SortTransparent, which is the whole of the ordering they had
// before this landed. Two particles get a tail of their own: their emitters' blend mode, then the
// material class above, then the texture -- each one only breaking a tie in the one before it.
//
// DIVERGED at the last key, and unavoidably. The reference ends on FUN_0047bf20, which is
// `(a - b) >> 2` over the two emitters' +0x128 -- a pointer difference shifted into an index,
// which is a stable total order over 32-bit handles. frozen's HTEXTURE is 64 bits, so that shift
// would discard the high half and order handles wrongly. Comparing the handles directly gives the
// same thing the reference was after: a deterministic order on texture, so particles sharing one
// draw together.
int32_t CM2Scene::SortTransparentGrouped(uint32_t a, uint32_t b, const void* userArg) {
    auto scene = static_cast<const CM2Scene*>(userArg);
    auto elements = scene->m_elements.Ptr();

    auto elementA = const_cast<M2Element*>(&elements[a]);
    auto elementB = const_cast<M2Element*>(&elements[b]);

    if (elementB->additiveRun < elementA->additiveRun) {
        return 1;
    }

    if (elementB->additiveRun > elementA->additiveRun) {
        return -1;
    }

    // A particle ahead of anything that is not one. The test runs only when at least one of the
    // two IS a particle, so it cannot reorder two ordinary elements.
    if (elementA->type == 4 || elementB->type == 4) {
        if (elementB->type < elementA->type) {
            return -1;
        }

        if (elementA->type < elementB->type) {
            return 1;
        }
    }

    if (elementA->type != 4) {
        return CM2Scene::SortTransparent(a, b, userArg);
    }

    // Both are particles.
    CM2ParticleEmitter* emitterA = elementA->emitter;
    CM2ParticleEmitter* emitterB = elementB->emitter;

    // FROZEN-ONLY: the reference dereferences both emitters here without checking. A particle
    // element with no emitter should not exist, and if one ever does this keeps the sort total
    // instead of faulting inside a comparator, where the stack says nothing useful.
    if (!emitterA || !emitterB) {
        return 0;
    }

    if (emitterA->m_blendMode < emitterB->m_blendMode) {
        return -1;
    }

    if (emitterA->m_blendMode > emitterB->m_blendMode) {
        return 1;
    }

    uint32_t classA = M2ParticleMaterialClass(emitterA->m_materialFlags);
    uint32_t classB = M2ParticleMaterialClass(emitterB->m_materialFlags);

    if (classA < classB) {
        return -1;
    }

    if (classA > classB) {
        return 1;
    }

    if (emitterA->m_texture < emitterB->m_texture) {
        return -1;
    }

    if (emitterA->m_texture > emitterB->m_texture) {
        return 1;
    }

    return 0;
}
// ref: FUN_0081f9e0
// Give every element of one list its additive-run number, then sort the list.
//
// The run number is the point of the pass: walking in order, every element takes the next number
// EXCEPT one whose blend is additive and whose predecessor's was too, which keeps the number it
// already had. A stretch of additive elements therefore shares one number and sorts as a unit,
// which is what stops the finer keys from interleaving it with the blends on either side.
//
// Where the blend comes from depends on the element's kind: a batch element reads its material,
// a ribbon reads the first material of its ribbon record, and a particle asks its emitter.
//
// DIVERGED, in one branch and narrowly. For a particle the reference chooses between asking the
// emitter and forcing blend index 4 outright, on a flag it reads two indirections off its own
// argument; which object that is did not survive the decompilation, so this always asks the
// emitter. The two arms agree whenever the particle's own blend is additive -- index 4 is Add --
// and differ only for a non-additive particle, which the forced arm would group as if it were
// additive. Recorded rather than guessed at, because the gate is one dereference away from being
// readable and a wrong guess here reorders particles silently.
void CM2Scene::KeyAndSortElementList(uint32_t listIndex) {
    TSGrowableArray<uint32_t>& list = this->array54[listIndex];

    uint32_t count = list.Count();
    uint32_t run = 0;
    bool inAdditiveRun = false;

    for (uint32_t i = 0; i < count; i++) {
        M2Element& element = this->m_elements[list[i]];

        M2Data* data = element.model->m_shared->m_data;

        uint32_t blendIndex = 0;

        switch (element.type) {
        case 0:
        case 1:
        case 2:
            blendIndex = data->materials[element.batch->materialIndex].blendMode;
            break;

        case 3: {
            M2Ribbon& ribbon = data->ribbons[element.index];

            blendIndex = data->materials[ribbon.materialIndices[0]].blendMode;
            break;
        }

        case 4:
            blendIndex = element.emitter
                       ? M2BlendIndexFromGx(element.emitter->m_blendMode)
                       : 0;
            break;

        default:
            break;
        }

        if (M2BlendIsAdditive(blendIndex)) {
            if (!inAdditiveRun) {
                inAdditiveRun = true;
                run++;
            }
        } else {
            inAdditiveRun = false;
            run++;
        }

        element.additiveRun = run;
    }

    M2HeapSort(CM2Scene::SortTransparentGrouped, list.Ptr(), count, this);
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

        // A model whose visible sections have been merged walks the MERGED batches, which are
        // fewer. Reachable as of 2026-09-26: CM2Model::OptimizeVisibleGeometry now fills this.
        uint32_t batchCount;
        if (model->ptr2D0) {
            batchCount = model->ptr2D0->batchCount;
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
                // No visibility test on this side, and that is not an omission: a merged batch
                // only exists because every source batch that went into it was visible, and its
                // skinSectionIndex points into the merged sections rather than the model's
                // per-section visibility array.
                batch = &model->ptr2D0->batches[batchIndex];
                skinSection = &model->ptr2D0->skinSections[batch->skinSectionIndex];
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
            if (v17 || (v17 = this->m_projectionCallback == nullptr, v222 = 1, v17)) {
                v222 = 0;
            }

            M2Material* layerMaterial = batch->materialLayer
                ? &data->materials[batch->materialIndex - batch->materialLayer]
                : &data->materials[batch->materialIndex];

            if (layerMaterial->blendMode > 1 || (v221 = 0, alpha < 0.99998999f)) {
                v221 = 1;
            }

            if (model->ptr2D0) {
                // Resolved per merged batch when the block was built, because the shared data's
                // m_batchShaders is indexed by the ORIGINAL batch number and this index is not
                // one of those any more.
                effect = model->ptr2D0->effects[batchIndex];
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

            // FROZEN-ONLY GUARD, and the SECOND place this exact one has been needed -- the
            // driver in CM2Model::AnimateParticleEmitter has the same one for the same reason.
            // The reference indexes m_boneMatrices with no check because its loader guarantees
            // the array and the index; frozen's allocates the array inside a `bones.Count()`
            // branch and never validates boneIndex against the bone count. Both would fault, and
            // this runs for every model every frame.
            //
            // Skipping leaves that emitter without an element for the frame, which is what the
            // four gates above already do.
            if (!model->m_boneMatrices || file.boneIndex >= data->bones.Count()) {
                continue;
            }

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

        // Ribbon elements, type 3: one per emitter that has any trail to draw.
        //
        // WHY THIS DOES NOT LIFT Animate's RECALL, so the next reader does not go looking for a
        // mistake here: the reference makes TWO passes over the models and frozen makes one. Its
        // first pass does IsDrawable then the particles (AddParticleElement at 0x821a20+0x226 in
        // the corpus rendering); its second does IsDrawable again, then the batch elements,
        // IsBatchDoodadCompatible, ComputeElementShaders, then THESE ribbons, then the draw
        // callbacks. So the reference emits particle elements BEFORE batch elements, and frozen
        // emits them after.
        //
        // This block is in the right place relative to its own neighbours -- batches before it,
        // draw callbacks after -- which is the reference's pass-2 order. The call-sequence
        // matcher still cannot align it, because the merged pass reorders everything around it.
        // Splitting Animate back into two passes is what would move that number, and it would also
        // change which elements get the low indices: the lists are heap-sorted by type first, so
        // the index order only decides ties, but it is a real difference and not just cosmetic.
        //
        // THIS EMITS NOTHING TODAY, for two independent reasons, and both were measured on
        // 2026-09-27 rather than assumed:
        //
        //   1. Almost no model carries ribbons. A counter in InitializeLoaded saw 5 of 6400
        //      loaded models with any, and none of those 5 was ever in this scene's animate list
        //      during a 45-second run on map 0 -- a probe on the loop below never iterated once.
        //   2. Even for a model that does, IsEmpty is permanently TRUE. It compares head against
        //      tail, and the ONLY writes to m_head in the whole tree are its default initialiser
        //      and Initialize's reset to zero. Nothing advances the ring, because the per-frame
        //      segment update is not ported.
        //
        // So this is the correct gate in place ahead of its producer, not working code. Reason 2
        // is what has to go first: port the segment update, and this starts emitting. Until then
        // CM2SceneRender::DrawRibbon stays empty and nothing would draw the elements anyway --
        // which is safe here in a way it would NOT be for doodads, because ribbons displace
        // nothing. See the note at CM2Model::IsBatchDoodadCompatible for the contrast.
        // This gather crashed the client when it first went live, and the fault was NOT here -- it
        // was a bare 0 clearing a texture stage in CM2SceneRender::SetupTextures, which half-wrote
        // an eight-byte pointer slot. See the note there; it is fixed.
        //
        // Worth keeping because it cost real time: an earlier probe reported 1,394,001 AnimateST
        // calls with no ribbon-bearing model and I read that as "this path is unreachable". It is
        // not -- WeatherRender reaches CM2Scene::Draw, weather models carry ribbons, and whether
        // any are active depends on the zone and the time of day. So the path was live all along
        // and merely idle whenever I happened to sample it.
        if (model->m_ribbonEmitters) {
            for (int32_t i = 0; i < data->ribbons.Count(); i++) {
                CM2Ribbon* emitter = model->m_ribbonEmitters[i];

                // An empty ring is not worth an element, and IsEmpty is the reference's own gate.
                if (emitter->IsEmpty()) {
                    continue;
                }

                const M2Ribbon& file = data->ribbons[i];

                // The model's alpha, scaled by the ribbon's own animated alpha ONLY when that
                // track has keys. The reference tests the track's sequenceTimes count (+0x3c on
                // the file record), not its key count.
                float alpha = model->float198;

                if (file.alphaTrack.sequenceTimes.Count()) {
                    alpha *= model->m_ribbons[i].alphaTrack.currentValue;
                }

                // The pass is chosen from the FIRST material only, however many the ribbon has.
                const M2Material& material = data->materials[file.materialIndices[0]];

                M2Element* element = this->m_elements.New();

                if (!element) {
                    continue;
                }

                element->type = 3;
                element->model = model;
                element->flags = 0x0;
                element->alpha = alpha;
                // BOTH of these take model->float88. The particle element puts a distance in the
                // second one; the ribbon element does not, and this is the reference's own
                // duplication rather than a transcription slip.
                element->float10 = model->float88;
                element->float14 = model->float88;
                // Which ribbon this element is for -- the draw has no other way back to it.
                element->index = i;
                // The reference writes this at dword 9. A note here used to call that an overload
                // of the skinSection slot; it is not -- +0x24 IS priorityPlane in the reference,
                // and frozen's field simply sits two slots earlier. See the layout note in
                // M2Types.hpp. The field written is the right one either way.
                element->priorityPlane = file.priorityPlane;
                // Same correction as AddParticleElement's -- these had been copied from it while
                // it was still transcribing by slot. By MEANING: the effect is nulled, both
                // permutes go to -1, and the reference's +0x3c takes 0.
                element->effect = nullptr;
                element->vertexPermute = 0xFFFFFFFF;
                element->pixelPermute = 0xFFFFFFFF;
                element->dword34 = 0;

                // The same pass split AddParticleElement uses, and the same 0.99999 (0x00a45528)
                // effectively-opaque threshold. The difference is that a ribbon has no equivalent
                // of the particle emitter's 0x40000 flag, so the choice between the two
                // transparent passes rests on the water side alone.
                if (material.blendMode <= 1 && alpha >= 0.9999899864196777f) {
                    *this->array54[0].New() = elementIndex;
                } else if (!v21) {
                    *this->array54[2].New() = elementIndex;
                } else {
                    *this->array54[1].New() = elementIndex;
                }

                elementIndex++;
            }
        }

        // TODO
        // - draw callbacks
    }

    // THE THREE LISTS ARE ALWAYS SORTED, opaque by SortOpaque and both transparent lists by
    // SortTransparent. The reference does these three unconditionally, back to back, at
    // 0x00822fcf / 0x00822fe2 / 0x00822ff5.
    M2HeapSort(CM2Scene::SortOpaque, this->array54[0].Ptr(), this->array54[0].Count(), this);
    M2HeapSort(CM2Scene::SortTransparent, this->array54[1].Ptr(), this->array54[1].Count(), this);
    M2HeapSort(CM2Scene::SortTransparent, this->array54[2].Ptr(), this->array54[2].Count(), this);

    // THE ADDITIVE-RUN PASS IS A GATED RE-SORT ON TOP OF THOSE, not a replacement for them. This
    // was got wrong once and it is worth saying plainly, because the mistake was invisible in the
    // world and wrecked the login screen: the keying pass was put here INSTEAD of the two
    // SortTransparent calls above and run every frame, so a scene with no particles at all lost
    // its depth sort and drew its transparent geometry in model order. On the glue screen that put
    // the sky over the terrain -- a flat cyan wash with the foreground missing.
    //
    // The reference gates it on two conditions ANDed, at 0x0082300e and 0x00823014:
    //
    //   * bit 0x80 of the model-list head's CREATION flags -- `scene->+0x4` then `+0x4`, which is
    //     CM2Model::m_flags. What that bit MEANS is not established and is not guessed at here;
    //     the test is transcribed as the reference makes it. Nothing in frozen sets it today, so
    //     this pass does not currently run -- which is correct, not broken. If a model ever asks
    //     for the bit, the pass is already here and already right.
    //
    //   * more than ONE additive particle element in the scene. additiveCount is exactly that,
    //     counted by AddParticleElement, and the comment there already said it was owed to this
    //     sort. With nothing or one thing to group there is no run to keep contiguous, so the
    //     depth order the sorts above produced is the better answer and is left alone.
    if (this->m_modelList && (this->m_modelList->m_flags & 0x80) && additiveCount > 1) {
        this->KeyAndSortElementList(1);
        this->KeyAndSortElementList(2);
    }
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

    // CORRECTED 2026-09-27. These four were transcribed from the reference SLOT BY SLOT, and this
    // struct's fields sit two positions earlier than the reference's meanings (see M2Types.hpp), so
    // every one of them landed on the wrong field. The reference writes 0 to EFFECT here, not to
    // pixelPermute, and -1 to the two permutes.
    //
    // Nulling the effect is the part that matters: SortOpaqueGeoBatches and SortTransparent both
    // test `elementA->effect && elementB->effect` before indexing a shader array with the permute,
    // and the element allocator does not zero -- so a particle element was carrying whatever effect
    // pointer the slot last held, and the guard let it through.
    element->effect = nullptr;
    element->vertexPermute = 0xFFFFFFFF;
    element->pixelPermute = 0xFFFFFFFF;
    element->dword34 = 0;

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

// Install the projected-decal callback.
//
// Two stores and nothing else, but it is the switch that decides whether this scene ever emits
// type-1 elements -- and so whether CM2SceneRender::DrawBatchProj is reachable at all. Nothing in
// frozen calls it yet; the reference's one caller is world init at 0x781340.
//
// ref: FUN_0081cc30
void CM2Scene::SetProjectionCallback(void* callback, void* context) {
    this->m_projectionCallback = callback;
    this->m_projectionContext = context;
}

// ref: FUN_0081f8c0
// The unwind both creation paths share. Not a destructor call on its own: the model's storage came
// out of an ObjectAlloc pool by handle, so the block has to go back to that pool rather than to
// free(). CM2Model::Release does these same two steps inline against g_modelPool.
void DestroyModel(uint32_t* pool, CM2Model* model) {
    model->~CM2Model();

    ObjectFree(*pool, model->m_memHandle);
}

// ref: FUN_0081f970
// Create a model against another one rather than against a file.
//
// The difference from CreateModel is entirely in what Initialize is given: the shared data comes
// from `source` instead of the cache, and `source` itself goes in as the fourth argument, which is
// the reference it keeps at ref +0x30 and hands back in its destructor. There is no Release of the
// shared here, and that is correct rather than an omission -- this path never took a reference on
// it, where CreateModel has to give back the one CreateShared took.
CM2Model* CM2Scene::CreateModelFrom(CM2Model* source, uint32_t flags) {
    if (!source) {
        return nullptr;
    }

    CM2Model* model = CM2Model::AllocModel(g_modelPool);

    if (model) {
        if (!model->Initialize(this, source->m_shared, source, flags)) {
            DestroyModel(g_modelPool, model);

            model = nullptr;
        }
    }

    return model;
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
                // The reference calls FUN_0081f8c0 here, which is DestroyModel above -- so the
                // failure path leaked a pooled block until 2026-09-27.
                DestroyModel(g_modelPool, model);

                model = nullptr;
            }
        }

        shared->Release();
    }

    return model;
}

int32_t CM2Scene::Draw(M2PASS pass) {
    // DELIBERATELY NOT GATED ON m_passMask. The reference tests `m_passMask & (1 << pass)` here
    // and returns without drawing when the bit is clear; frozen does not, and this is a recorded
    // divergence from a reference BUG rather than an unfinished port. The chase, in order:
    //
    //   1. Adding the test suppressed most of the drawing. An instrumented run counted 301 calls
    //      through here, 69 with a non-zero mask and 232 without, the first carrying 47 elements.
    //   2. The reference has FIVE callers of this function -- CGWorldFrame::OnWorldRender plus
    //      00619580, 007f08c0, 0095fc30 and 009abd50 -- and only OnWorldRender assigns the mask
    //      (at 0x004f9117, from the world enables word at 0x00cd7754).
    //   3. M2CreateScene (FUN_0081c080) allocates 0x148 bytes, so +0x144 is the LAST field of a
    //      CM2Scene, and it has nine callers -- there are many scenes, not one.
    //   4. The constructor (FUN_008216c0) never writes +0x144. It clears +0x4c and stops well
    //      short of the end of the object.
    //
    // So in the reference every scene that is not the world's tests UNINITIALISED HEAP MEMORY
    // against `1 << pass`. Whether such a scene draws is whatever the allocator happened to
    // leave there -- with Storm's uninitialised fill it would be 0xBAADF00D, whose low bits are
    // 0b101, so passes 0 and 2 would draw and pass 1 would not. That is not behaviour to
    // reproduce.
    //
    // The half that IS well defined is kept: m_passMask exists, CWorld::s_m2PassMask exists, and
    // CGWorldFrame assigns it where the reference does. If a future change gives every scene a
    // defined mask at construction, the gate becomes portable -- and then it belongs here.

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

// Mark a ray query as active: scene flag 0x2, which RaySetup clears again for a degenerate ray.
// ref: FUN_0081cac0
void CM2Scene::BeginRayQuery() {
    this->m_flags |= 0x2;
}

// ref: FUN_0081cbc0
// Order the ray candidates near to far: entry distance, then exit distance, then the candidate's
// own index. The driver walks the result front to back and stops as soon as a candidate's entry
// distance is further than the best hit it already has, which only works on this order.
//
// Like the other comparators here it sorts INDICES and takes the candidate array as its user
// argument, which is why the reference scales both by sixteen -- the size of M2SceneRayCandidate,
// and a useful check that the struct is the right shape.
//
// READ OFF THE INSTRUCTIONS, not the decompilation, because the x87 compare-and-branch pairs do
// not survive it. Each pair here loads B's key and compares it against A's, so the senses look
// inverted written out: `testb $0x41` with `jne` is taken when B <= A, and the fall-through is
// B > A, which is the -1 case. The final tiebreak ends `sbbl %eax, %eax` then NEGL -- negate, not
// or, which is what makes the two branches +1 and 0 rather than -1 and -1.
//
// The three keys are lexicographic and the last is unique, so this is a strict weak ordering by
// construction; no sampling needed.
int32_t CM2Scene::SortRayCandidates(uint32_t a, uint32_t b, const void* userArg) {
    auto candidates = static_cast<const M2SceneRayCandidate*>(userArg);

    const M2SceneRayCandidate& ca = candidates[a];
    const M2SceneRayCandidate& cb = candidates[b];

    if (cb.tNear > ca.tNear) {
        return -1;
    }

    if (cb.tNear < ca.tNear) {
        return 1;
    }

    if (cb.tFar > ca.tFar) {
        return -1;
    }

    if (cb.tFar < ca.tFar) {
        return 1;
    }

    if (b > a) {
        return -1;
    }

    if (b < a) {
        return 1;
    }

    return 0;
}

// Make room for one candidate per model on the ray list. The capacity only grows, doubling from
// one, and the old contents are not kept.
// ref: FUN_0081cad0
void CM2Scene::ReserveRayCandidates() {
    uint32_t count = 0;

    for (auto model = this->m_rayModelList; model; model = model->m_rayNext) {
        count++;
    }

    if (this->m_rayCandidateCapacity < count) {
        if (this->m_rayCandidates) {
            SMemFree(this->m_rayCandidates, "delete[]", -1, 0);
        }

        if (this->m_rayCandidateOrder) {
            SMemFree(this->m_rayCandidateOrder, "delete[]", -1, 0);
        }

        if (this->m_rayCandidateCapacity == 0) {
            this->m_rayCandidateCapacity = 1;
        }

        while (this->m_rayCandidateCapacity < count) {
            this->m_rayCandidateCapacity <<= 1;
        }

        this->m_rayCandidates = static_cast<M2SceneRayCandidate*>(SMemAlloc(sizeof(M2SceneRayCandidate) * this->m_rayCandidateCapacity, __FILE__, 0x4e9, 0));
        this->m_rayCandidateOrder = static_cast<uint32_t*>(SMemAlloc(sizeof(uint32_t) * this->m_rayCandidateCapacity, __FILE__, 0x4ea, 0));
    }
}

// Normalise the ray from start to end. A degenerate ray (t or the length under 1e-5, the constant
// at 0x009ea558) empties the ray list and clears scene flag 0x2 instead. Only each model's back
// link is cleared, through the link itself, exactly as the reference walks it.
// ref: FUN_0081cf20
int32_t CM2Scene::RaySetup(const C3Vector& start, const C3Vector& end, float t, float* length, C3Vector* dir) {
    const float epsilon = 1.0e-5f;

    if (epsilon <= t) {
        float dx = end.x - start.x;
        float dy = end.y - start.y;
        float dz = end.z - start.z;
        float len = sqrtf(dy * dy + dz * dz + dx * dx);
        *length = len;

        if (epsilon <= len) {
            float inv = 1.0f / len;
            dir->x = inv * dx;
            dir->y = dy * inv;
            dir->z = dz * inv;

            return 1;
        }
    }

    for (auto model = this->m_rayModelList; model; model = model->m_rayNext) {
        *model->m_rayPrev = nullptr;
        model->m_rayPrev = nullptr;
    }

    this->m_flags &= ~0x2;

    return 0;
}

// Blend up to four bone matrices by their byte weights (scaled by the 1/255 at 0x00a45564). A zero
// weight ends the list. The result is forced affine: last column (0, 0, 0, 1).
// ref: FUN_0081d2c0
void CM2Scene::BlendBoneMatrices(const C44Matrix* bones, ubyte4 weights, ubyte4 indices, C44Matrix* out) {
    const float scale = 0.0039215689f;

    auto m = reinterpret_cast<const float*>(&bones[indices.b[0]]);
    float w = static_cast<float>(weights.b[0]) * scale;
    float acc[16];

    for (int32_t k = 0; k < 16; k++) {
        acc[k] = m[k] * w;
    }

    for (int32_t i = 1; i < 4; i++) {
        if (weights.b[i] == 0) {
            break;
        }

        m = reinterpret_cast<const float*>(&bones[indices.b[i]]);
        w = static_cast<float>(weights.b[i]) * scale;

        for (int32_t k = 0; k < 16; k++) {
            acc[k] = m[k] * w + acc[k];
        }
    }

    auto dst = reinterpret_cast<float*>(out);

    for (int32_t k = 0; k < 16; k++) {
        dst[k] = acc[k];
    }

    dst[3] = 0.0f;
    dst[7] = 0.0f;
    dst[11] = 0.0f;
    dst[15] = 1.0f;
}

// The same blend over the three columns that carry the rotation and translation; the fourth column
// of out is left as it was.
// ref: FUN_0081d3d0
void CM2Scene::BlendBoneMatrices3x4(const C44Matrix* bones, ubyte4 weights, ubyte4 indices, C44Matrix* out) {
    static const int32_t s_elements[12] = { 0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14 };
    const float scale = 0.0039215689f;

    auto dst = reinterpret_cast<float*>(out);
    auto m = reinterpret_cast<const float*>(&bones[indices.b[0]]);
    float w = static_cast<float>(weights.b[0]) * scale;

    for (auto k : s_elements) {
        dst[k] = m[k] * w;
    }

    for (int32_t i = 1; i < 4; i++) {
        if (weights.b[i] == 0) {
            return;
        }

        m = reinterpret_cast<const float*>(&bones[indices.b[i]]);
        w = static_cast<float>(weights.b[i]) * scale;

        for (auto k : s_elements) {
            dst[k] = m[k] * w + dst[k];
        }
    }
}

// Test a run of triangles, already projected by ProjectSectionVertices, against a point in the
// query plane. A hit inside the triangle at a non-negative height replaces best when it is no
// higher than *bestHeight -- or unconditionally, with preferOther set, when best is empty or has a
// different key. Returns the (possibly new) best candidate.
// ref: FUN_0081dd50
// Project one model's collision mesh onto the query plane, then test its triangles.
//
// IT IS THE COLLISION MESH, not the render geometry, and that is the whole identification. The
// reference reads a count at +0xd8 and a pointer at +0xdc for the indices, a count at +0xe0 and a
// pointer at +0xe4 for the vertices, off the object at m_shared + 0x150 -- and it walks those
// vertices with a stride of TWELVE. Twelve bytes is a C3Vector, not the file's 48-byte M2Vertex,
// which rules out M2Data::vertices immediately. Counting M2Data's fields with M2Bounds at its
// real 28 bytes puts collisionIndices at exactly +0xd8 and collisionPositions at +0xe0, both
// matching, and uint16 indices explain the `+ count * 2` the reference uses for the end pointer.
// So m_shared + 0x150 is m_data and this runs against the collision hull, which is what a hit
// test should use and why the stride was never going to be a render vertex.
//
// THE PROJECTION drops each world-space vertex onto the plane and keeps the height separately:
// h is the signed distance, x and y are the point slid back along the normal by h, and z carries
// h itself. That is exactly what m_rayProjected's declaration already described -- in-plane x, y
// and the height above the plane -- and RayTestTriangles reads it back on that understanding.
M2SceneRayCandidate* CM2Scene::RayTestModel(CM2Model* model, int32_t preferOther,
                                            const C3Vector& planeNormal, float planeDist,
                                            const C2Vector& point, M2SceneRayCandidate* candidate,
                                            float* bestHeight, M2SceneRayCandidate* best) {
    // FROZEN-ONLY. The reference dereferences the data straight through. CLAUDE.md's bug class:
    // m_data is set when the async read lands but the array offsets are not patched until M2Init
    // has run, and m_m2DataLoaded is what says so. Without this a model still streaming resolves
    // collisionPositions to a wild pointer.
    if (!model || !model->m_shared || !model->m_shared->m_m2DataLoaded
        || !model->m_shared->m_data) {
        return best;
    }

    M2Data* data = model->m_shared->m_data;

    uint32_t vertexCount = data->collisionPositions.Count();

    if (this->m_rayProjectedCapacity < vertexCount) {
        if (this->m_rayProjected) {
            SMemFree(this->m_rayProjected, "delete[]", -1, 0);

            this->m_rayProjected = nullptr;
        }

        if (!this->m_rayProjectedCapacity) {
            this->m_rayProjectedCapacity = 1;
        }

        while (this->m_rayProjectedCapacity < vertexCount) {
            this->m_rayProjectedCapacity <<= 1;
        }

        this->m_rayProjected = static_cast<C3Vector*>(
            SMemAlloc(sizeof(C3Vector) * this->m_rayProjectedCapacity, __FILE__, __LINE__,
                      SMEM_FLAG_ZEROMEMORY));

        if (!this->m_rayProjected) {
            this->m_rayProjectedCapacity = 0;

            return best;
        }
    }

    for (uint32_t i = 0; i < vertexCount; i++) {
        C3Vector v = data->collisionPositions[i] * model->matrixF4;

        float h = planeNormal.x * v.x + planeNormal.y * v.y + planeNormal.z * v.z - planeDist;

        this->m_rayProjected[i].x = v.x - planeNormal.x * h;
        this->m_rayProjected[i].y = v.y - planeNormal.y * h;
        this->m_rayProjected[i].z = h;
    }

    uint32_t indexCount = data->collisionIndices.Count();

    if (!indexCount) {
        return best;
    }

    const uint16_t* indices = &data->collisionIndices[0];

    return this->RayTestTriangles(indices, indices + indexCount, 0, point, preferOther,
                                  candidate, bestHeight, best);
}

// ref: FUN_0081d510
M2SceneRayCandidate* CM2Scene::RayTestTriangles(const uint16_t* indices, const uint16_t* indicesEnd, uint32_t vertexBase, const C2Vector& point, int32_t preferOther, M2SceneRayCandidate* candidate, float* bestHeight, M2SceneRayCandidate* best) {
    const float epsilon = 1.0e-5f;

    for (; indices < indicesEnd; indices += 3) {
        auto projected = this->m_rayProjected;
        auto& p0 = projected[indices[0] - vertexBase];
        auto& p1 = projected[indices[1] - vertexBase];
        auto& p2 = projected[indices[2] - vertexBase];

        float area = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);

        if (epsilon <= fabsf(area)) {
            float inv = 1.0f / area;
            float b0 = ((p2.y - point.y) * (p1.x - point.x) - (p2.x - point.x) * (p1.y - point.y)) * inv;

            if (0.0f <= b0) {
                float b1 = ((p0.y - point.y) * (p2.x - point.x) - (p0.x - point.x) * (p2.y - point.y)) * inv;

                if (0.0f <= b1) {
                    float b2 = ((p0.x - point.x) * (p1.y - point.y) - (p0.y - point.y) * (p1.x - point.x)) * inv;

                    if (0.0f <= b2) {
                        float height = p0.z * b0 + b2 * p2.z + p1.z * b1;

                        if (0.0f <= height
                            && ((preferOther && (!best || best->key != candidate->key)) || height <= *bestHeight)) {
                            *bestHeight = height;
                            best = candidate;
                        }
                    }
                }
            }
        }
    }

    return best;
}

// Skin a section's vertices by their first bone only (optionally pushed out along the rotated
// normal) and project them onto a plane: m_rayProjected gets the in-plane x and y and the signed
// distance from the plane.
// ref: FUN_0081d9c0
void CM2Scene::ProjectSectionVertices(CM2Model* model, M2SkinProfile* skinProfile, M2SkinSection* section, int32_t addNormal, const C3Vector& planeNormal, float planeDist) {
    auto data = model->m_shared->m_data;
    uint32_t i = section->vertexStart;
    uint32_t end = section->vertexCount + i;
    auto dst = this->m_rayProjected;

    for (; i < end; i++) {
        auto& vertex = data->vertices[skinProfile->vertices[i]];
        auto& bone = model->m_boneMatrices[vertex.indices.b[0]];

        C3Vector p = vertex.position * bone;

        if (addNormal) {
            p.x = vertex.normal.x * bone.a0 + bone.b0 * vertex.normal.y + bone.c0 * vertex.normal.z + p.x;
            p.y = bone.a1 * vertex.normal.x + bone.b1 * vertex.normal.y + bone.c1 * vertex.normal.z + p.y;
            p.z = p.z + (bone.a2 * vertex.normal.x + bone.b2 * vertex.normal.y + bone.c2 * vertex.normal.z);
        }

        float d = (planeNormal.x * p.x + planeNormal.y * p.y + planeNormal.z * p.z) - planeDist;
        dst->x = p.x - planeNormal.x * d;
        dst->y = p.y - planeNormal.y * d;
        dst->z = d;
        dst++;
    }
}
