#ifndef WORLD_TERRAIN_SHADERS_ARB_HPP
#define WORLD_TERRAIN_SHADERS_ARB_HPP

// ARB assembly builds of the terrain programs, for the OpenGL and GLES backends.
//
// The D3D9 versions beside these (TerrainShadersD3d9.hpp) are compiled bytecode, so they are
// useless anywhere but D3D -- and EnsureShaders only built them when the API was D3D9, which left
// every GL backend with no terrain program at all and dropped the terrain to the untextured
// fallback path. On Android that is the whole ground rendering as flat white while models, WMOs,
// doodads and grass textured correctly.
//
// These are not a second implementation: each one is the same program as its HLSL counterpart in
// src/world/shaders/, written out in the assembly the GLES device's ArbToGlsl translator consumes.
// Change one, change the other -- they must stay in step or Android and Windows will disagree.
//
// Unlike the D3D9 header there is no generation step here. ARB programs are text, so these string
// literals ARE the source; there is no compiled artifact to regenerate and nothing to keep in sync
// with a .cso.
//
// Vertex constants: vc[0..3] are the rows of the combined chunk/instance * viewProj matrix, set by
// GxShaderConstantsSet(GxSh_Vertex, 0, ..., 4). Pixel constants start at pc[0].

// Terrain vertex program. Transcribed instruction for instruction from the vs_2_0 disassembly of
// g_terrainVsD3d9, whose HLSL source was lost:
//
//     def c4, 1, 0, 0.200000003, 0
//     mad r0, v0.xyzx, c4.xxxy, c4.yyyx      ; r0 = float4(position.xyz, 1)
//     dp4 oPos.x, r0, c0  ... dp4 oPos.w, r0, c3
//     mul oT1.xy, v0, c4.z                   ; position.xy * 0.2
//     mov oD0, v1
//     mov oT0.xy, v2
//
// Every pass that draws chunk geometry shares this program on purpose: terrain, blob shadows and
// detail doodads all transform coincident vertices through identical arithmetic, so their depth is
// bit-identical and the depth-EQUAL decal test selects exactly the visible surface. Giving any of
// them its own transform reintroduces the z-fighting this arrangement exists to avoid.
static const char g_terrainVsArb[] =
    "!!ARBvp1.0\n"
    "PARAM m0 = program.local[0];\n"
    "PARAM m1 = program.local[1];\n"
    "PARAM m2 = program.local[2];\n"
    "PARAM m3 = program.local[3];\n"
    "PARAM k = { 1.0, 0.0, 0.200000003, 0.0 };\n"
    "TEMP r0;\n"
    "MAD r0, vertex.position.xyzx, k.xxxy, k.yyyx;\n"
    "DP4 result.position.x, r0, m0;\n"
    "DP4 result.position.y, r0, m1;\n"
    "DP4 result.position.z, r0, m2;\n"
    "DP4 result.position.w, r0, m3;\n"
    "MUL result.texcoord[1].xy, vertex.position, k.z;\n"
    "MOV result.color, vertex.color;\n"
    "MOV result.texcoord[0].xy, vertex.texcoord[0];\n"
    "END\n";

// Terrain pixel program -- see src/world/shaders/terrain_ps.hlsl for the reasoning behind the
// weights and the per-pixel baked shadow.
//
//     w0     = saturate(1 - blend.r - blend.g - blend.b)
//     colour = layer0*w0 + layer1*blend.r + layer2*blend.g + layer3*blend.b
//     shadow = lerp(vertexColour.a, 1, blend.a)
//     result = colour * vertexColour.rgb * shadow
//
// s0..s3 are the four terrain layers sampled at the tiling coordinate (texcoord 1); s4 is the
// chunk's blend map sampled at its own 0..1 coordinate (texcoord 0), whose alpha carries the MCSH
// shadow at its native 64x64 resolution.
static const char g_terrainPsArb[] =
    "!!ARBfp1.0\n"
    "PARAM one = { 1.0, 1.0, 1.0, 1.0 };\n"
    "TEMP blend, w0, colour, texel, shadow;\n"
    "TEX blend, fragment.texcoord[0], texture[4], 2D;\n"
    "SUB w0.x, one.x, blend.r;\n"
    "SUB w0.x, w0.x, blend.g;\n"
    "SUB_SAT w0.x, w0.x, blend.b;\n"
    "TEX texel, fragment.texcoord[1], texture[0], 2D;\n"
    "MUL colour.rgb, texel, w0.x;\n"
    "TEX texel, fragment.texcoord[1], texture[1], 2D;\n"
    "MAD colour.rgb, texel, blend.r, colour;\n"
    "TEX texel, fragment.texcoord[1], texture[2], 2D;\n"
    "MAD colour.rgb, texel, blend.g, colour;\n"
    "TEX texel, fragment.texcoord[1], texture[3], 2D;\n"
    "MAD colour.rgb, texel, blend.b, colour;\n"
    "MUL colour.rgb, colour, fragment.color;\n"
    "SUB shadow.x, one.x, fragment.color.a;\n"
    "MAD shadow.x, shadow.x, blend.a, fragment.color.a;\n"
    "MUL result.color.rgb, colour, shadow.x;\n"
    "MOV result.color.a, one.x;\n"
    "END\n";

// Detail (ground effect) doodad pixel program -- src/world/shaders/detail_ps.hlsl.
//
// Paired with the terrain vertex program so ground doodads transform exactly as the terrain mesh
// does beneath them. texcoord 0 is the doodad's own UV; the vertex colour is the ground colour the
// batch baked under it.
static const char g_detailPsArb[] =
    "!!ARBfp1.0\n"
    "TEMP texel;\n"
    "TEX texel, fragment.texcoord[0], texture[0], 2D;\n"
    "MUL result.color.rgb, texel, fragment.color;\n"
    "MUL result.color.a, texel.a, fragment.color.a;\n"
    "END\n";

// Blob shadow decal pixel program -- src/world/shaders/blob_decal_ps.hlsl.
//
// pc[0] is the decal: xy = shadow centre in chunk-local space, z = 0.5 / radius, w = opacity. The
// blob's coordinate cannot come from a vertex stream, because the decal pass must present the same
// streams as the base pass for its depth to match; it is rebuilt here from texcoord 1, which the
// vertex program filled with position.xy * 0.2.
//
// Blending is MODULATE, so this returns white where there is no shadow: the receiver's own shading
// is scaled down rather than lerped toward flat black.
static const char g_blobDecalPsArb[] =
    "!!ARBfp1.0\n"
    "PARAM decal = program.local[0];\n"
    "PARAM half = { 0.5, 0.5, 0.5, 0.5 };\n"
    "PARAM five = { 5.0, 5.0, 5.0, 5.0 };\n"
    "PARAM one = { 1.0, 1.0, 1.0, 1.0 };\n"
    "TEMP local, uv, texel, shade;\n"
    "MUL local.xy, fragment.texcoord[1], five;\n"
    "SUB uv.xy, local, decal;\n"
    "MAD uv.xy, uv, decal.z, half;\n"
    "TEX texel, uv, texture[0], 2D;\n"
    "SUB shade.rgb, texel, one;\n"
    "MAD result.color.rgb, shade, decal.w, one;\n"
    "MOV result.color.a, one.x;\n"
    "END\n";

#endif
