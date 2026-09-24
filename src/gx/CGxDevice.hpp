#ifndef GX_C_GX_DEVICE_HPP
#define GX_C_GX_DEVICE_HPP

#include "gx/Buffer.hpp"
#include "gx/CGxCaps.hpp"
#include "gx/CGxFormat.hpp"
#include "gx/CGxMatrixStack.hpp"
#include "gx/CGxStateBom.hpp"
#include "gx/Types.hpp"
#include "gx/Shader.hpp"
#include <cstdint>
#include <storm/Hash.hpp>
#include <tempest/Box.hpp>
#include <tempest/Plane.hpp>
#include <tempest/Rect.hpp>

class CGxBatch;
class CGxMonitorMode;
class CGxTex;
class CGxTexFlags;

struct CGxAppRenderState {
    CGxStateBom m_value;
    uint32_t m_stackDepth;
    int32_t m_dirty;
};

struct CGxPushedRenderState {
    EGxRenderState m_which;
    CGxStateBom m_value;
    uint32_t m_stackDepth;
};

struct ShaderConstants {
    C4Vector constants[256];
    uint32_t unk1;
    uint32_t unk2;
};

// The light an application hands to CGxDevice::LightSet. 0x40 bytes in the reference, and every
// default below is the reference's own: its constructor at FUN_00683fb0 writes (0, 0, 1) for the
// direction, black ambient, WHITE diffuse, black specular, and the attenuation triple read out of
// the binary at 0x009e2ec0 and 0x009f23c8 -- 0.7 and 0.03, the same pair CM2Light defaults to.
//
// One deliberate divergence: the reference constructor clears only bits 0 and 1 of m_flags
// (`flags &= 0xfffffffc`) and leaves bits 2..31 holding whatever was on the stack. Nothing reads
// those bits, and reading an indeterminate value would be undefined behaviour here, so frozen
// zeroes the whole field.
struct CGxLight {
    // Bit 0 marks the light as set by the application. Bit 1 selects a positional (point) light
    // over a directional one, and is what LightSet turns into the w below.
    uint32_t m_flags = 0;
    // A position for a point light, a direction for a directional one.
    C3Vector m_posOrDir = { 0.0f, 0.0f, 1.0f };
    C3Vector m_ambient = { 0.0f, 0.0f, 0.0f };
    C3Vector m_diffuse = { 1.0f, 1.0f, 1.0f };
    C3Vector m_specular = { 0.0f, 0.0f, 0.0f };
    // Constant, linear, quadratic.
    C3Vector m_attenuation = { 0.0f, 0.69999999f, 0.029999999f };
};

// One light as the DEVICE holds it: 0x48 bytes, four of them at +0x2548 in the reference. It is
// not the same shape as CGxLight above -- the flags word at the front is replaced by a w on the
// position, so everything from m_ambient onwards sits at the same offset in both and only the
// first sixteen bytes differ. That is exactly what CGxLightState::Set relies on.
struct CGxLightState {
    // xyz is the position of a point light or the direction of a directional one; w is 1.0 for a
    // point light and 0.0 for a directional one, which is the only thing IStateSyncLights consults
    // to choose between D3DLIGHT_POINT and D3DLIGHT_DIRECTIONAL.
    C4Vector m_posOrDir;
    C3Vector m_ambient;
    C3Vector m_diffuse;
    C3Vector m_specular;
    C3Vector m_attenuation;
    int32_t m_enabled = 0;
    // Which fields changed since the backend last sent this light: 0x1 enabled, 0x2 position,
    // 0x4 ambient, 0x8 diffuse, 0x10 specular, and 0x20 / 0x40 / 0x80 for the three attenuation
    // terms one at a time.
    uint16_t m_dirty = 0;
    // The reference sets 0xe0 here for a point light and clears 0xe0 for a directional one -- the
    // same three bits that mark the attenuation terms dirty, but in a separate word that the
    // D3D backend never reads and that no sync ever clears. Written to match; see the note on
    // CGxDeviceD3d::IStateSyncLights for what is and is not known about it.
    uint16_t m_attenuationValid = 0;

    // Copy an application light in, marking per field what actually changed. Returns nothing: the
    // caller already holds the entry. ref: FUN_00684620
    void Set(const CGxLight& light);
};

