#ifndef MODEL_M2_ANIMATE_HPP
#define MODEL_M2_ANIMATE_HPP

#include "model/CM2Model.hpp"
#include "model/M2Data.hpp"
#include "model/M2Model.hpp"

struct M2SequenceFallback {
    uint16_t uint0;
    uint16_t uint2;
};

template<class T1, class T2>
void M2SetValue(const T1& sourceValue, T2& destValue) {
    destValue = sourceValue;
}

template<>
void M2SetValue(const M2CompQuat& sourceValue, C4Quaternion& destValue) {
    destValue.x = (sourceValue.auCompQ[0] & 0xFFFF) * 0.000030518044f - 1.0f;
    destValue.y = (sourceValue.auCompQ[0] >> 16)    * 0.000030518044f - 1.0f;
    destValue.z = (sourceValue.auCompQ[1] & 0xFFFF) * 0.000030518044f - 1.0f;
    destValue.w = (sourceValue.auCompQ[1] >> 16)    * 0.000030518044f - 1.0f;
}

template<>
void M2SetValue(const fixed16& sourceValue, float& destValue) {
    destValue = static_cast<float>(sourceValue);
}

void M2InterpolateLinear(const C3Vector& startValue, const C3Vector& endValue, float ratio, C3Vector& value) {
    value.x = startValue.x + (ratio * (endValue.x - startValue.x));
    value.y = startValue.y + (ratio * (endValue.y - startValue.y));
    value.z = startValue.z + (ratio * (endValue.z - startValue.z));
}

void M2InterpolateLinear(float startValue, float endValue, float ratio, float& value) {
    value = startValue + (ratio * (endValue - startValue));
}

void M2InterpolateLinear(fixed16 startValue, fixed16 endValue, float ratio, float& value) {
    value = static_cast<float>(startValue) + (ratio * (static_cast<float>(endValue) - static_cast<float>(startValue)));
}

void M2InterpolateLinear(uint8_t startValue, uint8_t endValue, float ratio, uint8_t& value) {
    value = startValue + (ratio * (endValue - startValue));
}

void M2InterpolateLinear(const M2CompQuat& startValue, const M2CompQuat& endValue, float ratio, C4Quaternion& value) {
    C4Quaternion quat1;
    C4Quaternion quat2;
    M2SetValue(startValue, quat1);
    M2SetValue(endValue, quat2);

    value = C4Quaternion::Nlerp(ratio, quat1, quat2);
}

// The four spline interpolators. All four were empty bodies that left `value` UNTOUCHED, so a
// track of type 2 or 3 held whatever it last had and never animated at all -- the switch in
// M2AnimateSplineTrack below reaches them for exactly those two types.
//
// These were originally written from the M2 format because the reference's own versions could
// not be found. THEY HAVE NOW BEEN FOUND AND THEY AGREE, coefficient for coefficient:
// FUN_0082b460 is the C3Vector instantiation (36-byte keys) and FUN_0082b8a0 the float one
// (12-byte keys), and both build their weights out of three constants -- 1.0 at 0x009e1130,
// 2.0 at 0x00a4040c and 3.0 at 0x009ebbc4, with 6.0 at 0x009e8cf8 in the Bezier.
//
//   trackType 2  -t^3 + 3t^2 - 3t + 1, 3t^3 - 6t^2 + 3t, 3t^2 - 3t^3, t^3
//                = (1-t)^3, 3t(1-t)^2, 3t^2(1-t), t^3, against value, outTan, inTan, value
//   trackType 3  2t^3 - 3t^2 + 1, t^3 - 2t^2 + t, 3t^2 - 2t^3, t^3 - t^2
//                = h00, h10, h01, h11, against value, outTan, value, inTan
//
// which is what is written below. The open question that note carried -- whether the reference
// premultiplies the tangents by the key interval -- is answered NO: the tangents enter the sum
// raw, with nothing but the polynomial weight on them.
void M2InterpolateCubicBezier(const M2SplineKey<C3Vector>& startKey, const M2SplineKey<C3Vector>& endKey, float ratio, C3Vector& value) {
    float t = ratio;
    float u = 1.0f - t;
    float w0 = u * u * u;
    float w1 = 3.0f * u * u * t;
    float w2 = 3.0f * u * t * t;
    float w3 = t * t * t;

    value.x = w0 * startKey.value.x + w1 * startKey.outTan.x + w2 * endKey.inTan.x + w3 * endKey.value.x;
    value.y = w0 * startKey.value.y + w1 * startKey.outTan.y + w2 * endKey.inTan.y + w3 * endKey.value.y;
    value.z = w0 * startKey.value.z + w1 * startKey.outTan.z + w2 * endKey.inTan.z + w3 * endKey.value.z;
}

void M2InterpolateCubicBezier(const M2SplineKey<float>& startKey, const M2SplineKey<float>& endKey, float ratio, float& value) {
    float t = ratio;
    float u = 1.0f - t;

    value = u * u * u * startKey.value
        + 3.0f * u * u * t * startKey.outTan
        + 3.0f * u * t * t * endKey.inTan
        + t * t * t * endKey.value;
}

