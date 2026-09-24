#include "model/CM2Model.hpp"
#include "util/Log.hpp"
#include <storm/String.hpp>
#include <cstdio>
#include "db/Db.hpp"
#include <cstdlib>
#include "async/AsyncFileRead.hpp"
#include "math/Types.hpp"
#include "model/CM2Scene.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Animate.hpp"
#include "model/M2Data.hpp"
#include "model/M2Internal.hpp"
#include "model/M2Model.hpp"
#include <common/DataMgr.hpp>
#include <common/ObjectAlloc.hpp>
#include <tempest/Math.hpp>
#include <cmath>
#include <new>

// Alignment helpers
#define ALIGN(addr, type) ((addr + alignof(type) - 1) & ~(alignof(type) - 1))
#define ALIGN_PAD(addr, type) (ALIGN(addr, type) - addr)
#define ALIGN_SIZE(addr, type, count) ALIGN(addr + sizeof(type) * count, type) - addr
#define ALIGN_BUFFER(current, start, type) (current + ALIGN_PAD((ptrdiff_t)((char*)current - (char*)start), type))

uint32_t CM2Model::s_loadingSequence = 0xFFFFFFFF;
uint8_t* CM2Model::s_sequenceBase;
uint32_t CM2Model::s_sequenceBaseSize;
uint32_t CM2Model::s_skinProfileBoneCountMax[] = { 256, 64, 53, 21 };

CM2Model* CM2Model::AllocModel(uint32_t* heapId) {
    uint32_t memHandle;
    void* mem = nullptr;

    if (ObjectAlloc(*heapId, &memHandle, &mem, false)) {
        auto model = new (mem) CM2Model();
        model->m_memHandle = memHandle;

        return model;
    }

    return nullptr;
}

bool CM2Model::Sub825E00(M2Data* data, uint32_t a2) {
    if (data->sequenceIdxHashById.Count() == 0) {
        for (int32_t i = 0; i < data->sequences.Count(); i++) {
            auto& sequence = data->sequences[i];

            if (sequence.id == a2) {
                return i < data->sequences.Count();
            }
        }

        return data->sequences.Count() > 0xFFFF;
    }

    uint32_t v8 = a2 % data->sequenceIdxHashById.Count();
    uint16_t v5 = data->sequenceIdxHashById[v8];
    if (v5 == 0xFFFF) {
        return data->sequences.Count() > 0xFFFF;
    }
    if (data->sequences[v5].id == a2) {
        return v5 < data->sequences.Count();
    }

    int32_t v10 = 1;
    while (1) {
        v8 = (v8 + v10 * v10) % data->sequenceIdxHashById.Count();
        v5 = data->sequenceIdxHashById[v8];
        if (v5 == 0xFFFF) {
            return data->sequences.Count() > 0xFFFF;
        }

        ++v10;

        if (data->sequences[v5].id == a2) {
            return v5 < data->sequences.Count();
        }
    }
}

uint16_t CM2Model::Sub8260C0(M2Data* data, uint32_t sequenceId, int32_t a3) {
    // Resolve an animation id to a sequence index, then walk `a3` links down the variation chain.
    //
    // The sequences of one animation are stored as a linked list threaded through variationNext:
    // the hash (or the linear scan when the model carries no hash) finds variation 0, and each
    // further variation is one hop along that chain. Returning 0xFFFF when the chain is shorter
    // than asked is what lets SetBoneSequence fall back to picking a variation at random.
    uint32_t index;

    if (data->sequenceIdxHashById.Count() == 0) {
        index = 0xFFFF;

        for (uint32_t i = 0; i < data->sequences.Count(); i++) {
            if (data->sequences[i].id == sequenceId) {
                index = i;
                break;
            }
        }
    } else {
        uint32_t slot = sequenceId % data->sequenceIdxHashById.Count();
        uint16_t probe = data->sequenceIdxHashById[slot];

        index = 0xFFFF;

        if (probe != 0xFFFF) {
            int32_t step = 1;

            while (data->sequences[probe].id != sequenceId) {
                slot = (slot + step * step) % data->sequenceIdxHashById.Count();
                probe = data->sequenceIdxHashById[slot];

                if (probe == 0xFFFF) {
                    break;
                }

                step++;
            }

            if (probe != 0xFFFF) {
                index = probe;
            }
        }
    }

    uint32_t count = data->sequences.Count();

    if (index < count) {
        while (a3) {
            index = data->sequences[index].variationNext;
            a3--;

            if (index >= count) {
                break;
            }
        }

        if (index < count && a3 == 0) {
            return static_cast<uint16_t>(index);
        }
    }

    return 0xFFFF;
}

CM2Model::~CM2Model() {
    // TODO

    // Any bone-sequence request still parked in CM2Shared's load list holds a raw pointer to this
    // model, and the callback that applies them dereferences it. The reference retires them here,
    // before anything else is torn down (FUN_00832640 calls FUN_00831e20 at 0x00832676).
    this->CancelAllDeferredSequences();

    // Unlink from lists

    this->UnlinkFromCallbackList();
    this->UnlinkFromAnimateList();
    this->UnlinkFromDrawList();

    // TODO

    this->DetachFromScene();

    if (this->m_shared) {
        this->FreeExternalResources();
        this->FreeInternalResources();

        this->m_shared->Release();
        this->m_shared = nullptr;
    }

    // Let go of every model attached to this one. Each child holds a counted reference taken when
    // it was attached, so without this they are never released and never destroyed, and each is
    // left with m_attachParent pointing at freed memory.
    //
    // DetachFromParent already does the whole of what the reference does to each child here
    // (FUN_00832640 at 0x0083274c): unlink it, clear m_flag40000, null its parent and attach id,
    // and Release it -- which destroys it in place when the count reaches zero, recursing into its
    // own children exactly as the reference recurses. It unlinks before releasing, so the head has
    // already advanced by the time the child can be freed, and this loop terminates.
    while (this->m_attachList) {
        this->m_attachList->DetachFromParent();
    }

    // TODO

    this->UnlinkFromAttachList();

    // TODO

    this->m_attachParent = nullptr;
    this->m_currentLighting = nullptr;
}

void CM2Model::AddRef() {
    this->m_refCount++;
}

// ref: FUN_00830dc0
// Bring this model's transform and bone matrices up to date for the scene's current frame, on
// demand rather than from the scene's animate pass. A model attached to another one cannot be
// animated on its own -- its transform starts at the parent's attachment point -- so the parent is
// animated first and this model is then animated onto the attachment matrix. When the animation
// could not run (not loaded, or the parent has no fresh bone matrices) the transform still has to
// be defined, so it falls back to the parent's, or to this model's own world transform.
void CM2Model::Animate() {
    if (this->m_animCounter == this->m_scene->uint14) {
        return;
    }

    if (!this->m_attachParent) {
        C3Vector diffuse = { 1.0f, 1.0f, 1.0f };
        C3Vector emissive = { 0.0f, 0.0f, 0.0f };

        if (this->m_flag1000) {
            this->AnimateMTSimple(&this->m_scene->m_view, diffuse, emissive, 1.0f, 1.0f);
        } else {
            this->AnimateMT(&this->m_scene->m_view, diffuse, emissive, 1.0f, 1.0f);
        }
    } else {
        this->m_attachParent->Animate();
    }

    auto scene = this->m_scene;

    if (this->m_animCounter == scene->uint14) {
        return;
    }

    auto parent = this->m_attachParent;

    if (parent && this->m_loaded) {
        C44Matrix view;

        const C44Matrix* attachView = &parent->matrixF4;

        if (parent->m_loaded && parent->m_animCounter == scene->uint14 && this->m_attachIndex != 0xFFFF) {
            auto& attachment = parent->m_shared->m_data->attachments[this->m_attachIndex];

            view = parent->m_boneMatrices[attachment.boneIndex];
            view.Translate(attachment.position);

            attachView = &view;
        }

        if (this->m_flag1000) {
            this->AnimateMTSimple(attachView, parent->m_currentDiffuse, parent->m_currentEmissive, parent->float198, parent->alpha19C);
        } else {
            this->AnimateMT(attachView, parent->m_currentDiffuse, parent->m_currentEmissive, parent->float198, parent->alpha19C);
        }

        if (this->m_animCounter == scene->uint14) {
            return;
        }
    }

    if (this->m_attachParent) {
        this->matrixF4 = this->m_attachParent->matrixF4;

        return;
    }

    this->matrixF4 = this->matrixB4 * scene->m_view;
}

