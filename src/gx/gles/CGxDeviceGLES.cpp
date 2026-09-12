#include "gx/gles/CGxDeviceGLES.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/Texture.hpp"
#include "gx/buffer/CGxBuf.hpp"
#include "gx/buffer/CGxPool.hpp"
#include "gx/shader/CGxShader.hpp"
#include "gx/texture/CGxTex.hpp"
#include "math/Utils.hpp"
#include "util/android/OsAndroid.hpp"
#include <GLES2/gl2ext.h>
#include <android/log.h>
#include <android/native_window.h>
#include <storm/Memory.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define LOG_TAG "CGxDeviceGLES"

// The ES extension header only spells out the DXT1 token; DXT3 and DXT5 share the desktop values
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
    #define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
    #define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
    #define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
    #define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
    #define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

// Texture ops bind on this unit so the units the shaders sample stay untouched
#define SCRATCH_TEXTURE_UNIT 15

GLenum CGxDeviceGLES::s_primitiveConversion[] = {
    GL_POINTS,          // GxPrim_Points
    GL_LINES,           // GxPrim_Lines
    GL_LINE_STRIP,      // GxPrim_LineStrip
    GL_TRIANGLES,       // GxPrim_Triangles
    GL_TRIANGLE_STRIP,  // GxPrim_TriangleStrip
    GL_TRIANGLE_FAN,    // GxPrim_TriangleFan
};

GLenum CGxDeviceGLES::s_srcBlend[] = {
    GL_ONE,                     // GxBlend_Opaque
    GL_ONE,                     // GxBlend_AlphaKey
    GL_SRC_ALPHA,               // GxBlend_Alpha
    GL_SRC_ALPHA,               // GxBlend_Add
    GL_DST_COLOR,               // GxBlend_Mod
    GL_DST_COLOR,               // GxBlend_Mod2x
    GL_DST_COLOR,               // GxBlend_ModAdd
    GL_ONE_MINUS_SRC_ALPHA,     // GxBlend_InvSrcAlphaAdd
    GL_ONE_MINUS_SRC_ALPHA,     // GxBlend_InvSrcAlphaOpaque
    GL_SRC_ALPHA,               // GxBlend_SrcAlphaOpaque
    GL_ONE,                     // GxBlend_NoAlphaAdd
    GL_CONSTANT_COLOR,          // GxBlend_ConstantAlpha
};

GLenum CGxDeviceGLES::s_dstBlend[] = {
    GL_ZERO,                    // GxBlend_Opaque
    GL_ZERO,                    // GxBlend_AlphaKey
    GL_ONE_MINUS_SRC_ALPHA,     // GxBlend_Alpha
    GL_ONE,                     // GxBlend_Add
    GL_ZERO,                    // GxBlend_Mod
    GL_SRC_COLOR,               // GxBlend_Mod2x
    GL_ONE,                     // GxBlend_ModAdd
    GL_ONE,                     // GxBlend_InvSrcAlphaAdd
    GL_ZERO,                    // GxBlend_InvSrcAlphaOpaque
    GL_ZERO,                    // GxBlend_SrcAlphaOpaque
    GL_ONE,                     // GxBlend_NoAlphaAdd
    GL_ONE_MINUS_CONSTANT_COLOR,// GxBlend_ConstantAlpha
};

GLenum CGxDeviceGLES::s_cmpFunc[] = {
    GL_LEQUAL,
    GL_EQUAL,
    GL_GEQUAL,
    GL_LESS,
};

namespace {

struct AttribFormat {
    GLint size;
    GLenum type;
    GLboolean normalized;
    uint32_t bytes;
};

// Indexed by CGxVertexAttrib::type, matching the D3D declaration types
const AttribFormat s_attribFormats[] = {
    { 4, GL_UNSIGNED_BYTE, GL_TRUE,  4 },   // color
    { 4, GL_UNSIGNED_BYTE, GL_FALSE, 4 },   // ubyte4
    { 4, GL_UNSIGNED_BYTE, GL_TRUE,  4 },   // ubyte4n
    { 2, GL_FLOAT,         GL_FALSE, 8 },   // float2
    { 3, GL_FLOAT,         GL_FALSE, 12 },  // float3
    { 2, GL_SHORT,         GL_TRUE,  4 },   // short2n
    { 1, GL_FLOAT,         GL_FALSE, 4 },   // float1
};

const GLenum s_minFilters[] = {
    GL_NEAREST,                 // GxTex_Nearest
    GL_LINEAR,                  // GxTex_Linear
    GL_NEAREST_MIPMAP_NEAREST,  // GxTex_NearestMipNearest
    GL_LINEAR_MIPMAP_NEAREST,   // GxTex_LinearMipNearest
    GL_LINEAR_MIPMAP_LINEAR,    // GxTex_LinearMipLinear
    GL_LINEAR_MIPMAP_LINEAR,    // GxTex_Anisotropic
};

const GLenum s_magFilters[] = {
    GL_NEAREST,
    GL_LINEAR,
    GL_NEAREST,
    GL_LINEAR,
    GL_LINEAR,
    GL_LINEAR,
};

bool IsDxt(EGxTexFormat format) {
    return format == GxTex_Dxt1 || format == GxTex_Dxt3 || format == GxTex_Dxt5;
}

void Unpack565(uint16_t c, uint8_t* rgb) {
    rgb[0] = static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31);
    rgb[1] = static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63);
    rgb[2] = static_cast<uint8_t>((c & 31) * 255 / 31);
}

// Decodes a rectangle of DXT blocks into tightly packed RGBA8 texels. src points at the first
// block of the rectangle; srcStride is the byte distance between block rows.
void DecodeDxt(EGxTexFormat format, const uint8_t* src, uint32_t srcStride, uint32_t width, uint32_t height, uint8_t* dst) {
    uint32_t blockBytes = format == GxTex_Dxt1 ? 8 : 16;
    uint32_t blocksWide = (width + 3) / 4;
    uint32_t blocksHigh = (height + 3) / 4;

    for (uint32_t by = 0; by < blocksHigh; by++) {
        for (uint32_t bx = 0; bx < blocksWide; bx++) {
            const uint8_t* block = src + by * srcStride + bx * blockBytes;
            const uint8_t* colorBlock = format == GxTex_Dxt1 ? block : block + 8;

            uint16_t c0 = colorBlock[0] | (colorBlock[1] << 8);
            uint16_t c1 = colorBlock[2] | (colorBlock[3] << 8);

            uint8_t colors[4][4];
            Unpack565(c0, colors[0]);
            Unpack565(c1, colors[1]);
            colors[0][3] = 255;
            colors[1][3] = 255;

            if (format != GxTex_Dxt1 || c0 > c1) {
                for (int32_t i = 0; i < 3; i++) {
                    colors[2][i] = static_cast<uint8_t>((2 * colors[0][i] + colors[1][i]) / 3);
                    colors[3][i] = static_cast<uint8_t>((colors[0][i] + 2 * colors[1][i]) / 3);
                }

                colors[2][3] = 255;
                colors[3][3] = 255;
            } else {
                for (int32_t i = 0; i < 3; i++) {
                    colors[2][i] = static_cast<uint8_t>((colors[0][i] + colors[1][i]) / 2);
                    colors[3][i] = 0;
                }

                colors[2][3] = 255;
                colors[3][3] = 0;
            }

            uint32_t indices = colorBlock[4] | (colorBlock[5] << 8) | (colorBlock[6] << 16) | (static_cast<uint32_t>(colorBlock[7]) << 24);

            uint8_t alphas[8] = { 0 };
            uint64_t alphaIndices = 0;

            if (format == GxTex_Dxt5) {
                alphas[0] = block[0];
                alphas[1] = block[1];

                if (alphas[0] > alphas[1]) {
                    for (int32_t i = 1; i < 7; i++) {
                        alphas[i + 1] = static_cast<uint8_t>(((7 - i) * alphas[0] + i * alphas[1]) / 7);
                    }
                } else {
                    for (int32_t i = 1; i < 5; i++) {
                        alphas[i + 1] = static_cast<uint8_t>(((5 - i) * alphas[0] + i * alphas[1]) / 5);
                    }

                    alphas[6] = 0;
                    alphas[7] = 255;
                }

                for (int32_t i = 0; i < 6; i++) {
                    alphaIndices |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
                }
            }

            for (uint32_t py = 0; py < 4; py++) {
                uint32_t y = by * 4 + py;

                if (y >= height) {
                    break;
                }

                for (uint32_t px = 0; px < 4; px++) {
                    uint32_t x = bx * 4 + px;

                    if (x >= width) {
                        break;
                    }

                    uint32_t texel = py * 4 + px;
                    uint32_t index = (indices >> (texel * 2)) & 3;
                    uint8_t* out = dst + (y * width + x) * 4;

                    out[0] = colors[index][0];
                    out[1] = colors[index][1];
                    out[2] = colors[index][2];

                    if (format == GxTex_Dxt3) {
                        uint32_t nibble = (block[texel / 2] >> ((texel & 1) * 4)) & 15;
                        out[3] = static_cast<uint8_t>(nibble * 17);
                    } else if (format == GxTex_Dxt5) {
                        out[3] = alphas[(alphaIndices >> (texel * 3)) & 7];
                    } else {
                        out[3] = colors[index][3];
                    }
                }
            }
        }
    }
}