void M2InterpolateCubicHermite(const M2SplineKey<C3Vector>& startKey, const M2SplineKey<C3Vector>& endKey, float ratio, C3Vector& value) {
    float t = ratio;
    float t2 = t * t;
    float t3 = t2 * t;
    float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    float h10 = t3 - 2.0f * t2 + t;
    float h01 = -2.0f * t3 + 3.0f * t2;
    float h11 = t3 - t2;

    value.x = h00 * startKey.value.x + h10 * startKey.outTan.x + h01 * endKey.value.x + h11 * endKey.inTan.x;
    value.y = h00 * startKey.value.y + h10 * startKey.outTan.y + h01 * endKey.value.y + h11 * endKey.inTan.y;
    value.z = h00 * startKey.value.z + h10 * startKey.outTan.z + h01 * endKey.value.z + h11 * endKey.inTan.z;
}

void M2InterpolateCubicHermite(const M2SplineKey<float>& startKey, const M2SplineKey<float>& endKey, float ratio, float& value) {
    float t = ratio;
    float t2 = t * t;
    float t3 = t2 * t;

    value = (2.0f * t3 - 3.0f * t2 + 1.0f) * startKey.value
        + (t3 - 2.0f * t2 + t) * startKey.outTan
        + (-2.0f * t3 + 3.0f * t2) * endKey.value
        + (t3 - t2) * endKey.inTan;
}

template<class T1, class T2>
void M2AnimateSplineTrack(CM2Model* model, M2ModelBone* modelBone, const M2Track<T1>& track, M2ModelTrack<T2>& modelTrack, const T2& defaultValue) {
    // Both of these have to be checked before anything is indexed.
    //
    // An M2Array resolves its data as (its own address + offset), so element 0 of an EMPTY array is
    // a wild pointer rather than null -- indexing sequenceKeys without gating on Count() is the
    // crash CLAUDE.md lists first among the bug classes that have bitten this codebase. And the
    // bone is optional: a track can belong to an emitter whose boneIndex is out of range, leaving
    // nothing to read a sequence from.
    //
    // Neither case arises for bones, colours, texture weights or lights, which is why this stood
    // for so long. Both arise for particle emitters.
    if (!modelBone || !track.sequenceKeys.Count()) {
        modelTrack.currentValue = defaultValue;

        return;
    }

    auto seqIndex = modelBone->sequence.uint4 < track.sequenceKeys.Count() ? modelBone->sequence.uint4 : 0;
    auto& seqKeys = track.sequenceKeys[seqIndex];

    if (seqKeys.keys.Count()) {
        uint32_t nextKey;
        float ratio;

        model->FindKey(&modelBone->sequence, track, modelTrack.currentKey, nextKey, ratio);

        if (track.trackType == 0) {
            modelTrack.currentValue = seqKeys.keys[modelTrack.currentKey].value;
            return;
        }

        auto& startKey = seqKeys.keys[modelTrack.currentKey];
        auto& endKey = seqKeys.keys[nextKey];

        switch (track.trackType) {
            case 1:
                M2InterpolateLinear(startKey.value, endKey.value, ratio, modelTrack.currentValue);
                break;

            case 2:
                M2InterpolateCubicBezier(startKey, endKey, ratio, modelTrack.currentValue);
                break;

            case 3:
                M2InterpolateCubicHermite(startKey, endKey, ratio, modelTrack.currentValue);
                break;
        }
    } else {
        modelTrack.currentValue = defaultValue;

        if (track.trackType == 0) {
            return;
        }
    }

    // Blend with the secondary sequence.
    //
    // The note that used to stand here said a spline track is not blended because the reference's
    // spline function had not been read. It has now (FUN_0082b460 and FUN_0082b8a0) and it DOES
    // blend, by the same rule as the non-spline pair: re-run the interpolator against the
    // secondary sequence and lerp the two results by the bone's weight.
    //
    // Two details are the reference's own rather than this template's symmetry. The secondary
    // walk takes the DEFAULT value when the secondary sequence has no keys -- it does not leave
    // the primary value alone -- so a bone fading out of a sequence the track does not cover
    // fades towards the default. And that walk has no trackType == 0 case at all: it tests 2,
    // then 3, then falls through to linear. Unreachable, because trackType == 0 returns above
    // before ever getting here, but it is why the switch below carries a `default` where the
    // primary one lists 1 explicitly.
    if (modelBone->floatA8 == 0.0f || track.loopIndex != 0xFFFF) {
        return;
    }

    auto secondIndex = modelBone->secondarySequence.uint4 < track.sequenceKeys.Count()
        ? modelBone->secondarySequence.uint4
        : 0;

    auto& secondKeys = track.sequenceKeys[secondIndex];

    T2 secondary = defaultValue;

    if (secondKeys.keys.Count()) {
        uint32_t nextKey;
        float ratio;

        // currentKey2 is this walk's own cursor, for the same reason the non-spline template
        // keeps one: sharing currentKey would make each walk fight the other's search.
        model->FindKey(&modelBone->secondarySequence, track, modelTrack.currentKey2, nextKey, ratio);

        auto& startKey = secondKeys.keys[modelTrack.currentKey2];
        auto& endKey = secondKeys.keys[nextKey];

        switch (track.trackType) {
            case 2:
                M2InterpolateCubicBezier(startKey, endKey, ratio, secondary);
                break;

            case 3:
                M2InterpolateCubicHermite(startKey, endKey, ratio, secondary);
                break;

            default:
                M2InterpolateLinear(startKey.value, endKey.value, ratio, secondary);
                break;
        }
    }

    M2BlendValue(modelTrack.currentValue, secondary, modelBone->floatA8);
}

