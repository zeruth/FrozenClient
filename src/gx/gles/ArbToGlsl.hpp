#ifndef GX_GLES_ARB_TO_GLSL_HPP
#define GX_GLES_ARB_TO_GLSL_HPP

#include <cstddef>
#include <cstdint>
#include <string>

// What a translated program needs from the device
struct ArbProgramInfo {
    bool fragment = false;
    bool fogLinear = false;         // OPTION ARB_fog_linear
    uint32_t samplerMask = 0;       // texture[n] sampled as 2D
    uint32_t cubeSamplerMask = 0;   // texture[n] sampled as CUBE
    uint32_t shadowSamplerMask = 0; // texture[n] sampled as SHADOW2D
    uint32_t attribMask = 0;        // vertex.attrib[n]
    std::string error;
};

// Translates an ARB_vertex_program or ARB_fragment_program text (the shipped arbvp1/arbfp1
// shaders) into a GLSL ES 3.00 shader. Vertex constants are read from the VsConstants uniform
// block (vc[256]) and pixel constants from PsConstants (pc[256]); samplers are named s<n>,
// sc<n>, and sh<n> for the texture unit they read. The fragment shader also declares
// u_alphaRef, u_fogParams (start, end, enable), and u_fogColor so the device can emulate the
// alpha test and linear fog that the fixed function pipeline applied after the program.
bool ArbToGlsl(const char* source, size_t length, std::string& glsl, ArbProgramInfo& info);

#endif