void Convert1555(const uint8_t* src, uint32_t srcStride, uint32_t width, uint32_t height, uint8_t* dst) {
    for (uint32_t y = 0; y < height; y++) {
        auto row = reinterpret_cast<const uint16_t*>(src + y * srcStride);

        for (uint32_t x = 0; x < width; x++) {
            uint16_t c = row[x];
            uint8_t* out = dst + (y * width + x) * 4;

            out[0] = static_cast<uint8_t>(((c >> 10) & 31) * 255 / 31);
            out[1] = static_cast<uint8_t>(((c >> 5) & 31) * 255 / 31);
            out[2] = static_cast<uint8_t>((c & 31) * 255 / 31);
            out[3] = (c & 0x8000) ? 255 : 0;
        }
    }
}

GLuint CompileShader(GLenum type, const std::string& source, const char* name) {
    GLuint shader = glCreateShader(type);
    const char* text = source.c_str();
    GLint length = static_cast<GLint>(source.size());

    glShaderSource(shader, 1, &text, &length);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

    if (!status) {
        char log[2048] = { 0 };
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Shader %s failed to compile: %s", name, log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

} // namespace

CGxDeviceGLES::CGxDeviceGLES() : CGxDevice() {
    // The GLL API id: the client's projection matrices are already in GL clip conventions, and
    // only the legacy OpenGl id asks for the z row to be flipped
    this->m_api = GxApi_GLL;
    this->m_caps.m_colorFormat = GxCF_rgba;

    this->DeviceCreatePools();
    this->DeviceCreateStreamBufs();
}

int32_t CGxDeviceGLES::DeviceCreate(int32_t (*windowProc)(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam), const CGxFormat& format) {
    if (!this->ICreateContext()) {
        return 0;
    }

    if (!CGxDevice::DeviceCreate(windowProc, format)) {
        this->IDestroyContext();
        return 0;
    }

    this->IUpdateWindowSize();
    this->ISetCaps(format);

    this->m_context = 1;

    this->ICursorCreate(format);

    return 1;
}

int32_t CGxDeviceGLES::DeviceSetFormat(const CGxFormat& format) {
    CGxDevice::DeviceSetFormat(format);

    // The activity owns the window, so only the resolution is honored: the client renders at
    // the requested size and the result is presented letterboxed on the surface
    this->m_format.window = 1;
    this->m_format.maximize = 0;

    this->IUpdateWindowSize();

    return 1;
}

void* CGxDeviceGLES::DeviceWindow() {
    return OsAndroidGetWindow();
}

void CGxDeviceGLES::DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2) {
    if (wm != GxWM_Size || this->m_eglDisplay == EGL_NO_DISPLAY) {
        return;
    }

    // A fold or unfold can hand the activity a new window; the surface must follow it
    if (OsAndroidGetWindow() && OsAndroidGetWindowGeneration() != this->m_windowGeneration) {
        if (!this->ICreateSurface()) {
            return;
        }
    }

    this->IUpdateWindowSize();
    this->intF6C = 1;
}

void CGxDeviceGLES::CapsWindowSize(CRect& rect) {
    rect = this->DeviceCurWindow();
}

void CGxDeviceGLES::CapsWindowSizeInScreenCoords(CRect& dst) {
    dst = this->DeviceCurWindow();
}

void CGxDeviceGLES::ScenePresent() {
    if (this->m_eglDisplay == EGL_NO_DISPLAY || this->m_eglSurface == EGL_NO_SURFACE) {
        return;
    }

    CGxDevice::ScenePresent();

    if (this->m_offscreenFramebuffer) {
        // Scale the frame onto the surface, keeping its aspect ratio, with black bars around it
        glBindFramebuffer(GL_READ_FRAMEBUFFER, this->m_offscreenFramebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, this->m_surfaceSize.x, this->m_surfaceSize.y);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBlitFramebuffer(
            0, 0, this->m_renderSize.x, this->m_renderSize.y,
            this->m_presentX, this->m_presentY, this->m_presentX + this->m_presentWidth, this->m_presentY + this->m_presentHeight,
            GL_COLOR_BUFFER_BIT,
            GL_LINEAR
        );
    }

    eglSwapBuffers(this->m_eglDisplay, this->m_eglSurface);

    if (this->m_offscreenFramebuffer) {
        glBindFramebuffer(GL_FRAMEBUFFER, this->m_offscreenFramebuffer);
        this->IRsSendToHw(GxRs_ScissorTest);
        this->intF6C = 1;
    }
}

