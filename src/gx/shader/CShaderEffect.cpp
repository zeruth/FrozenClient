#include "gx/shader/CShaderEffect.hpp"
#include "gx/Device.hpp"
#include "gx/Gx.hpp"
#include "gx/RenderState.hpp"
#include "gx/Shader.hpp"
#include "gx/Transform.hpp"
#include "model/CM2Light.hpp"
#include "model/CM2Lighting.hpp"
#include <tempest/Math.hpp>
#include <algorithm>
#include <cstring>

CShaderEffect* CShaderEffect::s_curEffect;
int32_t CShaderEffect::s_enableShaders;
C4Vector CShaderEffect::s_fogColorAlphaRef;
float CShaderEffect::s_fogMul;
C4Vector CShaderEffect::s_fogParams;
int32_t CShaderEffect::s_lightEnabled;
uint32_t CShaderEffect::s_localLightCount;
CShaderEffect::LocalLights CShaderEffect::s_localLights;
C3Vector CShaderEffect::s_sunAmbient;
C3Vector CShaderEffect::s_sunDiffuse;
C3Vector CShaderEffect::s_sunDir;
int32_t CShaderEffect::s_useAlphaRef;
int32_t CShaderEffect::s_usePcfFiltering;

// Packs up to four local lights into the eleven vertex constants that SetLocalLighting uploads at
// register 17. This was an empty body on the LIVE path: SetLocalLighting calls it whenever a model
// has local lights and then uploads s_localLights regardless, so every such model was lit by a
// block of zeros -- no torch, brazier or spell light reached a model at all.
//
// The destination layout is fixed by the upload being 11 registers of four floats, and the
// reference's own stores pin every slot (44 floats = 0xb0 bytes):
//
//   [ 0..15]  four lights, rgb + 1.0        -> c17..c20   colour, w is the slot-active flag
//   [16..31]  four lights, xyz + 1.0        -> c21..c24   position in camera space
//   [32..35]  constant attenuation per light -> c25
//   [36..39]  linear attenuation per light   -> c26
//   [40..43]  quadratic attenuation per light -> c27
//
// The colour is m_dirColor, the diffuse one. That follows from the reference reading light + 0x3c
// together with its CM2Light layout, which the constructor at 0x00834a40 pins: it zeroes 0x0c
// through 0x54 and then writes 0.7 to +0x58 and 0.03 to +0x5c, and those are the linear and
// quadratic attenuation defaults, so the three attenuations are +0x54/+0x58/+0x5c and the six
// vectors before them run m_pos, m_posCameraSpace, m_dir, m_ambColor, m_dirColor, m_specColor.
// +0x3c is the fifth of those.
//
// Two ways to reach camera space, and both end there. With a4 the reference subtracts it from the
// light's world position and transforms by the device's current VIEW matrix -- it reads
// `device + 0x1af8` for the stack level and `+0x1b00` for the matrix, and IStateSyncXforms sends
// that same pair as D3DTS_VIEW, which is what identifies it. Without a4 it takes the position
// CM2Lighting::CameraSpace already transformed. frozen's only caller passes null, so the second
// path is the live one; the first is ported anyway rather than left to rot.
//
// A light whose type is not 1 has its colour slot zeroed and nothing else written -- the reference
// jumps straight to the loop increment, leaving that slot's position and attenuation stale.
// Reproduced exactly: with w at 0 the shader ignores the slot, so the stale values cannot show.
//
// **Built, not seen running**, and this one changes what a lit model looks like.
// ref: FUN_00872900
void CShaderEffect::ComputeLocalLights(LocalLights* localLights, uint32_t localLightsCount, CM2Light** lights, const C3Vector* a4) {
    float* dst = localLights->float0;
    uint32_t i = 0;

    if (localLightsCount) {
        C44Matrix view;

        if (a4) {
            // The reference builds an identity in a local and then OVERWRITES it with the device's
            // view matrix -- 0x00407f80, which the call site made look like a multiply, turns out
            // to be C44Matrix's compiler-generated copy assignment: sixteen floats from the
            // argument into `this` and nothing else. So the identity is dead and the matrix used
            // is simply the view matrix, which is what this takes. Corrected 2026-09-23; the
            // earlier note here called it a multiply, which would have been the same answer by
            // luck rather than by reading it.
            GxXformView(view);
        }

        for (; i < localLightsCount && i < 4; i++) {
            CM2Light* light = lights[i];

            if (light->m_type != 1) {
                dst[i * 4 + 0] = 0.0f;
                dst[i * 4 + 1] = 0.0f;
                dst[i * 4 + 2] = 0.0f;
                dst[i * 4 + 3] = 0.0f;

                continue;
            }

            dst[i * 4 + 0] = light->m_dirColor.x;
            dst[i * 4 + 1] = light->m_dirColor.y;
            dst[i * 4 + 2] = light->m_dirColor.z;
            dst[i * 4 + 3] = 1.0f;

            C3Vector pos;

            if (a4) {
                C3Vector rel = {
                    light->m_pos.x - a4->x,
                    light->m_pos.y - a4->y,
                    light->m_pos.z - a4->z
                };

                pos = rel * view;
            } else {
                pos = light->m_posCameraSpace;
            }

            dst[16 + i * 4 + 0] = pos.x;
            dst[16 + i * 4 + 1] = pos.y;
            dst[16 + i * 4 + 2] = pos.z;
            dst[16 + i * 4 + 3] = 1.0f;

            // These are always the constructor's 0, 0.7 and 0.03, and that is correct rather than
            // unfinished. The M2 format carries attenuationStartTrack and attenuationEndTrack, and
            // the reference parses them and then never uses them: the only writes to a CM2Light's
            // +0x54, +0x58 and +0x5c anywhere in the binary are the three in its constructor at
            // 0x00834a7c, taking 0 and the 0.7 at 0x009e2ec0 and the 0.03 at 0x009f23c8. Nothing
            // animates them. So a local light's falloff is fixed, and wiring those tracks up would
            // be a divergence, not a fix.
            dst[32 + i] = light->m_constantAttenuation;
            dst[36 + i] = light->m_linearAttenuation;
            dst[40 + i] = light->m_quadraticAttenuation;
        }
    }

    // Every slot the loop did not fill is switched off, colour and flag together.
    for (; i < 4; i++) {
        dst[i * 4 + 0] = 0.0f;
        dst[i * 4 + 1] = 0.0f;
        dst[i * 4 + 2] = 0.0f;
        dst[i * 4 + 3] = 0.0f;
    }
}