// ref: FUN_0082e550
void CM2Model::AnimateAttachmentsMT() {
    // Animate attachment visibility

    for (int32_t i = 0; i < this->m_shared->m_data->attachments.Count(); i++) {
        auto& attachment = this->m_shared->m_data->attachments[i];
        auto& modelAttachment = this->m_attachments[i];
        auto& modelBone = this->m_bones[attachment.boneIndex];

        if (
            attachment.visibilityTrack.sequenceTimes.Count() > 1
            || (attachment.visibilityTrack.sequenceTimes.Count() == 1 && attachment.visibilityTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            uint8_t defaultValue = 1;
            M2AnimateTrack<uint8_t, uint8_t>(this, &modelBone, attachment.visibilityTrack, modelAttachment.visibilityTrack, defaultValue);
        }
    }

    // Animate attached models

    for (auto model = this->m_attachList; model; model = model->m_attachNext) {
        C44Matrix view;

        if (model->m_attachIndex == 0xFFFF) {
            if (!model->m_flag40000) {
                continue;
            }

            view = this->m_boneMatrices[0];
        } else {
            auto& attachment = this->m_shared->m_data->attachments[model->m_attachIndex];
            auto& modelAttachment = this->m_attachments[model->m_attachIndex];

            // Attachment not currently visible
            if (!modelAttachment.visibilityTrack.currentValue) {
                continue;
            }

            view = this->m_boneMatrices[attachment.boneIndex];
            view.Translate(attachment.position);
        }

        if (model->m_flag1000) {
            model->AnimateMTSimple(&view, this->m_currentDiffuse, this->m_currentEmissive, this->float198, this->alpha19C);
        } else {
            model->AnimateMT(&view, this->m_currentDiffuse, this->m_currentEmissive, this->float198, this->alpha19C);
        }
    }
}

void CM2Model::AnimateCamerasST() {
    for (int32_t i = 0; i < this->m_shared->m_data->cameras.Count(); i++) {
        auto& camera = this->m_shared->m_data->cameras[i];
        auto& modelCamera = this->m_cameras[i];

        C3Vector v56 = modelCamera.positionTrack.currentValue + camera.positionPivot;
        C3Vector cameraPos = (v56 * this->matrixF4) * this->m_scene->m_viewInv;
        DataMgrSetCoord(modelCamera.m_camera, 7, cameraPos, 0x0);

        C3Vector v57 = modelCamera.targetTrack.currentValue + camera.targetPivot;
        C3Vector targetPos = (v57 * this->matrixF4) * this->m_scene->m_viewInv;
        DataMgrSetCoord(modelCamera.m_camera, 8, targetPos, 0x0);

        DataMgrSetFloat(modelCamera.m_camera, 5, modelCamera.rollTrack.currentValue);
    }
}

// ref: FUN_0082f0f0
void CM2Model::AnimateMT(const C44Matrix* view, const C3Vector& a3, const C3Vector& a4, float a5, float a6) {
    if (!this->m_loaded) {
        return;
    }

    // Already animated for this frame of the scene.
    if (this->m_animCounter == this->m_scene->uint14) {
        return;
    }

    // Handle attachment visibility

    if (this->m_attachParent) {
        this->m_flag8 = this->m_attachParent->m_flag8 && this->m_flag80;
        this->m_flag10000 = this->m_attachParent->m_flag10000 && this->m_flag20000;

        // TODO dword174
    }

    // TODO

    for (int32_t i = 0; i < this->m_shared->m_data->loops.Count(); i++) {
        auto loopLength = this->m_shared->m_data->loops[i].length;
        this->m_loops[i] = loopLength ? (this->m_scene->m_time - this->uint74) % loopLength : 0;
    }

    this->matrixF4 = this->matrixB4 * *view;


    this->float88 = !this->m_attachParent || this->m_attachParent->m_flags & 0x1
        ? this->matrixF4.d2 * this->matrixF4.d2 + this->matrixF4.d1 * this->matrixF4.d1 + this->matrixF4.d0 * this->matrixF4.d0
        : this->m_attachParent->float88;

    C44Matrix v237;
    C44Matrix v224;
    C3Vector v236;

    // TODO

    uint32_t elapsedTime = 0;
    if (this->m_time && this->m_scene->m_time) {
        elapsedTime = this->m_scene->m_time - this->m_time;
        this->m_time = this->m_scene->m_time;
    }

    for (int32_t i = 0; i < this->m_shared->m_data->bones.Count(); i++) {
        auto& bone = this->m_shared->m_data->bones[i];
        auto& modelBone = this->m_bones[i];

        if (modelBone.sequence.uint8 == 0xFFFF) {
            if (bone.parentIndex >= this->m_shared->m_data->bones.Count()) {
                if (i != 0) {
                    modelBone.sequence.uint0 = this->m_bones[0].sequence.uint0;
                    modelBone.sequence.uint4 = this->m_bones[0].sequence.uint4;
                    modelBone.sequence.uint6 = this->m_bones[0].sequence.uint6;
                }
            } else {
                modelBone.sequence.uint0 = this->m_bones[bone.parentIndex].sequence.uint0;
                modelBone.sequence.uint4 = this->m_bones[bone.parentIndex].sequence.uint4;
                modelBone.sequence.uint6 = this->m_bones[bone.parentIndex].sequence.uint6;
            }
        } else {
            if (this->m_time) {
                modelBone.sequence.uintC += elapsedTime;
                modelBone.sequence.uint10 += elapsedTime;
            }

            auto v45 = this->m_scene->m_time;
            auto& v46 = this->m_shared->m_data->sequences[modelBone.sequence.uint8];
            uint32_t v47 = 0;

            if (v46.flags & 0x1) {
                if (modelBone.sequence.uint10 - v45 <= 0) {
                    auto v234 = modelBone.sequence.uint10 - modelBone.sequence.uintC;
                    auto v235 = CMath::fuint(v234 * modelBone.sequence.float14);
                    v47 = modelBone.sequence.uint1C + v235;
                    v47 = std::min(v47, v46.duration);
                } else {
                    if (modelBone.sequence.uintC - v45 > 0) {
                        v45 = modelBone.sequence.uintC;
                    }

                    if (v46.duration) {
                        auto v234 = v45 - modelBone.sequence.uintC;
                        auto v235 = CMath::fuint(v234 * modelBone.sequence.float14);
                        v47 = (modelBone.sequence.uint1C + v235) % v46.duration;
                    }
                }
            } else {
                if (v46.duration) {
                    auto v234 = v45 - modelBone.sequence.uintC;
                    auto v235 = CMath::fuint(v234 * modelBone.sequence.float14);
                    v47 = (modelBone.sequence.uint1C + v235) % v46.duration;
                }
            }

            modelBone.sequence.uint0 = v47;
            modelBone.sequence.uint4 = modelBone.sequence.uint8;
            modelBone.sequence.uint6 = i;
        }

        // TODO

        uint32_t boneFlags = bone.flags | modelBone.flags;

        C44Matrix* boneParentMatrix;

        if (bone.parentIndex == 0xFFFF) {
            boneParentMatrix = &this->matrixF4;
        } else {
            boneParentMatrix = &this->m_boneMatrices[bone.parentIndex];

            if (boneFlags & (0x1 | 0x2 | 0x4)) {
                // TODO
            }
        }

        if (boneFlags & (0x80 | 0x200)) {
            C44Matrix boneLocalMatrix;

            if (bone.rotationTrack.sequenceTimes.Count()) {
                auto& rotationTrack = bone.rotationTrack;

                if (
                    rotationTrack.sequenceTimes.Count() > 1
                    || (rotationTrack.sequenceTimes.Count() == 1 && rotationTrack.sequenceTimes[0].times.Count() > this->uint90)
                ) {
                    C4Quaternion defaultValue = { 0.0f, 0.0f, 0.0f, 1.0f };
                    M2AnimateTrack<M2CompQuat, C4Quaternion>(this, &modelBone, rotationTrack, modelBone.rotationTrack, defaultValue);
                }

                boneLocalMatrix = C44Matrix(modelBone.rotationTrack.currentValue);
            } else {
                // TODO
            }

            if (bone.scaleTrack.sequenceTimes.Count()) {
                auto& scaleTrack = bone.scaleTrack;

                if (
                    scaleTrack.sequenceTimes.Count() > 1
                    || (scaleTrack.sequenceTimes.Count() == 1 && scaleTrack.sequenceTimes[0].times.Count() > this->uint90)
                ) {
                    C3Vector defaultValue = { 1.0f, 1.0f, 1.0f };
                    M2AnimateTrack<C3Vector, C3Vector>(this, &modelBone, scaleTrack, modelBone.scaleTrack, defaultValue);
                }

                boneLocalMatrix.Scale(modelBone.scaleTrack.currentValue);
            }

            // TODO
            // conditional involving bone flags and a matrix member of M2ModelBone

            C3Vector translation;

            if (bone.translationTrack.sequenceTimes.Count()) {
                auto& translationTrack = bone.translationTrack;

                if (
                    translationTrack.sequenceTimes.Count() > 1
                    || (translationTrack.sequenceTimes.Count() == 1 && translationTrack.sequenceTimes[0].times.Count() > this->uint90)
                ) {
                    C3Vector defaultValue = { 0.0f, 0.0f, 0.0f };
                    M2AnimateTrack<C3Vector, C3Vector>(this, &modelBone, translationTrack, modelBone.translationTrack, defaultValue);
                }

                translation = modelBone.translationTrack.currentValue + bone.pivot;
            } else {
                translation = bone.pivot;
            }

            boneLocalMatrix.d0 += translation.x;
            boneLocalMatrix.d1 += translation.y;
            boneLocalMatrix.d2 += translation.z;

            C3Vector negPivot = {
                -bone.pivot.x,
                -bone.pivot.y,
                -bone.pivot.z
            };

            boneLocalMatrix.Translate(negPivot);

            this->m_boneMatrices[i] = boneLocalMatrix * *boneParentMatrix;
        } else {
            this->m_boneMatrices[i] = *boneParentMatrix;
        }

        if (boneFlags & 0x8) {
            // Spherical billboard. The bone matrix is already in view space (its parent chain roots
            // at matrixF4 = model x view), so replacing its rotation with the view axes makes the
            // geometry face the screen from any camera angle -- exactly what a glow/flare sprite
            // needs. Keep the animated per-axis scale (row lengths) and the view-space position;
            // drop only the orientation, mapping the sprite's local X/Y onto camera right/up like the
            // reference does. Non-billboard bones are untouched.
            C44Matrix& m = this->m_boneMatrices[i];

            float sx = sqrtf(m.a0 * m.a0 + m.a1 * m.a1 + m.a2 * m.a2);
            float sy = sqrtf(m.b0 * m.b0 + m.b1 * m.b1 + m.b2 * m.b2);
            float sz = sqrtf(m.c0 * m.c0 + m.c1 * m.c1 + m.c2 * m.c2);

            m.a0 = sx;   m.a1 = 0.0f; m.a2 = 0.0f;
            m.b0 = 0.0f; m.b1 = sy;   m.b2 = 0.0f;
            m.c0 = 0.0f; m.c1 = 0.0f; m.c2 = sz;
        } else if (boneFlags & (0x10 | 0x20 | 0x40)) {
            // Cylindrical billboard: keep one axis locked (e.g. a candle flame stays vertical) and
            // spin the geometry around it to face the screen. Still in view space, so the locked
            // axis is its own row and "toward the screen" is view +Z. By cyclic order the locked
            // axis is the sprite's up, the next axis its width (right), the third its normal:
            // lockX -> up=X,right=Y,normal=Z ; lockY -> up=Y,right=Z,normal=X ; lockZ -> up=Z,right=X,normal=Y.
            C44Matrix& m = this->m_boneMatrices[i];

            float ux, uy, uz;
            if (boneFlags & 0x10) { ux = m.a0; uy = m.a1; uz = m.a2; }
            else if (boneFlags & 0x20) { ux = m.b0; uy = m.b1; uz = m.b2; }
            else { ux = m.c0; uy = m.c1; uz = m.c2; }

            float ul = sqrtf(ux * ux + uy * uy + uz * uz);

            if (ul > 1e-6f) {
                ux /= ul; uy /= ul; uz /= ul; // normalized locked (up) axis

                // right = normalize(cross(up, viewZ)), viewZ = (0,0,1): a horizontal screen axis
                // perpendicular to up. If up is parallel to the view direction, pick any horizontal.
                float rx = uy, ry = -ux, rz = 0.0f;
                float rl = sqrtf(rx * rx + ry * ry + rz * rz);

                if (rl < 1e-6f) { rx = 1.0f; ry = 0.0f; rz = 0.0f; rl = 1.0f; }

                rx /= rl; ry /= rl; rz /= rl;

                // normal = cross(up, right) -> keeps X x Y = Z (right handed) and faces the screen
                float nx = uy * rz - uz * ry;
                float ny = uz * rx - ux * rz;
                float nz = ux * ry - uy * rx;

                if (boneFlags & 0x10) {        // lock X: right -> Y row, normal -> Z row
                    float rs = sqrtf(m.b0 * m.b0 + m.b1 * m.b1 + m.b2 * m.b2);
                    float ns = sqrtf(m.c0 * m.c0 + m.c1 * m.c1 + m.c2 * m.c2);
                    m.b0 = rx * rs; m.b1 = ry * rs; m.b2 = rz * rs;
                    m.c0 = nx * ns; m.c1 = ny * ns; m.c2 = nz * ns;
                } else if (boneFlags & 0x20) { // lock Y: right -> Z row, normal -> X row
                    float rs = sqrtf(m.c0 * m.c0 + m.c1 * m.c1 + m.c2 * m.c2);
                    float ns = sqrtf(m.a0 * m.a0 + m.a1 * m.a1 + m.a2 * m.a2);
                    m.c0 = rx * rs; m.c1 = ry * rs; m.c2 = rz * rs;
                    m.a0 = nx * ns; m.a1 = ny * ns; m.a2 = nz * ns;
                } else {                       // lock Z: right -> X row, normal -> Y row
                    float rs = sqrtf(m.a0 * m.a0 + m.a1 * m.a1 + m.a2 * m.a2);
                    float ns = sqrtf(m.b0 * m.b0 + m.b1 * m.b1 + m.b2 * m.b2);
                    m.a0 = rx * rs; m.a1 = ry * rs; m.a2 = rz * rs;
                    m.b0 = nx * ns; m.b1 = ny * ns; m.b2 = nz * ns;
                }
            }
        }

        // TODO
    }

    for (int32_t i = 0; i < this->m_shared->m_data->colors.Count(); i++) {
        auto& color = this->m_shared->m_data->colors[i];
        auto& modelColor = this->m_colors[i];

        auto& colorTrack = color.colorTrack;
        if (
            colorTrack.sequenceTimes.Count() > 1
            || (colorTrack.sequenceTimes.Count() == 1 && colorTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            C3Vector defaultValue = { 0.0f, 0.0f, 0.0f };
            M2AnimateTrack<C3Vector, C3Vector>(
                this,
                this->m_bones,
                color.colorTrack,
                modelColor.colorTrack,
                defaultValue
            );
        }

        auto& alphaTrack = color.alphaTrack;
        if (
            alphaTrack.sequenceTimes.Count() > 1
            || (alphaTrack.sequenceTimes.Count() == 1 && alphaTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            float defaultValue = 1.0f;
            M2AnimateTrack<fixed16, float>(
                this,
                this->m_bones,
                color.alphaTrack,
                modelColor.alphaTrack,
                defaultValue
            );
        }
    }

    for (int32_t i = 0; i < this->m_shared->m_data->textureWeights.Count(); i++) {
        auto& textureWeight = this->m_shared->m_data->textureWeights[i];
        auto& modelTextureWeight = this->m_textureWeights[i];

        auto& weightTrack = textureWeight.weightTrack;
        if (
            weightTrack.sequenceTimes.Count() > 1
            || (weightTrack.sequenceTimes.Count() == 1 && weightTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            float defaultValue = 1.0f;
            M2AnimateTrack<fixed16, float>(
                this,
                this->m_bones,
                textureWeight.weightTrack,
                modelTextureWeight.weightTrack,
                defaultValue
            );
        }
    }

    if (this->m_shared->m_data->textureTransforms.count) {
        this->AnimateTextureTransformsMT();
    }

    for (int32_t i = 0; i < this->m_shared->m_data->lights.Count(); i++) {
        auto& light = this->m_shared->m_data->lights[i];
        auto& modelLight = this->m_lights[i];

        if (modelLight.uint64) {
            uint8_t defaultValue = 1;
            M2AnimateTrack<uint8_t, uint8_t>(
                this,
                &this->m_bones[light.boneIndex],
                light.visibilityTrack,
                modelLight.visibilityTrack,
                defaultValue
            );
        }

        if ((modelLight.uint64 == 0 || modelLight.visibilityTrack.currentValue == 0) && this->uint90) {
            continue;
        }

        auto& ambientIntensityTrack = light.ambientIntensityTrack;
        if (
            ambientIntensityTrack.sequenceTimes.Count() > 1
            || (ambientIntensityTrack.sequenceTimes.Count() == 1 && ambientIntensityTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            float defaultValue = 0.0f;
            M2AnimateTrack<float, float>(
                this,
                &this->m_bones[light.boneIndex],
                light.ambientIntensityTrack,
                modelLight.ambientIntensityTrack,
                defaultValue
            );
        }

        auto& ambientColorTrack = light.ambientColorTrack;
        if (
            ambientColorTrack.sequenceTimes.Count() > 1
            || (ambientColorTrack.sequenceTimes.Count() == 1 && ambientColorTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            C3Vector defaultValue = { 0.0f, 0.0f, 0.0f };
            M2AnimateTrack<C3Vector, C3Vector>(
                this,
                &this->m_bones[light.boneIndex],
                light.ambientColorTrack,
                modelLight.ambientColorTrack,
                defaultValue
            );

            float mul = modelLight.ambientIntensityTrack.currentValue * this->float198;

            modelLight.light.m_ambColor.x = modelLight.ambientColorTrack.currentValue.x * mul;
            modelLight.light.m_ambColor.y = modelLight.ambientColorTrack.currentValue.y * mul;
            modelLight.light.m_ambColor.z = modelLight.ambientColorTrack.currentValue.z * mul;
        }

        auto& diffuseIntensityTrack = light.diffuseIntensityTrack;
        if (
            diffuseIntensityTrack.sequenceTimes.Count() > 1
            || (diffuseIntensityTrack.sequenceTimes.Count() == 1 && diffuseIntensityTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            float defaultValue = 0.0f;
            M2AnimateTrack<float, float>(
                this,
                &this->m_bones[light.boneIndex],
                light.diffuseIntensityTrack,
                modelLight.diffuseIntensityTrack,
                defaultValue
            );
        }

        auto& diffuseColorTrack = light.diffuseColorTrack;
        if (
            diffuseColorTrack.sequenceTimes.Count() > 1
            || (diffuseColorTrack.sequenceTimes.Count() == 1 && diffuseColorTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            C3Vector defaultValue = { 0.0f, 0.0f, 0.0f };
            M2AnimateTrack<C3Vector, C3Vector>(
                this,
                &this->m_bones[light.boneIndex],
                light.diffuseColorTrack,
                modelLight.diffuseColorTrack,
                defaultValue
            );

            float mul = modelLight.diffuseIntensityTrack.currentValue * this->float198;

            // CORRECTED 2026-09-23: these three read ambientColorTrack until now, so a light's
            // diffuse colour was its AMBIENT colour scaled by the diffuse intensity, and the M2's
            // diffuse colour was parsed, animated and thrown away. The evidence that it is a slip
            // rather than the reference's behaviour: this block is guarded on diffuseColorTrack and
            // animates it into modelLight.diffuseColorTrack immediately above, and that value is
            // then read NOWHERE in the codebase -- it is the only animated track with no reader.
            // The line also predates the recomp effort; it arrives in the initial commit, inherited
            // from the upstream fork rather than transcribed from the reference.
            //
            // NOT confirmed against the reference: the corresponding block was not located in the
            // disassembly, so this is reasoned from frozen's own structure. It matters as of this
            // week, because CShaderEffect::ComputeLocalLights reads m_dirColor and local lights
            // now reach it.
            modelLight.light.m_dirColor.x = modelLight.diffuseColorTrack.currentValue.x * mul;
            modelLight.light.m_dirColor.y = modelLight.diffuseColorTrack.currentValue.y * mul;
            modelLight.light.m_dirColor.z = modelLight.diffuseColorTrack.currentValue.z * mul;
        }
    }

    for (int32_t i = 0; i < this->m_shared->m_data->cameras.Count(); i++) {
        auto& camera = this->m_shared->m_data->cameras[i];
        auto& modelCamera = this->m_cameras[i];

        auto& positionTrack = camera.positionTrack;
        if (
            positionTrack.sequenceTimes.Count() > 1
            || (positionTrack.sequenceTimes.Count() == 1 && positionTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            C3Vector defaultValue = { 0.0f, 0.0f, 0.0f };
            M2AnimateSplineTrack<M2SplineKey<C3Vector>, C3Vector>(
                this,
                this->m_bones,
                camera.positionTrack,
                modelCamera.positionTrack,
                defaultValue
            );
        }

        auto& targetTrack = camera.targetTrack;
        if (
            targetTrack.sequenceTimes.Count() > 1
            || (targetTrack.sequenceTimes.Count() == 1 && targetTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            C3Vector defaultValue = { 0.0f, 0.0f, 0.0f };
            M2AnimateSplineTrack<M2SplineKey<C3Vector>, C3Vector>(
                this,
                this->m_bones,
                camera.targetTrack,
                modelCamera.targetTrack,
                defaultValue
            );
        }

        auto& rollTrack = camera.rollTrack;
        if (
            rollTrack.sequenceTimes.Count() > 1
            || (rollTrack.sequenceTimes.Count() == 1 && rollTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            float defaultValue = 0.0f;
            M2AnimateSplineTrack<M2SplineKey<float>, float>(
                this,
                this->m_bones,
                camera.rollTrack,
                modelCamera.rollTrack,
                defaultValue
            );
        }
    }

    this->m_flag400 = 0;

    // TODO particles

    if (this->m_attachments || this->m_attachList) {
        this->AnimateAttachmentsMT();
    }

    this->m_animCounter = this->m_scene->uint14;
}

// ref: FUN_0082e140
// The cut-down animate: the path a model takes when it carries the 0x1000 flag. It shares
// AnimateMT's prologue and epilogue but skips every bone, colour, light and camera track, doing
// only what the model needs to be placed and tinted. Note it still writes matrixF4, which is why
// leaving this empty left anything on this path drawing with a stale transform.
void CM2Model::AnimateMTSimple(const C44Matrix* view, const C3Vector& a3, const C3Vector& a4, float a5, float a6) {
    if (!this->m_loaded) {
        return;
    }

    // Already animated for this frame of the scene.
    if (this->m_animCounter == this->m_scene->uint14) {
        return;
    }

    auto data = this->m_shared->m_data;

    // Attachment visibility, inherited from the parent exactly as AnimateMT does it

    if (this->m_attachParent) {
        this->m_flag8 = this->m_attachParent->m_flag8 && this->m_flag80;
        this->m_flag10000 = this->m_attachParent->m_flag10000 && this->m_flag20000;

        // TODO dword174, copied from the parent's own
    }

    // The tint this model passes on. Data flag 0x4 means it ignores what its parent handed down
    // and stands on its own values; otherwise the parent's diffuse scales this model's and the
    // parent's emissive is added on top, unless flag 0x80000 opts out of the addition.
    if (data->flags & 0x4) {
        this->float198 = this->m_baseAlpha;
        this->alpha19C = this->m_baseAlphaScale * this->m_baseAlpha;
        this->m_currentDiffuse = this->m_baseDiffuse;
        this->m_currentEmissive = this->m_baseEmissive;
    } else {
        this->m_currentDiffuse = {
            a3.x * this->m_baseDiffuse.x,
            a3.y * this->m_baseDiffuse.y,
            a3.z * this->m_baseDiffuse.z
        };

        this->m_currentEmissive = this->m_baseEmissive;

        this->float198 = this->m_flag100000 ? this->m_baseAlpha : a5 * this->m_baseAlpha;
        this->alpha19C = this->m_baseAlphaScale * a6 * this->m_baseAlpha;

        if (!this->m_flag80000) {
            this->m_currentEmissive.x += a4.x;
            this->m_currentEmissive.y += a4.y;
            this->m_currentEmissive.z += a4.z;
        }
    }

    // Global sequences

    for (int32_t i = 0; i < data->loops.Count(); i++) {
        auto loopLength = data->loops[i].length;
        this->m_loops[i] = loopLength ? (this->m_scene->m_time - this->uint74) % loopLength : 0;
    }

    this->matrixF4 = this->matrixB4 * *view;

    this->float88 = !this->m_attachParent || this->m_attachParent->m_flags & 0x1
        ? this->matrixF4.d2 * this->matrixF4.d2 + this->matrixF4.d1 * this->matrixF4.d1 + this->matrixF4.d0 * this->matrixF4.d0
        : this->m_attachParent->float88;

    if (this->m_time && this->m_scene->m_time) {
        this->m_time = this->m_scene->m_time;
    }

    // TODO the sequence playback record the reference advances here (its own +0x94), which
    // retimes the model's current sequence. frozen has no counterpart for that record yet.

    for (int32_t i = 0; i < data->textureWeights.Count(); i++) {
        auto& textureWeight = data->textureWeights[i];
        auto& modelTextureWeight = this->m_textureWeights[i];

        auto& weightTrack = textureWeight.weightTrack;

        if (
            weightTrack.sequenceTimes.Count() > 1
            || (weightTrack.sequenceTimes.Count() == 1 && weightTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            float defaultValue = 1.0f;
            M2AnimateTrack<fixed16, float>(
                this,
                this->m_bones,
                textureWeight.weightTrack,
                modelTextureWeight.weightTrack,
                defaultValue
            );
        }
    }

    if (data->textureTransforms.Count()) {
        this->AnimateTextureTransformsMT();
    }

    this->m_flag400 = 0;

    if (this->m_attachments || this->m_attachList) {
        this->AnimateAttachmentsMT();
    }

    this->m_animCounter = this->m_scene->uint14;
}

// Identified from its calls rather than its position: it reaches CM2Light::SetPosition
// (0x00835690), SetDirection (0x00834ae0) and SetVisible (0x008356f0), which is this
// function's light loop and nothing else in the class. Its entry test, `[+0x10] & 1`, is
// m_loaded.
// ref: FUN_00828a00
// The runtime half of every emitter, animated through the same M2AnimateTrack that drives every
// other per-model track, against the emitter's own bone -- so a torch on a moving arm emits along
// the arm rather than along the model.
//
// The reference does this inside AnimateST. Here it is called from the particle system instead, and
// the difference is coverage rather than behaviour: CM2Scene::Animate unlinks each model from
// m_animateList as it walks it, so AnimateST reaches only the models that re-registered through
// SetAnimating this frame, while the particle system is driven for every model it holds a
// simulation for. A model in the second set and not the first would sit on default values, and the
// default emission rate is zero -- its fires would go out. When CM2Model::AnimateParticleEmitter
// lands it brings the reference's own driver and coverage, and this moves back.
void CM2Model::AnimateParticleTracks() {
    if (!this->m_particles || !this->m_shared || !this->m_shared->m_data) {
        return;
    }

    for (int32_t i = 0; i < this->m_shared->m_data->particles.Count(); i++) {
        auto& particle = this->m_shared->m_data->particles[i];
        auto& modelParticle = this->m_particles[i];

        auto bone = particle.boneIndex < this->m_shared->m_data->bones.Count()
            ? &this->m_bones[particle.boneIndex]
            : nullptr;

        // speed and life default to 1.0, not 0.0: an emitter whose track carries no keys still
        // emits, and a particle with no speed and no lifespan is not a particle. Both defaults are
        // carried over from the stand-in sampler this replaces; the reference's own have not been
        // read yet.
        M2AnimateTrack<float, float>(this, bone, particle.speedTrack, modelParticle.speedTrack, 1.0f);
        M2AnimateTrack<float, float>(this, bone, particle.variationTrack, modelParticle.variationTrack, 0.0f);
        M2AnimateTrack<float, float>(this, bone, particle.latitudeTrack, modelParticle.latitudeTrack, 0.0f);
        M2AnimateTrack<float, float>(this, bone, particle.longitudeTrack, modelParticle.longitudeTrack, 0.0f);
        M2AnimateTrack<float, float>(this, bone, particle.gravityTrack, modelParticle.gravityTrack, 0.0f);
        M2AnimateTrack<float, float>(this, bone, particle.lifeTrack, modelParticle.lifeTrack, 1.0f);
        M2AnimateTrack<float, float>(this, bone, particle.emissionRateTrack, modelParticle.emissionRateTrack, 0.0f);
        M2AnimateTrack<float, float>(this, bone, particle.widthTrack, modelParticle.widthTrack, 0.0f);
        M2AnimateTrack<float, float>(this, bone, particle.lengthTrack, modelParticle.lengthTrack, 0.0f);
        M2AnimateTrack<float, float>(this, bone, particle.zsourceTrack, modelParticle.zsourceTrack, 0.0f);

        // The gates the reference's driver latches and reads: whether the emitter ran at all, and
        // whether it is handed the animated rate or zero.
        modelParticle.enabled = 1;
        modelParticle.rateActive = modelParticle.emissionRateTrack.currentValue > 0.0f ? 1 : 0;
        modelParticle.active = this->m_flag10000 || this->m_flag20000 ? 1 : 0;
    }
}

void CM2Model::AnimateST() {
    if (!this->m_loaded) {
        return;
    }

    auto attachParent = this->m_attachParent;

    if (!attachParent) {
        this->m_currentLighting = &this->m_lighting;
    } else {
        this->m_flag8000 = attachParent->m_flag8000;

        if (this->m_flag8000 && attachParent->m_flags & 0x1) {
            this->m_currentLighting = &this->m_lighting;
        } else {
            this->m_currentLighting = attachParent->m_currentLighting;
        }
    }

    if (!this->m_currentLighting) {
        this->m_currentLighting = &this->m_lighting;
    }

    for (int32_t i = 0; i < this->m_shared->m_data->lights.Count(); i++) {
        auto& light = this->m_shared->m_data->lights[i];
        auto& modelLight = this->m_lights[i];

        int32_t visible = 0;
        if (modelLight.uint64 && modelLight.visibilityTrack.currentValue) {
            visible = 1;

            if (light.lightType == M2LIGHT_1) {
                // The reference chains two transforms: the light's model-space position through
                // its bone, which lands in camera space here as every bone matrix does, and then
                // through m_viewInv back out to world space -- the same m_viewInv the directional
                // branch below uses, at scene + 0xc4.
                C3Vector bone = light.position * this->m_boneMatrices[light.boneIndex];
                C3Vector world = bone * this->m_scene->m_viewInv;

                modelLight.light.SetPosition(world);
            } else {
                float v10 = -this->m_boneMatrices[light.boneIndex].c0;
                float v11 = -this->m_boneMatrices[light.boneIndex].c1;
                float v12 = -this->m_boneMatrices[light.boneIndex].c2;

                float x = this->m_scene->m_viewInv.a0 * v10
                        + this->m_scene->m_viewInv.b0 * v11
                        + this->m_scene->m_viewInv.c0 * v12;
                float y = this->m_scene->m_viewInv.a1 * v10
                        + this->m_scene->m_viewInv.b1 * v11
                        + this->m_scene->m_viewInv.c1 * v12;
                float z = this->m_scene->m_viewInv.a2 * v10
                        + this->m_scene->m_viewInv.b2 * v11
                        + this->m_scene->m_viewInv.c2 * v12;

                C3Vector dir = { x, y, z };

                modelLight.light.SetDirection(dir);
            }
        }

        modelLight.light.SetVisible(visible);

        // Stamped every frame, visible or not. CM2Scene::SelectLights compares it against the
        // same counter and switches off any point light that has fallen behind.
        modelLight.light.m_updateStamp = this->m_scene->uint14;
    }

    if (this->m_shared->m_data->cameras.Count()) {
        this->AnimateCamerasST();
    }

    // MISSING: the ribbon and particle emitter update. This is the single largest gap in this
    // function -- `--diff 00828a00` scores it 46% and the whole shortfall is one contiguous block
    // that belongs right here, between the camera update above and the draw-list link below.
    // Decompiled 2026-09-23 so the next attempt does not start cold:
    //
    //   1. Delta time, which nothing else in frozen computes:
    //          now = m_scene->time (scene + 0xc)
    //          ticks = now - this->[0x8c]           // a per-model last-update stamp frozen lacks
    //          dt = (float)ticks; if (ticks < 0) dt += 4294967296.0f;   // unsigned fixup
    //          dt *= 0.001f;                        // the 1/1000 at 0x009e1134
    //          this->[0x8c] = now
    //      The fixup is the reference's own way of reading the subtraction as unsigned; it matters
    //      only across a wrap, but it is one instruction and there is no reason to drop it.
    //
    //   2. For each of m_data->ribbons (count at data + 0x120, array at + 0x124, stride 0xb0):
    //      take the per-model runtime state (this + 0x2b8, stride 0x50) and the emitter object
    //      (this + 0x2bc, an array of pointers), then push the animated tracks into the emitter --
    //      colour, alpha SCALED BY this->float198, height above, height below, texture slot. Each
    //      push is guarded on the track actually having keys. Then copy the bone matrix
    //      (m_boneMatrices[ribbon->boneIndex], 0x40 bytes), transform by it, transform by
    //      m_scene->m_viewInv (scene + 0xc4), and if this->m_flags & 0x8000 advance the emitter by
    //      dt.
    //
    //   3. For each of m_data->particleEmitters (count at data + 0x128):
    //      this->FUN_008309c0(dt, i).
    //
    // Why it is not ported here: frozen has the M2Ribbon FILE data (M2Data.hpp) but none of the
    // runtime object graph the block drives -- no per-model ribbon state array, no emitter
    // objects, no CM2Model fields at +0x2b8 or +0x2bc, and no particle emitter runtime.
    // src/world/ParticleFx.cpp is a separate stand-in, not this. Porting the loops before the
    // objects exist would give frozen two loops over absent arrays, so the emitters come first.
    // CLAUDE.md already records that ribbons cannot be evaluated yet because unit movement is not
    // ported; this is the other half of why.

    if (this->m_flag8) {
        this->m_drawPrev = &this->m_scene->m_drawList;
        this->m_drawNext = this->m_scene->m_drawList;
        this->m_scene->m_drawList = this;

        if (this->m_drawNext) {
            this->m_drawNext->m_drawPrev = &this->m_drawNext;
        }
    }

    // TODO

    // Animate attached models

    for (auto model = this->m_attachList; model; model = model->m_attachNext) {
        bool animate;

        if (model->m_attachIndex == 0xFFFF) {
            animate = model->m_flag40000;
        } else {
            animate = this->m_attachments[model->m_attachIndex].visibilityTrack.currentValue;
        }

        if (animate) {
            model->AnimateST();
        }
    }

    if (this->float198 == 1.0f) {
        this->uint90 = 1;
    }
}

// ref: FUN_0082d6f0
void CM2Model::AnimateTextureTransformsMT() {
    for (int32_t i = 0; i < this->m_shared->m_data->textureTransforms.Count(); i++) {
        static C3Vector center = { 0.5f, 0.5f, 0.0f };

        auto& textureTransform = this->m_shared->m_data->textureTransforms[i];
        auto& modelTextureTransform = this->m_textureTransforms[i];
        auto& textureMatrix = this->m_textureMatrices[i];

        textureMatrix.Identity();

        // Rotation

        auto& rotationTrack = textureTransform.rotationTrack;
        auto& modelRotationTrack = modelTextureTransform.rotationTrack;

        if (rotationTrack.sequenceTimes.Count() > 0) {
            C4Quaternion defaultValue = { 0.0f, 0.0f, 0.0f, 1.0f };

            M2AnimateTrack(this, this->m_bones, rotationTrack, modelRotationTrack, defaultValue);

            textureMatrix.Translate(center);
            textureMatrix.Rotate(modelRotationTrack.currentValue);
            textureMatrix.Translate(-center);
        }

        // Scale

        auto& scaleTrack = textureTransform.scaleTrack;
        auto& modelScaleTrack = modelTextureTransform.scaleTrack;

        if (scaleTrack.sequenceTimes.Count() > 0) {
            C3Vector defaultValue = { 1.0f, 1.0f, 1.0f };

            M2AnimateTrack(this, this->m_bones, scaleTrack, modelScaleTrack, defaultValue);

            textureMatrix.Translate(center);
            textureMatrix.Scale(modelScaleTrack.currentValue);
            textureMatrix.Translate(-center);
        }

        // Translation

        auto& translationTrack = textureTransform.translationTrack;
        auto& modelTranslationTrack = modelTextureTransform.translationTrack;

        if (translationTrack.sequenceTimes.Count() > 0) {
            C3Vector defaultValue = { 0.0f, 0.0f, 0.0f };

            M2AnimateTrack(this, this->m_bones, translationTrack, modelTranslationTrack, defaultValue);

            textureMatrix.Translate(modelTranslationTrack.currentValue);
        }
    }
}

// ref: FUN_00831630
void CM2Model::AttachToParent(CM2Model* parent, uint32_t id, const C3Vector* position, int32_t a5) {
    if (this->m_attachParent) {
        this->DetachFromParent();
    }

    this->SetAnimating(0);

    // Look up parent attachment index by given ID

    uint16_t attachIndex = 0xFFFF;

    if (parent->m_loaded) {
        auto& parentAttachmentIndicesById = parent->m_shared->m_data->attachmentIndicesById;

        if (id < parentAttachmentIndicesById.Count()) {
            attachIndex = parentAttachmentIndicesById[id];
        }

        if (attachIndex == 0xFFFF && !a5) {
            return;
        }
    }

    this->m_attachIndex = attachIndex;
    this->m_attachId = id;
    this->m_attachParent = parent;

    this->m_flag80 = 1;
    this->m_flag20000 = 1;
    this->m_flag40000 = a5 ? 1 : 0;

    this->m_attachPrev = &parent->m_attachList;
    this->m_attachNext = parent->m_attachList;
    if (parent->m_attachList) {
        parent->m_attachList->m_attachPrev = &this->m_attachNext;
    }
    parent->m_attachList = this;

    if (!this->m_loaded || !this->m_flag100) {
        auto model = this->m_attachParent;
        while (model) {
            model->m_flag100 = 0;
            model = model->m_attachParent;
        }
    }

    if (!this->m_flag2) {
        auto model = this->m_attachParent;
        while (model) {
            model->m_flag200 = 0;
            model = model->m_attachParent;
        }
    }

    if (position && this->m_attachParent->m_loaded && this->m_attachIndex != 0xFFFF) {
        auto transform = parent->GetAttachmentWorldTransform(id);
        auto scale = sqrt(transform.a0 * transform.a0 + transform.a1 * transform.a1 + transform.a2 * transform.a2);
        transform = transform.AffineInverse(scale);

        if (!this->m_flag8000) {
            this->matrixB4.Identity();
        }

        auto transformedPosition = *position * transform;

        this->matrixB4.d0 = transformedPosition.x;
        this->matrixB4.d1 = transformedPosition.y;
        this->matrixB4.d2 = transformedPosition.z;

        this->m_flag8000 = 1;
    }

    this->AddRef();
}

void CM2Model::AttachToScene(CM2Scene* scene) {
    this->DetachFromScene();

    this->m_scene = scene;

    this->m_scenePrev = &this->m_scene->m_modelList;
    this->m_sceneNext = this->m_scene->m_modelList;
    this->m_scene->m_modelList = this;
    if (this->m_sceneNext) {
        this->m_sceneNext->m_scenePrev = &this->m_sceneNext;
    }

    if (this->m_loaded) {
        for (int32_t i = 0; i < this->m_shared->m_data->lights.Count(); i++) {
            this->m_lights[i].light.Initialize(this->m_scene);
        }

        // TODO
        // - sequence / sequence fallback logic
    } else {
        for (auto modelCall = this->m_modelCallList; modelCall; modelCall = modelCall->modelCallNext) {
            modelCall->time += this->m_scene->m_time;
        }
    }
}

// Drop this model's parked bone-sequence requests for one bone, so a request that has been
// superseded does not fire later and overwrite the sequence that replaced it. SetBoneSequence
// calls it immediately before parking or applying a new one.
//
// It was an empty body with two live callers, which made the cancel a no-op: every deferred
// request still landed when its .anim data arrived, however many times the bone had been
// re-sequenced in the meantime.
//
// The `primary` argument selects which half of the records to drop, and it is compared against
// bit 1 of the record's flags -- the bit SetBoneSequenceDeferred sets from its own a9. A primary
// request never cancels a secondary one or the other way round.
//
// Two branches, because the records cannot always be unlinked. CM2Shared::SequenceLoadedCallback
// raises m_flag10 while it walks these same lists, so during that walk the records are MARKED with
// bit 8 and the callback drops them as it passes -- which it already does. Outside the walk they
// are unlinked and freed here. The reference's removal helper captures the next pointer before
// freeing, so this does too.
// Drop every one of this model's parked bone-sequence requests, whatever bone or slot they are
// for. CancelDeferredSequences below is the same walk with a narrower predicate; this one matches
// on the model alone, because the model is going away.
//
// **Without this, destroying a model with a deferred request pending is a use-after-free.** The
// record keeps a raw CM2Model* and CM2Shared::SequenceLoadedCallback calls
// playback->model->ApplySequencePlayBack() on it when the .anim data lands. frozen already
// implemented the consumer half of the protocol -- the callback drops records carrying flag 8 --
// and this is the producer that was missing, so nothing ever set the flag for a dying model.
//
// The same two branches as CancelDeferredSequences, and for the same reason: while
// SequenceLoadedCallback is walking these lists it raises m_flag10, and records may then only be
// marked, not unlinked.
// ref: FUN_00831e20
void CM2Model::CancelAllDeferredSequences() {
    if (!this->m_shared) {
        return;
    }

    auto shared = this->m_shared;

    for (auto load = shared->m_sequenceLoads.Head(); load; load = shared->m_sequenceLoads.Next(load)) {
        for (auto playback = load->playbacks.Head(); playback;) {
            auto next = load->playbacks.Next(playback);

            if (playback->model == this) {
                if (shared->m_flag10) {
                    playback->flags |= 8;
                } else {
                    load->playbacks.UnlinkNode(playback);
                    STORM_FREE(playback);
                }
            }

            playback = next;
        }
    }
}

// ref: FUN_00831ec0
void CM2Model::CancelDeferredSequences(uint32_t boneIndex, bool primary) {
    auto shared = this->m_shared;

    for (auto load = shared->m_sequenceLoads.Head(); load; load = shared->m_sequenceLoads.Next(load)) {
        for (auto playback = load->playbacks.Head(); playback;) {
            auto next = load->playbacks.Next(playback);

            bool match = playback->model == this
                && playback->boneIndex == boneIndex
                && ((playback->flags >> 1) & 1) == (primary ? 1 : 0);

            if (match) {
                if (shared->m_flag10) {
                    playback->flags |= 8;
                } else {
                    load->playbacks.UnlinkNode(playback);
                    STORM_FREE(playback);
                }
            }

            playback = next;
        }
    }
}

// ref: FUN_00827560
void CM2Model::DetachAllChildrenById(uint32_t id) {
    // Hang on to attachNext in case model is freed during detach
    CM2Model* attachNext = nullptr;

    // Detach any model matching provided attach ID
    for (auto model = this->m_attachList; model; model = attachNext) {
        attachNext = model->m_attachNext;

        if (model->m_attachId == id) {
            model->DetachFromParent();
        }
    }
}

// ref: FUN_008274f0
void CM2Model::DetachFromParent() {
    if (this->m_attachPrev) {
        *this->m_attachPrev = this->m_attachNext;
    }

    if (this->m_attachNext) {
        this->m_attachNext->m_attachPrev = this->m_attachPrev;
    }

    this->m_flag40000 = 0;

    this->m_attachPrev = nullptr;
    this->m_attachNext = nullptr;
    this->m_attachParent = nullptr;
    this->m_attachId = -1;

    // TODO this->dword174 = 0

    this->Release();
}

void CM2Model::DetachFromScene() {
    // Unlink from scene list

    if (this->m_scenePrev) {
        *this->m_scenePrev = this->m_sceneNext;
    }

    if (this->m_sceneNext) {
        this->m_sceneNext->m_scenePrev = this->m_scenePrev;
    }

    this->m_scenePrev = nullptr;
    this->m_sceneNext = nullptr;

    // TODO

    this->m_scene = nullptr;
}

// ref: FUN_008284d0
void CM2Model::FindKey(M2ModelBoneSeq* sequence, const M2TrackBase& track, uint32_t& currentKey, uint32_t& nextKey, float& ratio) {
    if (!track.sequenceTimes.Count()) {
        currentKey = 0;
        nextKey = 0;

        ratio = 0.0f;

        return;
    }

    uint32_t sequenceTime = sequence->uint0;
    uint32_t sequenceIndex = sequence->uint4;

    if (track.loopIndex == 0xFFFF) {
        if (sequenceIndex >= track.sequenceTimes.Count()) {
            sequenceIndex = 0;
        }
    } else {
        sequenceTime = this->m_loops[track.loopIndex];
        sequenceIndex = 0;
    }

    auto& sequenceTimes = track.sequenceTimes[sequenceIndex];
    auto numKeys = sequenceTimes.times.Count();
    auto keyTimes = sequenceTimes.times.Data();

    if (numKeys <= 1) {
        currentKey = 0;
        nextKey = 0;

        ratio = 0.0f;

        return;
    }

    if (currentKey >= numKeys) {
        currentKey = 0;
    }

    uint32_t foundKey = currentKey;
    auto v16 = sequenceTime - keyTimes[currentKey];

    if (v16 >= 500) {
        if (v16 < 0xFFFFFE0C) {
            if (sequenceTime >= 500) {
                // Perform binary search for key containing sequence time

                int32_t lowKey = 0;
                int32_t highKey = numKeys - 1;

                while (lowKey < highKey) {
                    int32_t midKey = (lowKey + highKey) / 2;

                    if (midKey + 1 >= numKeys) {
                        lowKey = midKey;
                        break;
                    }

                    if (sequenceTime >= keyTimes[midKey] && sequenceTime < keyTimes[midKey + 1]) {
                        lowKey = midKey;
                        break;
                    }

                    if (sequenceTime >= keyTimes[midKey]) {
                        lowKey = midKey + 1;
                    } else {
                        highKey = midKey - 1;
                    }
                }

                foundKey = lowKey;
            } else {
                // Perform linear search forward from zero for key containing sequence time

                uint32_t key = 0;

                while (key < numKeys - 1 && sequenceTime >= keyTimes[key + 1]) {
                    key++;
                }

                foundKey = key;
            }
        } else if (currentKey > 0) {
            // Perform linear search backward from current key for key containing sequence time

            uint32_t key = currentKey;

            while (key > 0 && sequenceTime < keyTimes[key]) {
                key--;
            }

            foundKey = key;
        }
    } else if (currentKey < numKeys - 1) {
        // Perform linear search forward from current key for key containing sequence time

        uint32_t key = currentKey;

        while (key < numKeys - 1 && sequenceTime >= keyTimes[key + 1]) {
            key++;
        }

        foundKey = key;
    }

    if (foundKey + 1 >= numKeys) {
        currentKey = foundKey;
        nextKey = foundKey;

        ratio = 0.0f;
    } else {
        currentKey = foundKey;
        nextKey = foundKey + 1;

        auto currentKeyTime = keyTimes[currentKey];
        auto nextKeyTime = keyTimes[nextKey];

        ratio = static_cast<float>(sequenceTime - currentKeyTime) / static_cast<float>(nextKeyTime - currentKeyTime);
    }
}

void CM2Model::FreeExternalResources() {
    if (!this->m_loaded) {
        return;
    }

    // TODO

    if (this->m_textures) {
        for (int32_t i = 0; i < this->m_shared->m_data->textures.Count(); i++) {
            auto texture = this->m_textures[i];

            if (texture) {
                HandleClose(texture);
            }
        }
    }

    // TODO
}

void CM2Model::FreeInternalResources() {
    if (!this->m_internalResources) {
        return;
    }

    if (this->m_bones) {
        this->m_bones = nullptr;
    }

    if (this->m_loops) {
        this->m_loops = nullptr;
    }

    if (this->m_skinSections) {
        this->m_skinSections = nullptr;
    }

    if (this->m_colors) {
        this->m_colors = nullptr;
    }

    if (this->m_textureWeights) {
        this->m_textureWeights = nullptr;
    }

    if (this->m_textureTransforms) {
        this->m_textureTransforms = nullptr;
    }

    if (this->m_attachments) {
        this->m_attachments = nullptr;
    }

    if (this->m_lights) {
        for (int32_t i = 0; i < this->m_shared->m_data->lights.Count(); i++) {
            this->m_lights[i].light.~CM2Light();
        }

        this->m_lights = nullptr;
    }

    if (this->m_cameras) {
        this->m_cameras = nullptr;
    }

    // TODO
    // if (this->m_ribbons) {
    //     this->m_ribbons = nullptr;
    // }

    // TODO
    // if (this->m_particles) {
    //     this->m_particles = nullptr;
    // }

    // The two matrix arrays are NOT part of the pooled internal-resources block: InitializeLoaded
    // allocates each with its own SMemAlloc. Nothing freed them, so every model destroyed leaked
    // 64 bytes per bone plus 64 per texture transform -- a few kilobytes for a character, on every
    // unit that despawns and every model the character screen builds and throws away.
    //
    // Found on 2026-09-23 by following the fidelity diff on CM2Model::InitializeLoaded, where the
    // reference calls SequenceBufferAlloc twice and frozen calls SMemAlloc. That is a second,
    // separate divergence and it stands: the reference's allocator returns 16-byte-aligned memory
    // for exactly these two arrays, while frozen's SMemAlloc aligns to 8. Nothing here uses SSE on
    // them, so it is not a correctness problem, and closing it means exporting the alloc/free pair
    // out of the anonymous namespace in CM2Shared.cpp where they currently live.
    if (this->m_boneMatrices) {
        SMemFree(this->m_boneMatrices, __FILE__, __LINE__, 0);
        this->m_boneMatrices = nullptr;
    }

    if (this->m_textureMatrices) {
        SMemFree(this->m_textureMatrices, __FILE__, __LINE__, 0);
        this->m_textureMatrices = nullptr;
    }

    STORM_FREE(this->m_internalResources);
}

// ref: FUN_00831410
C44Matrix CM2Model::GetAttachmentWorldTransform(uint32_t id) {
    if (!this->m_loaded) {
        this->WaitForLoad("GetAttachmentWorldTransform");
    }

    auto& attachmentIndicesById = this->m_shared->m_data->attachmentIndicesById;
    auto& attachments = this->m_shared->m_data->attachments;

    // Look up attachment index

    uint16_t attachIndex = 0xFFFF;
    if (id < attachmentIndicesById.count) {
        attachIndex = attachmentIndicesById[id];
    }

    // Look up bone index

    uint16_t boneIndex = 0xFFFF;
    if (attachIndex < this->m_shared->m_data->attachments.count) {
        boneIndex = attachments[attachIndex].boneIndex;
    }

    // Animate

    this->Animate();

    // Calculate attachment world transform

    C44Matrix transform;

    if (attachIndex == 0xFFFF) {
        transform = this->m_boneMatrices[0];
    } else {
        transform = this->m_boneMatrices[boneIndex];
        transform.Translate(attachments[attachIndex].position);
    }

    return transform * this->m_scene->m_viewInv;
}

CAaBox& CM2Model::GetBoundingBox(CAaBox& bounds) {
    // TODO
    // WaitForLoad

    bounds = this->m_shared->m_data->bounds.extent;

    return bounds;
}

HCAMERA CM2Model::GetCameraByIndex(uint32_t index) {
    if (!this->m_loaded) {
        this->WaitForLoad("GetCameraByIndex");
    }

    return this->m_cameras[index].m_camera;
}

// The reference calls Animate() first, and frozen did not. Added 2026-09-23 with the tag: without
// it this returns whatever matrixF4 held when the model last animated, which for a model that has
// not animated yet this frame is the previous frame's position.
//
// It is inert at the only call site today -- SetupLighting runs after CM2Scene::Animate's loop, so
// the model is already current and Animate() early-outs on the frame counter -- but the point of
// the call is that GetPosition is self-sufficient for any future caller, which is how the
// reference wrote it.
//
// The offsets line up exactly: the reference transforms the vector at model + 0x124 by the scene's
// m_viewInv at scene + 0xc4, and frozen's matrixF4 sits at 0xF4, so matrixF4.d0 is 0xF4 + 0x30 =
// 0x124. It writes through a caller-supplied out pointer and returns it; returning by value here
// is the same thing.
// ref: FUN_004e2790
C3Vector CM2Model::GetPosition() {
    this->Animate();

    return reinterpret_cast<C3Vector&>(this->matrixF4.d0) * this->m_scene->m_viewInv;
}

// ref: FUN_0082ced0
// Report the sequence the model would actually play for `sequenceId`: the fallback chain is walked
// first (the model may not carry the animation asked for), then the requested variation of what it
// resolved to. The caller gets that sequence's header and its authored bounding box; the box is
// what the blob-shadow pass projects as a doodad's footprint, which is why the footprint follows
// the animation.
//
// Nothing in frozen calls this, so `M2SequenceInfo::moveSpeed`, `center` and `radius` are computed
// and discarded -- which is how tools/deaddata.py surfaced it. The port is not the problem; the
// consumers are. Measured 2026-09-23 from the reference call graph: FUN_0082ced0 has 20 callers
// there and NOT ONE of them is linked in frozen.
//
//     007385c0  4083 bytes, 57 callers   the big one; everything else here is downstream of it
//     0082dd80   819 bytes,  7 callers   the only caller inside the M2 module itself
//     007022d0  1931 bytes,  5 callers
//     00604e00 / 00619580 / 0070d1e0     3 callers each
//     007015d0 / 0071df30 / 00737ef0 / 0073c8e0        2 each
//     00606f90 / 006f80b0 / 00702fc0 / 0073adc0 / 00756040 / 00793980   1 each
//     0052f9b0 / 005995d0 / 0070fa70 / 0070fe10        0 (reached indirectly)
//
// The 0x70xxxx and 0x73xxxx cluster is unit animation state, which CLAUDE.md already records as
// unported, so most of this list is blocked behind that rather than behind anything render-side.
//
// FUN_0082dd80 looked like the exception, being inside CM2Model's own address range, and it was
// decompiled 2026-09-23 to settle that. It is not worth porting yet, and here is why, so that the
// next cycle does not spend another Ghidra run on it. What it does: reset the model's 4x4 matrix at
// +0xb4 to identity (the diagonal writes at 0xb4 / 0xc8 / 0xdc / 0xf0 are 20 bytes apart, which is
// the row-major 0, 5, 10, 15), scale it, drop the position argument into the translation row at
// +0xe4, and when its mode argument has (mode & 3) == 1 build the orientation from a direction
// vector with two cross products. Then -- only if the animating flag +0x10 & 1 is set -- it queries
// the current sequence state through FUN_008266b0, calls THIS function for that sequence's header,
// and reads `info.flags & 0xe`: 2 or 4 means fade the new matrix against the copy of the old one it
// saved before overwriting (4 inverts the factor), 8 means take it whole, anything else means skip
// the blend. Finally it sets +0x10 |= 0x8000. So it is the animation-blended world transform.
//
// The reason to leave it: its own seven callers are unlinked too, exactly like this function's
// twenty. Porting it would add a method nothing in frozen calls and move the dead end up one level
// instead of closing it. The chain is dead from the top, not from here, and the top is unit
// animation state. FUN_008266b0 (296 bytes, 22 callers) is the current-sequence-state query and
// computes animation time as `(scene->time - seq->startTime) * seq->rate + seq->offset`; it is the
// better seed of the two if this area is picked up again.
void CM2Model::GetSequenceInfo(uint32_t sequenceId, int32_t variationIndex, M2SequenceInfo& info) {
    if (!this->m_loaded) {
        this->WaitForLoad("GetSequenceInfo");
    }

    M2SequenceFallback fallback;
    this->Sub826350(fallback, sequenceId);

    info.sequenceId = fallback.uint0;
    info.playMode = fallback.uint2;

    auto data = this->m_shared->m_data;

    uint16_t sequenceIndex = CM2Model::Sub8260C0(data, fallback.uint0, variationIndex);

    // The model has no such variation: everything but the resolved fallback is cleared.
    if (sequenceIndex == 0xFFFF) {
        info.flags = 0;
        info.duration = 0;
        info.moveSpeed = 0.0f;
        info.extent.b = { 0.0f, 0.0f, 0.0f };
        info.extent.t = { 0.0f, 0.0f, 0.0f };
        info.center = { 0.0f, 0.0f, 0.0f };
        info.radius = 0.0f;

        return;
    }

    auto& sequence = data->sequences[sequenceIndex];

    info.flags = sequence.flags;
    info.duration = sequence.duration;
    info.moveSpeed = sequence.movespeed;

    // The authored move speed is in the model's own units, so a model carrying a world transform
    // has it scaled by the length of that transform's first row. An attached model has no world
    // transform of its own -- matrixF4 is relative to the scene view -- so the length is taken from
    // the row the view inverse maps back into world space.
    if (this->m_flag8000) {
        if (!this->m_attachParent) {
            float scale = sqrtf(
                this->matrixB4.a0 * this->matrixB4.a0
                + this->matrixB4.a1 * this->matrixB4.a1
                + this->matrixB4.a2 * this->matrixB4.a2
            );

            info.moveSpeed = scale * sequence.movespeed;
        } else {
            auto& viewInv = this->m_scene->m_viewInv;
            auto& transform = this->matrixF4;

            float x = transform.a0 * viewInv.a0
                + transform.a1 * viewInv.b0
                + transform.a2 * viewInv.c0
                + transform.a3 * viewInv.d0;
            float y = transform.a0 * viewInv.a1
                + transform.a1 * viewInv.b1
                + transform.a2 * viewInv.c1
                + transform.a3 * viewInv.d1;
            float z = transform.a0 * viewInv.a2
                + transform.a1 * viewInv.b2
                + transform.a2 * viewInv.c2
                + transform.a3 * viewInv.d2;

            info.moveSpeed = sqrtf(x * x + y * y + z * z) * sequence.movespeed;
        }
    }

    info.extent = sequence.bounds.extent;

    info.center.x = (sequence.bounds.extent.b.x + sequence.bounds.extent.t.x) * 0.5f;
    info.center.y = (sequence.bounds.extent.t.y + sequence.bounds.extent.b.y) * 0.5f;
    info.center.z = (sequence.bounds.extent.t.z + sequence.bounds.extent.b.z) * 0.5f;

    info.radius = sequence.bounds.radius;
}

// ref: FUN_008273d0
bool CM2Model::HasAttachment(uint32_t id) {
    if (!this->m_loaded) {
        this->WaitForLoad("HasAttachment");
    }

    if (id < this->m_shared->m_data->attachmentIndicesById.Count()) {
        return this->m_shared->m_data->attachmentIndicesById[id] < this->m_shared->m_data->attachments.Count();
    }

    return this->m_shared->m_data->attachments.Count() > 0xFFFF;
}

int32_t CM2Model::Initialize(CM2Scene* scene, CM2Shared* shared, CM2Model* a4, uint32_t flags) {
    this->AttachToScene(scene);

    // TODO
    // this->dword30[23] = this->m_scene->dwordC;

    this->m_shared = shared;
    this->m_shared->AddRef();

    // **A reference taken and thrown away.** This raises a4's refcount and stores the pointer
    // nowhere, so nothing can ever release it and that model would never be freed. The reference
    // keeps it at CM2Model+0x30 and its destructor opens by releasing it, destroying it in place
    // and returning it to the model pool when the count reaches zero (FUN_00832640 at 0x0083264e).
    //
    // Dormant rather than live: the one caller in frozen, CM2Scene::CreateModel, passes nullptr.
    // It stops being dormant the moment anything passes a real model, so give this a member and
    // release it in ~CM2Model in the same change.
    if (a4) {
        a4->AddRef();
    }

    this->m_flags = flags;

    this->m_modelCallTail = &this->m_modelCallList;

    this->uint74 = this->m_scene->m_time;

    // TODO

    return this->m_shared->CallbackWhenLoaded(this);
}

// ref: FUN_00832ea0
int32_t CM2Model::InitializeLoaded() {
    if (!this->m_shared->m_m2DataLoaded || !this->m_shared->m_skinProfileLoaded) {
        return 1;
    }

    // Allocate a single buffer to hold unique per-model data

    uint32_t bufferSize = 0;
    bufferSize += ALIGN_SIZE(bufferSize, M2ModelBone, this->m_shared->m_data->bones.Count());
    bufferSize += ALIGN_SIZE(bufferSize, uint32_t, this->m_shared->m_data->loops.Count());
    bufferSize += ALIGN_SIZE(bufferSize, uint32_t, this->m_shared->skinProfile->skinSections.Count());
    bufferSize += ALIGN_SIZE(bufferSize, M2ModelColor, this->m_shared->m_data->colors.Count());
    bufferSize += ALIGN_SIZE(bufferSize, HTEXTURE, this->m_shared->m_data->textures.Count());
    bufferSize += ALIGN_SIZE(bufferSize, M2ModelTextureWeight, this->m_shared->m_data->textureWeights.Count());
    bufferSize += ALIGN_SIZE(bufferSize, M2ModelTextureTransform, this->m_shared->m_data->textureTransforms.Count());
    bufferSize += ALIGN_SIZE(bufferSize, M2ModelAttachment, this->m_shared->m_data->attachments.Count());
    bufferSize += ALIGN_SIZE(bufferSize, M2ModelLight, this->m_shared->m_data->lights.Count());
    bufferSize += ALIGN_SIZE(bufferSize, M2ModelCamera, this->m_shared->m_data->cameras.Count());
    bufferSize += ALIGN_SIZE(bufferSize, M2ModelParticle, this->m_shared->m_data->particles.Count());

    // TODO allocate space for ribbons

    auto buffer = static_cast<char*>(SMemAlloc(bufferSize, __FILE__, __LINE__, 0));
    auto start = buffer;

    // Allocate and initialize per-model data

    if (this->m_shared->m_data->bones.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, M2ModelBone);
        this->m_bones = reinterpret_cast<M2ModelBone*>(buffer);
        buffer += sizeof(M2ModelBone) * this->m_shared->m_data->bones.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->bones.Count(); i++) {
            new (&this->m_bones[i]) M2ModelBone();
        }

        for (int32_t i = 0; i < this->m_shared->m_data->bones.Count(); i++) {
            this->m_bones[i].flags = this->m_shared->m_data->bones[i].flags;
        }

        // TODO use A16 allocator
        this->m_boneMatrices = static_cast<C44Matrix*>(SMemAlloc(sizeof(C44Matrix) * this->m_shared->m_data->bones.Count(), __FILE__, __LINE__, 0));

        for (int32_t i = 0; i < this->m_shared->m_data->bones.Count(); i++) {
            new (&this->m_boneMatrices[i]) C44Matrix();
        }
    }

    if (this->m_shared->m_data->loops.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, uint32_t);
        this->m_loops = reinterpret_cast<uint32_t*>(buffer);
        buffer += sizeof(uint32_t) * this->m_shared->m_data->loops.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->loops.Count(); i++) {
            if (this->m_loops[i]) {
                this->m_loops[i] = 0;
            }
        }
    }

    if (this->m_shared->skinProfile->skinSections.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, uint32_t);
        this->m_skinSections = reinterpret_cast<uint32_t*>(buffer);
        buffer += sizeof(uint32_t) * this->m_shared->skinProfile->skinSections.Count();

        if (this->model30) {
            memcpy(this->m_skinSections, model30->m_skinSections, sizeof(uint32_t) * this->m_shared->skinProfile->skinSections.Count());
        } else {
            // Mark all skin sections as visible by default
            for (int32_t i = 0; i < this->m_shared->skinProfile->skinSections.Count(); i++) {
                auto modelSkinSection = &this->m_skinSections[i];

                if (modelSkinSection) {
                    *modelSkinSection = 1;
                }
            }
        }
    }

    if (this->m_shared->m_data->colors.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, M2ModelColor);
        this->m_colors = reinterpret_cast<M2ModelColor*>(buffer);
        buffer += sizeof(M2ModelColor) * this->m_shared->m_data->colors.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->colors.Count(); i++) {
            new (&this->m_colors[i]) M2ModelColor();
        }
    }

    if (this->m_shared->m_data->textures.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, HTEXTURE);
        this->m_textures = reinterpret_cast<HTEXTURE*>(buffer);
        buffer += sizeof(HTEXTURE) * this->m_shared->m_data->textures.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->textures.Count(); i++) {
            HTEXTURE textureHandle = this->model30
                ? this->model30->m_textures[i]
                : this->m_shared->textures[i];

            this->m_textures[i] = textureHandle
                ? HandleDuplicate(textureHandle)
                : nullptr;
        }
    }

    if (this->m_shared->m_data->textureWeights.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, M2ModelTextureWeight);
        this->m_textureWeights = reinterpret_cast<M2ModelTextureWeight*>(buffer);
        buffer += sizeof(M2ModelTextureWeight) * this->m_shared->m_data->textureWeights.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->textureWeights.Count(); i++) {
            new (&this->m_textureWeights[i]) M2ModelTextureWeight();
        }
    }

    if (this->m_shared->m_data->textureTransforms.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, M2ModelTextureTransform);
        this->m_textureTransforms = reinterpret_cast<M2ModelTextureTransform*>(buffer);
        buffer += sizeof(M2ModelTextureTransform) * this->m_shared->m_data->textureTransforms.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->textureTransforms.Count(); i++) {
            new (&this->m_textureTransforms[i]) M2ModelTextureTransform();
        }

        // TODO use A16 allocator
        this->m_textureMatrices = static_cast<C44Matrix*>(SMemAlloc(sizeof(C44Matrix) * this->m_shared->m_data->textureTransforms.Count(), __FILE__, __LINE__, 0x0));
    }

    if (this->m_shared->m_data->attachments.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, M2ModelAttachment);
        this->m_attachments = reinterpret_cast<M2ModelAttachment*>(buffer);
        buffer += sizeof(M2ModelAttachment) * this->m_shared->m_data->attachments.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->attachments.Count(); i++) {
            new (&this->m_attachments[i]) M2ModelAttachment();

            auto& modelAttachment = this->m_attachments[i];
            modelAttachment.visibilityTrack.currentValue = 1;
        }
    }

    if (this->m_shared->m_data->lights.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, M2ModelLight);
        this->m_lights = reinterpret_cast<M2ModelLight*>(buffer);
        buffer += sizeof(M2ModelLight) * this->m_shared->m_data->lights.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->lights.Count(); i++) {
            new (&this->m_lights[i]) M2ModelLight();

            auto& light = this->m_shared->m_data->lights[i];
            auto& modelLight = this->m_lights[i];

            modelLight.light.Initialize(this->m_scene);
            modelLight.light.SetLightType(static_cast<M2LIGHTTYPE>(light.lightType));
            modelLight.ambientIntensityTrack.currentValue = 1.0f;
            modelLight.diffuseIntensityTrack.currentValue = 1.0f;
            modelLight.visibilityTrack.currentValue = 1;
        }
    }

    if (this->m_shared->m_data->cameras.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, M2ModelCamera);
        this->m_cameras = reinterpret_cast<M2ModelCamera*>(buffer);
        buffer += sizeof(M2ModelCamera) * this->m_shared->m_data->cameras.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->cameras.Count(); i++) {
            new (&this->m_cameras[i]) M2ModelCamera();
        }

        // The `break` is the reference's, not a simplification: FUN_00832ea0 aborts the whole
        // camera loop on the first bad one rather than skipping it, so a model whose camera 0 is
        // degenerate ends up with NO camera handles at all. That matters more than it looks --
        // CSimpleModel::SetCameraByIndex then stores a null m_camera, the model still draws, and
        // everything gated on having a camera silently does not. The login screen's snow is gated
        // that way.
        //
        // What was NOT the reference's: doing it silently. The reference makes these two separate
        // assertions and prints the failing expression and its value before breaking. frozen had
        // them merged into one condition with no message, so a model that tripped it looked
        // exactly like a model with no cameras. BLIZZARD_ASSERT is not usable here -- it compiles
        // to (void)0 under NDEBUG, and Release is the only build whose visuals are trustworthy --
        // so these report through SysMsgPrintf, which is what the reference's own helper does.
        for (int32_t i = 0; i < this->m_shared->m_data->cameras.Count(); i++) {
            auto& camera = this->m_shared->m_data->cameras[i];
            auto cameraHandle = CameraCreate();

            if (camera.fieldOfView <= 0.0f || camera.fieldOfView >= 3.1415927f) {
                SysMsgPrintf(SYSMSG_ERROR,
                             "M2 camera %d: \"shared->fieldOfView > 0.0f && shared->fieldOfView < PI\","
                             " shared->fieldOfView = %g (%s)",
                             i, camera.fieldOfView, this->m_shared->m_filePath);

                // DIVERGENCE, deliberate: the reference abandons this handle. Closing it costs
                // nothing, cannot change what is drawn -- the loop stops either way -- and leaving
                // a leak in on an error path only makes the next leak harder to find.
                HandleClose(cameraHandle);

                break;
            }

            if (camera.farClip <= camera.nearClip) {
                SysMsgPrintf(SYSMSG_ERROR,
                             "M2 camera %d: \"shared->nearClip < shared->farClip\","
                             " shared->nearClip = %g, shared->farClip = %g (%s)",
                             i, camera.nearClip, camera.farClip, this->m_shared->m_filePath);

                HandleClose(cameraHandle);

                break;
            }

            DataMgrSetFloat(cameraHandle, 4, camera.fieldOfView);
            DataMgrSetFloat(cameraHandle, 3, camera.nearClip);
            DataMgrSetFloat(cameraHandle, 2, camera.farClip);

            this->m_cameras[i].m_camera = cameraHandle;
        }
    }

    // The runtime half of every emitter. The reference allocates it here, out of the same buffer
    // and directly after the cameras, which is why the size list above has it in that position --
    // each ALIGN_SIZE is relative to the running offset, so the two orders have to agree.
    if (this->m_shared->m_data->particles.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, M2ModelParticle);
        this->m_particles = reinterpret_cast<M2ModelParticle*>(buffer);
        buffer += sizeof(M2ModelParticle) * this->m_shared->m_data->particles.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->particles.Count(); i++) {
            new (&this->m_particles[i]) M2ModelParticle();
        }
    }

    // TODO

    // Process model attachments that occurred during load

    CM2Model* attachNext = nullptr;
    for (auto attachModel = this->m_attachList; attachModel; attachModel = attachNext) {
        attachNext = attachModel->m_attachNext;

        uint16_t attachIndex = 0xFFFF;

        if (attachModel->m_attachId < this->m_shared->m_data->attachments.Count()) {
            attachIndex = this->m_shared->m_data->attachmentIndicesById[attachModel->m_attachId];
        }

        if (attachIndex != 0xFFFF || attachModel->m_flag40000) {
            attachModel->m_attachIndex = attachIndex;
        } else {
            attachModel->DetachFromParent();
        }
    }

    // TODO

    this->m_loaded = 1;

    uint32_t savedTime = this->m_scene->m_time;

    while (this->m_modelCallList) {
        auto modelCall = this->m_modelCallList;

        this->m_scene->m_time = modelCall->time;

        switch (modelCall->type) {
            case 0: {
                auto textureId = modelCall->args[0];
                auto texture = *reinterpret_cast<HTEXTURE*>(&modelCall->args[1]);

                this->ReplaceTexture(textureId, texture);

                break;
            }

            case 1: {
                this->SetGeometryVisible(
                    modelCall->args[0],
                    modelCall->args[1],
                    modelCall->args[2]
                );

                break;
            }

            case 2: {
                // TODO
                break;
            }

            case 3: {
                // TODO
                break;
            }

            case 4: {
                // TODO
                break;
            }

            case 5: {
                this->SetBoneSequence(
                    modelCall->args[0],
                    modelCall->args[1],
                    modelCall->args[2],
                    modelCall->args[3],
                    *reinterpret_cast<float*>(&modelCall->args[4]),
                    modelCall->args[5],
                    modelCall->args[6]
                );

                break;
            }

            case 6: {
                this->UnsetBoneSequence(
                    modelCall->args[0],
                    modelCall->args[1],
                    modelCall->args[2]
                );

                break;
            }

            case 7: {
                // TODO
                break;
            }

            case 8: {
                // TODO
                break;
            }

            case 9: {
                // TODO
                break;
            }

            case 10: {
                // TODO
                break;
            }

            case 11: {
                // TODO
                break;
            }

            case 12: {
                // TODO
                break;
            }

            case 13: {
                // TODO
                break;
            }

            case 14: {
                // TODO
                break;
            }
        }

        this->m_modelCallList = modelCall->modelCallNext;

        if (modelCall->type == 0) {
            auto texture = *reinterpret_cast<HTEXTURE*>(&modelCall->args[1]);

            if (texture) {
                HandleClose(texture);
            }
        }

        STORM_FREE(modelCall);
    }

    this->m_scene->m_time = savedTime;

    this->UpdateLoaded();
    this->m_flag800 = 0;

    return 1;
}