class CGxDevice {
    public:
        // Structs
        struct TextureTarget {
            CGxTex* m_texture;
            uint32_t m_plane;
            void* m_apiSpecific;
        };

        // Static variables
        static uint32_t s_alphaRef[];
        static C3Vector s_pointScaleIdentity;
        static uint32_t s_primVtxAdjust[];
        static uint32_t s_primVtxDiv[];
        static ShaderConstants s_shadowConstants[2];
        static uint32_t s_streamPoolSize[];
        static uint32_t s_texFormatBitDepth[];
        static uint32_t s_texFormatBytesPerBlock[];

        // Static functions
        static int32_t AdapterFormats(EGxApi api, TSGrowableArray<CGxFormat>& adapterFormats);
        static int32_t AdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& monitorModes);
#if defined(WHOA_SYSTEM_WIN)
        static void D3dAdapterFormats(TSGrowableArray<CGxFormat>& formats);
        static void D3d9ExAdapterFormats(TSGrowableArray<CGxFormat>& formats);
#endif
#if defined(WHOA_SYSTEM_MAC)
        static void GLLAdapterFormats(TSGrowableArray<CGxFormat>& adapterFormats);
        static int32_t GLLAdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& monitorModes);
#endif
        static void Log(const char* format, ...);
        static void Log(const CGxFormat& format);
#if defined(WHOA_SYSTEM_MAC)
        static int32_t MacAdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& monitorModes);
#endif
#if defined(WHOA_SYSTEM_WIN)
        static CGxDevice* NewD3d();
        static CGxDevice* NewD3d9Ex();
#endif
#if defined(WHOA_SYSTEM_MAC)
        static CGxDevice* NewGLL();
#endif
#if defined(WHOA_SYSTEM_ANDROID)
        static CGxDevice* NewGLES();
#endif
        static CGxDevice* NewOpenGl();
        static void OpenGlAdapterFormats(TSGrowableArray<CGxFormat>& adapterFormats);
        static uint32_t PrimCalcCount(EGxPrim primType, uint32_t count);
#if defined(WHOA_SYSTEM_WIN)
        static int32_t WinAdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& monitorModes);
