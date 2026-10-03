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

class CGxBuf;
class CGxPool;

// The special effect's field is cleared the next time its source draws: on device restore, and
// when a screen effect puts the special effect up. ref: FUN_007e7fe0
void FFXFieldRestored();

// The special effect's source: one row of its noise texture drawn across the bottom of the fog
// field each frame, tinted by the screen effect's colour, the row advancing frame by frame.
class PassFogSource : public FFX::Pass {
    public:
        // Member variables
        uint32_t m_row = 0;                 // +0x30
        CImVector m_color = { 0, 0, 0, 0 }; // +0x34

        // Virtual member functions
        ~PassFogSource() override;
        bool IsValid() override;
        void Render() override;

        // Member functions
        PassFogSource(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip);
};

// The fog field drawn into itself one row up through four taps below each texel, so what the
// source lays down rises and spreads (FFXPropagateFog).
class PassPropagateFog : public FFX::Pass {
    public:
        // Member variables
        uint32_t m_shaderIndex = 0;         // +0x30
        CGxShader* m_shaders[2] = {};       // +0x34
        float m_offsets[8] = {};            // +0x3c, the four taps in texels
        float m_strength = 0.0f;            // +0x5c

        // Virtual member functions
        ~PassPropagateFog() override;
        bool IsValid() override;
        void Render() override;

        // Member functions
        PassPropagateFog(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, const float* offsets, uint32_t blend, bool flip);
};

// The fog field wrapped around the screen's centre on a 33 by 33 grid and laid over the scene
// (FFXFogCombine).
class PassFogCombine : public FFX::Pass {
    public:
        // Member variables
        uint32_t m_shaderIndex = 0;         // +0x30
        CGxShader* m_shaders[2] = {};       // +0x34
        CGxPool* m_vertexPool = nullptr;    // +0x3c
        CGxBuf* m_vertexBuf = nullptr;      // +0x40
        CGxPool* m_indexPool = nullptr;     // +0x44
        CGxBuf* m_indexBuf = nullptr;       // +0x48
        int32_t m_builtWidth = 0;           // +0x4c
        int32_t m_builtHeight = 0;          // +0x50
        float m_strength = 0.0f;            // +0x54
        float m_fadeIn = 0.0f;              // +0x58, seconds left of the fade

        // Virtual member functions
        ~PassFogCombine() override;
        bool IsValid() override;
        void Render() override;

        // Member functions
        PassFogCombine(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip);
        // The grid's points, wrapped around the centre of the field. Rebuilt when the target's size
        // changes or the device dropped the buffer.
        void BuildVertices(const int32_t* targetSize, FFX::Target* field, FFX::Target* scene);
        void BuildIndices();
};

class EffectSpecial : public FFX::Effect {
    public:
        // Member variables
        FFX::Target m_field;                // +0x1c, 256 by 128, rendered into
        FFX::Target m_noise;                // +0x38, 256 by 256 of value noise

        // Virtual member functions
        ~EffectSpecial() override;
        void SetParams(uint32_t count, const uint32_t* params) override;

        // Member functions
        EffectSpecial();
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
