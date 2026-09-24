#ifndef MODEL_M2_MODEL_HPP
#define MODEL_M2_MODEL_HPP

#include "gx/Camera.hpp"
#include "model/CM2Light.hpp"
#include <cstdint>
#include <tempest/Quaternion.hpp>
#include <tempest/Vector.hpp>

template<class T>
class M2Track;

class CM2Model;

template<class T>
struct M2ModelTrack {
    uint32_t currentKey = 0;
    M2Track<T>* sourceTrack = nullptr;
    T currentValue;
};

struct M2ModelAttachment {
    M2ModelTrack<uint8_t> visibilityTrack;
};

struct M2ModelBoneSeq {
    uint32_t uint0 = 0;
    uint16_t uint4 = -1;
    uint16_t uint6 = -1;
    uint16_t uint8 = -1;
    uint8_t uintA = 1;
    uint8_t uintB = 1;
    uint32_t uintC = 0;
    uint32_t uint10 = 0;
    float float14 = 0.0;
    float float18 = 0.0;
    uint32_t uint1C = 0;
    uint32_t uint20 = 0;
};

struct M2ModelBone {
    M2ModelBone();

    M2ModelTrack<C3Vector> translationTrack;
    M2ModelTrack<C4Quaternion> rotationTrack;
    M2ModelTrack<C3Vector> scaleTrack;
    M2ModelBoneSeq sequence;
    M2ModelBoneSeq secondarySequence;
    uint32_t flags = 0;
    uint32_t uint90 = -1;
    uint16_t uint94 = 0;

    // Intrusive list of the bones that currently carry a sequence, threaded through the model's
    // m_boneSeqList head. CM2Model::ProcessCallbacks walks it once a frame to notice a sequence
    // that has just ended, so it does not have to scan every bone of every model.
    uint16_t word96 = 0xFFFF;
    uint16_t* dword98 = nullptr;

    uint32_t uint9C = 0;
    float floatA0 = 0.0f;
    float floatA4 = 1.0f;
    float floatA8 = 0.0f;
};

struct M2ModelCamera {
    M2ModelTrack<C3Vector> positionTrack;
    M2ModelTrack<C3Vector> targetTrack;
    M2ModelTrack<float> rollTrack;
    HCAMERA m_camera = nullptr;
};

// One emitter's animated state, the runtime half of an M2Particle. 0x88 bytes in the reference:
// ten 12-byte tracks from +0x08, then the four flags the driver latches.
//
// The tracks pair one-for-one with M2Particle's ten M2Track<float> members in file order, which is
// how the reference walks them. frozen's M2ModelTrack holds the same three fields in a different
// order and widens with 64-bit pointers, so this is structurally the reference's block rather than
// byte-identical to it -- the same divergence every other M2Model* runtime struct here carries.
struct M2ModelParticle {
    M2ModelTrack<float> speedTrack;
    M2ModelTrack<float> variationTrack;
    M2ModelTrack<float> latitudeTrack;
    M2ModelTrack<float> longitudeTrack;
    M2ModelTrack<float> gravityTrack;
    M2ModelTrack<float> lifeTrack;
    M2ModelTrack<float> emissionRateTrack;
    M2ModelTrack<float> widthTrack;
    M2ModelTrack<float> lengthTrack;
    M2ModelTrack<float> zsourceTrack;

    // +0x80: the emitter ran this frame. The driver only refreshes the tracks above when this is
    // set or the model has never animated, so a culled emitter keeps its last values.
    uint8_t enabled = 0;
    // +0x84: the emission rate is live. Clear means the driver hands the emitter a rate of zero
    // rather than the animated one.
    uint8_t rateActive = 0;
    // +0x85: the emitter is placed and stepped this frame.
    uint8_t active = 0;
    // +0x86: burst latch, for emitters with M2Particle flag 0x8000. The driver raises the
    // emitter's own 0x40 bit on the frame this goes from clear to set, so a burst fires once
    // rather than every frame it stays enabled.
    uint8_t burstLatch = 0;
};

struct M2ModelColor {
    M2ModelTrack<C3Vector> colorTrack;
    M2ModelTrack<float> alphaTrack;
};

struct M2ModelLight {
    M2ModelLight();

    M2ModelTrack<C3Vector> ambientColorTrack;
    M2ModelTrack<float> ambientIntensityTrack;
    M2ModelTrack<C3Vector> diffuseColorTrack;
    M2ModelTrack<float> diffuseIntensityTrack;
    M2ModelTrack<uint8_t> visibilityTrack;
    uint32_t uint64 = 1;
    CM2Light light;
};

struct M2ModelTextureTransform {
    M2ModelTrack<C3Vector> translationTrack;
    M2ModelTrack<C4Quaternion> rotationTrack;
    M2ModelTrack<C3Vector> scaleTrack;
};

struct M2ModelTextureWeight {
    M2ModelTrack<float> weightTrack;
};

#endif
