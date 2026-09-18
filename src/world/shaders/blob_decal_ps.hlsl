// Blob shadow decal, paired with terrain_vs (see TerrainShadersD3d9.hpp).
//
// The decal pass re-draws the receiver's own triangles with the SAME vertex shader and constants
// as the base pass, so the interpolated depth is bit-identical and a depth-EQUAL test selects
// exactly the visible surface. That is how the reference avoids z-fighting on coplanar decals
// (FUN_007e4480 sets GxRs_DepthFunc = 1 = D3DCMP_EQUAL); it does not use depth bias.
//
// Blending is MODULATE, as in the reference: the framebuffer is multiplied by what this returns,
// so the shader must emit WHITE where there is no shadow and darken toward the shadow colour
// underneath the blob. Multiplying keeps the receiver's own shading -- a shadow over bright grass
// and the same shadow over dark stone both scale down instead of lerping toward flat black, which
// is what an alpha-blended black quad does.
//
// Because the vertex streams must stay identical to the base pass, the blob's texture coordinate
// cannot come from a vertex stream: it is derived here from the vertex XY. terrain_vs emits
// oT1.xy = position.xy * 0.2, and those positions are chunk-local, so the centre arrives in the
// same space.

sampler2D blobTexture : register(s0);

// xy = shadow centre (chunk-local), z = 0.5 / radius, w = opacity
float4 decal : register(c0);

float4 main(float2 t0 : TEXCOORD0, float2 t1 : TEXCOORD1, float4 d0 : COLOR0) : COLOR {
    float2 local = t1 * 5.0f;
    float2 uv = (local - decal.xy) * decal.z + 0.5f;

    float4 texel = tex2D(blobTexture, uv);

    // Textures\ShadowBlob.blp is a palettised RGB blob -- white at the rim falling to 0xA0 grey at
    // the centre -- with only a 1-bit alpha plane. The reference modulates the receiver by that
    // RGB (GxBlend_Mod: dest *= src.rgb), so the darkening and its soft falloff both come from
    // the texture itself. Using the alpha instead multiplied the whole footprint uniformly, which
    // is the solid black disc this used to draw. The clamped sampler returns the white rim outside
    // the footprint, so untouched ground multiplies by 1. decal.w scales the effect: 1 is the
    // texture's own strength, 0 is no shadow.
    float3 shade = lerp(float3(1.0f, 1.0f, 1.0f), texel.rgb, decal.w);

    return float4(shade, 1.0f);
}
