#include "gx/gles/CGxDeviceGLES.hpp"
#include "gx/buffer/CGxBuf.hpp"
#include "gx/buffer/CGxPool.hpp"
#include "gx/shader/CGxShader.hpp"
#include "util/android/OsAndroid.hpp"
#include <GLES3/gl3.h>
#include <android/log.h>
#include <android/native_window.h>
#include <storm/Memory.hpp>
#include <cstring>

#define LOG_TAG "CGxDeviceGLES"

CGxDeviceGLES::CGxDeviceGLES() : CGxDevice() {
    // TODO a GxApi value of its own once the API tables have room for it
    this->m_api = GxApi_OpenGl;
    this->m_caps.m_colorFormat = GxCF_rgba;

    this->DeviceCreatePools();
    this->DeviceCreateStreamBufs();
}

void CGxDeviceGLES::ITexMarkAsUpdated(CGxTex* texId) {
    // TODO upload
}

void CGxDeviceGLES::IRsSendToHw(EGxRenderState which) {
    // TODO render states
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

    // The activity owns the window; the format cannot change its size or mode
    this->m_format.window = 1;
    this->m_format.maximize = 0;

    this->IUpdateWindowSize();

    return 1;
}

void* CGxDeviceGLES::DeviceWindow() {
    return OsAndroidGetWindow();
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

    eglSwapBuffers(this->m_eglDisplay, this->m_eglSurface);
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
        glClearDepthf(1.0f);
        glMask |= GL_DEPTH_BUFFER_BIT;
    }

    if (mask & 0x4) {
        glClearStencil(0);
        glMask |= GL_STENCIL_BUFFER_BIT;
    }

    if (glMask) {
        glClear(glMask);
    }
}

void CGxDeviceGLES::Draw(CGxBatch* batch, int32_t indexed) {
    // TODO
}

// Pools live in system memory until the GL buffer path is ported
void CGxDeviceGLES::PoolSizeSet(CGxPool* pool, uint32_t size) {
    pool->m_size = size;

    if (pool->m_mem) {
        pool->m_mem = SMemReAlloc(pool->m_mem, size, __FILE__, __LINE__, 0x0);
    } else {
        pool->m_mem = SMemAlloc(size, __FILE__, __LINE__, 0x0);
    }
}

char* CGxDeviceGLES::BufLock(CGxBuf* buf) {
    CGxDevice::BufLock(buf);

    auto pool = buf->m_pool;
    uint32_t needed = buf->m_index + buf->m_size;

    if (!pool->m_mem || static_cast<uint32_t>(pool->m_size) < needed) {
        this->PoolSizeSet(pool, needed);
    }

    return static_cast<char*>(pool->m_mem) + buf->m_index;
}

int32_t CGxDeviceGLES::BufUnlock(CGxBuf* buf, uint32_t size) {
    return CGxDevice::BufUnlock(buf, size);
}

void CGxDeviceGLES::BufData(CGxBuf* buf, const void* data, size_t size, uintptr_t offset) {
    CGxDevice::BufData(buf, data, size, offset);

    auto bufData = this->BufLock(buf);
    memcpy(&bufData[offset], data, size);
    this->BufUnlock(buf, size);
}

void CGxDeviceGLES::IShaderCreate(CGxShader* shader) {
    // TODO GLSL ES shaders; until then every shader is loaded but invalid
    shader->loaded = 1;
    shader->valid = 0;
}

int32_t CGxDeviceGLES::StereoEnabled() {
    return 0;
}

void CGxDeviceGLES::ISetCaps(const CGxFormat& format) {
    this->m_caps.m_pixelCenterOnEdge = 1;
    this->m_caps.m_texelCenterOnEdge = 1;
    this->m_caps.m_colorFormat = GxCF_rgba;
    this->m_caps.m_generateMipMaps = 1;
    this->m_caps.int10 = 1;

    // TODO check GL_EXT_texture_compression_s3tc; most Android GPUs need the DXT textures decoded
    this->m_caps.m_texFmt[GxTex_Dxt1] = 0;
    this->m_caps.m_texFmt[GxTex_Dxt3] = 0;
    this->m_caps.m_texFmt[GxTex_Dxt5] = 0;

    this->m_caps.m_shaderTargets[GxSh_Vertex] = GxShVS_arbvp1;
    this->m_caps.m_shaderTargets[GxSh_Pixel] = GxShPS_arbfp1;

    this->m_caps.m_texFilterAnisotropic = 0;
    this->m_caps.m_maxTexAnisotropy = 1;

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

    EGLint nativeFormat;
    eglGetConfigAttrib(this->m_eglDisplay, config, EGL_NATIVE_VISUAL_ID, &nativeFormat);
    ANativeWindow_setBuffersGeometry(window, 0, 0, nativeFormat);

    this->m_eglSurface = eglCreateWindowSurface(this->m_eglDisplay, config, window, nullptr);

    if (this->m_eglSurface == EGL_NO_SURFACE) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "eglCreateWindowSurface failed");
        return 0;
    }

    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    this->m_eglContext = eglCreateContext(this->m_eglDisplay, config, EGL_NO_CONTEXT, contextAttribs);

    if (this->m_eglContext == EGL_NO_CONTEXT) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "eglCreateContext failed");
        return 0;
    }

    if (!eglMakeCurrent(this->m_eglDisplay, this->m_eglSurface, this->m_eglSurface, this->m_eglContext)) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "eglMakeCurrent failed");
        return 0;
    }

    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "GL_RENDERER: %s", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "GL_VERSION: %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

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

    CRect rect = { 0.0f, 0.0f, static_cast<float>(height), static_cast<float>(width) };
    this->DeviceSetCurWindow(rect);

    this->m_format.size.x = width;
    this->m_format.size.y = height;

    if (this->m_eglContext != EGL_NO_CONTEXT) {
        glViewport(0, 0, width, height);
    }
}