// Returns 0, which is the safe answer rather than a placeholder: it means "this batch cannot be
// merged into a doodad batch", so CM2Scene::Animate gives every element type 0 and the doodad path
// is uniformly off. That agrees with the rest of frozen -- the M2BatchDoodads CVar reaches
// CM2Cache::m_flags bit 0x20 and nothing reads it.
//
// **Implementing this alone breaks rendering.** A non-zero answer makes Animate emit type 2
// elements, and type 2 dispatches to CM2SceneRender::DrawBatchDoodad, which is an empty body. Those
// batches would then silently not draw. Port the two together, and wire M2BatchDoodads to gate them
// in the same change.
// Identified 2026-09-23 as FUN_00824550, from CM2Scene::Animate's call order -- frozen calls it
// from the element gather at exactly that position, it is a thiscall on a model taking one pointer,
// and two of its reads settle it: `testb $0x10, (%eax)` on the argument is batch->flags & 0x10, and
// `[[esi+0x2c] + 0x150] + 0x2c` compared against 1 is m_shared->m_data->bones.count, the same
// expression DrawBatch uses. Its first call is to 0x0081c0b0, already mapped as M2GetCacheFlags,
// and it tests bit 0x20 of the result -- which is the M2BatchDoodads flag this comment predicted
// before the function was found.
//
// The body, so that whoever ports it does not have to redo this:
//
//     flags = M2GetCacheFlags();
//     if (!(flags & 0x20))                     return 0;   // M2BatchDoodads off
//     if (!(this->[0x10] & 0x10))              return 0;   // a model flag, not yet mapped here
//     if (m_shared->m_data->bones.count <= 1
//         && (flags & 0x40))                   return 0;
//     if (!(batch->flags & 0x10))              return 0;
//     other = this->[0x2a8];                               // the shared-animation source pointer
//     if (other && other->[0xa4])              return 0;
//     return 1;
//
// Two of those fields (+0x10 and +0x2a8/+0xa4) still need mapping onto frozen's CM2Model, so this
// is a specification rather than a port.
//
// **Still not implemented, deliberately, and the reason below has not changed.**
// ref: FUN_00824550
int32_t CM2Model::IsBatchDoodadCompatible(M2Batch* batch) {
    // TODO -- see the decoded body above, and read the warning above that before enabling it

    return 0;
}

