// Terrain pixel shader.
//
// Recovered from the compiled bytecode in TerrainShadersD3d9.hpp (the original source was lost) and
// then extended with the per-pixel baked shadow below. The original did:
//
//     w0 = saturate(1 - blend.r - blend.g - blend.b)
//     colour = layer0*w0 + layer1*blend.r + layer2*blend.g + layer3*blend.b
//     colour *= vertexColour.rgb
//
// t0 is the chunk's 0..1 blend coordinate; t1 is position.xy * 0.2, which tiles the layers.

sampler2D layer0 : register(s0);
sampler2D layer1 : register(s1);
sampler2D layer2 : register(s2);
sampler2D layer3 : register(s3);
sampler2D blendMap : register(s4);

float4 main(float2 t0 : TEXCOORD0, float2 t1 : TEXCOORD1, float4 v0 : COLOR0) : COLOR {
    float4 blend = tex2D(blendMap, t0);

    float w0 = saturate(1.0f - blend.r - blend.g - blend.b);
    float3 colour = tex2D(layer0, t1).rgb * w0;
    colour += tex2D(layer1, t1).rgb * blend.r;
    colour += tex2D(layer2, t1).rgb * blend.g;
    colour += tex2D(layer3, t1).rgb * blend.b;

    // Per-pixel baked shadow. The blend map's alpha carries the chunk's MCSH shadow map at its
    // native 64x64 resolution: 1 where the sun reaches, 0 where it is occluded. The vertex colour
    // is lit WITHOUT the shadow, and its alpha carries the ambient ratio -- how much of that lit
    // colour survives once the sun's contribution is removed. So a shadowed pixel scales to the
    // ambient term and a lit one is untouched.
    //
    // Folding the shadow into the vertex colour instead, as this used to, quantised it to the 9x9
    // height grid, which made mountain shadows blocky compared with the reference (which samples
    // MCSH per pixel).
    float shadow = lerp(v0.a, 1.0f, blend.a);

    return float4(colour * v0.rgb * shadow, 1.0f);
}