void CShaderEffect::InitShaderSystem(int32_t enableShaders, int32_t usePcf) {
    CShaderEffect::s_enableShaders = enableShaders;
    CShaderEffect::s_usePcfFiltering = enableShaders && usePcf ? 1 : 0;
    CShaderEffect::s_fogMul = 1.0f;

    CShaderEffect::s_useAlphaRef = GxCaps().int130;
}

// ref: FUN_00873ba0
void CShaderEffect::SetAlphaRef(float alphaRef) {
    CShaderEffect::s_fogColorAlphaRef.w = alphaRef;

    if (CShaderEffect::s_useAlphaRef) {
        GxRsSet(GxRs_AlphaRef, static_cast<int32_t>(alphaRef * 255.0f));
    } else {
        GxShaderConstantsSet(GxSh_Pixel, 2, reinterpret_cast<float*>(&CShaderEffect::s_fogColorAlphaRef), 1);
    }
}

namespace {

// The fixed-function form of a material colour: each component clamped to 0..1 and rounded to a
// byte, packed the way CImVector stores them. The reference writes this straight into the render
// state, which is why the clamp lives here rather than in the caller.
CImVector PackMaterialColor(const C4Vector& color) {
    float r = color.x < 0.0f ? 0.0f : (color.x >= 1.0f ? 1.0f : color.x);
    float g = color.y < 0.0f ? 0.0f : (color.y >= 1.0f ? 1.0f : color.y);
    float b = color.z < 0.0f ? 0.0f : (color.z >= 1.0f ? 1.0f : color.z);
    float a = color.w < 0.0f ? 0.0f : (color.w >= 1.0f ? 1.0f : color.w);

    CImVector out;
    out.r = static_cast<uint8_t>(CMath::fuint_n(r * 255.0f));
    out.g = static_cast<uint8_t>(CMath::fuint_n(g * 255.0f));
    out.b = static_cast<uint8_t>(CMath::fuint_n(b * 255.0f));
    out.a = static_cast<uint8_t>(CMath::fuint_n(a * 255.0f));

    return out;
}

} // namespace

// ref: FUN_00873900
void CShaderEffect::SetDiffuse(const C4Vector& diffuse) {
    if (CShaderEffect::s_enableShaders) {
        GxShaderConstantsSet(GxSh_Vertex, 28, reinterpret_cast<const float*>(&diffuse), 1);
        return;
    }

    GxRsSet(GxRs_MatDiffuse, PackMaterialColor(diffuse).value);
}

