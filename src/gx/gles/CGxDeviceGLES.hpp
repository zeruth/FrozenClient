#ifndef GX_GLES_C_GX_DEVICE_GLES_HPP
#define GX_GLES_C_GX_DEVICE_GLES_HPP

#include "gx/CGxDevice.hpp"
#include <EGL/egl.h>

// OpenGL ES 3 device for Android. This is the first cut: it owns the EGL context on the activity
// window, keeps the client's buffer pools in system memory, and clears and presents. Drawing,
// textures, and shaders are ported from the GLL device next.
class CGxDeviceGLES : public CGxDevice {
    public:
        // Member variables
        EGLDisplay m_eglDisplay = EGL_NO_DISPLAY;
        EGLSurface m_eglSurface = EGL_NO_SURFACE;
        EGLContext m_eglContext = EGL_NO_CONTEXT;

        // Virtual member functions
        virtual void ITexMarkAsUpdated(CGxTex*);
        virtual void IRsSendToHw(EGxRenderState);
        virtual int32_t DeviceCreate(int32_t (*windowProc)(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam), const CGxFormat&);
        virtual int32_t DeviceSetFormat(const CGxFormat&);
        virtual void* DeviceWindow();
        virtual void DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2) {};
        virtual void CapsWindowSize(CRect&);
        virtual void CapsWindowSizeInScreenCoords(CRect& dst);
        virtual void ScenePresent(void);
        virtual void SceneClear(uint32_t, CImVector);
        virtual void Draw(CGxBatch* batch, int32_t indexed);
        virtual void PoolSizeSet(CGxPool*, uint32_t);
        virtual char* BufLock(CGxBuf*);
        virtual int32_t BufUnlock(CGxBuf*, uint32_t);
        virtual void BufData(CGxBuf* buf, const void* data, size_t size, uintptr_t offset);
        virtual void IShaderCreate(CGxShader*);
        virtual int32_t StereoEnabled(void);

        // Member functions
        CGxDeviceGLES();
        void ISetCaps(const CGxFormat& format);
        int32_t ICreateContext();
        void IDestroyContext();
        void IUpdateWindowSize();
};

#endif