void CGxDeviceGLES::SceneClear(uint32_t mask, CImVector color) {
    if (this->m_eglContext == EGL_NO_CONTEXT) {
        return;
    }

    GLbitfield glMask = 0;

    if (mask & 0x1) {
        glClearColor(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
        glMask |= GL_COLOR_BUFFER_BIT;
    }

    if (mask & 0x2) {
        // Clearing depth needs the depth mask on
        glDepthMask(GL_TRUE);
        glClearDepthf(1.0f);
        glMask |= GL_DEPTH_BUFFER_BIT;
    }

    if (mask & 0x4) {
        glClearStencil(0);
        glMask |= GL_STENCIL_BUFFER_BIT;
    }

    if (glMask) {
        glDisable(GL_SCISSOR_TEST);
        glClear(glMask);
        this->IRsSendToHw(GxRs_ScissorTest);
        this->IRsSendToHw(GxRs_DepthWrite);
    }
}

// The application projection maps depth to [-1, 1] with w = z, which is what GL clips against,
// so unlike the D3D device only the w term normalization and the shrink are needed
void CGxDeviceGLES::XformSetProjection(const C44Matrix& matrix) {
    CGxDevice::XformSetProjection(matrix);

    C44Matrix proj = matrix;
    float* m = &proj.a0;

    if (NotEqual(proj.c3, 1.0f, WHOA_EPSILON_1) && NotEqual(proj.c3, 0.0f, WHOA_EPSILON_1)) {
        float scale = 1.0f / proj.c3;

        for (int32_t i = 0; i < 16; i++) {
            m[i] *= scale;
        }
    }

    if (!this->MasterEnable(GxMasterEnable_NormalProjection) && proj.d3 != 1.0f) {
        // Scale the x, y, z outputs, matching the D3D device's shrink matrix
        for (int32_t row = 0; row < 4; row++) {
            m[row * 4 + 0] *= 0.2f;
            m[row * 4 + 1] *= 0.2f;
            m[row * 4 + 2] *= 0.2f;
        }
    }

    this->m_xforms[GxXform_Projection].m_dirty = 1;
    this->m_projNative = proj;
}

int32_t CGxDeviceGLES::StereoEnabled() {
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Buffers

void CGxDeviceGLES::PoolSizeSet(CGxPool* pool, uint32_t size) {
    pool->m_size = size;

    if (pool->m_mem) {
        pool->m_mem = SMemReAlloc(pool->m_mem, size, __FILE__, __LINE__, 0x0);
    } else {
        pool->m_mem = SMemAlloc(size, __FILE__, __LINE__, 0x0);
    }
}

CGxDeviceGLES::GlesPool* CGxDeviceGLES::IPoolGet(CGxPool* pool) {
    if (!pool->m_apiSpecific) {
        pool->m_apiSpecific = new GlesPool();
    }

    return static_cast<GlesPool*>(pool->m_apiSpecific);
}

char* CGxDeviceGLES::IBufLock(CGxBuf* buf) {
    auto pool = buf->m_pool;
    auto glesPool = this->IPoolGet(pool);

    if (pool->m_usage == GxPoolUsage_Stream) {
        // Ring allocation through the stream pool, like the D3D device
        uint32_t next = buf->m_itemSize + pool->unk1C - 1 - (buf->m_itemSize + pool->unk1C - 1) % buf->m_itemSize;

        if (buf->m_size + next <= static_cast<uint32_t>(pool->m_size)) {
            buf->m_index = next;
            pool->unk1C = buf->m_size + next;
        } else {
            pool->Discard();
            buf->m_index = 0;
            pool->unk1C = buf->m_size;
            glesPool->orphan = 1;
        }
    }

    uint32_t needed = buf->m_index + buf->m_size;

    if (!pool->m_mem || static_cast<uint32_t>(pool->m_size) < needed) {
        this->PoolSizeSet(pool, std::max(static_cast<uint32_t>(pool->m_size), needed));
    }

    return static_cast<char*>(pool->m_mem) + buf->m_index;
}

void CGxDeviceGLES::IBufUnlock(CGxBuf* buf) {
    auto glesPool = this->IPoolGet(buf->m_pool);

    glesPool->dirtyMin = std::min(glesPool->dirtyMin, buf->m_index);
    glesPool->dirtyMax = std::max(glesPool->dirtyMax, buf->m_index + buf->m_size);
}

char* CGxDeviceGLES::BufLock(CGxBuf* buf) {
    CGxDevice::BufLock(buf);

    return this->IBufLock(buf);
}

int32_t CGxDeviceGLES::BufUnlock(CGxBuf* buf, uint32_t size) {
    CGxDevice::BufUnlock(buf, size);
    this->IBufUnlock(buf);

    return 1;
}

void CGxDeviceGLES::BufData(CGxBuf* buf, const void* data, size_t size, uintptr_t offset) {
    CGxDevice::BufData(buf, data, size, offset);

    auto bufData = this->IBufLock(buf);
    memcpy(&bufData[offset], data, size);
    this->IBufUnlock(buf);
}

void CGxDeviceGLES::IBindArrayBuffer(GLuint buffer) {
    if (this->m_boundArrayBuffer != buffer) {
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        this->m_boundArrayBuffer = buffer;
    }
}

void CGxDeviceGLES::IBindElementBuffer(GLuint buffer) {
    if (this->m_boundElementBuffer != buffer) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
        this->m_boundElementBuffer = buffer;
    }
}

// Uploads whatever the client wrote into the pool since the last draw
void CGxDeviceGLES::IPoolFlush(CGxPool* pool) {
    auto glesPool = this->IPoolGet(pool);
    auto target = pool->m_target == GxPoolTarget_Index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    auto usage = pool->m_usage == GxPoolUsage_Static ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW;

    if (!glesPool->buffer) {
        glGenBuffers(1, &glesPool->buffer);
    }

    if (target == GL_ELEMENT_ARRAY_BUFFER) {
        this->IBindElementBuffer(glesPool->buffer);
    } else {
        this->IBindArrayBuffer(glesPool->buffer);
    }

    uint32_t size = static_cast<uint32_t>(pool->m_size);

    if (glesPool->bufferSize != size) {
        glBufferData(target, size, pool->m_mem, usage);
        glesPool->bufferSize = size;
        glesPool->orphan = 0;
        glesPool->dirtyMin = 0xFFFFFFFF;
        glesPool->dirtyMax = 0;
        return;
    }

    if (glesPool->orphan) {
        glBufferData(target, size, nullptr, usage);
        glesPool->orphan = 0;
    }

    if (glesPool->dirtyMin < glesPool->dirtyMax && pool->m_mem) {
        uint32_t end = std::min(glesPool->dirtyMax, size);

        if (glesPool->dirtyMin < end) {
            glBufferSubData(target, glesPool->dirtyMin, end - glesPool->dirtyMin, static_cast<char*>(pool->m_mem) + glesPool->dirtyMin);
        }
    }

    glesPool->dirtyMin = 0xFFFFFFFF;
    glesPool->dirtyMax = 0;
}

// ---------------------------------------------------------------------------------------------
// Shaders

void CGxDeviceGLES::IShaderCreate(CGxShader* shader) {
    shader->loaded = 1;
    shader->valid = 0;

    if (!shader->code.Count()) {
        return;
    }

    auto glesShader = new GlesShader();
    std::string glsl;

    if (!ArbToGlsl(reinterpret_cast<const char*>(shader->code.m_data), shader->code.Count(), glsl, glesShader->info)) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Shader %s could not be translated: %s", shader->m_key.GetString(), glesShader->info.error.c_str());
        delete glesShader;
        this->m_shaderFailures++;
        return;
    }

    auto type = shader->target == GxSh_Pixel ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER;
    glesShader->shader = CompileShader(type, glsl, shader->m_key.GetString());

    if (!glesShader->shader) {
        delete glesShader;
        this->m_shaderFailures++;
        return;
    }

    shader->apiSpecific = glesShader;
    shader->valid = 1;
}

