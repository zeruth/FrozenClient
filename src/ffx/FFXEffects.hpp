#ifndef FFX_FFX_EFFECTS_HPP
#define FFX_FFX_EFFECTS_HPP

#include "ffx/FFX.hpp"
#include <tempest/Random.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CGxShader;

// The scene and its blur combined into the grey, blue-tinted death view (FFXDeath, or one of the
// register-combiner variants on nvts hardware).
class PassDeath : public FFX::Pass {
    public:
        // Member variables
        uint32_t m_shaderIndex = 0;         // +0x30
        CGxShader* m_shaders[2] = {};       // +0x34

        // Virtual member functions
        ~PassDeath() override;
        bool IsValid() override;
        void Render() override;

        // Member functions
        PassDeath(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip);
};

// The death view in one pass on nvrc hardware, which has no blur (FFXDeath_nvrc).
class PassDeathNvrc : public FFX::Pass {
    public:
        // Member variables
        CGxShader* m_shader = nullptr;      // +0x30

        // Virtual member functions
        ~PassDeathNvrc() override;
        bool IsValid() override;
        void Render() override;

        // Member functions
        PassDeathNvrc(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip);
};

// The nether world's swirl: the scene warped through a six-by-six grid whose points drift between
// three tables of random offsets, blurred twice over (FFXNetherBlur, vertex and pixel).
class PassNetherBlur : public FFX::Pass {
    public:
        // Member variables
        uint32_t m_shaderIndex = 0;         // +0x30
        float m_tables[3][36] = {};         // +0x34
        float m_current[36] = {};           // +0x1e4
        float* m_from = nullptr;            // +0x274
        float* m_to = nullptr;              // +0x278
        float* m_next = nullptr;            // +0x27c
        float m_blend = 0.0f;               // +0x280, 0..1 from m_from to m_to
        CRndSeed m_seed = CRndSeed(0);      // +0x284
        CGxShader* m_vertexShader = nullptr; // +0x28c
        CGxShader* m_shaders[2] = {};       // +0x290

        // Virtual member functions
        ~PassNetherBlur() override;
        bool IsValid() override;
        void Render() override;

        // Member functions
        PassNetherBlur(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip);
};

// The scene faded into the swirl over three quarters of a second (FFXNetherCombine).
class PassNetherCombine : public FFX::Pass {
    public:
        // Member variables
        uint32_t m_shaderIndex = 0;         // +0x30
        CGxShader* m_shaders[2] = {};       // +0x34
        float m_fade = 0.0f;                // +0x3c

        // Virtual member functions
        ~PassNetherCombine() override;
        bool IsValid() override;
        void Render() override;

        // Member functions
        PassNetherCombine(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip);
};

class EffectDeath : public FFX::Effect {
    public:
        // Virtual member functions
        ~EffectDeath() override;

        // Member functions
        EffectDeath();
};

class EffectNether : public FFX::Effect {
    public:
        // Virtual member functions
        ~EffectNether() override;
        void Deactivate() override;

        // Member functions
        EffectNether();
};

#endif