// ref: FUN_00824fc0
int32_t CM2Model::IsDrawable(int32_t a2, int32_t a3) {
    if (!this->m_loaded && a2) {
        this->WaitForLoad(nullptr);
    }

    if (!this->m_flag2) {
        if (!this->m_loaded) {
            return 0;
        }

        for (uint32_t i = 0; i < this->m_shared->m_data->textures.Count(); i++) {
            auto texture = this->m_textures[i];

            if (!texture) {
                continue;
            }

            if (!TextureGetGxTex(texture, a2, nullptr)) {
                return 0;
            }
        }

        this->m_flag2 = 1;
    }

    if (!this->m_flag200 && a3) {
        // TODO
    }

    return 1;
}

// ref: FUN_00824f00
int32_t CM2Model::IsLoaded(int32_t a2, int32_t attachments) {
    if (this->m_flags & 0x20) {
        if (this->m_loaded) {
            return 1;
        }

        if (a2) {
            this->WaitForLoad(nullptr);
        }

        // Either this model finished loading while we waited, or the shared data it needs is
        // already there. The reference ORs the two, and sign-extends the shared test, so this
        // returns -1 as readily as 1; every caller only asks whether it is non-zero.
        int32_t sharedReady = this->m_shared->m_m2DataLoaded && this->m_shared->m_skinProfileLoaded ? -1 : 0;

        return sharedReady | static_cast<int32_t>(this->m_loaded);
    }

    if (!this->m_loaded && a2) {
        this->WaitForLoad(nullptr);
    }

    if (!this->m_loaded) {
        return 0;
    }

    // Every attached model has to be loaded too before this one counts as ready. The answer is
    // cached in the 0x100 flag, so the walk happens once.
    if (attachments && !this->m_flag100) {
        for (auto child = this->m_attachList; child; child = child->m_attachNext) {
            if (!child->IsLoaded(a2, 1)) {
                return 0;
            }
        }

        this->m_flag100 = 1;
    }

    return 1;
}