CGxDeviceGLES::GlesProgram* CGxDeviceGLES::IProgramGet(CGxShader* vs, CGxShader* ps) {
    auto glesVs = static_cast<GlesShader*>(vs->apiSpecific);
    auto glesPs = static_cast<GlesShader*>(ps->apiSpecific);

    uint64_t key = (static_cast<uint64_t>(glesVs->shader) << 32) | glesPs->shader;

    auto found = this->m_programs.find(key);

    if (found != this->m_programs.end()) {
        return &found->second;
    }

    auto& program = this->m_programs[key];

    GLuint id = glCreateProgram();
    glAttachShader(id, glesVs->shader);
    glAttachShader(id, glesPs->shader);
    glLinkProgram(id);
    glDetachShader(id, glesVs->shader);
    glDetachShader(id, glesPs->shader);

    GLint status = 0;
    glGetProgramiv(id, GL_LINK_STATUS, &status);

    if (!status) {
        char log[2048] = { 0 };
        glGetProgramInfoLog(id, sizeof(log), nullptr, log);
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Program %s + %s failed to link: %s", vs->m_key.GetString(), ps->m_key.GetString(), log);
        glDeleteProgram(id);
        program.program = 0;
        return &program;
    }

    program.program = id;
    program.fogLinear = glesPs->info.fogLinear;

    glUseProgram(id);
    this->m_curProgram = nullptr;

    // Constants come from the two uniform buffers
    GLuint block = glGetUniformBlockIndex(id, "VsConstants");

    if (block != GL_INVALID_INDEX) {
        glUniformBlockBinding(id, block, 1);
    }

    block = glGetUniformBlockIndex(id, "PsConstants");

    if (block != GL_INVALID_INDEX) {
        glUniformBlockBinding(id, block, 0);
    }

    // Samplers read the texture unit they are named after
    for (int32_t i = 0; i < 16; i++) {
        char name[16];

        if (glesPs->info.samplerMask & (1 << i)) {
            snprintf(name, sizeof(name), "s%d", i);
            glUniform1i(glGetUniformLocation(id, name), i);
        }

        if (glesPs->info.cubeSamplerMask & (1 << i)) {
            snprintf(name, sizeof(name), "sc%d", i);
            glUniform1i(glGetUniformLocation(id, name), i);
        }

        if (glesPs->info.shadowSamplerMask & (1 << i)) {
            snprintf(name, sizeof(name), "sh%d", i);
            glUniform1i(glGetUniformLocation(id, name), i);
        }
    }

    program.alphaRef = glGetUniformLocation(id, "u_alphaRef");
    program.fogParams = glGetUniformLocation(id, "u_fogParams");
    program.fogColor = glGetUniformLocation(id, "u_fogColor");

    return &program;
}

void CGxDeviceGLES::IBindProgram() {
    auto vs = this->m_vertexShader;
    auto ps = this->m_pixelShader;

    if (!vs || !ps || !vs->valid || !ps->valid) {
        if (this->m_curProgram) {
            glUseProgram(0);
            this->m_curProgram = nullptr;
        }

        return;
    }

    auto program = this->IProgramGet(vs, ps);

    if (!program->program) {
        if (this->m_curProgram) {
            glUseProgram(0);
            this->m_curProgram = nullptr;
        }

        return;
    }

    if (program != this->m_curProgram) {
        glUseProgram(program->program);
        this->m_curProgram = program;
    }

    if (program->alphaRef >= 0) {
        glUniform1f(program->alphaRef, this->m_alphaRef);
    }

    if (program->fogLinear) {
        if (program->fogParams >= 0) {
            glUniform4fv(program->fogParams, 1, this->m_fogParams);
        }

        if (program->fogColor >= 0) {
            glUniform4fv(program->fogColor, 1, this->m_fogColor);
        }
    }
}

void CGxDeviceGLES::IShaderConstantsFlush() {
    // Index 1 holds the vertex constants and index 0 the pixel constants, matching the binding
    // points the programs use
    for (int32_t target = 0; target < 2; target++) {
        auto shadow = &CGxDevice::s_shadowConstants[target];

        if (shadow->unk2 <= shadow->unk1) {
            glBindBuffer(GL_UNIFORM_BUFFER, this->m_constantBuffers[target]);
            glBufferSubData(
                GL_UNIFORM_BUFFER,
                shadow->unk2 * sizeof(C4Vector),
                (shadow->unk1 - shadow->unk2 + 1) * sizeof(C4Vector),
                &shadow->constants[shadow->unk2]
            );
        }

        shadow->unk2 = 255;
        shadow->unk1 = 0;
    }
}

// ---------------------------------------------------------------------------------------------
// Draw

void CGxDeviceGLES::IStateSyncVertexPtrs() {
    // Group the attributes by the buffer they read from to work out each stream's stride
    CGxBuf* streamBufs[GxVAs_Last] = { nullptr };
    uint32_t streamStrides[GxVAs_Last] = { 0 };
    uint32_t streamCount = 0;

    for (uint32_t i = 0; i < GxVAs_Last; i++) {
        if (!((1 << i) & this->m_primVertexMask)) {
            continue;
        }

        auto buf = this->m_primVertexFormatBuf[i];
        uint32_t stream = 0;

        while (stream < streamCount && streamBufs[stream] != buf) {
            stream++;
        }

        if (stream == streamCount) {
            streamBufs[stream] = buf;
            streamCount++;
        }

        streamStrides[stream] += s_attribFormats[this->m_primVertexFormatAttrib[i].type].bytes;
    }

    for (uint32_t stream = 0; stream < streamCount; stream++) {
        if (this->m_primVertexFormat < GxVertexBufferFormats_Last && streamBufs[stream] == this->m_primVertexBuf) {
            streamStrides[stream] = this->m_primVertexSize;
        }

        this->IPoolFlush(streamBufs[stream]->m_pool);
    }

    uint32_t enabled = 0;

    for (uint32_t i = 0; i < GxVAs_Last; i++) {
        if (!((1 << i) & this->m_primVertexMask)) {
            continue;
        }

        auto& attrib = this->m_primVertexFormatAttrib[i];
        auto buf = this->m_primVertexFormatBuf[i];
        uint32_t stream = 0;

        while (streamBufs[stream] != buf) {
            stream++;
        }

        auto& format = s_attribFormats[attrib.type];

        this->IBindArrayBuffer(this->IPoolGet(buf->m_pool)->buffer);

        glVertexAttribPointer(
            attrib.attrib,
            format.size,
            format.type,
            format.normalized,
            streamStrides[stream],
            reinterpret_cast<const void*>(static_cast<uintptr_t>(buf->m_index + attrib.offset))
        );

        enabled |= 1 << attrib.attrib;
    }

    for (uint32_t slot = 0; slot < 16; slot++) {
        uint32_t bit = 1 << slot;

        if ((enabled & bit) && !(this->m_enabledAttribs & bit)) {
            glEnableVertexAttribArray(slot);
        } else if (!(enabled & bit) && (this->m_enabledAttribs & bit)) {
            glDisableVertexAttribArray(slot);
        }
    }

    this->m_enabledAttribs = enabled;
}

void CGxDeviceGLES::IStateSyncIndexPtr() {
    if (!this->m_primIndexBuf) {
        return;
    }

    this->IPoolFlush(this->m_primIndexBuf->m_pool);
    this->m_primIndexDirty = 0;
}

void CGxDeviceGLES::IXformSetViewport() {
    const auto& viewport = this->m_viewport;
    auto windowRect = this->DeviceCurWindow();

    // The viewport box is bottom up, which is GL's window convention
    GLint x = static_cast<GLint>(viewport.x.l * windowRect.maxX + 0.5f);
    GLint y = static_cast<GLint>(viewport.y.l * windowRect.maxY + 0.5f);
    GLint width = static_cast<GLint>(viewport.x.h * windowRect.maxX + 0.5f) - x;
    GLint height = static_cast<GLint>(viewport.y.h * windowRect.maxY + 0.5f) - y;

    glViewport(x, y, std::max(width, 0), std::max(height, 0));
    glDepthRangef(viewport.z.l, viewport.z.h);

    this->intF6C = 0;
}

void CGxDeviceGLES::IStateSync() {
    if (!this->m_statesInitialized) {
        this->m_statesInitialized = 1;
        this->IRsForceUpdate();
    }

    this->IShaderConstantsFlush();
    this->IRsSync(0);
    this->IBindProgram();
    this->IStateSyncVertexPtrs();
    this->IStateSyncIndexPtr();

    if (this->intF6C) {
        this->IXformSetViewport();
    }
}