#endif

        // Member variables
        TSGrowableArray<CGxPushedRenderState> m_pushedStates;
        TSGrowableArray<size_t> m_stackOffsets;
        TSGrowableArray<EGxRenderState> m_dirtyStates;
        CRect m_defWindowRect;
        CRect m_curWindowRect;
        EGxApi m_api = GxApis_Last;
        CGxFormat m_format;
        CGxCaps m_caps;
        TSHashTable<CGxShader, HASHKEY_STRI> m_shaderList[GxShTargets_Last];
        int32_t (*m_windowProc)(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam) = nullptr;
        int32_t m_context = 0;
        int32_t intF5C = 0;
        int32_t m_windowVisible = 0;
        int32_t intF64 = 0;
        // Reference offset +0xf68. Incremented once per ScenePresent and never reset, so it is the
        // frame number. It exists here because the reference's texture code reads it: the priority
        // bump TextureIncreasePriority reaches (FUN_004bac20 and its neighbours at 004ba6f9 /
        // 004babbb) compares it against a per-texture stamp, which is how a texture that has not
        // been drawn for a while loses its place. frozen's side of that is still a stub, so nothing
        // reads this yet -- it ticks so that it is correct when something does.
        uint32_t m_frameCount = 0;
        int32_t intF6C = 1;
        CBoundingBox m_viewport;
        C44Matrix m_projection;
        C44Matrix m_projNative;
        CGxMatrixStack m_xforms[GxXforms_Last];
        // Reference offset +0x2934. A one-shot request flag: FUN_00682d30 sets it, each backend's
        // ScenePresent reads it just before calling the base, and the base clears it. What the
        // backends do with it has not been read, and nothing in frozen sets it yet.
        int32_t int2934 = 0;
        uint32_t m_appMasterEnables = 0;
        uint32_t m_hwMasterEnables = 0;
        TSList<CGxPool, TSGetLink<CGxPool>> m_poolList;
        CGxBuf* m_bufLocked[GxPoolTargets_Last] = {};
        CGxPool* m_vertexPool = nullptr;
        CGxPool* m_indexPool = nullptr;
        CGxBuf* m_streamBufs[GxPoolTargets_Last] = {};
        CGxVertexAttrib m_primVertexFormatAttrib[GxVertexBufferFormats_Last];
        CGxBuf* m_primVertexFormatBuf[GxVertexBufferFormats_Last] = {};
        // Six user clip planes and a per-plane dirty mask, at +0x24d4 and +0x24d0 in the
        // reference, which memsets 0x60 bytes over the array -- 6 * sizeof(C4Plane) -- and zeroes
        // the mask at device create. Nothing in frozen sets a plane yet, so the mask stays 0 and
        // the sync below returns immediately; that is the correct resting state rather than a
        // stub, and the moment something calls ClipPlaneSet it works.
        C4Plane m_clipPlanes[6] = {};
        uint32_t m_clipPlaneDirty = 0;
        // The scissor rectangle in NORMALISED coordinates, at +0x2538..+0x2544, with its dirty
        // flag at +0x2534. Device create zeroes the rect and sets the flag to 1, so the first
        // sync sends an empty rectangle; see IStateSyncScissorRect for why that is harmless.
        CRect m_scissorRect;
        int32_t m_scissorDirty = 1;
        // The fixed-function light bank, at +0x2548 in the reference with this same stride of
        // 0x48. Four is not a cap frozen chose: CM2Lighting::SetupGxLights gives slot 0 to the sun
        // and fills at most three more from CM2Lighting::m_lights, and the backend loop that sends
        // them runs `while (i < 4)`.
        CGxLightState m_lights[4];
        uint32_t m_primVertexMask = 0;
        uint32_t m_primVertexDirty = 0;
        EGxVertexBufferFormat m_primVertexFormat = GxVertexBufferFormats_Last;
        CGxBuf* m_primVertexBuf = nullptr;
        uint32_t m_primVertexSize;
        CGxBuf* m_primIndexBuf = nullptr;
        int32_t m_primIndexDirty = 0;
        TSFixedArray<CGxAppRenderState> m_appRenderStates;
        TSFixedArray<CGxStateBom> m_hwRenderStates;
        // TODO
        TextureTarget m_textureTarget[GxBuffers_Last] = {};
        // TODO
        uint32_t m_baseMipLevel = 0; // TODO placeholder

        // Virtual member functions
        virtual void ITexMarkAsUpdated(CGxTex*) = 0;
        // Bind the slot's texture as the device's colour/depth target; null restores the default.
        virtual void IRenderTargetSet(EGxBuffer, CGxTex*, uint32_t) {}

        // Debug only: read a render target back and write it to a file. There is no equivalent in
        // the reference, which had a debugger attached instead; frozen needs it because a shadow map
        // that is never sampled correctly is indistinguishable from one that was never drawn.
        virtual int32_t IRenderTargetDump(CGxTex*, const char*) { return 0; }
        virtual int32_t IScreenShot(const char*) { return 0; }
        virtual void IRsSendToHw(EGxRenderState) = 0;
        virtual void ICursorCreate(const CGxFormat& format);
        virtual int32_t DeviceCreate(int32_t (*windowProc)(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam), const CGxFormat&);
        virtual int32_t DeviceSetFormat(const CGxFormat&);
        virtual void* DeviceWindow() = 0;
        virtual void DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2) = 0;
        virtual void CapsWindowSize(CRect&) = 0;
        virtual void CapsWindowSizeInScreenCoords(CRect& dst) = 0;
        virtual void ScenePresent(void);
        virtual void SceneClear(uint32_t, CImVector) {};
        virtual void XformSetProjection(const C44Matrix&);
        virtual void XformSetView(const C44Matrix&);
        virtual void Draw(CGxBatch* batch, int32_t indexed) {};
        virtual void ValidateDraw(CGxBatch*, int32_t);
        virtual void MasterEnableSet(EGxMasterEnables, int32_t);
        virtual void PoolSizeSet(CGxPool*, uint32_t) = 0;
        virtual char* BufLock(CGxBuf*);
        virtual int32_t BufUnlock(CGxBuf*, uint32_t);
        virtual void BufData(CGxBuf* buf, const void* data, size_t size, uintptr_t offset);
        virtual int32_t TexCreate(EGxTexTarget, uint32_t, uint32_t, uint32_t, EGxTexFormat, EGxTexFormat, CGxTexFlags, void*, void (*)(EGxTexCommand, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t&, const void*&), const char*, CGxTex*&);
        virtual void TexDestroy(CGxTex* texId);
        virtual void ShaderCreate(CGxShader*[], EGxShTarget, const char*, const char*, int32_t);
        virtual void ShaderConstantsSet(EGxShTarget, uint32_t, const float*, uint32_t);
        virtual void IShaderCreate(CGxShader*) = 0;
        virtual int32_t StereoEnabled(void) = 0;

        // Member functions
        CGxDevice();
        const CGxCaps& Caps() const;
        CGxBuf* BufCreate(CGxPool*, uint32_t, uint32_t, uint32_t);
        CGxBuf* BufStream(EGxPoolTarget, uint32_t, uint32_t);
        void DeviceCreatePools(void);
        void DeviceCreateStreamBufs(void);
        const CRect& DeviceCurWindow(void);
        void DeviceSetCurWindow(const CRect&);
        void DeviceSetDefWindow(CRect const&);
        const CRect& DeviceDefWindow(void);
        int32_t IDevIsWindowed();
        void IRsDirty(EGxRenderState);
        // Turn one light slot on or off. ref: FUN_00683080
        void LightEnable(uint32_t index, int32_t enable);
        // Store one light into the given slot. `origin` is subtracted from a POINT light's
        // position, which lets a caller keep positions relative to something other than the
        // camera; a directional light ignores it, and so does a zero vector. ref: FUN_006847d0
        void LightSet(uint32_t index, const CGxLight& light, const C3Vector& origin);
        void IRsForceUpdate(void);
        void IRsForceUpdate(EGxRenderState);
        void IRsInit(void);
        void IRsSync(int32_t);
        void IShaderBind(void) {};
        void IShaderLoad(CGxShader*[], EGxShTarget, const char*, const char*, int32_t);
        void ITexBind(void) {};
        void ITexWHDStartEnd(CGxTex*, uint32_t&, uint32_t&, uint32_t&, uint32_t&);
        int32_t MasterEnable(EGxMasterEnables);
        CGxPool* PoolCreate(EGxPoolTarget, EGxPoolUsage, uint32_t, EGxPoolHintBits, const char*);
        void PrimIndexPtr(CGxBuf*);
        void ClipPlaneSet(uint32_t, const C4Plane*);
        void ScissorSet(const CRect*);
        void PrimVertexFormat(CGxBuf*, CGxVertexAttrib*, uint32_t);
        void PrimVertexMask(uint32_t);
        void PrimVertexPtr(CGxBuf*, EGxVertexBufferFormat);
        void RenderTargetGet(EGxBuffer buffer, CGxTex*& gxTex);
        // Redirect rendering into a texture (or back to the frame buffer with a null texture).
        // The full-screen effects and the map shadow map both need this; until it existed the gx
        // layer could only read the current target, never set one.
        void RenderTargetSet(EGxBuffer buffer, CGxTex* gxTex, uint32_t plane = 0);
        int32_t RenderTargetDump(CGxTex* gxTex, const char* path);
        int32_t ScreenShot(const char* path);
        void RsGet(EGxRenderState, int32_t&);
        void RsSet(EGxRenderState, int32_t);
        void RsSet(EGxRenderState, float);
        void RsSet(EGxRenderState, uint32_t);
        void RsSet(EGxRenderState, void*);
        void RsSetAlphaRef(void);
        void RsPop(void);
        void RsPush(void);
        void ShaderConstantsClear(void);
        char* ShaderConstantsLock(EGxShTarget target);
        void ShaderConstantsUnlock(EGxShTarget target, uint32_t index, uint32_t count);
        void TexMarkForUpdate(CGxTex*, const CiRect&, int32_t);
        void TexSetWrap(CGxTex* texId, EGxTexWrapMode wrapU, EGxTexWrapMode wrapV);
        void XformPop(EGxXform xf);
        void XformProjection(C44Matrix&);
        void XformProjNative(C44Matrix&);
        void XformPush(EGxXform xf);
        void XformSet(EGxXform xf, const C44Matrix& matrix);
        void XformSetViewport(float, float, float, float, float, float);
        void XformView(C44Matrix&);
        void XformWorld(C44Matrix&);
        void XformViewport(float&, float&, float&, float&, float&, float&);
};

#endif
