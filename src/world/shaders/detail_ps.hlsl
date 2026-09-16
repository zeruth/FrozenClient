// Detail (ground effect) doodad pixel shader, paired with terrain_vs.
//
// Ground doodads sit exactly ON the terrain surface: BuildDetailDoodads places each base vertex at
// ChunkHeightAt(...), the same value the terrain mesh interpolates there. Drawing them through a
// DIFFERENT vertex program than the terrain pass makes those coincident positions differ by a few
// ULPs, so a LESSEQUAL depth test accepts and rejects per pixel and the grass bases shimmer. Using
// the terrain's own vertex program keeps the transform bit-identical, so coincident vertices
// compare equal and draw cleanly.
//
// terrain_vs emits oT0 = the vertex texcoord, oD0 = the vertex colour (here the ground colour the
// batch baked under each doodad), oT1 = position.xy * 0.2 (unused).

sampler2D doodadTexture : register(s0);

float4 main(float2 t0 : TEXCOORD0, float2 t1 : TEXCOORD1, float4 d0 : COLOR0) : COLOR {
    float4 texel = tex2D(doodadTexture, t0);

    return float4(texel.rgb * d0.rgb, texel.a * d0.a);
}