// ref: FUN_00873a50
void CShaderEffect::SetEmissive(const C4Vector& emissive) {
    if (CShaderEffect::s_enableShaders) {
        GxShaderConstantsSet(GxSh_Vertex, 29, reinterpret_cast<const float*>(&emissive), 1);
        return;
    }

    GxRsSet(GxRs_MatEmissive, PackMaterialColor(emissive).value);
}

// ref: FUN_00873390
void CShaderEffect::SetFogEnabled(int32_t fogEnabled) {
    if (fogEnabled && GxMasterEnable(GxMasterEnable_Fog)) {
        if (CShaderEffect::s_enableShaders && !GxCaps().int138) {
            GxShaderConstantsSet(GxSh_Vertex, 30, reinterpret_cast<float*>(&CShaderEffect::s_fogParams), 1);
        } else {
            GxRsSet(GxRs_Fog, 1);
        }
    } else {
        if (CShaderEffect::s_enableShaders && !GxCaps().int138) {
            float fogParams[] = { 0.0f, 1.0f, 1.0f, 0.0f };
            GxShaderConstantsSet(GxSh_Vertex, 30, fogParams, 1);
        } else {
            GxRsSet(GxRs_Fog, 0);
        }
    }
}

// ref: FUN_00873210
void CShaderEffect::SetFogParams(float fogStart, float fogEnd, float fogRate, const CImVector& fogColor) {
    if (CShaderEffect::s_enableShaders) {
        CShaderEffect::s_fogColorAlphaRef.x = fogColor.r / 255.0f;
        CShaderEffect::s_fogColorAlphaRef.y = fogColor.g / 255.0f;
        CShaderEffect::s_fogColorAlphaRef.z = fogColor.b / 255.0f;

        float v4 = 1.0f / (fogEnd - fogStart);
        CShaderEffect::s_fogParams.x = -(CShaderEffect::s_fogMul * v4);
        CShaderEffect::s_fogParams.y = fogEnd * v4;
        CShaderEffect::s_fogParams.z = fogRate;
        CShaderEffect::s_fogParams.w = 0.0f;

        if (!GxCaps().int134) {
            GxShaderConstantsSet(GxSh_Pixel, 2, reinterpret_cast<float*>(&CShaderEffect::s_fogColorAlphaRef), 1);
            return;
        }
    } else {
        GxRsSet(GxRs_FogStart, fogStart);
        GxRsSet(GxRs_FogEnd, fogEnd);
    }

    GxRsSet(GxRs_FogColor, fogColor.value);
}

// ref: FUN_00873ca0
void CShaderEffect::SetLocalLighting(CM2Lighting* lighting, int32_t lightEnabled, const C3Vector* a3) {
    CShaderEffect::s_lightEnabled = lightEnabled;

    if (!CShaderEffect::s_enableShaders) {
        GxRsSet(GxRs_Lighting, lightEnabled);
    }

    CShaderEffect::s_localLightCount = lighting ? lighting->m_lightCount : 0;

    if (!lightEnabled) {
        return;
    }

    if (CShaderEffect::s_enableShaders) {
        CShaderEffect::s_sunDir = lighting->m_sunDir;

        if (CShaderEffect::s_sunDir.x != 0.0f || CShaderEffect::s_sunDir.y != 0.0f || CShaderEffect::s_sunDir.z != 0.0f) {
            CShaderEffect::s_sunDir.Normalize();
        }

        CShaderEffect::s_sunAmbient = lighting->m_sunAmbient;

        CShaderEffect::s_sunDiffuse = {
            std::min(lighting->m_sunDiffuse.x, 1.0f),
            std::min(lighting->m_sunDiffuse.y, 1.0f),
            std::min(lighting->m_sunDiffuse.z, 1.0f)
        };

        GxShaderConstantsSet(GxSh_Vertex, 10, reinterpret_cast<float*>(&CShaderEffect::s_sunDiffuse), 1);
        GxShaderConstantsSet(GxSh_Vertex, 11, reinterpret_cast<float*>(&CShaderEffect::s_sunAmbient), 1);
        GxShaderConstantsSet(GxSh_Vertex, 12, reinterpret_cast<float*>(&CShaderEffect::s_sunDir), 1);

        if (CShaderEffect::s_localLightCount) {
            CShaderEffect::ComputeLocalLights(
                &CShaderEffect::s_localLights,
                CShaderEffect::s_localLightCount,
                lighting->m_lights,
                a3
            );

            GxShaderConstantsSet(GxSh_Vertex, 17, reinterpret_cast<float*>(&CShaderEffect::s_localLights), 11);
        }

        // TODO
        // CShadowCache::SetShadowMapGenericInterior(lighting->m_flags & 0x8);
    } else {
        lighting->SetupGxLights(a3);
    }
}

