#include <cstring>
#include <cfloat>
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
#include <tempest/Ray.hpp>
#include <tempest/Intersect.hpp>
#include <tempest/Matrix.hpp>
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
bool CM2Scene::s_rayBlend4x4 = false;

// ref: FUN_0081ce70
// The animate thread's half of the interleave: entries 1, 3, 5, ... of the animate list, the
// caller taking 0, 2, 4, ...; run on the cache thread through CM2Cache::BeginThread.
void CM2Scene::AnimateThread(void* arg) {
    auto scene = static_cast<CM2Scene*>(arg);

    for (auto model = scene->m_animateList; model && (model = model->m_animateNext); model = model->m_animateNext) {
        if (model->m_attachParent) {
            continue;
        }

        if (!model->m_flag1000) {
            C3Vector scale = { 1.0f, 1.0f, 1.0f };
            C3Vector translation = { 0.0f, 0.0f, 0.0f };

            model->AnimateMT(&scene->m_view, scale, translation, 1.0f, 1.0f);
        } else {
            C3Vector scale = { 1.0f, 1.0f, 1.0f };
            C3Vector translation = { 0.0f, 0.0f, 0.0f };

            model->AnimateMTSimple(&scene->m_view, scale, translation, 1.0f, 1.0f);
        }
    }
}