void CM2Model::LinkToCallbackListTail() {
    this->m_callbackPrev = this->m_shared->m_callbackListTail;
    this->m_callbackNext = nullptr;
    *this->m_shared->m_callbackListTail = this;
    this->m_shared->m_callbackListTail = &this->m_callbackNext;
}

void CM2Model::OptimizeVisibleGeometry() {
    // TODO
}

int32_t CM2Model::ProcessCallbacks() {
    // Notice the bone sequences that finished during the frame just stepped, and let each one pick
    // its next variation. This is what keeps a standing NPC alive: the model's Stand animation is a
    // chain of variations weighted by frequency, and a new one is rolled every time the current one
    // runs out. Returns 0 when the model was destroyed while handling a callback.
    if (!this->m_flag400000 || !this->m_loaded || !this->m_shared || !this->m_shared->m_m2DataLoaded) {
        return 1;
    }

    auto data = this->m_shared->m_data;
    int32_t now = this->m_scene->m_time;
    int32_t previous = now - this->m_scene->uint10;

    for (uint32_t boneIndex = this->m_boneSeqList; boneIndex != 0xFFFF; ) {
        auto& modelBone = this->m_bones[boneIndex];

        // Read the link before the handler runs: re-issuing a sequence can relink the bone.
        uint32_t next = modelBone.word96;

        if (!modelBone.sequence.uintA && modelBone.sequence.uint8 < data->sequences.Count()) {
            auto& sequence = data->sequences[modelBone.sequence.uint8];

            // The sequence plays at float18's rate, so its wall-clock length is the authored
            // duration scaled by it.
            uint32_t duration = sequence.duration;

            if (fabsf(fabsf(modelBone.sequence.float18) - 1.0f) >= 0.0000099999997f) {
                duration = static_cast<uint32_t>(
                    floorf(static_cast<float>(sequence.duration) * fabsf(modelBone.sequence.float18) + 0.5f));
            }

            if (duration) {
                int32_t endTime;

                if (sequence.flags & 0x1) {
                    // Plays once: SetupBoneSequence already worked out when it stops.
                    endTime = modelBone.sequence.uint10;
                } else {
                    // Loops: the end of whichever repetition the frame is inside.
                    int32_t from = static_cast<int32_t>(modelBone.sequence.uintC);

                    if (previous - from >= 0) {
                        from = previous;
                    }

                    uint32_t loops = static_cast<uint32_t>(from - static_cast<int32_t>(modelBone.sequence.uintC)) / duration;
                    endTime = modelBone.sequence.uintC + (loops + 1) * duration - 1;
                }

                if (endTime - previous > 0 && endTime - now <= 0) {
                    this->SequenceFinished(
                        static_cast<uint16_t>(boneIndex),
                        static_cast<uint32_t>(now - endTime),
                        modelBone.sequence.uint8,
                        modelBone.sequence.uintC
                    );

                    // A callback may have released the model out from under us; the caller holds
                    // one reference of its own, so anything less means it is already gone.
                    if (this->m_refCount <= 1) {
                        return 0;
                    }
                }
            }
        }

        boneIndex = next;
    }

    return 1;
}