// Upload world * view, transposed, to the bone-0 constant slot.
//
// ref: FUN_00872b00
void CShaderEffect::SetWorldViewConstants() {
    if (!CShaderEffect::s_enableShaders) {
        return;
    }

    C44Matrix view;
    C44Matrix world;

    GxXformView(view);
    GxXformWorld(world);

    C44Matrix worldView = (world * view).Transpose();

    GxShaderConstantsSet(GxSh_Vertex, 31, reinterpret_cast<const float*>(&worldView), 4);
}

// Matched on four independent facts, not a guess: render states 0x4d and 0x4e are 77 and 78
// (GxRs_VertexShader, GxRs_PixelShader); the shader arrays sit at effect + 0x2c and + 0x194,
// and 0x194 - 0x2c is 90 * 4, which is m_vertexShaders[90] exactly; and the alpha-ref test
// `~(pixelPermute >> 3) & 1` is the condition below written the other way round.
//
// ref: FUN_00873060
void CShaderEffect::SetShaders(uint32_t vertexPermute, uint32_t pixelPermute) {
    int32_t useAlphaRef = 1;

    if (CShaderEffect::s_enableShaders) {
        GxRsSet(GxRs_VertexShader, CShaderEffect::s_curEffect->m_vertexShaders[vertexPermute]);
        GxRsSet(GxRs_PixelShader, CShaderEffect::s_curEffect->m_pixelShaders[pixelPermute]);

        useAlphaRef = (pixelPermute & 0x8) == 0;
    }

    if (CShaderEffect::s_useAlphaRef != useAlphaRef) {
        CShaderEffect::s_useAlphaRef = useAlphaRef;

        if (useAlphaRef) {
            GxRsSet(GxRs_AlphaRef, static_cast<uint8_t>(CShaderEffect::s_fogColorAlphaRef.w * 255.0f));
        } else {
            GxShaderConstantsSet(GxSh_Pixel, 2, reinterpret_cast<float*>(&CShaderEffect::s_fogColorAlphaRef), 1);
            GxRsSet(GxRs_AlphaRef, 0);
        }
    }
}

// ref: FUN_00873620
void CShaderEffect::SetTexMtx(const C44Matrix& matrix, uint32_t tcIndex) {
    if (CShaderEffect::s_enableShaders) {
        float constants[] = {
            matrix.a0, matrix.b0, matrix.c0, matrix.d0,
            matrix.a1, matrix.b1, matrix.c1, matrix.d1
        };

        GxShaderConstantsSet(GxSh_Vertex, 2 * tcIndex + 6, constants, 2);

        return;
    }

    // TODO non-shader path
}

// ref: FUN_00873480
void CShaderEffect::SetTexMtx_Identity(uint32_t tcIndex) {
    if (CShaderEffect::s_enableShaders) {
        float constants[] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f
        };

        GxShaderConstantsSet(GxSh_Vertex, 2 * tcIndex + 6, constants, 2);

        return;
    }

    // TODO non-shader path
}

// ref: FUN_00873550
void CShaderEffect::SetTexMtx_SphereMap(uint32_t tcIndex) {
    if (CShaderEffect::s_enableShaders) {
        float constants[] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f
        };

        GxShaderConstantsSet(GxSh_Vertex, 2 * tcIndex + 6, constants, 2);

        return;
    }

    // TODO non-shader path
}

void CShaderEffect::UpdateProjMatrix() {
    if (!CShaderEffect::s_enableShaders) {
        return;
    }

    C44Matrix proj;
    GxXformProjNativeTranspose(proj);

    GxShaderConstantsSet(GxSh_Vertex, 2, reinterpret_cast<float*>(&proj), 4);
}

void CShaderEffect::InitEffect(const char* vsName, const char* psName) {
    memset(this->m_vertexShaders, 0, sizeof(this->m_vertexShaders));
    memset(this->m_pixelShaders, 0, sizeof(this->m_pixelShaders));

    // TODO
    // this->dword18 = 0;

    if (CShaderEffect::s_enableShaders) {
        if (vsName && psName) {
            g_theGxDevicePtr->ShaderCreate(this->m_vertexShaders, GxSh_Vertex, "Shaders\\Vertex", vsName, 90);
            g_theGxDevicePtr->ShaderCreate(this->m_pixelShaders, GxSh_Pixel, "Shaders\\Pixel", psName, 16);
        }
    }
}

// ref: FUN_00872f90
void CShaderEffect::SetCurrent() {
    CShaderEffect::s_curEffect = this;

    // TODO
    // - non-shader code path
}