void CGxDeviceGLES::Draw(CGxBatch* batch, int32_t indexed) {
    if (!this->m_context) {
        return;
    }

    this->IStateSync();

    auto prim = CGxDeviceGLES::s_primitiveConversion[batch->m_primType];


    if (!this->m_curProgram) {
        return;
    }

    if (indexed) {
        if (!this->m_primIndexBuf) {
            return;
        }

        uintptr_t offset = this->m_primIndexBuf->m_index + batch->m_start * sizeof(uint16_t);

        glDrawRangeElements(
            prim,
            batch->m_minIndex,
            batch->m_maxIndex,
            batch->m_count,
            GL_UNSIGNED_SHORT,
            reinterpret_cast<const void*>(offset)
        );
    } else {
        glDrawArrays(prim, 0, batch->m_count);
    }
}

// ---------------------------------------------------------------------------------------------
// Render states

void CGxDeviceGLES::IRsSendToHw(EGxRenderState which) {
    auto state = &this->m_appRenderStates[which];

    switch (which) {
    case GxRs_BlendingMode: {
        auto blendMode = static_cast<int32_t>(state->m_value);

        if (blendMode < GxBlend_Alpha || blendMode >= GxBlends_Last) {
            glDisable(GL_BLEND);
        } else {
            glEnable(GL_BLEND);
            glBlendFunc(CGxDeviceGLES::s_srcBlend[blendMode], CGxDeviceGLES::s_dstBlend[blendMode]);
        }

        break;
    }

    case GxRs_AlphaRef: {
        auto alphaRef = static_cast<int32_t>(state->m_value);
        this->m_alphaRef = alphaRef <= 0 ? -1.0f : alphaRef / 255.0f;
        break;
    }

    case GxRs_DepthTest:
    case GxRs_DepthFunc: {
        auto depthTest = static_cast<uint32_t>(this->m_appRenderStates[GxRs_DepthTest].m_value);
        auto depthFunc = static_cast<uint32_t>(this->m_appRenderStates[GxRs_DepthFunc].m_value);

        // The test stays enabled so depth writes keep working; an always passing function
        // stands in for a disabled test, like the D3D device
        GLenum glFunc = GL_ALWAYS;

        if (this->MasterEnable(GxMasterEnable_DepthTest) && depthTest && depthFunc < 4) {
            glFunc = CGxDeviceGLES::s_cmpFunc[depthFunc];
        }

        glDepthFunc(glFunc);

        this->m_appRenderStates[GxRs_DepthTest].m_dirty = 0;
        this->m_appRenderStates[GxRs_DepthFunc].m_dirty = 0;

        break;
    }

    case GxRs_DepthWrite: {
        auto depthWrite = static_cast<uint32_t>(state->m_value);

        if (!this->MasterEnable(GxMasterEnable_DepthWrite)) {
            depthWrite = 0;
        }

        glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
        break;
    }

    case GxRs_Culling: {
        auto cullMode = static_cast<int32_t>(state->m_value);

        if (!this->MasterEnable(GxMasterEnable_Culling)) {
            cullMode = 0;
        }

        if (cullMode > 2) {
            cullMode = 2;
        }

        if (cullMode == 0) {
            glDisable(GL_CULL_FACE);
        } else {
            // D3DCULL_CW removes faces that are clockwise on screen, so their counterparts are
            // the front faces here
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glFrontFace(cullMode == 1 ? GL_CCW : GL_CW);
        }

        break;
    }

    case GxRs_ScissorTest: {
        if (static_cast<uint32_t>(state->m_value)) {
            glEnable(GL_SCISSOR_TEST);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }

        break;
    }

    case GxRs_Fog: {
        auto fog = static_cast<uint32_t>(state->m_value);
        this->m_fogParams[2] = fog && this->MasterEnable(GxMasterEnable_Fog) ? 1.0f : 0.0f;
        break;
    }

    case GxRs_FogStart: {
        this->m_fogParams[0] = static_cast<float>(state->m_value);
        break;
    }

    case GxRs_FogEnd: {
        this->m_fogParams[1] = static_cast<float>(state->m_value);
        break;
    }

    case GxRs_FogColor: {
        auto color = static_cast<CImVector>(state->m_value);
        this->m_fogColor[0] = color.r / 255.0f;
        this->m_fogColor[1] = color.g / 255.0f;
        this->m_fogColor[2] = color.b / 255.0f;
        this->m_fogColor[3] = color.a / 255.0f;
        break;
    }

    case GxRs_Texture0:
    case GxRs_Texture1:
    case GxRs_Texture2:
    case GxRs_Texture3:
    case GxRs_Texture4:
    case GxRs_Texture5:
    case GxRs_Texture6:
    case GxRs_Texture7:
    case GxRs_Texture8:
    case GxRs_Texture9:
    case GxRs_Texture10:
    case GxRs_Texture11:
    case GxRs_Texture12:
    case GxRs_Texture13:
    case GxRs_Texture14:
    case GxRs_Texture15: {
        uint32_t tmu = which - GxRs_Texture0;
        auto texture = static_cast<CGxTex*>(static_cast<void*>(state->m_value));
        this->ISetTexture(tmu, texture);
        break;
    }

    case GxRs_VertexShader: {
        auto shader = static_cast<CGxShader*>(static_cast<void*>(state->m_value));

        if (shader && !shader->loaded) {
            this->IShaderCreate(shader);
        }

        this->m_vertexShader = shader;
        break;
    }

    case GxRs_PixelShader: {
        auto shader = static_cast<CGxShader*>(static_cast<void*>(state->m_value));

        if (shader && !shader->loaded) {
            this->IShaderCreate(shader);
        }

        this->m_pixelShader = shader;
        break;
    }

    default:
        break;
    }
}

// ---------------------------------------------------------------------------------------------
// Textures

void CGxDeviceGLES::ISetTexture(uint32_t tmu, CGxTex* texId) {
    if (tmu > 15) {
        return;
    }

    GlesTexture* glesTex = nullptr;

    if (texId) {
        this->ITexMarkAsUpdated(texId);
        glesTex = static_cast<GlesTexture*>(texId->m_apiSpecificData);
    }

    glActiveTexture(GL_TEXTURE0 + tmu);

    if (glesTex && glesTex->texture) {
        glBindTexture(glesTex->target, glesTex->texture);
    } else {
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }
}

void CGxDeviceGLES::ITexMarkAsUpdated(CGxTex* texId) {
    if (!this->m_context) {
        return;
    }

    if (texId->m_needsFlagUpdate && !texId->m_needsCreation && texId->m_apiSpecificData) {
        this->ITexSetFlags(texId);
    }

    if (!texId->m_needsUpdate) {
        return;
    }

    if (texId->m_needsCreation || !texId->m_apiSpecificData) {
        this->ITexCreate(texId);
    }

    if (!texId->m_needsCreation && texId->m_apiSpecificData) {
        if (texId->m_userFunc) {
            this->ITexUpload(texId);
        }

        CGxDevice::ITexMarkAsUpdated(texId);
    }
}