// One bone sequence has just run out. Roll the next variation of the same animation and start it,
// carrying the overshoot so the new sequence begins where the old one actually ended rather than at
// the frame boundary.
void CM2Model::SequenceFinished(uint16_t boneIndex, uint32_t overshoot, uint16_t seqIndexWas, uint32_t startTimeWas) {
    auto data = this->m_shared->m_data;

    // Only key bones (and the root) drive sequence callbacks in the reference, and the callback is
    // addressed by bone *id*, which resolves back to the canonical bone for that id.
    if (boneIndex >= data->bones.Count()) {
        return;
    }

    if (data->bones[boneIndex].boneId == 0xFFFFFFFF && boneIndex != 0) {
        return;
    }

    uint32_t boneId = (data->bones[boneIndex].parentIndex == 0xFFFF) ? 0xFFFFFFFF : data->bones[boneIndex].boneId;
    uint16_t resolved;

    if (boneId == 0xFFFFFFFF) {
        resolved = 0;
    } else if (boneId < data->boneIndicesById.Count()) {
        resolved = data->boneIndicesById[boneId];
    } else {
        return;
    }

    if (resolved >= data->bones.Count()) {
        return;
    }

    auto& modelBone = this->m_bones[resolved];
    uint16_t seqIndex = modelBone.sequence.uint8;

    // The bone may have been handed something else in the meantime; only the sequence that actually
    // ended gets to choose what follows it.
    if (seqIndex != seqIndexWas || modelBone.sequence.uintC != startTimeWas || seqIndex == 0xFFFF) {
        return;
    }

    auto& sequence = data->sequences[seqIndex];

    if (sequence.flags & 0x1) {
        // Plays once and holds its last frame -- nothing follows it.
        modelBone.sequence.uintA = 1;

        return;
    }

    // Nothing to roll when the animation has a single variation, and nothing to roll when the
    // variation was chosen explicitly rather than at random (uintB).
    if (!modelBone.sequence.uintB || (sequence.variationIndex == 0 && sequence.variationNext == 0xFFFF)) {
        return;
    }

    float rate = modelBone.sequence.float14;

    M2SequenceFallback fallback;
    this->Sub826350(fallback, modelBone.uint90);

    uint32_t index = CM2Model::Sub8260C0(data, fallback.uint0, 0);
    uint32_t variation = 0;
    this->Sub826E60(&variation, &index);

    if (index >= data->sequences.Count()) {
        return;
    }

    // The overshoot is measured in scene milliseconds; a sequence playing at a rate other than 1
    // consumes it faster or slower.
    uint32_t time = overshoot;

    if (fabsf(rate - 1.0f) >= 0.0000099999997f) {
        time = static_cast<uint32_t>(floorf(static_cast<float>(overshoot) * modelBone.sequence.float18 + 0.5f));
    }

    if (data->sequences[index].flags & 0x20) {
        modelBone.uint94 = static_cast<uint16_t>(variation);

        this->SetPrimaryBoneSequence(static_cast<uint16_t>(index), resolved, fallback, time, rate, 1);

        return;
    }

    this->SetBoneSequenceDeferred(static_cast<uint16_t>(index), data, resolved, time, rate, fallback, 1, 1, 1);
}