// Mix a track value with the one the secondary sequence produced, by the bone's blend
// weight. A quaternion takes the SHORTEST ARC and everything else interpolates straight,
// which is why these are two overloads and not one template: the reference slerps in
// FUN_00828680 and lerps in FUN_0082b0a0, and using the wrong one shows on a wide blend.
inline void M2BlendValue(C4Quaternion& value, const C4Quaternion& secondary, float weight) {
    value = C4Quaternion::Slerp(weight, value, secondary);
}

inline void M2BlendValue(C3Vector& value, const C3Vector& secondary, float weight) {
    value.x += (secondary.x - value.x) * weight;
    value.y += (secondary.y - value.y) * weight;
    value.z += (secondary.z - value.z) * weight;
}

inline void M2BlendValue(float& value, float secondary, float weight) {
    value += (secondary - value) * weight;
}

// Anything else -- a texture slot index, a visibility byte -- does not interpolate at all,
// so the blend cannot mean anything for it and the primary value stands.
template<class T>
inline void M2BlendValue(T&, const T&, float) {
}

template<class T1, class T2>
void M2AnimateTrack(CM2Model* model, M2ModelBone* modelBone, const M2Track<T1>& track, M2ModelTrack<T2>& modelTrack, const T2& defaultValue) {
    // Both of these have to be checked before anything is indexed.
    //
    // An M2Array resolves its data as (its own address + offset), so element 0 of an EMPTY array is
    // a wild pointer rather than null -- indexing sequenceKeys without gating on Count() is the
    // crash CLAUDE.md lists first among the bug classes that have bitten this codebase. And the
    // bone is optional: a track can belong to an emitter whose boneIndex is out of range, leaving
    // nothing to read a sequence from.
    //
    // Neither case arises for bones, colours, texture weights or lights, which is why this stood
    // for so long. Both arise for particle emitters.
    if (!modelBone || !track.sequenceKeys.Count()) {
        modelTrack.currentValue = defaultValue;

        return;
    }

    auto seqIndex = modelBone->sequence.uint4 < track.sequenceKeys.Count() ? modelBone->sequence.uint4 : 0;
    auto& seqKeys = track.sequenceKeys[seqIndex];

    if (seqKeys.keys.Count()) {
        uint32_t nextKey;
        float ratio;

        model->FindKey(&modelBone->sequence, track, modelTrack.currentKey, nextKey, ratio);

        if (track.trackType == 0) {
            M2SetValue<T1, T2>(seqKeys.keys[modelTrack.currentKey], modelTrack.currentValue);
            return;
        }

        auto& startValue = seqKeys.keys[modelTrack.currentKey];
        auto& endValue = seqKeys.keys[nextKey];

        M2InterpolateLinear(startValue, endValue, ratio, modelTrack.currentValue);
    } else {
        modelTrack.currentValue = defaultValue;

        if (track.trackType == 0) {
            return;
        }
    }

    // Blend with the secondary sequence.
    //
    // The bone's weight is how much of the sequence it is fading OUT of still applies; it counts
    // down to zero as the blend completes, at which point this whole block is a no-op. Guarded on
    // loopIndex -- frozen's name for the global-sequence index, the uint16 at track + 2 the
    // reference tests against -1 -- because a global-sequence track runs off world time and the
    // bone's sequences do not drive it.
    if (modelBone->floatA8 == 0.0f || track.loopIndex != 0xFFFF) {
        return;
    }

    auto secondIndex = modelBone->secondarySequence.uint4 < track.sequenceKeys.Count()
        ? modelBone->secondarySequence.uint4
        : 0;

    auto& secondKeys = track.sequenceKeys[secondIndex];

    T2 secondary = defaultValue;

    if (secondKeys.keys.Count()) {
        uint32_t nextKey;
        float ratio;

        // currentKey2 is this walk's own cursor. Sharing currentKey with the primary walk would
        // make each fight the other's search every frame.
        model->FindKey(&modelBone->secondarySequence, track, modelTrack.currentKey2, nextKey, ratio);

        if (track.trackType == 0) {
            M2SetValue<T1, T2>(secondKeys.keys[modelTrack.currentKey2], secondary);
        } else {
            M2InterpolateLinear(secondKeys.keys[modelTrack.currentKey2], secondKeys.keys[nextKey],
                                ratio, secondary);
        }
    }

    M2BlendValue(modelTrack.currentValue, secondary, modelBone->floatA8);
}

#endif