CGxDeviceGLES::GlesTexture* CGxDeviceGLES::ITexCreate(CGxTex* texId) {
    auto glesTex = static_cast<GlesTexture*>(texId->m_apiSpecificData);

    if (glesTex && glesTex->texture) {
        glDeleteTextures(1, &glesTex->texture);
        glesTex->texture = 0;
    }

    if (!glesTex) {
        glesTex = new GlesTexture();
        texId->m_apiSpecificData = glesTex;
    }

    uint32_t width, height, baseMip, mipCount;
    this->ITexWHDStartEnd(texId, width, height, baseMip, mipCount);

    glesTex->target = texId->m_target == GxTex_CubeMap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    glesTex->baseMip = baseMip;
    glesTex->levels = mipCount - baseMip;
    glesTex->compressed = 0;
    glesTex->decodeDxt = 0;
    glesTex->convert = 0;

    uint32_t levelWidth = std::max(width >> baseMip, 1u);
    uint32_t levelHeight = std::max(height >> baseMip, 1u);

    if (texId->m_flags.m_generateMipMaps) {
        // The whole chain is generated from the base level
        uint32_t edge = std::max(levelWidth, levelHeight);
        glesTex->levels = 1;

        while (edge > 1) {
            edge /= 2;
            glesTex->levels++;
        }
    }

    auto dataFormat = texId->m_dataFormat == GxTex_Unknown ? texId->m_format : texId->m_dataFormat;
    GLint swizzle[4] = { GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA };

    switch (dataFormat) {
    case GxTex_Abgr8888:
        glesTex->internalFormat = GL_RGBA8;
        glesTex->format = GL_RGBA;
        glesTex->type = GL_UNSIGNED_BYTE;
        break;

    case GxTex_Argb8888:
        // Bytes are B, G, R, A in memory
        glesTex->internalFormat = GL_RGBA8;
        glesTex->format = GL_RGBA;
        glesTex->type = GL_UNSIGNED_BYTE;
        swizzle[0] = GL_BLUE;
        swizzle[2] = GL_RED;
        break;

    case GxTex_Argb4444:
        // GL reads the 16 bits as R, G, B, A from the top, so the channels come in as A, R, G, B
        glesTex->internalFormat = GL_RGBA4;
        glesTex->format = GL_RGBA;
        glesTex->type = GL_UNSIGNED_SHORT_4_4_4_4;
        swizzle[0] = GL_GREEN;
        swizzle[1] = GL_BLUE;
        swizzle[2] = GL_ALPHA;
        swizzle[3] = GL_RED;
        break;

    case GxTex_Argb1555:
        glesTex->internalFormat = GL_RGBA8;
        glesTex->format = GL_RGBA;
        glesTex->type = GL_UNSIGNED_BYTE;
        glesTex->convert = 1;
        break;

    case GxTex_Rgb565:
        glesTex->internalFormat = GL_RGB565;
        glesTex->format = GL_RGB;
        glesTex->type = GL_UNSIGNED_SHORT_5_6_5;
        break;

    case GxTex_Dxt1:
    case GxTex_Dxt3:
    case GxTex_Dxt5:
        if (this->m_hasS3tc) {
            glesTex->internalFormat = dataFormat == GxTex_Dxt1
                ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
                : dataFormat == GxTex_Dxt3 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
            glesTex->compressed = 1;
        } else {
            glesTex->internalFormat = GL_RGBA8;
            glesTex->format = GL_RGBA;
            glesTex->type = GL_UNSIGNED_BYTE;
            glesTex->decodeDxt = 1;
        }
        break;

    case GxTex_Uv88:
        glesTex->internalFormat = GL_RG8_SNORM;
        glesTex->format = GL_RG;
        glesTex->type = GL_BYTE;
        break;

    case GxTex_Gr1616F:
        glesTex->internalFormat = GL_RG16F;
        glesTex->format = GL_RG;
        glesTex->type = GL_HALF_FLOAT;
        break;

    case GxTex_R32F:
        glesTex->internalFormat = GL_R32F;
        glesTex->format = GL_RED;
        glesTex->type = GL_FLOAT;
        break;

    case GxTex_D24X8:
        glesTex->internalFormat = GL_DEPTH_COMPONENT24;
        glesTex->format = GL_DEPTH_COMPONENT;
        glesTex->type = GL_UNSIGNED_INT;
        break;

    default:
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Unsupported texture format %d", dataFormat);
        return glesTex;
    }

    glGenTextures(1, &glesTex->texture);

    glActiveTexture(GL_TEXTURE0 + SCRATCH_TEXTURE_UNIT);
    glBindTexture(glesTex->target, glesTex->texture);

    glTexStorage2D(glesTex->target, glesTex->levels, glesTex->internalFormat, levelWidth, levelHeight);
    glesTex->storage = 1;


    glTexParameteri(glesTex->target, GL_TEXTURE_SWIZZLE_R, swizzle[0]);
    glTexParameteri(glesTex->target, GL_TEXTURE_SWIZZLE_G, swizzle[1]);
    glTexParameteri(glesTex->target, GL_TEXTURE_SWIZZLE_B, swizzle[2]);
    glTexParameteri(glesTex->target, GL_TEXTURE_SWIZZLE_A, swizzle[3]);

    if (dataFormat == GxTex_D24X8) {
        // Sampled through sampler2DShadow by the shadow map permutations
        glTexParameteri(glesTex->target, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(glesTex->target, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }

    this->ITexSetFlags(texId);

    texId->m_needsCreation = 0;

    return glesTex;
}

void CGxDeviceGLES::ITexSetFlags(CGxTex* texId) {
    auto glesTex = static_cast<GlesTexture*>(texId->m_apiSpecificData);

    if (!glesTex || !glesTex->texture) {
        return;
    }

    glActiveTexture(GL_TEXTURE0 + SCRATCH_TEXTURE_UNIT);
    glBindTexture(glesTex->target, glesTex->texture);

    uint32_t filter = std::min<uint32_t>(texId->m_flags.m_filter, GxTexFilters_Last - 1);
    GLenum minFilter = s_minFilters[filter];

    if (glesTex->levels <= 1) {
        // A single level texture is incomplete with a mipmap filter
        minFilter = minFilter == GL_NEAREST || minFilter == GL_NEAREST_MIPMAP_NEAREST ? GL_NEAREST : GL_LINEAR;
    }

    glTexParameteri(glesTex->target, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(glesTex->target, GL_TEXTURE_MAG_FILTER, s_magFilters[filter]);
    glTexParameteri(glesTex->target, GL_TEXTURE_WRAP_S, texId->m_flags.m_wrapU ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(glesTex->target, GL_TEXTURE_WRAP_T, texId->m_flags.m_wrapV ? GL_REPEAT : GL_CLAMP_TO_EDGE);

    if (this->m_hasAnisotropic) {
        float anisotropy = filter == GxTex_Anisotropic ? std::max(1.0f, static_cast<float>(texId->m_flags.m_maxAnisotropy)) : 1.0f;
        glTexParameterf(glesTex->target, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(anisotropy, this->m_maxAnisotropy));
    }

    texId->m_needsFlagUpdate = 0;
}

void CGxDeviceGLES::ITexUpload(CGxTex* texId) {
    auto glesTex = static_cast<GlesTexture*>(texId->m_apiSpecificData);

    if (!glesTex || !glesTex->texture || texId->m_flags.m_renderTarget) {
        return;
    }

    uint32_t texelStrideInBytes = 0;
    const void* texels = nullptr;

    texId->m_userFunc(GxTex_Lock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);

    uint32_t width, height, baseMip, mipCount;
    this->ITexWHDStartEnd(texId, width, height, baseMip, mipCount);

    auto dataFormat = texId->m_dataFormat == GxTex_Unknown ? texId->m_format : texId->m_dataFormat;
    bool blocks = glesTex->compressed || glesTex->decodeDxt;

    glActiveTexture(GL_TEXTURE0 + SCRATCH_TEXTURE_UNIT);
    glBindTexture(glesTex->target, glesTex->texture);

    int32_t numFaces = texId->m_target == GxTex_CubeMap ? 6 : 1;
    std::vector<uint8_t> scratch;

    for (int32_t face = 0; face < numFaces; face++) {
        for (uint32_t level = baseMip; level < mipCount; level++) {
            texels = nullptr;

            texId->m_userFunc(
                GxTex_Latch,
                texId->m_width >> level,
                texId->m_height >> level,
                face,
                level,
                texId->m_userArg,
                texelStrideInBytes,
                texels
            );

            if (!texels) {
                continue;
            }

            int32_t levelWidth = std::max<int32_t>(width >> level, 1);
            int32_t levelHeight = std::max<int32_t>(height >> level, 1);

            int32_t left = texId->m_updateRect.minX >> level;
            int32_t top = texId->m_updateRect.minY >> level;
            int32_t right = std::max(texId->m_updateRect.maxX >> level, left + 1);
            int32_t bottom = std::max(texId->m_updateRect.maxY >> level, top + 1);

            right = std::min(right, levelWidth);
            bottom = std::min(bottom, levelHeight);

            if (left < 0 || top < 0 || left >= right || top >= bottom) {
                continue;
            }

            GLenum faceTarget = texId->m_target == GxTex_CubeMap ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : GL_TEXTURE_2D;
            GLint glLevel = level - baseMip;
            auto src = static_cast<const uint8_t*>(texels);

            if (blocks) {
                left &= ~3;
                top &= ~3;
                right = std::min((right + 3) & ~3, levelWidth);
                bottom = std::min((bottom + 3) & ~3, levelHeight);

                uint32_t blockBytes = GxCalcTexelStrideInBytes(dataFormat, 4);
                uint32_t blocksWide = (right - left + 3) / 4;
                uint32_t blocksHigh = (bottom - top + 3) / 4;
                uint32_t rowBytes = blocksWide * blockBytes;

                src += (top / 4) * texelStrideInBytes + (left / 4) * blockBytes;

                if (glesTex->compressed) {
                    const uint8_t* data = src;

                    if (rowBytes != texelStrideInBytes) {
                        // Compressed uploads have no row length; pack the block rows
                        scratch.resize(rowBytes * blocksHigh);

                        for (uint32_t row = 0; row < blocksHigh; row++) {
                            memcpy(&scratch[row * rowBytes], src + row * texelStrideInBytes, rowBytes);
                        }

                        data = scratch.data();
                    }

                    glCompressedTexSubImage2D(faceTarget, glLevel, left, top, right - left, bottom - top, glesTex->internalFormat, rowBytes * blocksHigh, data);
                } else {
                    scratch.resize((right - left) * (bottom - top) * 4);
                    DecodeDxt(dataFormat, src, texelStrideInBytes, right - left, bottom - top, scratch.data());

                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                    glTexSubImage2D(faceTarget, glLevel, left, top, right - left, bottom - top, GL_RGBA, GL_UNSIGNED_BYTE, scratch.data());
                }
            } else {
                uint32_t texelBytes = GxCalcTexelStrideInBytes(dataFormat, 1);
                src += top * texelStrideInBytes + left * texelBytes;

                if (glesTex->convert) {
                    scratch.resize((right - left) * (bottom - top) * 4);
                    Convert1555(src, texelStrideInBytes, right - left, bottom - top, scratch.data());

                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                    glTexSubImage2D(faceTarget, glLevel, left, top, right - left, bottom - top, GL_RGBA, GL_UNSIGNED_BYTE, scratch.data());
                } else {
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, texelBytes ? texelStrideInBytes / texelBytes : 0);
                    glTexSubImage2D(faceTarget, glLevel, left, top, right - left, bottom - top, glesTex->format, glesTex->type, src);
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                }
            }
        }
    }

    texels = nullptr;
    texId->m_userFunc(GxTex_Unlock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);


    if (texId->m_flags.m_generateMipMaps && glesTex->levels > 1) {
        glGenerateMipmap(glesTex->target);
    }
}

void CGxDeviceGLES::TexDestroy(CGxTex* texId) {
    if (texId) {
        auto glesTex = static_cast<GlesTexture*>(texId->m_apiSpecificData);

        if (glesTex) {
            if (glesTex->texture) {
                glDeleteTextures(1, &glesTex->texture);
            }

            delete glesTex;
            texId->m_apiSpecificData = nullptr;
        }
    }

    CGxDevice::TexDestroy(texId);
}

// ---------------------------------------------------------------------------------------------
// Context

void CGxDeviceGLES::ISetCaps(const CGxFormat& format) {
    this->m_caps.m_pixelCenterOnEdge = 1;
    this->m_caps.m_texelCenterOnEdge = 1;
    this->m_caps.m_colorFormat = GxCF_rgba;
    this->m_caps.m_generateMipMaps = 1;
    this->m_caps.int10 = 1;
    this->m_caps.m_numTmus = 8;
    this->m_caps.m_numStreams = 1;
    this->m_caps.m_maxIndex = 0xFFFF;

    auto extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));

    this->m_hasS3tc = extensions && strstr(extensions, "GL_EXT_texture_compression_s3tc") != nullptr;
    this->m_hasAnisotropic = extensions && strstr(extensions, "GL_EXT_texture_filter_anisotropic") != nullptr;

    if (this->m_hasAnisotropic) {
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &this->m_maxAnisotropy);
    }

    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "S3TC textures: %s, anisotropic filtering: %s (max %.0f)", this->m_hasS3tc ? "native" : "decoded", this->m_hasAnisotropic ? "yes" : "no", this->m_maxAnisotropy);

    // DXT data is decoded on the CPU when the GPU cannot take it
    this->m_caps.m_texFmt[GxTex_Abgr8888] = 1;
    this->m_caps.m_texFmt[GxTex_Argb8888] = 1;
    this->m_caps.m_texFmt[GxTex_Argb4444] = 1;
    this->m_caps.m_texFmt[GxTex_Argb1555] = 1;
    this->m_caps.m_texFmt[GxTex_Rgb565] = 1;
    this->m_caps.m_texFmt[GxTex_Dxt1] = 1;
    this->m_caps.m_texFmt[GxTex_Dxt3] = 1;
    this->m_caps.m_texFmt[GxTex_Dxt5] = 1;
    this->m_caps.m_texFmt[GxTex_D24X8] = 1;

    this->m_caps.m_shaderTargets[GxSh_Vertex] = GxShVS_arbvp1;
    this->m_caps.m_shaderTargets[GxSh_Pixel] = GxShPS_arbfp1;

    this->m_caps.m_texFilterTrilinear = 1;
    this->m_caps.m_texFilterAnisotropic = this->m_hasAnisotropic;
    this->m_caps.m_maxTexAnisotropy = static_cast<uint32_t>(this->m_maxAnisotropy);

    GLint maxTextureSize = 2048;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);

    this->m_caps.m_texTarget[GxTex_2d] = 1;
    this->m_caps.m_texTarget[GxTex_CubeMap] = 1;
    this->m_caps.m_texTarget[GxTex_Rectangle] = 0;
    this->m_caps.m_texTarget[GxTex_NonPow2] = 1;

    this->m_caps.m_texMaxSize[GxTex_2d] = maxTextureSize;
    this->m_caps.m_texMaxSize[GxTex_CubeMap] = maxTextureSize;
    this->m_caps.m_texMaxSize[GxTex_Rectangle] = maxTextureSize;
    this->m_caps.m_texMaxSize[GxTex_NonPow2] = maxTextureSize;
}