void CM2Model::ProcessCallbacksRecursive() {
    if (!this->m_loaded) {
        return;
    }

    this->AddRef();

    if (this->ProcessCallbacks()) {
        // TODO process attachments
    }

    this->Release();
}

// ref: FUN_00824ed0
uint32_t CM2Model::Release() {
    STORM_ASSERT(this->m_refCount > 0);

    this->m_refCount--;

    if (this->m_refCount > 0) {
        return this->m_refCount;
    }

    this->~CM2Model();
    ObjectFree(*g_modelPool, this->m_memHandle);

    return 0;
}

// ref: FUN_00825260
void CM2Model::ReplaceTexture(uint32_t textureId, HTEXTURE texture) {
    // Waiting for load

    if (!this->m_loaded) {
        auto modelCall = STORM_NEW(CM2ModelCall);

        modelCall->type = 0;
        modelCall->modelCallNext = nullptr;
        modelCall->time = this->m_scene->m_time;
        modelCall->args[0] = textureId;
        *reinterpret_cast<HTEXTURE*>(&modelCall->args[1]) = texture ? HandleDuplicate(texture) : nullptr;

        *this->m_modelCallTail = modelCall;
        this->m_modelCallTail = &modelCall->modelCallNext;

        return;
    }

    // Replace textures

    for (int32_t i = 0; i < this->m_shared->m_data->textures.Count(); i++) {
        // Only replace if texture IDs match
        if (this->m_shared->m_data->textures[i].textureId != textureId) {
            continue;
        };

        auto currentTexture = this->m_textures[i];

        if (currentTexture) {
            HandleClose(currentTexture);
        }

        if (texture) {
            this->m_textures[i] = HandleDuplicate(texture);

            auto gxTexture = TextureGetGxTex(this->m_textures[i], 0, nullptr);

            if (!gxTexture) {
                this->m_flag2 = 0;
            }
        } else {
            this->m_textures[i] = nullptr;
        }
    }

    // TODO replace ribbon textures

    // TODO replace particle textures
}