// The sign of `(a - b) >> 2` over two handles, which is how the reference orders textures
// (FUN_0047bf20); a 64-bit pointer difference does not fit its 32-bit result, so only the sign
// it would give is reproduced.
static int32_t M2CompareHandles(const void* a, const void* b) {
    return a < b ? -1 : a > b ? 1 : 0;
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

// ref: FUN_0081edf0
// By blend, then by the material class its flags give, then by texture.
int32_t CM2Scene::SortOpaqueParticles(M2Element* elementA, M2Element* elementB) {
    auto emitterA = elementA->emitter;
    auto emitterB = elementB->emitter;

    if (static_cast<int32_t>(emitterA->m_blendMode) < static_cast<int32_t>(emitterB->m_blendMode)) {
        return -1;
    }

    if (static_cast<int32_t>(emitterA->m_blendMode) > static_cast<int32_t>(emitterB->m_blendMode)) {
        return 1;
    }

    auto materialClass = [](uint32_t flags) {
        uint32_t value = (flags & 1) ? 4 : 5;

        if (!(flags & 2)) {
            value |= 2;
        }

        if (!(flags & 4)) {
            value |= 0x10;
        }

        return value;
    };

    uint32_t classA = materialClass(emitterA->m_materialFlags);
    uint32_t classB = materialClass(emitterB->m_materialFlags);

    if (classA < classB) {
        return -1;
    }

    if (classA > classB) {
        return 1;
    }

    return M2CompareHandles(emitterA->m_texture, emitterB->m_texture);
}

// ref: FUN_0081ed10
// By the textures the two ribbons draw with, slot by slot; then by how many they have; then by
// ribbon index.
int32_t CM2Scene::SortOpaqueRibbons(M2Element* elementA, M2Element* elementB) {
    auto& ribbonA = elementA->model->m_shared->m_data->ribbons[elementA->index];
    auto& ribbonB = elementB->model->m_shared->m_data->ribbons[elementB->index];

    uint32_t countA = ribbonA.textureIndices.Count();
    uint32_t countB = ribbonB.textureIndices.Count();
    uint32_t shared = countB <= countA ? countB : countA;

    for (uint32_t i = 0; i < shared; i++) {
        int32_t order = M2CompareHandles(
            elementA->model->m_textures[ribbonA.textureIndices[i]],
            elementB->model->m_textures[ribbonB.textureIndices[i]]);

        if (order < 0) {
            return -1;
        }

        if (order > 0) {
            return 1;
        }
    }

    if (countB <= countA) {
        if (countA != countB) {
            return 1;
        }

        if (static_cast<uint32_t>(elementB->index) <= static_cast<uint32_t>(elementA->index)) {
            return static_cast<uint32_t>(elementB->index) < static_cast<uint32_t>(elementA->index);
        }
    }

    return -1;
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

// ref: FUN_00821850
// Every model still in the scene is released until it lets go (a model unlinks itself from the
// list on its last release), then the ray-query arrays go; the growable arrays free themselves.
CM2Scene::~CM2Scene() {
    while (auto model = this->m_modelList) {
        while (model->Release()) {
        }
    }

    if (this->m_rayCandidates) {
        SMemFree(this->m_rayCandidates, __FILE__, __LINE__, 0);
    }

    if (this->m_rayCandidateOrder) {
        SMemFree(this->m_rayCandidateOrder, __FILE__, __LINE__, 0);
    }

    if (this->m_rayProjected) {
        SMemFree(this->m_rayProjected, __FILE__, __LINE__, 0);
    }
}

// ref: FUN_00823040
uint32_t CM2Scene::Release() {
    this->m_refCount--;

    if (this->m_refCount == 0) {
        this->~CM2Scene();
        SMemFree(this, __FILE__, __LINE__, 0);
        return 0;
    }

    return this->m_refCount;
}

// ref: FUN_0081c990
// The scene clock set outright rather than advanced; the cache gets the same per-frame upkeep.
void CM2Scene::SetTime(uint32_t time) {
    this->m_time = time;

    this->m_cache->UpdateShared();
    this->m_cache->GarbageCollect(0);
}

// ref: FUN_0081cab0
// Destroys every shared model waiting on the cache's pending list at once (the map calls it on unload).
void CM2Scene::CollectSharedGarbage() {
    if (this->m_cache) {
        this->m_cache->GarbageCollect(1);
    }
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

// The model's bounding sphere against its lighting's liquid plane: the signed distance of the
// centre, and the radius scaled by the placement.
static void M2LiquidPlaneDistance(CM2Model* model, float& distance, float& radius) {
    auto data = model->m_shared->m_data;
    const CAaBox& extent = data->bounds.extent;

    C3Vector centre = { (extent.t.x + extent.b.x) * 0.5f,
                        (extent.t.y + extent.b.y) * 0.5f,
                        (extent.t.z + extent.b.z) * 0.5f };

    const C44Matrix& placement = model->matrixF4;

    radius = sqrtf(placement.a0 * placement.a0 + placement.a1 * placement.a1 + placement.a2 * placement.a2)
        * data->bounds.radius;

    C3Vector world = centre * placement;
    const C4Plane& plane = model->m_currentLighting->m_liquidPlane;

    distance = plane.n.x * world.x + plane.n.z * world.z + plane.n.y * world.y + plane.d;
}

static inline uint32_t M2FloatBits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// The doodad grouping table, reference 0x00d40da0: element indices, 0xFFFFFFFF for empty.
static uint32_t s_doodadBatchTable[251];

// ref: FUN_0081cc50
// Folds by 19 everything a doodad element shares a draw on: the model's shared data, the batch,
// the lighting's fog range, fog colour, sun diffuse and ambient, the batch's colour, and the
// model's current diffuse, emissive and +0x1b8. The shared pointer is folded as its low 32 bits.
static uint32_t M2DoodadBatchHash(const M2Element* element) {
    auto model = element->model;
    auto lighting = model->m_currentLighting;

    uint32_t hash = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(model->m_shared)) * 0x13 + element->index;

    const float lightingValues[] = {
        lighting->m_fogStart, lighting->m_fogEnd,
        lighting->m_fogColor.x, lighting->m_fogColor.y, lighting->m_fogColor.z,
        lighting->m_sunDiffuse.x, lighting->m_sunDiffuse.y, lighting->m_sunDiffuse.z,
        lighting->m_sunAmbient.x, lighting->m_sunAmbient.y, lighting->m_sunAmbient.z,
    };

    for (float value : lightingValues) {
        hash = hash * 0x13 + M2FloatBits(value);
    }

    uint32_t colorIndex = element->batch->colorIndex;

    if (colorIndex < model->m_shared->m_data->colors.Count()) {
        const C3Vector& color = model->m_colors[colorIndex].colorTrack.currentValue;

        hash = hash * 0x13 + M2FloatBits(color.x);
        hash = hash * 0x13 + M2FloatBits(color.y);
        hash = hash * 0x13 + M2FloatBits(color.z);
    }

    const float modelValues[] = {
        model->m_currentDiffuse.x, model->m_currentDiffuse.y, model->m_currentDiffuse.z,
        model->m_currentEmissive.x, model->m_currentEmissive.y, model->m_currentEmissive.z,
        model->float1B8,
    };

    for (float value : modelValues) {
        hash = hash * 0x13 + M2FloatBits(value);
    }

    return hash;
}

static int32_t M2CompareBytes(const void* a, const void* b, size_t size) {
    int32_t order = memcmp(a, b, size);
    return order < 0 ? -1 : order > 0 ? 1 : 0;
}

// ref: FUN_0081e5c0
// Orders two doodad elements by the same fields the hash folds, so that 0 means they can be drawn
// as instances of one another.
static int32_t M2DoodadBatchCompare(uint32_t a, uint32_t b, const CM2Scene* scene) {
    const M2Element& elementA = scene->m_elements[a];
    const M2Element& elementB = scene->m_elements[b];
    auto modelA = elementA.model;
    auto modelB = elementB.model;

    if (modelA->m_shared < modelB->m_shared) {
        return -1;
    }

    if (modelB->m_shared < modelA->m_shared) {
        return 1;
    }

    if (static_cast<uint32_t>(elementA.index) < static_cast<uint32_t>(elementB.index)) {
        return -1;
    }

    if (elementA.index != elementB.index) {
        return 1;
    }

    auto lightingA = modelA->m_currentLighting;
    auto lightingB = modelB->m_currentLighting;

    if (lightingA != lightingB) {
        if (lightingA->m_fogStart < lightingB->m_fogStart) {
            return -1;
        }

        if (lightingB->m_fogStart < lightingA->m_fogStart) {
            return 1;
        }

        if (lightingA->m_fogEnd < lightingB->m_fogEnd) {
            return -1;
        }

        if (lightingB->m_fogEnd < lightingA->m_fogEnd) {
            return 1;
        }

        if (int32_t order = M2CompareBytes(&lightingA->m_fogColor, &lightingB->m_fogColor, sizeof(C3Vector))) {
            return order;
        }

        if (int32_t order = M2CompareBytes(&lightingA->m_sunDiffuse, &lightingB->m_sunDiffuse, sizeof(C3Vector))) {
            return order;
        }

        if (int32_t order = M2CompareBytes(&lightingA->m_sunAmbient, &lightingB->m_sunAmbient, sizeof(C3Vector))) {
            return order;
        }
    }

    uint32_t colorIndex = elementA.batch->colorIndex;

    if (colorIndex < modelA->m_shared->m_data->colors.Count()) {
        if (int32_t order = M2CompareBytes(&modelA->m_colors[colorIndex].colorTrack.currentValue,
                                           &modelB->m_colors[colorIndex].colorTrack.currentValue, sizeof(C3Vector))) {
            return order;
        }
    }

    if (int32_t order = M2CompareBytes(&modelA->m_currentDiffuse, &modelB->m_currentDiffuse, sizeof(C3Vector))) {
        return order;
    }

    if (int32_t order = M2CompareBytes(&modelA->m_currentEmissive, &modelB->m_currentEmissive, sizeof(C3Vector))) {
        return order;
    }

    if (modelA->float1B8 < modelB->float1B8) {
        return -1;
    }

    if (modelA->float1B8 <= modelB->float1B8) {
        return 0;
    }

    return 1;
}

// ref: FUN_0081ea90
// Doodad elements by group, so each group's members are contiguous.
int32_t CM2Scene::SortDoodadGroups(uint32_t a, uint32_t b, const void* userArg) {
    auto scene = static_cast<const CM2Scene*>(userArg);
    uint32_t groupA = scene->m_elements[a].doodadGroup;
    uint32_t groupB = scene->m_elements[b].doodadGroup;

    if (groupA < groupB) {
        return -1;
    }

    return groupB < groupA;
}

// ref: FUN_00821a20
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

    // M2UseThreads: the cache thread (CM2Cache::ThreadProc) takes the odd entries.
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

    // Emptied every frame: CM2Model::SetAnimating puts a model back on for the next one.
    while (this->m_animateList) {
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

    // The draw list: each model's batches, ribbons and draw callback.
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

        auto lighting = model->m_currentLighting;
        auto data = model->m_shared->m_data;

        // Which transparent passes the model's elements join: 1 draws before the liquid surface
        // and 2 after it. With both bits set the bounding sphere is tested against the liquid plane,
        // and a model straddling it with clip planes off (M2UseClipPlanes) goes to one side only.
        int32_t aboveLiquid = (lighting->m_flags & 0x20) != 0;
        int32_t belowLiquid = (lighting->m_flags & 0x40) != 0;

        if (aboveLiquid && belowLiquid) {
            float distance;
            float radius;
            M2LiquidPlaneDistance(model, distance, radius);

            aboveLiquid = distance >= -radius;
            belowLiquid = distance <= radius;

            if (!(this->m_cache->m_flags & 0x2) && aboveLiquid && belowLiquid) {
                aboveLiquid = this->uint140 == 0;
                belowLiquid = !aboveLiquid;
            }
        }

        auto skinProfile = model->m_shared->skinProfile;
        int32_t merged = model->ptr2D0 != nullptr;

        // M2UseZFill: alpha-tested batches of a model that asks for it lay depth first.
        int32_t zFill = (this->m_cache->m_flags & 0x1) && !(model->m_flags & 0x1) && model->m_flag40;

        // A model whose visible sections have been merged walks the MERGED batches, which are
        // fewer; a merged batch exists only because its sources were visible, so it skips the
        // per-section visibility test.
        uint32_t batchCount = merged ? model->ptr2D0->batchCount : skinProfile->batches.Count();

        for (uint32_t batchIndex = 0; batchIndex < batchCount; batchIndex++) {
            M2Batch* batch;
            M2SkinSection* skinSection;

            if (merged) {
                batch = &model->ptr2D0->batches[batchIndex];
                skinSection = &model->ptr2D0->skinSections[batch->skinSectionIndex];
            } else {
                batch = &skinProfile->batches[batchIndex];
                skinSection = &model->m_shared->m_skinSections[batch->skinSectionIndex];

                if (!model->m_skinSections[batch->skinSectionIndex]) {
                    continue;
                }
            }

            if (batch->shader == 0x8000) {
                continue;
            }

            float alpha = model->alpha19C;

            if (batch->colorIndex < data->colors.Count()) {
                alpha *= model->m_colors[batch->colorIndex].alphaTrack.currentValue;
            }

            if (batch->textureCount) {
                alpha *= model->m_textureWeights[data->textureWeightCombos[batch->textureWeightComboIndex]].weightTrack.currentValue;
            }

            if (!(alpha >= 0.0001f)) {
                continue;
            }

            M2Material* material = &data->materials[batch->materialIndex];

            int32_t projected = (batch->flags & 0x4) && this->m_projectionCallback;

            M2Material* layerMaterial = batch->materialLayer
                ? &data->materials[batch->materialIndex - batch->materialLayer]
                : material;

            int32_t transparent = layerMaterial->blendMode > 1 || alpha < 0.99999f;

            CShaderEffect* effect = merged
                ? model->ptr2D0->effects[batchIndex]
                : model->m_shared->m_batchShaders[batchIndex];

            if (!effect) {
                continue;
            }

            auto element = this->m_elements.New();

            if (projected) {
                element->type = 1;
            } else if (!model->IsBatchDoodadCompatible(batch) || transparent) {
                element->type = 0;
            } else {
                element->type = 2;
            }

            element->model = model;
            element->flags = 0x0;

            if (transparent && aboveLiquid && belowLiquid && !projected) {
                element->flags = 0x2;
            }

            if (merged) {
                element->flags |= 0x4;
            }

            element->alpha = alpha;
            element->index = batchIndex;
            element->priorityPlane = batch->priorityPlane;
            element->batch = batch;
            element->skinSection = skinSection;
            element->effect = effect;

            CM2Scene::ComputeElementShaders(element);

            // Two depths: float14 orders the transparent lists, float10 the opaque one. An opaque
            // batch sorts by the model; a transparent one by its section's sort centre, pushed
            // toward or away from the camera by the section's sort radius when the batch asks.
            float sortDistance;

            if (!transparent) {
                element->float14 = model->float88;
                sortDistance = model->float88;
            } else if (!(data->flags & 0x10)) {
                const C44Matrix& bone = model->m_boneMatrices[skinSection->centerBoneIndex];
                C3Vector center = skinSection->sortCenterPosition * bone;
                float distance;

                if (!(batch->flags & (0x1 | 0x2))) {
                    distance = center.x * center.x + center.y * center.y + center.z * center.z;
                } else {
                    C3Vector direction = center;
                    float lengthSquared = center.x * center.x + center.y * center.y + center.z * center.z;

                    if (lengthSquared > 2.3841858e-07f) {
                        float scale = 1.0f / sqrtf(lengthSquared);
                        direction = { center.x * scale, center.y * scale, center.z * scale };
                    }

                    float offset = sqrtf(bone.a0 * bone.a0 + bone.a1 * bone.a1 + bone.a2 * bone.a2) * skinSection->sortRadius;

                    if (batch->flags & 0x1) {
                        offset = -offset;
                    }

                    center.x += offset * direction.x;
                    center.y += offset * direction.y;
                    center.z += offset * direction.z;

                    distance = center.x * center.x + center.y * center.y + center.z * center.z;

                    if (center.z < 0.0f) {
                        distance = -distance;
                    }
                }

                element->float14 = distance;
                sortDistance = (!zFill || projected || (material->flags & 0x10)) ? distance : model->float88;
            } else {
                element->float14 = (skinSection->sortCenterPosition * model->m_boneMatrices[skinSection->centerBoneIndex]).SquaredMag();
                sortDistance = model->float88;
            }

            element->float10 = sortDistance;

            if (element->type == 2) {
                *this->array44.New() = elementIndex;
            } else if (transparent) {
                if (!projected) {
                    if (aboveLiquid) {
                        *this->array54[1].New() = elementIndex;
                    }

                    if (belowLiquid) {
                        *this->array54[2].New() = elementIndex;
                    }
                } else if (belowLiquid) {
                    *this->array54[2].New() = elementIndex;
                } else {
                    *this->array54[1].New() = elementIndex;
                }
            } else {
                *this->array54[0].New() = elementIndex;
            }

            elementIndex++;

            // The alpha-tested depth prepass: a verbatim copy of the element, flagged 0x1, which
            // CM2SceneRender::SetupMaterial draws alpha-keyed with colour writes off. Both copies
            // sort last among the transparent elements.
            if (zFill && !projected && transparent && !(material->flags & 0x10)) {
                element->float14 = FLT_MAX;

                // Through the array: New() can reallocate.
                auto prepass = this->m_elements.New();
                *prepass = this->m_elements[elementIndex - 1];
                prepass->flags |= 0x1;

                if (aboveLiquid) {
                    *this->array54[1].New() = elementIndex;
                }

                if (belowLiquid) {
                    *this->array54[2].New() = elementIndex;
                }

                elementIndex++;
            }
        }

        // Ribbon elements, type 3: one per emitter with any trail to draw.
        if (model->m_ribbonEmitters) {
            for (int32_t i = 0; i < data->ribbons.Count(); i++) {
                CM2Ribbon* emitter = model->m_ribbonEmitters[i];

                if (emitter->IsEmpty()) {
                    continue;
                }

                const M2Ribbon& file = data->ribbons[i];

                // The model's alpha, scaled by the ribbon's own only when that track has keys.
                float alpha = model->float198;

                if (file.alphaTrack.sequenceTimes.Count()) {
                    alpha *= model->m_ribbons[i].alphaTrack.currentValue;
                }

                // The pass is chosen from the first material only.
                const M2Material& material = data->materials[file.materialIndices[0]];

                M2Element* element = this->m_elements.New();

                if (!element) {
                    continue;
                }

                element->alpha = alpha;
                element->index = i;
                element->type = 3;
                element->model = model;
                element->flags = 0x0;
                element->priorityPlane = file.priorityPlane;
                element->float10 = model->float88;
                element->effect = nullptr;
                element->float14 = model->float88;
                element->vertexPermute = 0xFFFFFFFF;
                element->pixelPermute = 0xFFFFFFFF;
                element->dword34 = 0;

                if (material.blendMode > 1 || alpha < 0.99999f) {
                    if (!aboveLiquid) {
                        *this->array54[2].New() = elementIndex;
                    } else {
                        *this->array54[1].New() = elementIndex;
                    }
                } else {
                    *this->array54[0].New() = elementIndex;
                }

                elementIndex++;
            }
        }

        // The owner's own draw, type 5 (CM2SceneRender::DrawCallback): opaque when the model is
        // flagged so, otherwise on the model's side of the liquid.
        if (model->m_drawCallback) {
            M2Element* element = this->m_elements.New();

            if (element) {
                element->type = 5;
                element->alpha = 1.0f;
                element->model = model;
                element->flags = 0x0;
                element->index = 0;
                element->priorityPlane = 0;
                element->float10 = model->float88;
                element->effect = nullptr;
                element->float14 = model->float88;
                element->vertexPermute = 0xFFFFFFFF;
                element->pixelPermute = 0xFFFFFFFF;
                element->dword34 = 0;

                if (model->m_flag20) {
                    *this->array54[0].New() = elementIndex;
                } else if (!aboveLiquid) {
                    *this->array54[2].New() = elementIndex;
                } else {
                    *this->array54[1].New() = elementIndex;
                }

                elementIndex++;
            }
        }
    }

    // The particle draw list: each model's emitters. How many of the elements use an additive
    // blend decides below whether the additive runs are regrouped.
    uint32_t additiveCount = 0;

    while (this->m_particleDrawList) {
        auto model = this->m_particleDrawList;
        this->m_particleDrawList = model->m_particleDrawNext;

        model->m_particleDrawPrev = nullptr;
        model->m_particleDrawNext = nullptr;

        if (!model->IsDrawable(0, 0)) {
            continue;
        }

        auto lighting = model->m_currentLighting;
        auto data = model->m_shared->m_data;

        int32_t aboveLiquid = (lighting->m_flags & 0x20) != 0;

        if (aboveLiquid && (lighting->m_flags & 0x40)) {
            float distance;
            float radius;
            M2LiquidPlaneDistance(model, distance, radius);

            aboveLiquid = distance >= -radius;
        }

        for (int32_t i = 0; i < data->particles.Count(); i++) {
            CM2ParticleEmitter* emitter = model->m_particleEmitters
                ? model->m_particleEmitters[i]
                : nullptr;

            // Frozen-only: null for an emitter type frozen does not build. The reference's
            // factory always builds something.
            if (!emitter) {
                continue;
            }

            if (model->m_flag2000 && (emitter->m_flags & 0x200)) {
                continue;
            }

            if (emitter->m_flags & 0x2000000) {
                continue;
            }

            if (!model->m_particles[i].active) {
                continue;
            }

            float alpha = model->float198;

            if (!(alpha >= 0.0001f)) {
                continue;
            }

            const M2Particle& file = data->particles[i];

            // Frozen-only guard: frozen's loader does not validate boneIndex against the bone
            // count the way the reference's does.
            if (!model->m_boneMatrices || file.boneIndex >= data->bones.Count()) {
                continue;
            }

            // The emitter's position in the model's space, squared.
            C3Vector local = file.position * model->m_boneMatrices[file.boneIndex];
            float distance = local.x * local.x + local.y * local.y + local.z * local.z;

            this->AddParticleElement(emitter, model, distance, alpha, aboveLiquid, elementIndex, additiveCount);

            for (uint32_t c = 0; c < emitter->m_childCount; c++) {
                this->AddParticleElement(emitter->m_children[c], model, distance, alpha, aboveLiquid,
                                         elementIndex, additiveCount);
            }
        }
    }

    // The doodad list: batchable opaque elements are grouped by everything a shared draw has to
    // agree on, through a 251-slot open-addressing table keyed by M2DoodadBatchHash; a group of two
    // or more is drawn as instances of its head, and a lone member becomes an ordinary opaque
    // element.
    uint32_t doodadCount = this->array44.Count();

    if (doodadCount > 1) {
        memset(s_doodadBatchTable, 0xFF, sizeof(s_doodadBatchTable));

        for (uint32_t i = 0; i < doodadCount; i++) {
            uint32_t index = this->array44[i];
            uint32_t start = M2DoodadBatchHash(&this->m_elements[index]) % 251;
            uint32_t slot = start;

            do {
                slot++;

                if (slot > 250) {
                    slot = 0;
                }

                if (s_doodadBatchTable[slot] == 0xFFFFFFFF || slot == start) {
                    s_doodadBatchTable[slot] = index;
                    break;
                }
            } while (M2DoodadBatchCompare(index, s_doodadBatchTable[slot], this) != 0);

            this->m_elements[index].doodadGroup = s_doodadBatchTable[slot];
        }
    }

    M2HeapSort(CM2Scene::SortDoodadGroups, this->array44.Ptr(), doodadCount, this);

    uint32_t kept = 0;

    for (uint32_t first = 0; first < doodadCount; first++) {
        uint32_t head = this->array44[first];
        this->array44[kept] = head;

        uint32_t out = kept + 1;
        uint32_t next = first + 1;

        for (; next < doodadCount; next++) {
            uint32_t member = this->array44[next];

            if (this->m_elements[head].doodadGroup != this->m_elements[member].doodadGroup) {
                break;
            }

            this->array44[out++] = member;
        }

        if (next - first < 2) {
            this->m_elements[head].type = 0;
            CM2Scene::ComputeElementShaders(&this->m_elements[head]);
            *this->array54[0].New() = head;
            out--;
        } else {
            this->m_elements[head].doodadCount = next - first;
            first = next - 1;
        }

        kept = out;
    }

    this->array44.SetCount(kept);

    // The three pass lists are always sorted, opaque by SortOpaque and both transparent lists by
    // SortTransparent.
    M2HeapSort(CM2Scene::SortOpaque, this->array54[0].Ptr(), this->array54[0].Count(), this);
    M2HeapSort(CM2Scene::SortTransparent, this->array54[1].Ptr(), this->array54[1].Count(), this);
    M2HeapSort(CM2Scene::SortTransparent, this->array54[2].Ptr(), this->array54[2].Count(), this);

    // M2ForceAdditiveParticleSort (cache flag 0x80): with more than one additive particle element,
    // the transparent lists are re-sorted so additive runs stay together. A re-sort on top of the
    // depth sort above, never instead of it.
    if ((this->m_cache->m_flags & 0x80) && additiveCount > 1) {
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
// The particle ground query, reference 0x00dce8c8 / 0x00dce8c4: one for the whole process, so the
// last scene to install a query is the one every emitter asks.
void* g_m2ParticleQueryCallback;
void* g_m2ParticleQueryContext;

// ref: FUN_0081e590
// Installs the query on the scene and as the process-wide one.
void CM2Scene::SetParticleQueryCallback(void* callback, void* context) {
    this->m_particleQueryCallback = callback;
    this->m_particleQueryContext = context;
    g_m2ParticleQueryCallback = callback;
    g_m2ParticleQueryContext = context;
}

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

// ref: FUN_00823cb0
// One pass of the scene: that pass's list, and for pass 0 the doodad list after it. Only the passes
// in m_passMask draw; every scene starts with all of them, and the world frame narrows its own.
int32_t CM2Scene::Draw(M2PASS pass) {
    if (!(this->m_passMask & (1u << pass))) {
        return 1;
    }

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

    // THE CASTER SET IS NOT THE OPAQUE PASS. It used to be: this submitted array54[M2PASS_0],
    // and a batch only reaches pass 0 at alpha >= 0.99999. The reference does not reuse a draw
    // pass at all -- CM2Model::CollectShadowCasters (FUN_00834660) walks every model's batches
    // and keeps those at alpha >= 0.55, so the whole band of partial transparency cast a shadow
    // there and nothing here: a unit fading in or out, a stealthing rogue, any animated alpha
    // track part-way through its curve. The two thresholds answer different questions -- 0.99999
    // is 'does this need sorting', 0.55 is 'does this block enough light to matter'.
    //
    // THE GATE IS THE REFERENCE'S, THE MECHANISM IS FROZEN'S, and that is a deliberate trade.
    // The reference walks models and collects (model, batch) pairs into two lists, then draws
    // them through an INSTANCED path (FUN_0082da40 -> FUN_00829e40 -> FUN_00829ba0) that packs
    // bone matrices for several casters into one constant upload. frozen has no instanced model
    // draw, so rebuilding that path would mean inventing most of it. What it does have is
    // m_elements -- one entry per drawable batch, carrying the model, the batch, the resolved
    // skin section and the SAME alpha product the reference tests -- already built this frame.
    // Filtering that with the reference's own gate selects the identical set of batches and
    // reuses a draw path that works. CollectShadowCasters stays as the faithful transcription of
    // the walk, for whoever lands the instanced path.
    //
    // The two lists split on the batch's shader field, and they are NOT drawn the same way: the
    // reference gives list 0 an alpha ref of 0 and list 1 an alpha ref of 0.50196 (DAT_00a45568,
    // which is 128/255). Drawing both at one alpha ref was a second, smaller defect hidden
    // behind the first.
    static const float CASTER_MIN_ALPHA = 0.55f;          // DAT_009edce0
    static const float CASTER_ALPHA_REF = 0.501960814f;   // DAT_00a45568, 128/255

    TSGrowableArray<uint32_t> casterLists[2];

    for (uint32_t i = 0; i < this->m_elements.Count(); i++) {
        M2Element* element = &this->m_elements[i];

        // Batch elements only. Particles are type 4 and ribbons type 3, and neither has a batch.
        if (element->type > 2 || !element->batch || !element->model) {
            continue;
        }

        M2Batch* batch = element->batch;

        // Read as a SIGNED short, which is how the reference compares it.
        if (static_cast<int16_t>(batch->shader) == static_cast<int16_t>(0x8000)) {
            continue;
        }

        if (batch->flags & 0x4) {
            continue;
        }

        // Base layer only.
        if (batch->materialLayer != 0) {
            continue;
        }

        auto data = element->model->m_shared ? element->model->m_shared->m_data : nullptr;

        if (!data || batch->materialIndex >= data->materials.Count()) {
            continue;
        }

        M2Material& material = data->materials[batch->materialIndex];

        if (material.flags & 0x40) {
            continue;
        }

        // Either the material says so outright, or its blend mode is one that occludes.
        if (!(material.flags & 0x80) && material.blendMode != 0 && material.blendMode != 1) {
            continue;
        }

        if (element->alpha < CASTER_MIN_ALPHA) {
            continue;
        }

        *casterLists[batch->shader == 0 ? 0 : 1].New() = i;
    }

    CM2SceneRender render(this);

    // Whether the map has any content at all is answerable without reading the texture back: if
    // nothing is submitted, the map is the white it was cleared to. Reported once so a run says
    // plainly whether shadows are being cast, and against what the opaque pass would have given.
    static bool reported = false;

    if (!reported) {
        reported = true;
        fprintf(
            stderr,
            "MapShadow: caster pass %u + %u batches at alpha >= 0.55 (opaque pass would give %u)\n",
            casterLists[0].Count(), casterLists[1].Count(), this->array54[M2PASS_0].Count());
    }

    // This pass draws casters one batch at a time. A doodad-batch element (type 2) only means
    // something inside the scene's doodad list, where the group head carries the instance count;
    // here it would send DrawBatchDoodad an uninitialised count over the wrong index list. Before
    // the doodad grouping went live no element was type 2, so they are drawn as plain batches for
    // the length of the pass and restored after. The reference casts through its own instanced
    // path (CM2Model::DrawShadowCasterLists, FUN_0082da40) instead of this one.
    TSGrowableArray<uint32_t> doodadElements;

    for (uint32_t list = 0; list < 2; list++) {
        for (uint32_t i = 0; i < casterLists[list].Count(); i++) {
            M2Element& element = this->m_elements[casterLists[list][i]];

            if (element.type == 2) {
                element.type = 0;
                *doodadElements.New() = casterLists[list][i];
            }
        }
    }

    CShaderEffect::SetAlphaRef(0.0f);
    render.Draw(M2PASS_0, this->m_elements.m_data, casterLists[0].m_data, casterLists[0].Count());

    CShaderEffect::SetAlphaRef(CASTER_ALPHA_REF);
    render.Draw(M2PASS_0, this->m_elements.m_data, casterLists[1].m_data, casterLists[1].Count());

    for (uint32_t i = 0; i < doodadElements.Count(); i++) {
        this->m_elements[doodadElements[i]].type = 2;
    }

    // How many of the submitted batches actually reached a draw call. A caster pass that
    // submits thousands and draws none looks identical, from the log, to one that works.
    static bool drawnReported = false;

    if (!drawnReported) {
        drawnReported = true;
        fprintf(
            stderr,
            "MapShadow: %u of %u caster batches reached GxDraw; effect %p shaders %d\n",
            CM2SceneRender::s_shadowCasterDrawn,
            casterLists[0].Count() + casterLists[1].Count(),
            static_cast<void*>(effect), CShaderEffect::s_enableShaders ? 1 : 0);
    }

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

    //
    // FLOOR, not truncation: each bound goes through FUN_0088ce30, the C runtime's floor, before
    // the cast. Truncation rounds toward zero, so for the negative half of the world -- which is
    // most of it -- every bound landed one cell high and the sweep missed the low edge's lights.
    int32_t xMin = static_cast<int32_t>(floorf((c.x - r) * 0.05f - 0.5f)) & 0x3f;
    int32_t xMax = static_cast<int32_t>(floorf((c.x + r) * 0.05f + 0.5f)) & 0x3f;
    int32_t yMin = static_cast<int32_t>(floorf((c.y - r) * 0.05f - 0.5f)) & 0x3f;
    int32_t yMax = static_cast<int32_t>(floorf((c.y + r) * 0.05f + 0.5f)) & 0x3f;

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

                // Written with the cull first because that is the order the reference's code is
                // laid out in (the SetVisible call precedes the AddLight in the binary); the
                // condition is the same one inverted.
                if (light->m_scene && light->m_updateStamp != this->uint14) {
                    // Nobody drove this light this frame, so it belongs to a model that stopped
                    // animating. The reference culls it here rather than anywhere else.
                    light->SetVisible(0);
                } else {
                    lighting->AddLight(light);
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

    this->ReserveRayProjected(vertexCount);

    if (!this->m_rayProjected) {
        return best;
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

// Make room for `count` projected vertices, doubling from one and keeping nothing.
//
// The reference open-codes this in RayTestModel and again in RayTestModelGeometry. One copy is
// the same doubling with one place to get it wrong -- a FORM divergence only; the sequence of
// frees, the growth and the zero-fill are unchanged.
void CM2Scene::ReserveRayProjected(uint32_t count) {
    if (this->m_rayProjectedCapacity >= count) {
        return;
    }

    if (this->m_rayProjected) {
        SMemFree(this->m_rayProjected, "delete[]", -1, 0);

        this->m_rayProjected = nullptr;
    }

    if (!this->m_rayProjectedCapacity) {
        this->m_rayProjectedCapacity = 1;
    }

    while (this->m_rayProjectedCapacity < count) {
        this->m_rayProjectedCapacity <<= 1;
    }

    this->m_rayProjected = static_cast<C3Vector*>(
        SMemAlloc(sizeof(C3Vector) * this->m_rayProjectedCapacity, __FILE__, __LINE__,
                  SMEM_FLAG_ZEROMEMORY));

    if (!this->m_rayProjected) {
        this->m_rayProjectedCapacity = 0;
    }
}

// ref: FUN_0081cff0
// The BROAD PHASE: walk the ray model list, keep the ones whose bounding sphere the ray reaches,
// and record where it enters and leaves each. The driver sorts those by entry distance and then
// does the exact test on them in order.
//
// The list is CONSUMED as it is walked -- each model is unlinked on the way past, whether or not
// it is kept -- so the ray list is empty when this returns and the next query starts clean. That
// is the reference's own behaviour and the reason nothing else has to clear it.
//
// THE SPHERE comes from a bounding box turned into a centre and a radius, brought into view space
// by the model's own matrix, with the radius scaled by that matrix's first row -- a uniform scale
// assumption the reference makes and this keeps. The test is the standard one: project the centre
// onto the ray, reject when the perpendicular distance exceeds the radius, then reject again when
// both intersections fall off either end of the segment. The two kept distances are clamped into
// [0, length], which is what M2SceneRayCandidate's declaration already describes.
//
// TWO THINGS TO KNOW, both marked where they happen:
//
//   The un-animated fix-up assigns matrixB4 * m_view. The reference's destination register does
//   not survive the decompilation, but it cannot be anything else: matrixF4 is the only matrix
//   the rest of the function reads, and computing a model-view matrix to then not use it would
//   be dead code. m_view is scene + 0x84, which is where array54's three entries end.
//
//   The non-collision bounds are the CURRENT SEQUENCE's animated box, which is what the reference
//   uses. This was a documented widening for one commit, because model + 0x94 was unidentified.
//   It is m_bones: the reference reads a uint16 at +0x48 of whatever it points at, and
//   M2ModelBoneSeq sits at +0x40 inside M2ModelBone with its uint8 field at +0x48 -- and frozen's
//   own CM2Model already indexes sequences[] with exactly that field. M2Sequence's 0x40 stride and
//   its bounds at +0x20 match the reference's scaling too, which is three independent checks.
//
//   The fallback to the model's overall box is kept for a bone with no sequence, an index past
//   the end, or a model whose bones have not been built. That box encloses every sequence, so the
//   sphere only ever comes out too big -- extra candidates for the exact test to reject, never a
//   dropped hit.
uint32_t CM2Scene::CollectRayCandidates(const C3Vector& start, const C3Vector& dir, float length,
                                        int32_t requireAnimated) {
    const float epsilon = 9.9999997e-6f;

    uint32_t count = 0;

    CM2Model* model = this->m_rayModelList;

    while (model) {
        CM2Model* next = model->m_rayNext;

        if (model->m_rayPrev) {
            *model->m_rayPrev = nullptr;

            model->m_rayPrev = nullptr;
        }


        do {
            if (!model->m_loaded || model->m_animCounter == 0xFFFFFFFF) {
                break;
            }

            if (requireAnimated && model->m_animCounter != this->uint14) {
                if (model->m_rayQueryType != 3 || model->m_attachParent) {
                    break;
                }

                model->matrixF4 = model->matrixB4 * this->m_view;
            }

            if (!model->m_shared || !model->m_shared->m_m2DataLoaded || !model->m_shared->m_data) {
                break;
            }

            M2Data* data = model->m_shared->m_data;

            const M2Bounds* bounds = &data->collisionBounds;

            if (model->m_rayQueryType != 3) {
                bounds = &data->bounds;

                if (model->m_bones) {
                    uint16_t sequence = model->m_bones[0].sequence.uint8;

                    if (sequence != 0xFFFF && sequence < data->sequences.Count()) {
                        bounds = &data->sequences[sequence].bounds;
                    }
                }
            }

            if (fabsf(bounds->radius) < epsilon) {
                break;
            }

            // CAaBox's corners are b (bottom) and t (top); the reference averages the two.
            C3Vector centre = { (bounds->extent.t.x + bounds->extent.b.x) * 0.5f,
                                (bounds->extent.t.y + bounds->extent.b.y) * 0.5f,
                                (bounds->extent.t.z + bounds->extent.b.z) * 0.5f };

            C3Vector c = centre * model->matrixF4;

            float scale = model->matrixF4.a0 * model->matrixF4.a0
                        + model->matrixF4.a1 * model->matrixF4.a1
                        + model->matrixF4.a2 * model->matrixF4.a2;

            float radiusSq = scale * bounds->radius * bounds->radius;

            float along = dir.x * (c.x - start.x) + dir.y * (c.y - start.y)
                        + dir.z * (c.z - start.z);

            float px = dir.x * along - (c.x - start.x);
            float py = dir.y * along - (c.y - start.y);
            float pz = dir.z * along - (c.z - start.z);

            float perpSq = px * px + py * py + pz * pz;

            if (perpSq > radiusSq) {
                break;
            }

            float half = radiusSq - perpSq;

            // Both intersections behind the start, or both past the end, and the segment misses.
            if (along < 0.0f && along * along > half) {
                break;
            }

            float beyond = along - length;

            if (beyond > 0.0f && beyond * beyond > half) {
                break;
            }

            float root = sqrtf(half);

            float tNear = along - root;
            float tFar = along + root;

            tNear = tNear < 0.0f ? 0.0f : (tNear > length ? length : tNear);
            tFar = tFar < 0.0f ? 0.0f : (tFar > length ? length : tFar);

            M2SceneRayCandidate& candidate = this->m_rayCandidates[count];

            candidate.model = model;
            candidate.tNear = tNear;
            candidate.tFar = tFar;
            candidate.key = model->m_rayKey;

            this->m_rayCandidateOrder[count] = count;

            count++;
        } while (false);

        model = next;
    }

    return count;
}

// ref: FUN_0081df10
// Cast a ray through the scene. The top of the chain: set the ray up, collect the models whose
// bounding spheres it reaches, sort those near to far, and test them in that order until one is
// hit. Returns the owner of whatever was hit, and leaves the full result in m_rayHit*.
//
// THE PLANE the whole chain projects onto is the one through the ray's start with the ray's
// direction as its normal -- which is why planeDist is dot(start, dir) and the 2D query point is
// the start itself: the start's own height above that plane is zero, so it projects to its own x
// and y. That is the piece that makes the projections and RayTestTriangles agree.
//
// TWO PASSES, and the second only when the caller allows it. The first walks the sorted
// candidates and stops early -- the list is ordered by entry distance, so once a candidate starts
// further away than the best hit so far, nothing behind it can win. The second pass drops that
// shortcut and instead prefers a candidate with a HIGHER key, breaking ties on distance; it runs
// only when the first found nothing.
//
// THE FLOAT TESTS ARE DECODED, not transcribed from the decompiler, which renders both as
// comparisons between two booleans. `a < b != (a == b)` is true when exactly one holds, and they
// are mutually exclusive, so it is a <= b. `a < b == (a == b)` is true when neither holds, so it
// is a > b. Getting either backwards would either stop the search one candidate early or never
// stop it.
//
// The owner is found by walking up the attach chain from the model that was hit until one carries
// a real m_rayOwner -- so a hit on an attached piece reports whatever it is attached to. The
// reference's sentinel for `none` is -1 where frozen's is null, the field being a pointer here.
void* CM2Scene::RayQuery(const C3Vector& start, const C3Vector& end, float* fraction,
                         int32_t allowSecondPass) {
    C3Vector dir = { 0.0f, 0.0f, 0.0f };
    float length = 0.0f;

    if (!this->RaySetup(start, end, *fraction, &length, &dir)) {
        return nullptr;
    }

    this->ReserveRayCandidates();

    uint32_t count = this->CollectRayCandidates(start, dir, length, 1);

    M2HeapSort(CM2Scene::SortRayCandidates, this->m_rayCandidateOrder, count, this->m_rayCandidates);

    M2SceneRayCandidate* best = nullptr;

    float planeDist = start.x * dir.x + start.y * dir.y + start.z * dir.z;

    // The search limit: the caller's fraction of the ray, or all of it.
    float limit = *fraction < 1.0f ? *fraction * length : length;

    for (uint32_t pass = 0; ; pass++) {
        float bestT = limit;

        for (uint32_t i = 0; i < count; i++) {
            M2SceneRayCandidate* candidate = &this->m_rayCandidates[this->m_rayCandidateOrder[i]];

            if (pass == 0) {
                if (bestT <= candidate->tNear) {
                    break;
                }
            } else if (best
                       && !(best->key <= candidate->key
                            && (best->key != candidate->key || bestT > candidate->tNear))) {
                continue;
            }

            CM2Model* model = candidate->model;

            if (model->m_rayQueryType == 3) {
                best = this->RayTestModel(model, pass, dir, planeDist,
                                          *reinterpret_cast<const C2Vector*>(&start), candidate,
                                          &bestT, best);
            } else {
                best = this->RayTestModelGeometry(model, pass, dir, planeDist,
                                                  *reinterpret_cast<const C2Vector*>(&start),
                                                  candidate, &bestT, best);
            }
        }

        limit = bestT;

        if (best || !allowSecondPass || pass >= 1) {
            break;
        }

        limit = length;
    }

    this->m_flags &= ~0x2u;

    if (!best) {
        return nullptr;
    }

    *fraction = limit / length;

    void* owner = nullptr;

    for (CM2Model* m = best->model; m; m = m->m_attachParent) {
        if (m->m_rayOwner) {
            owner = m->m_rayOwner;

            break;
        }
    }

    this->m_rayHitModel = best->model;
    this->m_rayHitNear = best->tNear;
    this->m_rayHitFar = best->tFar;
    this->m_rayHitKey = best->key;
    this->m_rayHitOwner = owner;

    return owner;
}

// ref: FUN_0081daf0
// Walk one model's batches, project every section that is actually visible, and test its
// triangles. The counterpart to RayTestModel, which runs against the collision hull instead --
// this one hits what is DRAWN, which is why every test below is a visibility test.
//
// A batch is skipped unless it is material layer 0 and clear of flag 0x8 -- one draw per section
// rather than one per layer, the same rule DrawReceiverGeometry uses -- and then:
//
//   * ray query type 2 wants only blended materials, type 1 only opaque ones unless the material
//     carries flag 0x20. Those are the two halves of the reference's condition, which reads as a
//     double negative and is written here as the two cases it actually excludes.
//   * the section must be switched on in m_skinSections, which is the geoset visibility array.
//   * the accumulated alpha must be positive: the model's own, times the animated colour's alpha
//     when the batch names one the file carries, times the animated texture weight when the batch
//     has any textures. A fully transparent batch is not hittable.
//
// THE ALPHA CHAIN'S OFFSETS were checked rather than assumed, because two of them are inside
// animated-state structs: M2ModelColor is 0x20 bytes and its alpha track's value lands at +0x1c,
// M2ModelTextureWeight is 0x0c with its value at +8, and those are exactly the strides and
// displacements the reference uses.
//
// WHICH BLEND a multi-bone section uses is the one thing not reproduced. The reference picks the
// 4x4 variant on bit 4 of the global at 0x00d3fcec, which is written by FUN_0081c0d0 -- unported,
// and NOT CM2Scene::s_optFlags, which is a different word. The flag below stands in for it so the
// branch is present and named; nothing sets it, so the 3x4 path runs, which is the reference's
// behaviour whenever that bit is clear.
M2SceneRayCandidate* CM2Scene::RayTestModelGeometry(CM2Model* model, int32_t addNormal,
                                                    const C3Vector& planeNormal, float planeDist,
                                                    const C2Vector& point,
                                                    M2SceneRayCandidate* candidate,
                                                    float* bestHeight, M2SceneRayCandidate* best) {
    // FROZEN-ONLY, the bug class CLAUDE.md names: the skin profile and the model data are set
    // when their async reads land, and neither is usable until its loaded bit is up.
    if (!model || !model->m_shared || !model->m_shared->m_m2DataLoaded
        || !model->m_shared->m_skinProfileLoaded || !model->m_shared->m_data
        || !model->m_shared->skinProfile) {
        return best;
    }

    M2Data* data = model->m_shared->m_data;
    M2SkinProfile* skin = model->m_shared->skinProfile;

    for (uint32_t b = 0; b < skin->batches.Count(); b++) {
        const M2Batch& batch = skin->batches[b];

        if (batch.materialLayer != 0 || (batch.flags & 0x8)) {
            continue;
        }

        if (batch.materialIndex >= data->materials.Count()) {
            continue;
        }

        const M2Material& material = data->materials[batch.materialIndex];

        if (model->m_rayQueryType == 2 && material.blendMode == 0) {
            continue;
        }

        if (model->m_rayQueryType == 1 && material.blendMode != 0
            && !(material.flags & 0x20)) {
            continue;
        }

        if (!model->m_skinSections || !model->m_skinSections[batch.skinSectionIndex]) {
            continue;
        }

        float alpha = model->alpha19C;

        if (batch.colorIndex < data->colors.Count() && model->m_colors) {
            alpha *= model->m_colors[batch.colorIndex].alphaTrack.currentValue;
        }

        if (batch.textureCount && model->m_textureWeights
            && batch.textureWeightComboIndex < data->textureWeightCombos.Count()) {
            uint16_t weight = data->textureWeightCombos[batch.textureWeightComboIndex];

            alpha *= model->m_textureWeights[weight].weightTrack.currentValue;
        }

        if (alpha <= 0.0f) {
            continue;
        }

        M2SkinSection& section = skin->skinSections[batch.skinSectionIndex];

        this->ReserveRayProjected(section.vertexCount);

        if (!this->m_rayProjected) {
            return best;
        }

        if (section.boneInfluences == 1) {
            this->ProjectSectionVertices(model, skin, &section, addNormal, planeNormal, planeDist);
        } else if (!CM2Scene::s_rayBlend4x4) {
            this->ProjectSectionVerticesBlended3x4(model, skin, &section, addNormal, planeNormal,
                                                   planeDist);
        } else {
            this->ProjectSectionVerticesBlended4x4(model, skin, &section, addNormal, planeNormal,
                                                   planeDist);
        }

        const uint16_t* indices = &skin->indices[0] + section.indexStart;

        best = this->RayTestTriangles(indices, indices + section.indexCount, section.vertexStart,
                                      point, addNormal, candidate, bestHeight, best);
    }

    return best;
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

// ref: FUN_0081d830
// The same projection as above for a section whose vertices are blended across several bones.
//
// The difference is one matrix. Where the single-bone version indexes m_boneMatrices directly,
// this asks BlendBoneMatrices3x4 for the weighted combination and then uses it exactly as the
// other one uses the bone -- transform the position, optionally push out along the rotated
// normal, project onto the plane keeping the signed distance in z.
//
// THE BLEND IS CACHED across consecutive vertices, on BOTH the weights and the indices, because
// a skinned section is authored with long runs sharing an influence set and the blend is the
// expensive part. The two caches start at zero, which is the reference's own seed, so a first
// vertex with no weights and no indices keeps the identity the matrix is initialised to rather
// than blending anything -- reproduced rather than tidied, since that is a real run of vertices
// for an unskinned section and the identity is the right answer for it.
void CM2Scene::ProjectSectionVerticesBlended3x4(CM2Model* model, M2SkinProfile* skinProfile, M2SkinSection* section, int32_t addNormal, const C3Vector& planeNormal, float planeDist) {
    auto data = model->m_shared->m_data;
    uint32_t i = section->vertexStart;
    uint32_t end = section->vertexCount + i;
    auto dst = this->m_rayProjected;

    C44Matrix blended(1.0f);

    uint32_t cachedWeights = 0;
    uint32_t cachedIndices = 0;

    for (; i < end; i++) {
        auto& vertex = data->vertices[skinProfile->vertices[i]];

        if (vertex.weights.u != cachedWeights || vertex.indices.u != cachedIndices) {
            cachedWeights = vertex.weights.u;
            cachedIndices = vertex.indices.u;

            CM2Scene::BlendBoneMatrices3x4(model->m_boneMatrices, vertex.weights, vertex.indices,
                                           &blended);
        }

        C3Vector p = vertex.position * blended;

        if (addNormal) {
            p.x = vertex.normal.x * blended.a0 + blended.b0 * vertex.normal.y + blended.c0 * vertex.normal.z + p.x;
            p.y = blended.a1 * vertex.normal.x + blended.b1 * vertex.normal.y + blended.c1 * vertex.normal.z + p.y;
            p.z = p.z + (blended.a2 * vertex.normal.x + blended.b2 * vertex.normal.y + blended.c2 * vertex.normal.z);
        }

        float d = (planeNormal.x * p.x + planeNormal.y * p.y + planeNormal.z * p.z) - planeDist;

        dst->x = p.x - planeNormal.x * d;
        dst->y = p.y - planeNormal.y * d;
        dst->z = d;
        dst++;
    }
}

// ref: FUN_0081d680
// The same blended projection as above, asking for the FULL bone blend rather than the 3x4 one.
//
// The two bodies are otherwise identical, instruction for instruction: same vertex walk, same
// cache on weights and indices, same seeded identity, same normal push and same plane projection.
// Only the helper differs -- BlendBoneMatrices against BlendBoneMatrices3x4 -- and frozen already
// had both, with the same signature.
//
// WHICH ONE RUNS is decided by the caller, FUN_0081daf0, on bit 4 of the global at 0x00d3fcec.
// That global is NOT CM2Scene::s_optFlags, however much the shape suggests it: s_optFlags is
// written by CM2Scene::Animate from the cache's flags masked to 0xE000, while 0x00d3fcec is
// written by FUN_0081c0d0 and tested at bit 4. Conflating them would have put the wrong blend on
// every skinned hit test, so the selector is left to the caller rather than guessed at here.
void CM2Scene::ProjectSectionVerticesBlended4x4(CM2Model* model, M2SkinProfile* skinProfile, M2SkinSection* section, int32_t addNormal, const C3Vector& planeNormal, float planeDist) {
    auto data = model->m_shared->m_data;
    uint32_t i = section->vertexStart;
    uint32_t end = section->vertexCount + i;
    auto dst = this->m_rayProjected;

    C44Matrix blended(1.0f);

    uint32_t cachedWeights = 0;
    uint32_t cachedIndices = 0;

    for (; i < end; i++) {
        auto& vertex = data->vertices[skinProfile->vertices[i]];

        if (vertex.weights.u != cachedWeights || vertex.indices.u != cachedIndices) {
            cachedWeights = vertex.weights.u;
            cachedIndices = vertex.indices.u;

            CM2Scene::BlendBoneMatrices(model->m_boneMatrices, vertex.weights, vertex.indices,
                                        &blended);
        }

        C3Vector p = vertex.position * blended;

        if (addNormal) {
            p.x = vertex.normal.x * blended.a0 + blended.b0 * vertex.normal.y + blended.c0 * vertex.normal.z + p.x;
            p.y = blended.a1 * vertex.normal.x + blended.b1 * vertex.normal.y + blended.c1 * vertex.normal.z + p.y;
            p.z = p.z + (blended.a2 * vertex.normal.x + blended.b2 * vertex.normal.y + blended.c2 * vertex.normal.z);
        }

        float d = (planeNormal.x * p.x + planeNormal.y * p.y + planeNormal.z * p.z) - planeDist;

        dst->x = p.x - planeNormal.x * d;
        dst->y = p.y - planeNormal.y * d;
        dst->z = d;
        dst++;
    }
}

// How far off a triangle's plane still counts as on it, for the collision cast. DAT_00a32b48.
static const float RAY_COLLISION_EPSILON = 1.0e-6f;

// ref: FUN_0081e110
// Cast a ray at the scene's ray-type 3 models and return the owner of the nearest one hit.
//
// A sibling of RayQuery above rather than a variant of it, and it differs in both phases.
//
// BROAD PHASE IN VIEW SPACE. RayQuery collects candidates in world space; this one puts the ray
// through m_view first -- the start through the whole matrix, the direction through its rotation
// alone, and the length scaled by the length of the matrix's first row. It also asks for
// candidates WITHOUT requiring them to be animated, where RayQuery passes 1.
//
// NARROW PHASE AGAINST THE COLLISION MESH, one triangle at a time, with the ray moved into the
// model's own space through the inverse of its matrixB4 -- the matrix SetWorldTransform writes.
// Note this is NOT how CM2Scene::RayTestModel does it: that one transforms the collision VERTICES
// out by matrixF4 and leaves the ray alone. Two reference functions, two different matrices and
// two different directions of travel; neither is a simplification of the other.
//
// The parameter is a FRACTION of the segment in and out, so the hit distance is scaled back by
// the ray length before it is stored. The break is the usual near-to-far one: the candidates are
// sorted by entry distance, so the first whose entry is at or past the best hit ends the walk.
void* CM2Scene::RayQueryCollision(const C3Vector& start, const C3Vector& end, float* fraction) {
    C3Vector dir = { 0.0f, 0.0f, 0.0f };
    float length = 0.0f;

    if (!this->RaySetup(start, end, *fraction, &length, &dir)) {
        return nullptr;
    }

    this->ReserveRayCandidates();

    C3Vector viewStart = start * this->m_view;

    // The rotation only: a direction has no translation to pick up.
    C33Matrix rot(this->m_view);

    C3Vector viewDir = {
        rot.a0 * dir.x + rot.b0 * dir.y + rot.c0 * dir.z,
        rot.a1 * dir.x + rot.b1 * dir.y + rot.c1 * dir.z,
        rot.a2 * dir.x + rot.b2 * dir.y + rot.c2 * dir.z,
    };

    // The matrix's own scale, taken off its first row, so the length arrives in view units too.
    float scale = sqrtf(this->m_view.a0 * this->m_view.a0
                      + this->m_view.a1 * this->m_view.a1
                      + this->m_view.a2 * this->m_view.a2);

    uint32_t count = this->CollectRayCandidates(viewStart, viewDir, scale * length, 0);

    M2HeapSort(CM2Scene::SortRayCandidates, this->m_rayCandidateOrder, count,
               this->m_rayCandidates);

    M2SceneRayCandidate* best = nullptr;

    float limit = *fraction < 1.0f ? length * *fraction : length;

    for (uint32_t i = 0; i < count; i++) {
        M2SceneRayCandidate* candidate = &this->m_rayCandidates[this->m_rayCandidateOrder[i]];

        if (limit <= candidate->tNear) {
            break;
        }

        CM2Model* model = candidate->model;

        if (model->m_rayQueryType != 3) {
            continue;
        }

        auto data = model->m_shared->m_data;

        C44Matrix inverse = model->matrixB4.Inverse(model->matrixB4.Determinant());

        C3Vector localStart = start * inverse;
        C3Vector localEnd = end * inverse;

        C3Vector delta = { localEnd.x - localStart.x,
                           localEnd.y - localStart.y,
                           localEnd.z - localStart.z };

        // One over the local segment's length: it turns a distance along the NORMALISED local
        // direction back into a fraction of the segment, which is what the comparison wants.
        float invLocalLength = 1.0f / sqrtf(delta.x * delta.x
                                          + delta.y * delta.y
                                          + delta.z * delta.z);

        C3Ray ray;
        ray.origin = localStart;
        ray.dir = { delta.x * invLocalLength,
                    delta.y * invLocalLength,
                    delta.z * invLocalLength };

        for (uint32_t t = 0; t + 2 < data->collisionIndices.Count(); t += 3) {
            float hit = 0.0f;

            if (!IntersectRayTriangle(ray, data->collisionPositions.Data(),
                                     &data->collisionIndices[t], &hit, nullptr,
                                     RAY_COLLISION_EPSILON)) {
                continue;
            }

            if (hit < 0.0f) {
                continue;
            }

            float along = hit * invLocalLength * length;

            if (along <= limit) {
                best = candidate;
                limit = along;
            }
        }
    }

    this->m_flags &= ~0x2u;

    if (!best) {
        return nullptr;
    }

    *fraction = limit / length;

    return best->model->m_rayOwner;
}