int32_t CGxDeviceGLES::ICreateContext() {
    auto window = OsAndroidGetWindow();

    if (!window) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "No window to create the context on");
        return 0;
    }

    this->m_eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    if (this->m_eglDisplay == EGL_NO_DISPLAY || !eglInitialize(this->m_eglDisplay, nullptr, nullptr)) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "eglInitialize failed");
        return 0;
    }

    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs = 0;

    if (!eglChooseConfig(this->m_eglDisplay, configAttribs, &config, 1, &numConfigs) || numConfigs < 1) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "No EGL config with a 24 bit depth buffer");
        return 0;
    }

    this->m_eglConfig = config;
    eglGetConfigAttrib(this->m_eglDisplay, config, EGL_NATIVE_VISUAL_ID, &this->m_nativeFormat);

    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    this->m_eglContext = eglCreateContext(this->m_eglDisplay, config, EGL_NO_CONTEXT, contextAttribs);

    if (this->m_eglContext == EGL_NO_CONTEXT) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "eglCreateContext failed");
        return 0;
    }

    if (!this->ICreateSurface()) {
        return 0;
    }

    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "GL_RENDERER: %s", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "GL_VERSION: %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    // Shader constant buffers: binding 0 is pixel, binding 1 is vertex
    glGenBuffers(2, this->m_constantBuffers);

    for (int32_t i = 0; i < 2; i++) {
        glBindBuffer(GL_UNIFORM_BUFFER, this->m_constantBuffers[i]);
        glBufferData(GL_UNIFORM_BUFFER, 256 * sizeof(C4Vector), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, i, this->m_constantBuffers[i]);
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_DITHER);

    return 1;
}