void CM2Model::SetAnimating(int32_t animating) {
    if (!animating) {
        if (this->m_animatePrev) {
            *this->m_animatePrev = this->m_animateNext;

            if (this->m_animateNext) {
                this->m_animateNext->m_animatePrev = this->m_animatePrev;
            }

            this->m_animatePrev = nullptr;
            this->m_animateNext = nullptr;
        }

        return;
    }

    if (this->m_flags & 0x20 && !this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    if (!this->m_animatePrev) {
        this->m_animatePrev = &this->m_scene->m_animateList;
        this->m_animateNext = this->m_scene->m_animateList;
        this->m_scene->m_animateList = this;

        if (this->m_animateNext) {
            this->m_animateNext->m_animatePrev = &this->m_animateNext;
        }
    }
}

// ref: FUN_00832ab0
void CM2Model::SetBoneSequence(uint32_t boneId, uint32_t sequenceId, uint32_t a4, uint32_t time, float a6, int32_t a7, int32_t a8) {
    if (sequenceId == -1) {
        this->UnsetBoneSequence(boneId, a7, a8);
        return;
    }

    if (!this->m_loaded) {
        auto m = SMemAlloc(sizeof(CM2ModelCall), __FILE__, __LINE__, 0x0);
        auto modelCall = new (m) CM2ModelCall();

        modelCall->type = 5;
        modelCall->modelCallNext = nullptr;
        modelCall->time = this->m_scene->m_time;
        modelCall->args[0] = boneId;
        modelCall->args[1] = sequenceId;
        modelCall->args[2] = a4;
        modelCall->args[3] = time;
        *reinterpret_cast<float*>(&modelCall->args[4]) = a6;
        modelCall->args[5] = a7;
        modelCall->args[6] = a8;

        *this->m_modelCallTail = modelCall;
        this->m_modelCallTail = &modelCall->modelCallNext;

        return;
    }

    if (this->m_flag800) {
        a7 = 0;
    }

    uint16_t boneIndex;
    if (boneId == -1) {
        boneIndex = 0;
    } else if (boneId < this->m_shared->m_data->boneIndicesById.Count()) {
        boneIndex = this->m_shared->m_data->boneIndicesById[boneId];
    } else {
        boneIndex = -1;
    }

    if (boneIndex >= this->m_shared->m_data->bones.Count()) {
        return;
    }

    M2SequenceFallback fallback;
    this->Sub826350(fallback, sequenceId);
    int32_t v33 = a4 == -1;

    uint16_t v15 = CM2Model::Sub8260C0(this->m_shared->m_data, fallback.uint0, a4 != -1 ? a4 : 0);
    uint32_t v16 = v15;
    uint32_t v32 = v15;
    uint32_t v17;

    if (v15 != 0xFFFF) {
        if (!v33) {
            goto LABEL_30;
        }

        goto LABEL_29;
    }

    v17 = this->m_shared->m_data->sequenceIdxHashById.Count();
    v33 = 1;
    v32 = v17;
    uint16_t v18;

    if (v17) {
        uint32_t v20 = fallback.uint0 % v17;
        v18 = this->m_shared->m_data->sequenceIdxHashById[v20];

        if (v18 != 0xFFFF) {
            uint32_t v21 = 1;

            if (this->m_shared->m_data->sequences[v18].id != fallback.uint0) {
                while (1) {
                    v20 = (v20 + v21 * v21) % v32;
                    v18 = this->m_shared->m_data->sequenceIdxHashById[v20];

                    if (v18 == 0xFFFF) {
                        break;
                    }

                    ++v21;

                    if (this->m_shared->m_data->sequences[v18].id == fallback.uint0) {
                        goto LABEL_26;
                    }
                }

                v32 = 0xFFFF;

                goto LABEL_29;
            }

            goto LABEL_26;
        }

        v32 = 0xFFFF;
    } else {
        v18 = 0;

        if (this->m_shared->m_data->sequences.Count())  {
            while (this->m_shared->m_data->sequences[v18].id != fallback.uint0) {
                ++v18;

                if (v18 >= this->m_shared->m_data->sequences.Count()) {
                    goto LABEL_20;
                }
            }

LABEL_26:
            v32 = v18;
            goto LABEL_29;
        }

LABEL_20:
        v32 = 0xFFFF;
    }

LABEL_29:
    this->Sub826E60(&a4, &v32);
    v16 = v32;

LABEL_30:
    // A sequence the model does not carry leaves the not-found marker (0xFFFF) in v16: the lookup
    // above yields it, and Sub826E60 (which resolves the variation in the original) is still a
    // stub here, so nothing clears it. Indexing sequences[] with it read far out of bounds and
    // crashed the client whenever a unit asked for an animation its model lacks.
    if (v16 >= this->m_shared->m_data->sequences.Count()) {
        return;
    }

    if (this->m_shared->m_data->sequences[v16].flags & 0x20) {
        if (this->Sub8269C0(boneId, boneIndex)) {
            this->CancelDeferredSequences(boneIndex, a8 != 0);

            auto& modelBone = this->m_bones[boneIndex];

            if (a8) {
                modelBone.uint90 = sequenceId;
                modelBone.uint94 = a4;

                this->SetPrimaryBoneSequence(v16, boneIndex, fallback, time, a6, a7);
                modelBone.sequence.uintB = v33;
            } else {
                this->SetSecondaryBoneSequence(v16, boneIndex, fallback, time, a6);
                modelBone.secondarySequence.uintB = v33;
            }
        }
    } else {
        this->SetBoneSequenceDeferred(v16, this->m_shared->m_data, boneIndex, time, a6, fallback, a7, a8, v33);
    }
}

// The replay half of FUN_0083d840 (CM2Shared::SequenceLoadedCallback): apply one parked request
// now that the sequence's keyframes are in. 0 when the bone no longer takes sequences, in which
// case the callback keeps the record (the reference leaves it in the list as well).
int32_t CM2Model::ApplySequencePlayBack(uint16_t sequenceIndex, CM2SequencePlayBack* playback) {
    auto data = this->m_shared->m_data;

    if (!this->Sub8269C0(data->bones[playback->boneIndex].boneId, playback->boneIndex)) {
        return 0;
    }

    auto& sequence = data->sequences[sequenceIndex];
    auto& modelBone = this->m_bones[playback->boneIndex];
    M2SequenceFallback fallback = { playback->fallbackId, playback->fallbackMode };

    if (playback->flags & 2) {
        modelBone.uint90 = sequence.id;
        modelBone.uint94 = sequence.variationIndex;
        this->SetPrimaryBoneSequence(sequenceIndex, playback->boneIndex, fallback, playback->time, playback->speed, playback->flags & 1);
        modelBone.sequence.uintB = playback->flags & 4;
    } else {
        this->SetSecondaryBoneSequence(sequenceIndex, playback->boneIndex, fallback, playback->time, playback->speed);
        modelBone.secondarySequence.uintB = playback->flags & 4;
    }

    return 1;
}

// ref: FUN_00831c30
// A sequence whose keyframes are still on disk (no flag 0x20): park the request on the shared
// model's load record for it -- the one already in flight when the sequence (or any alias in its
// chain) carries the loading flag 0x10, else a fresh CM2Shared::LoadSequence -- and let
// CM2Shared::SequenceLoadedCallback apply it when the .anim data lands. One record per model per
// load: a repeat request from the same model just overwrites its parked parameters.
void CM2Model::SetBoneSequenceDeferred(uint16_t a2, M2Data* data, uint16_t boneIndex, uint32_t time, float a6, M2SequenceFallback fallback, int32_t a8, int32_t a9, int32_t a10) {
    CM2SequenceLoad* load = nullptr;
    CM2SequencePlayBack* playback = nullptr;

    if (data->sequences[a2].flags & 0x10) {
        uint16_t index = a2;

        for (;;) {
            for (load = this->m_shared->m_sequenceLoads.Head(); load; load = this->m_shared->m_sequenceLoads.Next(load)) {
                if (load->sequenceIndex == index) {
                    break;
                }
            }

            if (load) {
                for (playback = load->playbacks.Head(); playback; playback = load->playbacks.Next(playback)) {
                    if (playback->model == this) {
                        break;
                    }
                }

                break;
            }

            index = data->sequences[index].aliasNext;

            if (index == a2) {
                return;
            }
        }
    } else {
        load = this->m_shared->LoadSequence(a2);

        if (!load) {
            return;
        }
    }

    if (!playback) {
        playback = STORM_NEW(CM2SequencePlayBack);
        load->playbacks.LinkToTail(playback);
        playback->model = this;
    }

    playback->speed = a6;
    playback->boneIndex = boneIndex;
    playback->time = time;
    playback->fallbackId = fallback.uint0;
    playback->fallbackMode = fallback.uint2;
    playback->flags = (a8 ? 1 : 0) | (a9 ? 2 : 0) | (a10 ? 4 : 0);
}

// ref: FUN_0082c7c0
void CM2Model::SetGeometryVisible(uint32_t start, uint32_t end, int32_t visible) {
    // Waiting for load

    if (!this->m_loaded) {
        auto modelCall = STORM_NEW(CM2ModelCall);

        modelCall->type = 1;
        modelCall->modelCallNext = nullptr;
        modelCall->time = this->m_scene->m_time;
        modelCall->args[0] = start;
        modelCall->args[1] = end;
        modelCall->args[2] = visible;

        *this->m_modelCallTail = modelCall;
        this->m_modelCallTail = &modelCall->modelCallNext;

        return;
    }

    // No skin sections

    if (!this->m_shared->skinProfile->skinSections.Count()) {
        return;
    }

    // Update visibility

    bool visibilityChanged = false;

    for (uint32_t i = 0; i < this->m_shared->skinProfile->skinSections.Count(); i++) {
        auto& skinSection = this->m_shared->skinProfile->skinSections[i];
        auto modelSkinSection = &this->m_skinSections[i];

        // Out of range
        if (skinSection.skinSectionId < start || skinSection.skinSectionId > end) {
            continue;
        }

        // No change
        if (*modelSkinSection == 0 && visible == 0 || *modelSkinSection == 1 && visible == 1) {
            continue;
        }

        *modelSkinSection = visible;
        visibilityChanged = true;
    }

    if (visibilityChanged) {
        this->UnoptimizeVisibleGeometry();
    }
}

// Identified from DrawBatch's call order: the reference runs 0x00683560, 0x00683580, 0x00828f90,
// 0x008360a0 and then SetBatchVertices, and frozen runs GxShaderConstantsLock, Unlock,
// m_curModel->SetIndices, m_curShared->SetIndices and then SetBatchVertices. The other three of
// those are pinned independently, and this is the remaining one -- a thiscall on the model,
// reaching through +0x2d0. Still a stub here, so the tag is a claim about identity, not about
// behaviour.
// ref: FUN_00828f90
void CM2Model::SetIndices() {
    // Unreachable today, so this is dead rather than broken: CM2SceneRender::DrawBatch only calls
    // it for an element with flag 0x4, which CM2Scene sets from model->ptr2D0, which nothing ever
    // allocates because OptimizeVisibleGeometry is itself unported. Port that first.
    // TODO
}

void CM2Model::SetLightingCallback(void (*lightingCallback)(CM2Model*, CM2Lighting*, void*), void* lightingArg) {
    this->m_lightingCallback = lightingCallback;
    this->m_lightingArg = lightingArg;
}

void CM2Model::SetLoadedCallback(void (*loadedCallback)(CM2Model*, void*), void* loadedArg) {
    this->m_loadedCallback = loadedCallback;
    this->m_loadedArg = loadedArg;

    this->UpdateLoaded();
}

void CM2Model::SetPrimaryBoneSequence(uint16_t sequenceIndex, uint16_t boneIndex, M2SequenceFallback fallback, uint32_t time, float a6, int32_t a7) {
    auto& modelBone = this->m_bones[boneIndex];
    auto& sequence = this->m_shared->m_data->sequences[sequenceIndex];

    if (a7) {
        if (!modelBone.sequence.uintA || sequenceIndex != modelBone.sequence.uint8) {
            double v10;
            double v11;
            double v12;

            if (modelBone.secondarySequence.uint8 == 0xFFFF
                || ((v10 = (double)(modelBone.uint9C - this->m_scene->m_time) * modelBone.floatA0, v10 >= 0.0) ? (v10 <= 1.0 ? (v11 = v10 * ((3.0 - (v10 + v10)) * v10)) : (v11 = 1.0)) : (v11 = 0.0), v11 * modelBone.floatA4 <= 0.5)
            ) {
                memcpy(&modelBone.secondarySequence, &modelBone.sequence, sizeof(modelBone.secondarySequence));

                modelBone.uint9C = this->m_scene->m_time + sequence.blendtime;
                if (sequence.blendtime) {
                    v12 = 1.0 / (double)sequence.blendtime;
                } else {
                    v12 = 1.0;
                }
                modelBone.floatA0 = v12;
                modelBone.floatA4 = 1.0f;
            }
        }
    } else {
        modelBone.secondarySequence.uint8 = -1;
    }

    this->SetupBoneSequence(sequenceIndex, fallback, time, a6, &modelBone.sequence);

    int32_t v13 = modelBone.sequence.uint10;
    if (modelBone.sequence.uintC == v13 || ((sequence.flags & 0x1) != 0 && (v13 -= this->m_scene->m_time, v13 <= 0))) {
        modelBone.sequence.uintA = 1;
    }

    // Link the bone into the model's animating-bone list the first time it is given a sequence, so
    // ProcessCallbacks can find it again when that sequence ends. Without this list nothing ever
    // noticed a finished animation, and a unit held whichever variation it was first handed.
    if (!modelBone.dword98) {
        modelBone.dword98 = &this->m_boneSeqList;
        modelBone.word96 = this->m_boneSeqList;

        if (modelBone.word96 != 0xFFFF) {
            this->m_bones[modelBone.word96].dword98 = &modelBone.word96;
        }

        this->m_boneSeqList = boneIndex;
        this->m_flag400000 = 1;
    }
}

// ref: FUN_00826dd0
// Put a sequence straight into the bone's secondary slot and start it fading.
//
// Note it times the fade from the sequence's DURATION, where SetPrimaryBoneSequence uses its
// blendtime, and it starts the weight at 0.75 rather than 1. That reads like a slip but it is
// what the reference does: FUN_00826dd0 takes the field at +0x4 while FUN_00826c40 takes +0x1c.
void CM2Model::SetSecondaryBoneSequence(uint16_t a2, uint16_t boneIndex, M2SequenceFallback fallback, uint32_t time, float a6) {
    auto& modelBone = this->m_bones[boneIndex];
    auto& sequence = this->m_shared->m_data->sequences[a2];

    modelBone.uint9C = this->m_scene->m_time + sequence.duration;
    modelBone.floatA0 = sequence.duration ? 1.0f / static_cast<float>(sequence.duration) : 1.0f;
    modelBone.floatA4 = 0.75f;

    this->SetupBoneSequence(a2, fallback, time, a6, &modelBone.secondarySequence);
}

void CM2Model::SetupBoneSequence(uint16_t sequenceIndex, M2SequenceFallback fallback, uint32_t a4, float a5, M2ModelBoneSeq* boneSequence) {
    auto& sequence = this->m_shared->m_data->sequences[sequenceIndex];

    int32_t v9 = rand();
    uint32_t v10 = (sequence.replay.l + (sequence.replay.h - sequence.replay.l) * v9 / 0x8000 == 0)
        + sequence.replay.l + (sequence.replay.h - sequence.replay.l) * v9 / 0x8000;
    int32_t v11 = v10 * sequence.duration;

    double v12;
    double v13;
    long double v15;

    if (fallback.uint2 == 1 || fallback.uint2 == 3) {
        v12 = -a5;
    } else {
        v12 = a5;
    }

    v13 = 0.0;

    uint32_t v18 = 0;
    if (v12 < 0.0) {
        v18 = v11;
    }

    if (fallback.uint2 == 2 || fallback.uint2 == 3) {
        v12 = 0.0;
    }

    if (abs(v12) > 0.0000099999997) {
        v13 = 1.0 / v12;
    }

    v15 = abs(v13);
    uint32_t v16 = this->m_scene->m_time - floor((double)a4 * v15);

    if ((~(this->m_scene->m_flags >> 2) & 0x1) != 0) {
        v16++;
    }

    boneSequence->uint8 = sequenceIndex;
    boneSequence->uintC = v16;
    boneSequence->uint20 = v10;
    boneSequence->uintA = 0;
    boneSequence->uint10 = v16 + floor(v15 * (double)(unsigned int)v11);
    boneSequence->uint1C = v18;
    boneSequence->float14 = v12;
    boneSequence->float18 = v13;
}

// Identified beyond doubt from its call list: CM2Lighting::Initialize (0x00834900),
// CM2Scene::SelectLights (0x0081e400), the lighting callback through a pointer,
// CM2Lighting::SetupSunlight (0x00835280), CM2Lighting::CameraSpace (0x008350a0) -- and
// then itself, which is the recursion into m_attachList below.
// ref: FUN_00831af0
void CM2Model::SetupLighting() {
    if (!this->m_attachParent || this->m_attachParent->m_flags & 0x1) {
        this->Animate();

        CAaSphere sphere;
        sphere.c = this->GetPosition();
        sphere.r = 0.0f;

        this->m_lighting.Initialize(this->m_scene, sphere);
        this->m_scene->SelectLights(&this->m_lighting);

        if (this->m_lightingCallback) {
            this->m_lightingCallback(this, &this->m_lighting, this->m_lightingArg);
        }

        this->m_lighting.SetupSunlight();
        this->m_lighting.CameraSpace();
    }

    for (auto model = this->m_attachList; model; model = model->m_attachNext) {
        model->SetupLighting();
    }
}

void CM2Model::SetVisible(int32_t visible) {
    if (this->m_attachParent) {
        this->m_flag80 = visible ? 1 : 0;
    } else {
        this->m_flag8 = visible ? 1 : 0;
    }
}

void CM2Model::SetWorldTransform(const C3Vector& position, float orientation, float scale) {
    this->matrixB4.Identity();
    this->matrixB4.RotateAroundZ(orientation);
    this->matrixB4.Scale(scale);
    this->matrixB4.d0 = position.x;
    this->matrixB4.d1 = position.y;
    this->matrixB4.d2 = position.z;

    this->m_flag8000 = 1;
}

void CM2Model::Sub826350(M2SequenceFallback& fallback, uint32_t sequenceId) {
    auto data = this->m_shared->m_data;

    int32_t v12;
    if (CM2Model::Sub825E00(data, 0)) {
        v12 = 0;
    } else if (CM2Model::Sub825E00(data, 147)) {
        v12 = 147;
    } else {
        v12 = data->sequences[0].id;
    }

    uint32_t v10[506];
    memset(v10, 0, sizeof(v10));

    if (CM2Model::Sub825E00(data, sequenceId)) {
        fallback.uint0 = sequenceId;
        fallback.uint2 = 0;
        return;
    }

    // The model does not carry this animation, so follow AnimationData.dbc's fallback chain until
    // it reaches one the model does have. Two flags on the records passed through decide how the
    // result is played: 0x10 reverses the direction (a "close" animation is the "open" one run
    // backwards), 0x20 stops it dead on its last frame (a hold pose). uint2 encodes that as
    // 0 = forwards, 1 = backwards, 2 = hold at the start, 3 = hold at the end.
    //
    // The visited set is bounded at 506 entries the way the reference bounds it -- its own scratch
    // array is 0x7E8 bytes -- so a cyclic or out-of-range chain terminates instead of spinning.
    uint32_t visited[506];
    memset(visited, 0, sizeof(visited));

    int32_t direction = 1;
    int32_t held = 0;
    uint32_t animID = sequenceId;
    uint32_t result = v12;
    uint32_t next = 0;

    while (1) {
        auto record = g_animationDataDB.GetRecord(animID);

        result = v12;

        if (animID >= 506 || visited[animID] || !record || record->m_fallback == static_cast<int32_t>(animID)) {
            break;
        }

        next = static_cast<uint32_t>(record->m_fallback);
        visited[animID] = 1;

        if (record->m_flags & 0x10) {
            held += direction;
            direction = -direction;
        }

        if (record->m_flags & 0x20) {
            held += direction;
            direction = 0;
        }

        animID = next;

        if (CM2Model::Sub825E00(data, animID)) {
            // Found one the model carries. A still-positive direction means the chain only renamed
            // the animation, so it plays normally and falls through to the plain result below.
            result = animID;

            if (direction < 0) {
                fallback.uint0 = animID;
                fallback.uint2 = 1;
                return;
            }

            if (direction == 0) {
                fallback.uint0 = animID;
                fallback.uint2 = (held > 0) + 2;
                return;
            }

            break;
        }
    }

    fallback.uint0 = result;
    fallback.uint2 = 0;
}

// ref: FUN_008269c0
// Ask the model's sequence-finished callback whether stopping this bone is allowed, and report
// whether the model survived the call.
//
// The reference only does anything when the callback pointer is set: it raises the refcount,
// invokes the callback, releases, and returns 0 if that release destroyed the model. frozen has
// no such callback member yet, so the whole body is skipped and 1 -- "the model is still here,
// carry on" -- is the correct answer rather than a placeholder. It stops being correct the day
// the callback is ported; UnsetBoneSequence is the caller that depends on it.
int32_t CM2Model::Sub8269C0(uint32_t boneId, uint16_t boneIndex) {
    return 1;
}

void CM2Model::Sub826E60(uint32_t* a2, uint32_t* a3) {
    // Choose which variation of an animation to play, weighted by M2Sequence::frequency.
    //
    // The frequencies of one animation's variations sum to 0x7FFF, which is exactly RAND_MAX here,
    // so a single rand() walks the chain subtracting each variation's weight until it lands inside
    // one. This is what gives an NPC its rare idle flourishes -- without it every unit sits on
    // variation 0 forever, which is why the world looked frozen.
    *a2 = 0;

    uint32_t roll = rand();
    uint32_t index = *a3;
    int32_t variation = 0;

    if (index == 0xFFFF) {
        return;
    }

    auto& sequences = this->m_shared->m_data->sequences;

    while (1) {
        uint32_t frequency = sequences[index].frequency;

        if (roll < frequency) {
            break;
        }

        roll -= frequency;
        index = sequences[index].variationNext;
        variation++;

        // Ran off the end of the chain (every variation weighted zero, or weights that do not sum
        // to RAND_MAX): leave the caller's sequence index alone.
        if (index == 0xFFFF) {
            return;
        }
    }

    *a3 = index;
    *a2 = variation;
}

void CM2Model::UnlinkFromAnimateList() {
    if (this->m_animatePrev) {
        *this->m_animatePrev = this->m_animateNext;
    }

    if (this->m_animateNext) {
        this->m_animateNext->m_animatePrev = this->m_animatePrev;
    }
}

void CM2Model::UnlinkFromAttachList() {
    if (this->m_attachPrev) {
        *this->m_attachPrev = this->m_attachNext;
    }

    if (this->m_attachNext) {
        this->m_attachNext->m_attachPrev = this->m_attachPrev;
    }
}

void CM2Model::UnlinkFromCallbackList() {
    if (this->m_callbackPrev) {
        *this->m_callbackPrev = this->m_callbackNext;

        if (this->m_callbackNext) {
            this->m_callbackNext->m_callbackPrev = this->m_callbackPrev;
        } else {
            this->m_shared->m_callbackListTail = this->m_callbackPrev;
        }

        this->m_callbackPrev = nullptr;
        this->m_callbackNext = nullptr;
    }
}

void CM2Model::UnlinkFromDrawList() {
    if (this->m_drawPrev) {
        *this->m_drawPrev = this->m_drawNext;
    }

    if (this->m_drawNext) {
        this->m_drawNext->m_drawPrev = this->m_drawPrev;
    }
}

void CM2Model::UnoptimizeVisibleGeometry() {
    // TODO
}

// ref: FUN_00832840
// Stop whatever a bone is playing. a4 picks which slot: the primary sequence, or the secondary
// one alone. a3 asks for the stop to be blended rather than instant, which the model's 0x800 flag
// can veto. SetBoneSequence routes here when handed sequence id -1, and the character component
// calls it directly to clear the face and hair bones.
void CM2Model::UnsetBoneSequence(uint32_t boneId, int32_t a3, int32_t a4) {
    // Waiting for load

    if (!this->m_loaded) {
        auto modelCall = STORM_NEW(CM2ModelCall);

        modelCall->type = 6;
        modelCall->modelCallNext = nullptr;
        modelCall->time = this->m_scene->m_time;
        modelCall->args[0] = boneId;
        modelCall->args[1] = a3;
        modelCall->args[2] = a4;

        *this->m_modelCallTail = modelCall;
        this->m_modelCallTail = &modelCall->modelCallNext;

        return;
    }

    if (this->m_flag800) {
        a3 = 0;
    }

    auto data = this->m_shared->m_data;

    // Resolve the bone id to an index

    uint16_t boneIndex;

    if (boneId == 0xFFFFFFFF) {
        boneIndex = 0;
    } else if (boneId < data->boneIndicesById.Count()) {
        boneIndex = data->boneIndicesById[boneId];
    } else {
        boneIndex = 0xFFFF;
    }

    if (boneIndex >= data->bones.Count() || boneIndex == 0) {
        return;
    }

    if (data->bones[boneIndex].parentIndex == 0xFFFF) {
        return;
    }

    if (!this->Sub8269C0(boneId, boneIndex)) {
        return;
    }

    this->CancelDeferredSequences(boneIndex, a4 != 0);

    auto& modelBone = this->m_bones[boneIndex];

    // Secondary slot only: clear it and leave the primary alone.
    if (!a4) {
        modelBone.secondarySequence.uint8 = 0xFFFF;
        modelBone.secondarySequence.float14 = 0.0f;
        modelBone.secondarySequence.uintC = 0;
        modelBone.secondarySequence.float18 = 0.0f;
        modelBone.secondarySequence.uint10 = 0;
        modelBone.secondarySequence.uint1C = 0;

        return;
    }

    // Unlink this bone from the model's animating-bone list

    if (modelBone.dword98) {
        *modelBone.dword98 = modelBone.word96;
    }

    if (modelBone.word96 != 0xFFFF) {
        this->m_bones[modelBone.word96].dword98 = modelBone.dword98;
    }

    modelBone.dword98 = nullptr;
    modelBone.word96 = 0xFFFF;

    if (!a3) {
        modelBone.secondarySequence.uint8 = 0xFFFF;
    } else {
        // Blended stop: the sequence being stopped is promoted into the secondary slot and faded
        // out over 150ms. If a previous fade is still more than half way through, it wins and the
        // promotion is skipped, so a fast stream of stops cannot keep restarting the blend.
        bool promote = true;

        if (modelBone.secondarySequence.uint8 != 0xFFFF) {
            float t = static_cast<float>(static_cast<int32_t>(modelBone.uint9C) - static_cast<int32_t>(this->m_scene->m_time)) * modelBone.floatA0;
            float weight = 0.0f;

            if (t >= 0.0f && t <= 1.0f) {
                weight = (3.0f - (t + t)) * t * t;
            } else if (t > 1.0f) {
                weight = 1.0f;
            }

            weight = weight * modelBone.floatA4;

            if (weight > 0.5f) {
                promote = false;
            }
        }

        if (promote) {
            modelBone.secondarySequence = modelBone.sequence;
            modelBone.floatA0 = 1.0f / 150.0f;
            modelBone.uint9C = this->m_scene->m_time + 150;
            modelBone.floatA4 = 1.0f;
        }
    }

    modelBone.sequence.float14 = 0.0f;
    modelBone.sequence.uint8 = 0xFFFF;
    modelBone.sequence.float18 = 0.0f;
    modelBone.uint90 = 0xFFFFFFFF;
    modelBone.uint94 = 0;
    modelBone.sequence.uintC = 0;
    modelBone.sequence.uint10 = 0;
    modelBone.sequence.uint1C = 0;
}

void CM2Model::UpdateLoaded() {
    auto model = this;

    while (model) {
        if (!model->IsLoaded(0, !(this->m_flags & 0x20))) {
            break;
        }

        if (model->m_loadedCallback) {
            model->m_loadedCallback(this, model->m_loadedArg);
            model->m_loadedCallback = nullptr;
        }

        model = model->m_attachParent;
    }
}

void CM2Model::WaitForLoad(const char* a2) {
    if (this->m_shared->asyncObject) {
        AsyncFileReadWait(this->m_shared->asyncObject);
    }

    if (this->m_shared->asyncObject) {
        AsyncFileReadWait(this->m_shared->asyncObject);
    }

    if (this->m_flags & 0x20) {
        this->InitializeLoaded();
    }
}
