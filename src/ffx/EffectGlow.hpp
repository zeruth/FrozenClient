#ifndef FFX_EFFECT_GLOW_HPP
#define FFX_EFFECT_GLOW_HPP

#include "ffx/FFX.hpp"
#include <tempest/Vector.hpp>
#include <cstdint>

class CGxShader;

// A four-tap box blur from one target into another (FFXBox4).
class PassBox4 : public FFX::Pass {
    public:
        // Member variables
        uint32_t m_shaderIndex = 0;         // +0x30: 1 for rectangle targets
        CGxShader* m_shaders[2] = {};       // +0x34

        // Virtual member functions
        ~PassBox4() override;
        bool IsValid() override;
        void Render() override;

        // Member functions
        PassBox4(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip);
};

// A separable four-tap Gaussian: across into the spare quarter target, then down into its own
// (FFXGauss4).
class PassGauss4 : public FFX::Pass {
    public:
        // Member variables
        uint32_t m_shaderIndex = 0;         // +0x30
        CGxShader* m_shaders[2] = {};       // +0x34

        // Virtual member functions
        ~PassGauss4() override;
        bool IsValid() override;
        void Render() override;

        // Member functions
        PassGauss4(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip);
};

// The scene and its blurred copy combined onto the screen, weighted by a colour the effect's
// parameters set (FFXGlow).
class PassGlow : public FFX::Pass {
    public:
        // Member variables
        CImVector m_color = { 0, 0, 0, 0 };  // +0x30
        uint32_t m_shaderIndex = 0;         // +0x34
        CGxShader* m_shaders[2] = {};       // +0x38

        // Virtual member functions
        ~PassGlow() override;
        bool IsValid() override;
        void Render() override;

        // Member functions
        PassGlow(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip);
};

// The glow seen through water: the same combine, its texture coordinates bent by a scrolling,
// rotated wave texture (FFXGlowWave).
class PassGlowWave : public FFX::Pass {
    public:
        // Member variables
        CImVector m_color = { 0, 0, 0, 0 };  // +0x30
        uint32_t m_shaderIndex = 0;         // +0x34: +1 rectangle, +2 without a UV88 wave
        CGxShader* m_shaders[4] = {};       // +0x38

        // Virtual member functions
        ~PassGlowWave() override;
        bool IsValid() override;
        void Render() override;

        // Member functions
        PassGlowWave(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip);
};

// The wave texture's target, shared by every GlowWave pass (DAT_00d45c70).
extern FFX::Target s_waveTarget;

class EffectGlow : public FFX::Effect {
    public:
        // Member variables
        TSGrowableArray<FFX::Pass*> m_underwaterPasses;  // +0x1c
        uint32_t m_underwater = 0;                       // +0x2c: which list runs

        // Virtual member functions
        ~EffectGlow() override;
        bool IsEnabled() override;
        void Render() override;
        void SetParams(uint32_t count, const uint32_t* params) override;

        // Member functions
        EffectGlow();
};

#endif