// Creates the window surface on the activity's current window and makes it current, replacing
// any previous surface
int32_t CGxDeviceGLES::ICreateSurface() {
    auto window = OsAndroidGetWindow();

    if (!window) {
        return 0;
    }

    if (this->m_eglSurface != EGL_NO_SURFACE) {
        eglMakeCurrent(this->m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, this->m_eglContext);
        eglDestroySurface(this->m_eglDisplay, this->m_eglSurface);
        this->m_eglSurface = EGL_NO_SURFACE;
    }

    ANativeWindow_setBuffersGeometry(window, 0, 0, this->m_nativeFormat);

    this->m_eglSurface = eglCreateWindowSurface(this->m_eglDisplay, this->m_eglConfig, window, nullptr);

    if (this->m_eglSurface == EGL_NO_SURFACE) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "eglCreateWindowSurface failed");
        return 0;
    }

    if (!eglMakeCurrent(this->m_eglDisplay, this->m_eglSurface, this->m_eglSurface, this->m_eglContext)) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "eglMakeCurrent failed");
        return 0;
    }

    this->m_windowGeneration = OsAndroidGetWindowGeneration();

    return 1;
}

void CGxDeviceGLES::IDestroyContext() {
    if (this->m_eglDisplay == EGL_NO_DISPLAY) {
        return;
    }

    eglMakeCurrent(this->m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    if (this->m_eglContext != EGL_NO_CONTEXT) {
        eglDestroyContext(this->m_eglDisplay, this->m_eglContext);
        this->m_eglContext = EGL_NO_CONTEXT;
    }

    if (this->m_eglSurface != EGL_NO_SURFACE) {
        eglDestroySurface(this->m_eglDisplay, this->m_eglSurface);
        this->m_eglSurface = EGL_NO_SURFACE;
    }

    eglTerminate(this->m_eglDisplay);
    this->m_eglDisplay = EGL_NO_DISPLAY;
}

void CGxDeviceGLES::IUpdateWindowSize() {
    EGLint width = 0;
    EGLint height = 0;

    if (this->m_eglDisplay != EGL_NO_DISPLAY && this->m_eglSurface != EGL_NO_SURFACE) {
        eglQuerySurface(this->m_eglDisplay, this->m_eglSurface, EGL_WIDTH, &width);
        eglQuerySurface(this->m_eglDisplay, this->m_eglSurface, EGL_HEIGHT, &height);
    }

    if (width <= 0 || height <= 0) {
        return;
    }

    this->m_surfaceSize.x = width;
    this->m_surfaceSize.y = height;

    // The requested resolution is rendered as is; zero or anything the surface cannot hold
    // means native
    int32_t renderWidth = this->m_format.size.x;
    int32_t renderHeight = this->m_format.size.y;

    if (renderWidth <= 0 || renderHeight <= 0 || renderWidth > width || renderHeight > height) {
        renderWidth = width;
        renderHeight = height;
    }

    this->m_renderSize.x = renderWidth;
    this->m_renderSize.y = renderHeight;
    this->m_format.size.x = renderWidth;
    this->m_format.size.y = renderHeight;

    // Fit the frame on the surface keeping its aspect ratio
    float scale = std::min(static_cast<float>(width) / renderWidth, static_cast<float>(height) / renderHeight);
    this->m_presentWidth = static_cast<int32_t>(renderWidth * scale + 0.5f);
    this->m_presentHeight = static_cast<int32_t>(renderHeight * scale + 0.5f);
    this->m_presentX = (width - this->m_presentWidth) / 2;
    this->m_presentY = (height - this->m_presentHeight) / 2;

    // Touches come in surface pixels with y from the top
    OsAndroidSetPresentRect(this->m_presentX, height - (this->m_presentY + this->m_presentHeight), this->m_presentWidth, this->m_presentHeight, renderWidth, renderHeight);

    // The default window rect is what the UI lays out against and input positions are
    // normalized against
    CRect rect = { 0.0f, 0.0f, static_cast<float>(renderHeight), static_cast<float>(renderWidth) };
    this->DeviceSetDefWindow(rect);

    if (this->m_eglContext != EGL_NO_CONTEXT) {
        this->IOffscreenSetup();
        glViewport(0, 0, renderWidth, renderHeight);
    }

    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Rendering %dx%d on a %dx%d surface at %d,%d %dx%d", renderWidth, renderHeight, width, height, this->m_presentX, this->m_presentY, this->m_presentWidth, this->m_presentHeight);

    this->intF6C = 1;
}

void CGxDeviceGLES::IOffscreenDestroy() {
    if (this->m_offscreenFramebuffer) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &this->m_offscreenFramebuffer);
        this->m_offscreenFramebuffer = 0;
    }

    if (this->m_offscreenColor) {
        glDeleteTextures(1, &this->m_offscreenColor);
        this->m_offscreenColor = 0;
    }

    if (this->m_offscreenDepth) {
        glDeleteRenderbuffers(1, &this->m_offscreenDepth);
        this->m_offscreenDepth = 0;
    }
}

// Creates the render target for the configured resolution; at native size the surface itself
// is drawn to
void CGxDeviceGLES::IOffscreenSetup() {
    this->IOffscreenDestroy();

    if (this->m_renderSize.x == this->m_surfaceSize.x && this->m_renderSize.y == this->m_surfaceSize.y) {
        return;
    }

    glGenTextures(1, &this->m_offscreenColor);
    glActiveTexture(GL_TEXTURE0 + SCRATCH_TEXTURE_UNIT);
    glBindTexture(GL_TEXTURE_2D, this->m_offscreenColor);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, this->m_renderSize.x, this->m_renderSize.y);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenRenderbuffers(1, &this->m_offscreenDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, this->m_offscreenDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, this->m_renderSize.x, this->m_renderSize.y);

    glGenFramebuffers(1, &this->m_offscreenFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, this->m_offscreenFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, this->m_offscreenColor, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, this->m_offscreenDepth);

    auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Offscreen framebuffer incomplete (0x%x); rendering at native size", status);
        this->IOffscreenDestroy();
        this->m_renderSize = this->m_surfaceSize;
        this->m_format.size = this->m_surfaceSize;
    }
}
