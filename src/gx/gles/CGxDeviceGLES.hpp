#ifndef GX_GLES_C_GX_DEVICE_GLES_HPP
#define GX_GLES_C_GX_DEVICE_GLES_HPP

#include "gx/CGxDevice.hpp"
#include "gx/gles/ArbToGlsl.hpp"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <cstdint>
#include <unordered_map>

// OpenGL ES 3 device for Android. Buffer pools live in system memory and are uploaded to GL
// buffers before each draw, the shipped ARB shaders are translated to GLSL ES on load and
// linked per vertex/pixel pair, and shader constants go through two uniform buffers. Render
// states follow the D3D device, with the alpha test and fixed function fog emulated in the
// fragment shaders.
class CGxDeviceGLES : public CGxDevice {
    public:
        // Types
        struct GlesPool {
            GLuint buffer = 0;
            uint32_t bufferSize = 0;
            uint32_t dirtyMin = 0xFFFFFFFF;
            uint32_t dirtyMax = 0;
            int32_t orphan = 0;
        };

        struct GlesShader {
            GLuint shader = 0;
            ArbProgramInfo info;
        };

        struct GlesProgram {
            GLuint program = 0;
            GLint alphaRef = -1;
            GLint fogParams = -1;
            GLint fogColor = -1;
            int32_t fogLinear = 0;
        };

        struct GlesTexture {
            GLuint texture = 0;
            GLenum target = GL_TEXTURE_2D;
            GLenum internalFormat = 0;
            GLenum format = 0;
            GLenum type = 0;
            uint32_t baseMip = 0;
            uint32_t levels = 1;
            int32_t compressed = 0;
            int32_t decodeDxt = 0;
            int32_t convert = 0;
            int32_t storage = 0;
        };

        // Static variables
        static GLenum s_primitiveConversion[];
        static GLenum s_srcBlend[];
        static GLenum s_dstBlend[];
        static GLenum s_cmpFunc[];

        // Member variables
        EGLDisplay m_eglDisplay = EGL_NO_DISPLAY;
        EGLSurface m_eglSurface = EGL_NO_SURFACE;
        EGLContext m_eglContext = EGL_NO_CONTEXT;
        GLuint m_constantBuffers[2] = { 0, 0 };
        std::unordered_map<uint64_t, GlesProgram> m_programs;
        GlesProgram* m_curProgram = nullptr;
        CGxShader* m_vertexShader = nullptr;
        CGxShader* m_pixelShader = nullptr;
        uint32_t m_enabledAttribs = 0;
        GLuint m_boundArrayBuffer = 0;
        GLuint m_boundElementBuffer = 0;
        int32_t m_statesInitialized = 0;
        float m_alphaRef = -1.0f;
        float m_fogParams[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
        float m_fogColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        int32_t m_hasS3tc = 0;
        int32_t m_hasAnisotropic = 0;
        float m_maxAnisotropy = 1.0f;
        int32_t m_shaderFailures = 0;

        // Virtual member functions
        virtual void ITexMarkAsUpdated(CGxTex*);
        virtual void IRsSendToHw(EGxRenderState);
        virtual int32_t DeviceCreate(int32_t (*windowProc)(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam), const CGxFormat&);
        virtual int32_t DeviceSetFormat(const CGxFormat&);
        virtual void* DeviceWindow();
        virtual void DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2);
        virtual void CapsWindowSize(CRect&);
        virtual void CapsWindowSizeInScreenCoords(CRect& dst);
        virtual void ScenePresent(void);
        virtual void SceneClear(uint32_t, CImVector);
        virtual void XformSetProjection(const C44Matrix&);
        virtual void Draw(CGxBatch* batch, int32_t indexed);
        virtual void PoolSizeSet(CGxPool*, uint32_t);
        virtual char* BufLock(CGxBuf*);
        virtual int32_t BufUnlock(CGxBuf*, uint32_t);
        virtual void BufData(CGxBuf* buf, const void* data, size_t size, uintptr_t offset);
        virtual void TexDestroy(CGxTex* texId);
        virtual void IShaderCreate(CGxShader*);
        virtual int32_t StereoEnabled(void);

        // Member functions
        CGxDeviceGLES();
        void ISetCaps(const CGxFormat& format);
        int32_t ICreateContext();
        void IDestroyContext();
        void IUpdateWindowSize();
        char* IBufLock(CGxBuf* buf);
        void IBufUnlock(CGxBuf* buf);
        GlesPool* IPoolGet(CGxPool* pool);
        void IPoolFlush(CGxPool* pool);
        void IBindArrayBuffer(GLuint buffer);
        void IBindElementBuffer(GLuint buffer);
        void IStateSync();
        void IShaderConstantsFlush();
        void IStateSyncVertexPtrs();
        void IStateSyncIndexPtr();
        void IXformSetViewport();
        GlesProgram* IProgramGet(CGxShader* vs, CGxShader* ps);
        void IBindProgram();
        void ISetTexture(uint32_t tmu, CGxTex* texId);
        GlesTexture* ITexCreate(CGxTex* texId);
        void ITexUpload(CGxTex* texId);
        void ITexSetFlags(CGxTex* texId);
};

#endif
