#include "gx/d3d/CGxDeviceD3d.hpp"
#include <common/Os.hpp>
#include <common/Time.hpp>
#include <cstdio>
#include "gx/Texture.hpp"
#include "gx/Blit.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/texture/CGxTex.hpp"
#include "math/Utils.hpp"
#include <algorithm>
#include <directxmath.h>


HCURSOR CGxDeviceD3d::s_classCursor = nullptr;

int32_t CGxDeviceD3d::s_clientAdjustWidth;
int32_t CGxDeviceD3d::s_clientAdjustHeight;

D3DCMPFUNC CGxDeviceD3d::s_cmpFunc[] = {
    D3DCMP_LESSEQUAL,
    D3DCMP_EQUAL,
    D3DCMP_GREATEREQUAL,
    D3DCMP_LESS,
};

D3DCULL CGxDeviceD3d::s_cullMode[] = {
    D3DCULL_NONE,
    D3DCULL_CW,
    D3DCULL_CCW,
};

// s_srcBlend and s_dstBlend were checked against the reference on 2026-09-23 by searching its
// image for these exact twelve-dword sequences. Both are present verbatim in .rdata, twice
// each -- src at 0x00a2f964 and 0x00a2fb68, dst at 0x00a2f994 and 0x00a2fb98, the pairs 0x204
// apart, which is one set per device backend. Every entry matches, so no transparent surface
// in this client composites with the wrong factors.
//
// Read, not run: this proves the tables, not the code that indexes them.
D3DBLEND CGxDeviceD3d::s_dstBlend[] = {
    D3DBLEND_ZERO,              // GxBlend_Opaque
    D3DBLEND_ZERO,              // GxBlend_AlphaKey
    D3DBLEND_INVSRCALPHA,       // GxBlend_Alpha
    D3DBLEND_ONE,               // GxBlend_Add
    D3DBLEND_ZERO,              // GxBlend_Mod
    D3DBLEND_SRCCOLOR,          // GxBlend_Mod2x
    D3DBLEND_ONE,               // GxBlend_ModAdd
    D3DBLEND_ONE,               // GxBlend_InvSrcAlphaAdd
    D3DBLEND_ZERO,              // GxBlend_InvSrcAlphaOpaque
    D3DBLEND_ZERO,              // GxBlend_SrcAlphaOpaque
    D3DBLEND_ONE,               // GxBlend_NoAlphaAdd
    D3DBLEND_INVBLENDFACTOR,    // GxBlend_ConstantAlpha
};

D3DCUBEMAP_FACES CGxDeviceD3d::s_faceTypes[] = {
    D3DCUBEMAP_FACE_POSITIVE_X,
    D3DCUBEMAP_FACE_NEGATIVE_X,
    D3DCUBEMAP_FACE_POSITIVE_Y,
    D3DCUBEMAP_FACE_NEGATIVE_Y,
    D3DCUBEMAP_FACE_POSITIVE_Z,
    D3DCUBEMAP_FACE_NEGATIVE_Z,
};

D3DTEXTUREFILTERTYPE CGxDeviceD3d::s_filterModes[GxTexFilters_Last][3] = {
    // Min, Mag, Mip
    { D3DTEXF_POINT,    D3DTEXF_POINT,  D3DTEXF_NONE    },  // GxTex_Nearest
    { D3DTEXF_LINEAR,   D3DTEXF_LINEAR, D3DTEXF_NONE    },  // GxTex_Linear
    { D3DTEXF_POINT,    D3DTEXF_POINT,  D3DTEXF_POINT   },  // GxTex_NearestMipNearest
    { D3DTEXF_LINEAR,   D3DTEXF_LINEAR, D3DTEXF_POINT   },  // GxTex_LinearMipNearest
    { D3DTEXF_LINEAR,   D3DTEXF_LINEAR, D3DTEXF_LINEAR  },  // GxTex_LinearMipLinear
    { D3DTEXF_LINEAR,   D3DTEXF_LINEAR, D3DTEXF_LINEAR  },  // GxTex_Anisotropic
};

uint32_t CGxDeviceD3d::s_gxAttribToD3dAttribSize[] = {
    4,                      // type 0
    4,                      // type 1
    4,                      // type 2
    8,                      // type 3
    12,                     // type 4
    4,                      // type 5
    4,                      // type 6
};

// Reference 0x00a2e4e4: Gx query type to D3DQUERYTYPE. Only occlusion is defined.
static D3DQUERYTYPE s_gxQueryToD3dQuery[] = {
    D3DQUERYTYPE_OCCLUSION,
};

D3DDECLTYPE CGxDeviceD3d::s_gxAttribToD3dAttribType[] = {
    D3DDECLTYPE_D3DCOLOR,   // type 0
    D3DDECLTYPE_UBYTE4,     // type 1
    D3DDECLTYPE_UBYTE4N,    // type 2
    D3DDECLTYPE_FLOAT2,     // type 3
    D3DDECLTYPE_FLOAT3,     // type 4
    D3DDECLTYPE_SHORT2,     // type 5
    D3DDECLTYPE_FLOAT1,     // type 6
};

D3DDECLUSAGE CGxDeviceD3d::s_gxAttribToD3dAttribUsage[] = {
    D3DDECLUSAGE_POSITION,      // GxVA_Position
    D3DDECLUSAGE_BLENDWEIGHT,   // GxVA_BlendWeight
    D3DDECLUSAGE_BLENDINDICES,  // GxVA_BlendIndices
    D3DDECLUSAGE_NORMAL,        // GxVA_Normal
    D3DDECLUSAGE_COLOR,         // GxVA_Color0
    D3DDECLUSAGE_COLOR,         // GxVA_Color1
    D3DDECLUSAGE_TEXCOORD,      // GxVA_TexCoord0
    D3DDECLUSAGE_TEXCOORD,      // GxVA_TexCoord1
    D3DDECLUSAGE_TEXCOORD,      // GxVA_TexCoord2
    D3DDECLUSAGE_TEXCOORD,      // GxVA_TexCoord3
    D3DDECLUSAGE_TEXCOORD,      // GxVA_TexCoord4
    D3DDECLUSAGE_TEXCOORD,      // GxVA_TexCoord5
    D3DDECLUSAGE_TEXCOORD,      // GxVA_TexCoord6
    D3DDECLUSAGE_TEXCOORD,      // GxVA_TexCoord7
};

uint32_t CGxDeviceD3d::s_gxAttribToD3dAttribUsageIndex[] = {
    0,                          // GxVA_Position
    0,                          // GxVA_BlendWeight
    0,                          // GxVA_BlendIndices
    0,                          // GxVA_Normal
    0,                          // GxVA_Color0
    1,                          // GxVA_Color1
    0,                          // GxVA_TexCoord0
    1,                          // GxVA_TexCoord1
    2,                          // GxVA_TexCoord2
    3,                          // GxVA_TexCoord3
    4,                          // GxVA_TexCoord4
    5,                          // GxVA_TexCoord5
    6,                          // GxVA_TexCoord6
    7,                          // GxVA_TexCoord7
};

D3DFORMAT CGxDeviceD3d::s_GxFormatToD3dFormat[] = {
    D3DFMT_R5G6B5,      // Fmt_Rgb565
    D3DFMT_X8R8G8B8,    // Fmt_ArgbX888
    D3DFMT_A8R8G8B8,    // Fmt_Argb8888
    D3DFMT_A2R10G10B10, // Fmt_Argb2101010
    D3DFMT_D16,         // Fmt_Ds160
    D3DFMT_D24X8,       // Fmt_Ds24X
    D3DFMT_D24S8,       // Fmt_Ds248
    D3DFMT_D32,         // Fmt_Ds320
};

D3DFORMAT CGxDeviceD3d::s_GxTexFmtToD3dFmt[] = {
    D3DFMT_UNKNOWN,     // GxTex_Unknown
    D3DFMT_A8B8G8R8,    // GxTex_Abgr8888
    D3DFMT_A8R8G8B8,    // GxTex_Argb8888
    D3DFMT_A4R4G4B4,    // GxTex_Argb4444
    D3DFMT_A1R5G5B5,    // GxTex_Argb1555
    D3DFMT_R5G6B5,      // GxTex_Rgb565
    D3DFMT_DXT1,        // GxTex_Dxt1
    D3DFMT_DXT3,        // GxTex_Dxt3
    D3DFMT_DXT5,        // GxTex_Dxt5
    D3DFMT_V8U8,        // GxTex_Uv88
    D3DFMT_G16R16F,     // GxTex_Gr1616F
    D3DFMT_R32F,        // GxTex_R32F
    D3DFMT_D24X8,       // GxTex_D24X8
};

EGxTexFormat CGxDeviceD3d::s_GxTexFmtToUse[] = {
    GxTex_Unknown,
    GxTex_Abgr8888,
    GxTex_Argb8888,
    GxTex_Argb4444,
    GxTex_Argb1555,
    GxTex_Rgb565,
    GxTex_Dxt1,
    GxTex_Dxt3,
    GxTex_Dxt5,
    GxTex_Uv88,
    GxTex_Gr1616F,
    GxTex_R32F,
    GxTex_D24X8,
};

D3DPRIMITIVETYPE CGxDeviceD3d::s_primitiveConversion[] = {
    D3DPT_POINTLIST,    // GxPrim_Points
    D3DPT_LINELIST,     // GxPrim_Lines
    D3DPT_LINESTRIP,    // GxPrim_LineStrip
    D3DPT_TRIANGLELIST, // GxPrim_Triangles
    D3DPT_TRIANGLESTRIP, // GxPrim_TriangleStrip
    D3DPT_TRIANGLEFAN,  // GxPrim_TriangleFan
};

D3DBLEND CGxDeviceD3d::s_srcBlend[] = {
    D3DBLEND_ONE,           // GxBlend_Opaque
    D3DBLEND_ONE,           // GxBlend_AlphaKey
    D3DBLEND_SRCALPHA,      // GxBlend_Alpha
    D3DBLEND_SRCALPHA,      // GxBlend_Add
    D3DBLEND_DESTCOLOR,     // GxBlend_Mod
    D3DBLEND_DESTCOLOR,     // GxBlend_Mod2x
    D3DBLEND_DESTCOLOR,     // GxBlend_ModAdd
    D3DBLEND_INVSRCALPHA,   // GxBlend_InvSrcAlphaAdd
    D3DBLEND_INVSRCALPHA,   // GxBlend_InvSrcAlphaOpaque
    D3DBLEND_SRCALPHA,      // GxBlend_SrcAlphaOpaque
    D3DBLEND_ONE,           // GxBlend_NoAlphaAdd
    D3DBLEND_BLENDFACTOR,   // GxBlend_ConstantAlpha
};

EGxTexFormat CGxDeviceD3d::s_tolerableTexFmtMapping[] = {
    GxTex_Unknown,      // GxTex_Unknown
    GxTex_Argb4444,     // GxTex_Abgr8888
    GxTex_Argb4444,     // GxTex_Argb8888
    GxTex_Argb4444,     // GxTex_Argb4444
    GxTex_Argb4444,     // GxTex_Argb1555
    GxTex_Argb4444,     // GxTex_Rgb565
    GxTex_Dxt1,         // GxTex_Dxt1
    GxTex_Dxt3,         // GxTex_Dxt3
    GxTex_Dxt5,         // GxTex_Dxt5
    GxTex_Uv88,         // GxTex_Uv88
    GxTex_Gr1616F,      // GxTex_Gr1616F
    GxTex_R32F,         // GxTex_R32F
    GxTex_D24X8,        // GxTex_D24X8
};

D3DTEXTUREADDRESS CGxDeviceD3d::s_wrapModes[] = {
    D3DTADDRESS_CLAMP,  // GxTex_Clamp
    D3DTADDRESS_WRAP,   // GxTex_Wrap
};

// ref: FUN_0068eb20
// One recorded divergence: the reference falls back to LoadCursorA(instance, IDC_ARROW), which
// can only fail for a module handle; frozen asks the system (see below). The reference never
// reaches the fallback because its exe carries BlizzardCursor.cur.
ATOM WindowClassCreate() {
    auto instance = GetModuleHandle(nullptr);

    WNDCLASSEX wc = { 0 };

    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = CGxDeviceD3d::WindowProcD3d;
    wc.hInstance = instance;
    wc.lpszClassName = TEXT("GxWindowClassD3d");

    wc.hIcon = static_cast<HICON>(LoadImage(instance, TEXT("BlizzardIcon.ico"), 1u, 0, 0, 0x40));
    wc.hCursor = LoadCursor(instance, TEXT("BlizzardCursor.cur"));

    if (!wc.hCursor) {
        // A STANDARD cursor must be loaded with a null module handle. Passing `instance` makes
        // LoadCursor look for a resource named IDC_ARROW inside the exe, which does not exist, so it
        // returned NULL -- and a window class with no cursor leaves whatever the previous window
        // set, which at startup is the "app starting" arrow-and-spinner. That is why the cursor was
        // a spinning wheel over the whole window for the life of the process.
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    }

    CGxDeviceD3d::s_classCursor = wc.hCursor;

    return RegisterClassEx(&wc);
}

int32_t CGxDeviceD3d::ILoadD3dLib(HINSTANCE& d3dLib, LPDIRECT3D9& d3d) {
    d3dLib = nullptr;
    d3d = nullptr;

    d3dLib = LoadLibrary(TEXT("d3d9.dll"));

    if (d3dLib) {
        auto d3dCreateProc = GetProcAddress(d3dLib, "Direct3DCreate9");

        if (d3dCreateProc) {
            d3d = reinterpret_cast<LPDIRECT3D9>(d3dCreateProc());

            if (d3d) {
                return 1;
            }

            CGxDevice::Log("CGxDeviceD3d::ILoadD3dLib(): unable to d3dCreateProc()");
        } else {
            CGxDevice::Log("CGxDeviceD3d::ILoadD3dLib(): unable to GetProcAddress()");
        }
    } else {
        CGxDevice::Log("CGxDeviceD3d::ILoadD3dLib(): unable to LoadLibrary()");
    }

    CGxDeviceD3d::IUnloadD3dLib(d3dLib, d3d);

    return 0;
}

void CGxDeviceD3d::IUnloadD3dLib(HINSTANCE& d3dLib, LPDIRECT3D9& d3d) {
    if (d3d) {
        d3d->Release();
    }

    if (d3dLib) {
        FreeLibrary(d3dLib);
    }
}

LRESULT CGxDeviceD3d::WindowProcD3d(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto device = reinterpret_cast<CGxDeviceD3d*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (uMsg) {
    case WM_CREATE: {
        auto lpcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LPARAM>(lpcs->lpCreateParams));

        return 0;
    }

    case WM_DESTROY: {
        device->DeviceWM(GxWM_Destroy, 0, 0);

        return 0;
    }

    case WM_SIZE: {
        CRect windowRect = {
            0.0f,
            0.0f,
            static_cast<float>(HIWORD(lParam)),
            static_cast<float>(LOWORD(lParam))
        };

        int32_t resizeType = 0;
        if (wParam == SIZE_MINIMIZED) {
            resizeType = 1;
        } else if (wParam == SIZE_MAXHIDE) {
            resizeType = 2;
        }

        device->DeviceWM(GxWM_Size, reinterpret_cast<uintptr_t>(&windowRect), resizeType);

        break;
    }

    case WM_ACTIVATE: {
        if (wParam == WA_INACTIVE && !device->IDevIsWindowed()) {
            CRect windowRect = { 0.0f, 0.f, 0.0f, 0.0f };
            device->DeviceWM(GxWM_Size, reinterpret_cast<uintptr_t>(&windowRect), 1);
        } else if (wParam == WA_ACTIVE && !device->IDevIsWindowed()) {
            CRect windowRect;
            device->CapsWindowSizeInScreenCoords(windowRect);
            device->DeviceWM(GxWM_Size, reinterpret_cast<uintptr_t>(&windowRect), 3);
        }

        break;
    }

    case WM_SETFOCUS: {
        device->DeviceWM(GxWM_SetFocus, 0, 0);

        return 0;
    }

    case WM_KILLFOCUS: {
        device->DeviceWM(GxWM_KillFocus, 0, 0);

        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT paint;
        BeginPaint(hWnd, &paint);
        EndPaint(hWnd, &paint);

        return 0;
    }

    case WM_ERASEBKGND: {
        return 0;
    }

    case WM_SETCURSOR: {
        // Returning TRUE means "handled, do not change the cursor". Doing that without ever calling
        // SetCursor left whatever cursor was last set in place -- the other half of the spinning
        // wheel. Set it for the client area, and let DefWindowProc handle the frame so the resize
        // borders still get their own arrows.
        if (LOWORD(lParam) == HTCLIENT) {
            if (CGxDeviceD3d::s_classCursor) {
                SetCursor(CGxDeviceD3d::s_classCursor);
            }

            return 1;
        }

        break;
    }

    case WM_DISPLAYCHANGE: {
        // TODO

        break;
    }

    case WM_SYSCOMMAND: {
        // TODO

        break;
    }

    case WM_SIZING: {
        // TODO

        return 1;
    }

    default:
        break;
    }

    if (device && device->m_windowProc) {
        return device->m_windowProc(hWnd, uMsg, wParam, lParam);
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

// ref: FUN_0068fd50
CGxDeviceD3d::CGxDeviceD3d() : CGxDevice() {
    // TODO

    this->m_api = GxApi_D3d9;

    // TODO

    memset(this->m_deviceStates, 0xFF, sizeof(this->m_deviceStates));

    // TODO

    this->DeviceCreatePools();
    this->DeviceCreateStreamBufs();
}

// ref: FUN_0068fce0
char* CGxDeviceD3d::BufLock(CGxBuf* buf) {
    CGxDevice::BufLock(buf);
    return this->IBufLock(buf);
}

// ref: FUN_0068fae0
int32_t CGxDeviceD3d::BufUnlock(CGxBuf* buf, uint32_t size) {
    CGxDevice::BufUnlock(buf, size);
    this->IBufUnlock(buf);

    return 1;
}

// ref: FUN_0068fd00
// IBufLock never returns null now -- it falls back to the device scratch -- so the copy is
// unconditional, as the reference's is.
void CGxDeviceD3d::BufData(CGxBuf* buf, const void* data, size_t size, uintptr_t offset) {
    CGxDevice::BufData(buf, data, size, offset);

    auto bufData = this->IBufLock(buf);
    memcpy(&bufData[offset], data, size);

    this->IBufUnlock(buf);
}

// ref: FUN_006a5a00
void CGxDeviceD3d::CapsWindowSize(CRect& dst) {
    dst = this->DeviceCurWindow();
}

// ref: FUN_006a9920
void CGxDeviceD3d::CapsWindowSizeInScreenCoords(CRect& dst) {
    if (this->IDevIsWindowed()) {
        auto windowRect = this->DeviceCurWindow();

        POINT points[2];
        points[0].x = 0;
        points[0].y = 0;
        points[1].x = windowRect.maxX;
        points[1].y = windowRect.maxY;

        MapWindowPoints(this->m_hwnd, nullptr, points, 2);

        dst.minY = points[0].y;
        dst.minX = points[0].x;
        dst.maxY = points[1].y;
        dst.maxX = points[1].x;
    } else {
        dst = this->DeviceCurWindow();
    }
}

// ref: FUN_0069fb70
int32_t CGxDeviceD3d::CreatePoolAPI(CGxPool* pool) {
    if (pool->m_target == GxPoolTarget_Vertex) {
        pool->m_apiSpecific = this->ICreateD3dVB(pool->m_usage, pool->m_size);
    } else if (pool->m_target == GxPoolTarget_Index) {
        pool->m_apiSpecific = this->ICreateD3dIB(pool->m_usage, pool->m_size);
    }

    return 1;
}

// ref: FUN_00690750
// The desktop's gamma ramp is read before anything else and becomes the starting ramp, so a
// device that never sets gamma leaves the desktop as it was.
int32_t CGxDeviceD3d::DeviceCreate(int32_t (*windowProc)(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam), const CGxFormat& format) {
    this->m_ownhwnd = 1;

    HDC dc = GetDC(nullptr);

    if (GetDeviceGammaRamp(dc, &this->m_desktopGammaRamp) && &this->m_gammaRamp != &this->m_desktopGammaRamp) {
        this->m_gammaRamp = this->m_desktopGammaRamp;
    }

    ReleaseDC(nullptr, dc);

    this->m_hwndClass = WindowClassCreate();

    if (this->m_hwndClass) {
        if (this->ICreateD3d() && this->CGxDevice::DeviceCreate(windowProc, format)) {
            return 1;
        }

        this->DeviceDestroy();
        return 0;
    }

    auto error = OsGetLastErrorStr();
    CGxDevice::Log("CGxDeviceD3d::DeviceCreate(): WindowClassCreate() failed: %s", error);
    OsFreeLastErrorStr(error);

    this->DeviceDestroy();

    return 0;
}

// ref: FUN_00690830
// Creating into a window the caller owns: no window class, no window of our own.
int32_t CGxDeviceD3d::DeviceCreate(void* window, const CGxFormat& format) {
    this->m_ownhwnd = 0;

    CGxDevice::DeviceCreate(window, format);
    this->m_hwnd = static_cast<HWND>(window);

    if (this->ICreateD3d() && this->ICreateD3dDevice(format) && CGxDevice::DeviceCreate(window, format)) {
        return 1;
    }

    this->DeviceDestroy();

    return 0;
}

// ref: FUN_006905f0
void CGxDeviceD3d::DeviceDestroy() {
    CGxDevice::DeviceDestroy();

    if (this->m_hwnd && this->m_ownhwnd) {
        DestroyWindow(this->m_hwnd);
        this->m_hwnd = nullptr;
    }

    if (this->m_hwndClass) {
        UnregisterClassA(reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(this->m_hwndClass)), GetModuleHandleA(nullptr));
        this->m_hwndClass = 0;
    }

    this->IDestroyD3dDevice();

    if (this->m_d3d) {
        this->m_d3d->Release();
        this->m_d3d = nullptr;
    }

    if (this->m_d3dLib) {
        FreeLibrary(this->m_d3dLib);
        this->m_d3dLib = nullptr;
    }
}

// ref: FUN_006904d0
// The old device is destroyed before the new window and device are made (it was not), and a
// failure tears both down and returns 0 (it fell off the end of the function, which is
// undefined behaviour).
int32_t CGxDeviceD3d::DeviceSetFormat(const CGxFormat& format) {
    CGxDevice::Log("CGxDeviceD3d::DeviceSetFormat():");
    CGxDevice::Log(format);

    if (this->m_hwnd) {
        ShowWindow(this->m_hwnd, SW_HIDE);
    }

    this->IDestroyD3dDevice();

    if (this->m_hwnd) {
        DestroyWindow(this->m_hwnd);
    }

    this->m_hwnd = nullptr;

    this->m_format = format;

    CGxFormat createFormat = format;

    if (this->ICreateWindow(createFormat) && this->ICreateD3dDevice(createFormat) && this->CGxDevice::DeviceSetFormat(createFormat)) {
        this->intF64 = 1;
        this->m_cursorDirty = 1;

        if (this->m_format.window == 0) {
            RECT windowRect;
            GetWindowRect(this->m_hwnd, &windowRect);
            ClipCursor(&windowRect);
        }

        return 1;
    }

    CGxDevice::Log("CGxDeviceD3d::DeviceSetFormat(): unable to set format!");

    this->IDestroyD3dDevice();

    if (this->m_hwnd) {
        DestroyWindow(this->m_hwnd);
    }

    this->m_hwnd = nullptr;

    return 0;
}

void* CGxDeviceD3d::DeviceWindow() {
    return this->m_hwnd;
}

// ref: FUN_00690230
// One recorded divergence: on a size message whose back buffer already matches the window, frozen
// skips the reset (see the note inside). Everything else follows the reference, including the
// focus messages, which drive the window-active flag the frame cap reads.
void CGxDeviceD3d::DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2) {
    switch (wm) {
    case GxWM_Size: {
        if (param2 == 1 || param2 == 2) {
            this->m_windowVisible = 0;
        } else {
            this->m_windowVisible = 1;

            auto& windowRect = *reinterpret_cast<CRect*>(param1);
            this->DeviceSetDefWindow(windowRect);

            if (this->m_d3dDevice && this->m_context) {
                D3DPRESENT_PARAMETERS wanted;
                this->ISetPresentParms(wanted, this->m_format);

                // Resetting a device whose back buffer already matches the window is pointless,
                // and not free. Windows sends a WM_SIZE on entering the world with the size
                // unchanged; resetting on it failed with D3DERR_INVALIDCALL, which cleared
                // m_context, and nothing ever set it again -- so the client rendered at full rate
                // and presented nothing for the rest of the run. The window kept its last frame,
                // which looks exactly like a hang and is not one.
                uint32_t haveWidth = 0;
                uint32_t haveHeight = 0;

                LPDIRECT3DSURFACE9 back = nullptr;

                if (SUCCEEDED(this->m_d3dDevice->GetRenderTarget(0, &back)) && back) {
                    D3DSURFACE_DESC desc;

                    if (SUCCEEDED(back->GetDesc(&desc))) {
                        haveWidth = desc.Width;
                        haveHeight = desc.Height;
                    }

                    back->Release();
                }

                if (haveWidth == wanted.BackBufferWidth && haveHeight == wanted.BackBufferHeight) {
                    this->intF6C = 1;

                    return;
                }

                this->IReleaseD3dResources(0);

                D3DPRESENT_PARAMETERS d3dpp;
                this->ISetPresentParms(d3dpp, this->m_format);

                HRESULT resetResult = this->m_d3dDevice->Reset(&d3dpp);

                if (SUCCEEDED(resetResult)) {
                    this->IStateSetD3dDefaults();
                    this->IWindowActiveSet(1);

                    this->m_context = 1;
                    this->intF5C = 0;

                    this->intF6C = 1;

                    return;
                } else {
                    // Name the failure instead of guessing. D3DERR_INVALIDCALL means something in
                    // D3DPOOL_DEFAULT is still alive; D3DERR_DEVICELOST means the device is not
                    // ready to be reset and retrying right now cannot help. Those two want opposite
                    // responses, so the distinction is worth printing.
                    fprintf(stderr, "Reset FAILED 0x%08lX (%s)\n", resetResult,
                            resetResult == D3DERR_INVALIDCALL
                                ? "INVALIDCALL - a default-pool resource is still alive"
                            : resetResult == D3DERR_DEVICELOST ? "DEVICELOST - device not ready"
                            : resetResult == D3DERR_DRIVERINTERNALERROR ? "DRIVERINTERNALERROR"
                            : resetResult == D3DERR_OUTOFVIDEOMEMORY ? "OUTOFVIDEOMEMORY"
                            : "unknown");

                    this->m_context = 0;
                }
            }

            this->intF6C = 1;
        }

        break;
    }

    case GxWM_DisplayChange: {
        if (this->m_windowVisible) {
            this->DeviceSetDefWindow(*reinterpret_cast<CRect*>(param1));
            this->intF6C = 1;
        }

        break;
    }

    case GxWM_Destroy:
    case GxWM_KillFocus: {
        this->intF64 = 0;

        break;
    }

    case GxWM_SetFocus: {
        this->intF64 = 1;

        if (!this->m_format.window) {
            RECT windowRect;
            GetWindowRect(this->m_hwnd, &windowRect);
            ClipCursor(&windowRect);
        }

        break;
    }
    }
}

int32_t g_terrZEnable = -1;
int32_t g_terrZFunc = -1;
int32_t g_terrZWrite = -1;
int32_t g_terrHasDepth = -1;
int32_t g_modelZEnable = -1;
int32_t g_modelZFunc = -1;
int32_t g_modelZWrite = -1;
int32_t g_modelHasDepth = -1;
int32_t g_d3dZEnable = -1;
int32_t g_d3dZFunc = -1;
int32_t g_d3dZWrite = -1;
int32_t g_d3dHasDepthSurface = -1;

// ref: FUN_006a3620
void CGxDeviceD3d::Draw(CGxBatch* batch, int32_t indexed) {
    if (!this->m_context || this->intF5C) {
        return;
    }

    this->IStateSync();
    this->ValidateDraw(batch, indexed);

    int32_t baseIndex = 0;
    if (!this->m_caps.int10) {
        baseIndex = this->m_primVertexFormatBuf[0]->m_index / this->m_primVertexFormatBuf[0]->m_itemSize;
    }

    if (indexed) {
        this->m_d3dDevice->DrawIndexedPrimitive(
            CGxDeviceD3d::s_primitiveConversion[batch->m_primType],
            baseIndex,
            batch->m_minIndex,
            batch->m_maxIndex - batch->m_minIndex + 1,
            batch->m_start + (this->m_primIndexBuf->m_index / 2),
            CGxDevice::PrimCalcCount(batch->m_primType, batch->m_count)
        );
    } else {
        this->m_d3dDevice->DrawPrimitive(
            CGxDeviceD3d::s_primitiveConversion[batch->m_primType],
            baseIndex,
            CGxDevice::PrimCalcCount(batch->m_primType, batch->m_count)
        );
    }
}

// ref: FUN_006a3c40
// One flat switch from frozen's device-state enum onto the D3D render and sampler states, with
// the same early-out when the cached value already matches -- the reference's shape exactly,
// including the per-stage blocks of sixteen.
void CGxDeviceD3d::DsSet(EDeviceState state, uint32_t val) {
    if (this->m_deviceStates[state] == val) {
        return;
    }

    // COMPLETED 2026-10-01 from FUN_006a3c40. The enum already mirrored the reference's indices
    // one for one, but this switch sent only the blend factors, the three filters, U/V wrap and
    // nine render states; everything else -- max anisotropy, texture-transform flags, the
    // coordinate index, the stage combiners and their arguments, the material sources, ambient,
    // fog, lighting, specular, clip planes and point scale -- was cached here and never sent.
    if (state >= Ds_TssMagFilter0 && state <= Ds_TssMagFilter15) {
        this->m_d3dDevice->SetSamplerState(state - Ds_TssMagFilter0, D3DSAMP_MAGFILTER, val);
    } else if (state >= Ds_TssMinFilter0 && state <= Ds_TssMinFilter15) {
        this->m_d3dDevice->SetSamplerState(state - Ds_TssMinFilter0, D3DSAMP_MINFILTER, val);
    } else if (state >= Ds_TssMipFilter0 && state <= Ds_TssMipFilter15) {
        this->m_d3dDevice->SetSamplerState(state - Ds_TssMipFilter0, D3DSAMP_MIPFILTER, val);
    } else if (state >= Ds_TssWrapU0 && state <= Ds_TssWrapU15) {
        this->m_d3dDevice->SetSamplerState(state - Ds_TssWrapU0, D3DSAMP_ADDRESSU, val);
    } else if (state >= Ds_TssWrapV0 && state <= Ds_TssWrapV15) {
        this->m_d3dDevice->SetSamplerState(state - Ds_TssWrapV0, D3DSAMP_ADDRESSV, val);
    } else if (state >= Ds_TssTTF0 && state <= Ds_TssTTF7) {
        this->m_d3dDevice->SetTextureStageState(state - Ds_TssTTF0, D3DTSS_TEXTURETRANSFORMFLAGS, val);
    } else if (state >= Ds_TssMaxAnisotropy0 && state <= Ds_TssMaxAnisotropy15) {
        this->m_d3dDevice->SetSamplerState(state - Ds_TssMaxAnisotropy0, D3DSAMP_MAXANISOTROPY, val);
    } else if (state >= Ds_TssTexCoordIndex0 && state <= Ds_TssTexCoordIndex7) {
        this->m_d3dDevice->SetTextureStageState(state - Ds_TssTexCoordIndex0, D3DTSS_TEXCOORDINDEX, val);
    } else if (state >= Ds_TssColorOp0 && state <= Ds_TssColorOp7) {
        this->m_d3dDevice->SetTextureStageState(state - Ds_TssColorOp0, D3DTSS_COLOROP, val);
    } else if (state >= Ds_TssAlphaOp0 && state <= Ds_TssAlphaOp7) {
        this->m_d3dDevice->SetTextureStageState(state - Ds_TssAlphaOp0, D3DTSS_ALPHAOP, val);
    } else if (state >= Ds_TssColorArg10 && state <= Ds_TssColorArg17) {
        this->m_d3dDevice->SetTextureStageState(state - Ds_TssColorArg10, D3DTSS_COLORARG1, val);
    } else if (state >= Ds_TssColorArg20 && state <= Ds_TssColorArg27) {
        this->m_d3dDevice->SetTextureStageState(state - Ds_TssColorArg20, D3DTSS_COLORARG2, val);
    } else if (state >= Ds_TssAlphaArg10 && state <= Ds_TssAlphaArg17) {
        this->m_d3dDevice->SetTextureStageState(state - Ds_TssAlphaArg10, D3DTSS_ALPHAARG1, val);
    } else if (state >= Ds_TssAlphaArg20 && state <= Ds_TssAlphaArg27) {
        this->m_d3dDevice->SetTextureStageState(state - Ds_TssAlphaArg20, D3DTSS_ALPHAARG2, val);
    } else {
        switch (state) {
            case Ds_SrcBlend:
                this->m_d3dDevice->SetRenderState(D3DRS_SRCBLEND, val);
                break;
            case Ds_DstBlend:
                this->m_d3dDevice->SetRenderState(D3DRS_DESTBLEND, val);
                break;
            case Ds_AmbientMaterialSource:
                this->m_d3dDevice->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, val);
                break;
            case Ds_DiffuseMaterialSource:
                this->m_d3dDevice->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, val);
                break;
            case Ds_SpecularMaterialSource:
                this->m_d3dDevice->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, val);
                break;
            case Ds_EmissiveMaterialSource:
                this->m_d3dDevice->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, val);
                break;
            case Ds_Ambient:
                this->m_d3dDevice->SetRenderState(D3DRS_AMBIENT, val);
                break;
            case Ds_AlphaBlendEnable:
                this->m_d3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, val);
                break;
            case Ds_AlphaTestEnable:
                this->m_d3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, val);
                break;
            case Ds_AlphaRef:
                this->m_d3dDevice->SetRenderState(D3DRS_ALPHAREF, val);
                break;
            case Ds_FogEnable:
                this->m_d3dDevice->SetRenderState(D3DRS_FOGENABLE, val);
                break;
            case Ds_ZWriteEnable:
                this->m_d3dDevice->SetRenderState(D3DRS_ZWRITEENABLE, val);
                break;
            case Ds_ColorWriteEnable: {
                // Takes the Gx channel mask (red 1, blue 2, green 4, alpha 8) and remaps it.
                uint32_t mask = (val & 0x1) ? D3DCOLORWRITEENABLE_RED : 0;

                if (val & 0x4) {
                    mask |= D3DCOLORWRITEENABLE_GREEN;
                }

                if (val & 0x2) {
                    mask |= D3DCOLORWRITEENABLE_BLUE;
                }

                if (val & 0x8) {
                    mask |= D3DCOLORWRITEENABLE_ALPHA;
                }

                this->m_d3dDevice->SetRenderState(D3DRS_COLORWRITEENABLE, mask);
                break;
            }
            case Ds_Lighting:
                this->m_d3dDevice->SetRenderState(D3DRS_LIGHTING, val);
                break;
            case Ds_SpecularEnable:
                this->m_d3dDevice->SetRenderState(D3DRS_SPECULARENABLE, val);
                break;
            case Ds_CullMode:
                this->m_d3dDevice->SetRenderState(D3DRS_CULLMODE, val);
                break;
            case Ds_ClipPlaneEnable:
                this->m_d3dDevice->SetRenderState(D3DRS_CLIPPLANEENABLE, val);
                break;
            case Ds_ZFunc:
                this->m_d3dDevice->SetRenderState(D3DRS_ZFUNC, val);
                break;
            case Ds_PointScaleA:
                this->m_d3dDevice->SetRenderState(D3DRS_POINTSCALE_A, val);
                break;
            case Ds_PointScaleB:
                this->m_d3dDevice->SetRenderState(D3DRS_POINTSCALE_B, val);
                break;
            case Ds_PointScaleC:
                this->m_d3dDevice->SetRenderState(D3DRS_POINTSCALE_C, val);
                break;
            case Ds_PointScaleEnable:
                this->m_d3dDevice->SetRenderState(D3DRS_POINTSCALEENABLE, val);
                break;
            default:
                break;
        }
    }

    this->m_deviceStates[state] = val;
}

// ref: FUN_0068fb10
// Every way of not getting real buffer memory -- no context, no D3D buffer, a failed lock --
// ends in the device scratch for the pool's target rather than null. The reference also guards
// the touch of the first byte with a structured exception handler and takes the same scratch
// path if it faults; frozen does not reproduce the SEH frame.
char* CGxDeviceD3d::IBufLock(CGxBuf* buf) {
    auto pool = buf->m_pool;

    if (!this->m_context) {
        return this->m_bufScratch[pool->m_target].Lock(buf->m_size);
    }

    uint32_t lockFlags = 0x0;

    if (pool->m_usage == GxPoolUsage_Stream) {
        uint32_t index = buf->m_itemSize + pool->unk1C - 1;
        index -= index % buf->m_itemSize;

        if (buf->m_size + index <= pool->m_size) {
            lockFlags = D3DLOCK_NOOVERWRITE;
            buf->m_index = index;
            pool->unk1C = buf->m_size + index;
        } else {
            lockFlags = D3DLOCK_DISCARD;
            pool->Discard();
            buf->m_index = 0;
            pool->unk1C = buf->m_size;
        }
    } else if (pool->m_usage == GxPoolUsage_Dynamic) {
        lockFlags = D3DLOCK_NOOVERWRITE;
    }

    char* data = nullptr;

    if (!pool->m_apiSpecific) {
        this->CreatePoolAPI(pool);
    }

    if (!pool->m_apiSpecific) {
        return this->m_bufScratch[pool->m_target].Lock(buf->m_size);
    }

    HRESULT lockResult = S_OK;

    if (pool->m_target == GxPoolTarget_Vertex) {
        auto d3dBuf = static_cast<LPDIRECT3DVERTEXBUFFER9>(pool->m_apiSpecific);
        lockResult = d3dBuf->Lock(buf->m_index, buf->m_size, reinterpret_cast<void**>(&data), lockFlags);
    } else if (pool->m_target == GxPoolTarget_Index) {
        auto d3dBuf = static_cast<LPDIRECT3DINDEXBUFFER9>(pool->m_apiSpecific);
        lockResult = d3dBuf->Lock(buf->m_index, buf->m_size, reinterpret_cast<void**>(&data), lockFlags);
    }

    if (SUCCEEDED(lockResult)) {
        // Touch the first byte while the lock is fresh, as the reference does.
        if (buf->m_size) {
            if (pool->m_usage == GxPoolUsage_Stream) {
                *data = 0;
            } else {
                *data = *data;
            }
        }

        return data;
    }

    this->IBufUnlock(buf);

    return this->m_bufScratch[pool->m_target].Lock(buf->m_size);
}

// ref: FUN_0068fa60
// A lock that was served from the scratch is undone there; the buffer is marked as not written
// (unk1D) and the device asks for a full re-sync (intF5C, reference +0xf5c).
void CGxDeviceD3d::IBufUnlock(CGxBuf* buf) {
    auto pool = buf->m_pool;
    auto& scratch = this->m_bufScratch[pool->m_target];

    if (scratch.m_locked) {
        scratch.Unlock();
        buf->unk1D = 0;
        this->intF5C = 1;

        return;
    }

    if (pool->m_target == GxPoolTarget_Vertex) {
        auto d3dBuf = static_cast<LPDIRECT3DVERTEXBUFFER9>(pool->m_apiSpecific);
        buf->unk1D = SUCCEEDED(d3dBuf->Unlock());
    } else if (pool->m_target == GxPoolTarget_Index) {
        auto d3dBuf = static_cast<LPDIRECT3DINDEXBUFFER9>(pool->m_apiSpecific);
        buf->unk1D = SUCCEEDED(d3dBuf->Unlock());
    } else {
        buf->unk1D = 1;
    }
}

// ref: FUN_00690680
int32_t CGxDeviceD3d::ICreateD3d() {
    if (CGxDeviceD3d::ILoadD3dLib(this->m_d3dLib, this->m_d3d) && SUCCEEDED(this->m_d3d->GetDeviceCaps(0, D3DDEVTYPE_HAL, &this->m_d3dCaps))) {
        if (this->m_desktopDisplayMode.Format != D3DFMT_UNKNOWN) {
            return 1;
        }

        D3DDISPLAYMODE displayMode;
        if (SUCCEEDED(this->m_d3d->GetAdapterDisplayMode(0, &displayMode))) {
            this->m_desktopDisplayMode.Width = displayMode.Width;
            this->m_desktopDisplayMode.Height = displayMode.Height;
            this->m_desktopDisplayMode.RefreshRate = displayMode.RefreshRate;
            this->m_desktopDisplayMode.Format = displayMode.Format;

            return 1;
        }
    }

    this->IDestroyD3dDevice();

    if (this->m_d3d) {
        this->m_d3d->Release();
        this->m_d3d = nullptr;
    }

    if (this->m_d3dLib) {
        FreeLibrary(this->m_d3dLib);
        this->m_d3dLib = nullptr;
    }

    return 0;
}

// ref: FUN_0068f3d0
// PARTIAL. Ported: hardware T&L selection, present parameters, CreateDevice with the reference's
// behaviour flags, the adapter format, ISetCaps, the D3D defaults. Not yet, all recorded in the
// roadmap's Phase 3a item 5: NVAPI stereo setup, the caps log, the gamma ramp (vtable slot 15),
// the hardware cursor (slot 2, FUN_0068e900), the 8x8 placeholder texture at +0x3b58 (callback
// FUN_0068f370) and the device's own "UI" shader sets (0x00c5dfd8, 0x00c5fffc) that the base
// ScenePresent draws the software cursor with.
int32_t CGxDeviceD3d::ICreateD3dDevice(const CGxFormat& format) {
    // TODO stereoscopic setup

    auto hwTnL = format.hwTnL;
    if (hwTnL && (this->m_d3dCaps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) == 0) {
        hwTnL = false;
    }
    this->m_d3dIsHwDevice = hwTnL;

    D3DPRESENT_PARAMETERS d3dpp;
    this->ISetPresentParms(d3dpp, format);

    uint32_t behaviorFlags = hwTnL
        ? D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE | D3DCREATE_FPU_PRESERVE
        : D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE;

    if (SUCCEEDED(this->m_d3d->CreateDevice(0, D3DDEVTYPE_HAL, this->m_hwnd, behaviorFlags, &d3dpp, &this->m_d3dDevice))) {
        // TODO

        this->m_devAdapterFormat = d3dpp.BackBufferFormat;
        this->m_context = 1;

        // TODO

        this->ISetCaps(format);

        // TODO logs

        this->IStateSetD3dDefaults();

        // TODO

        return 1;
    }

    this->m_d3dDevice = nullptr;
    return 0;
}

// ref: FUN_0068e180
LPDIRECT3DINDEXBUFFER9 CGxDeviceD3d::ICreateD3dIB(EGxPoolUsage usage, uint32_t size) {
    uint32_t d3dUsage = this->m_d3dIsHwDevice ? D3DUSAGE_WRITEONLY : D3DUSAGE_SOFTWAREPROCESSING;
    D3DPOOL d3dPool = D3DPOOL_MANAGED;

    if (usage == GxPoolUsage_Dynamic || usage == GxPoolUsage_Stream) {
        d3dUsage |= D3DUSAGE_DYNAMIC;
        d3dPool = D3DPOOL_DEFAULT;
    }

    LPDIRECT3DINDEXBUFFER9 indexBuf = nullptr;

    if (SUCCEEDED(this->m_d3dDevice->CreateIndexBuffer(size, d3dUsage, D3DFMT_INDEX16, d3dPool, &indexBuf, nullptr))) {
        return indexBuf;
    }

    return nullptr;
}

// ref: FUN_0069fb00
// No FVF: the reference passes 0. This passed D3DFMT_INDEX16 (0x65) here, copied from the index
// buffer twin, which D3D reads as a set of FVF bits and fixes the buffer's vertex layout to.
LPDIRECT3DVERTEXBUFFER9 CGxDeviceD3d::ICreateD3dVB(EGxPoolUsage usage, uint32_t size) {
    uint32_t d3dUsage = this->m_d3dIsHwDevice ? D3DUSAGE_WRITEONLY : D3DUSAGE_SOFTWAREPROCESSING;
    D3DPOOL d3dPool = D3DPOOL_MANAGED;

    if (usage == GxPoolUsage_Dynamic || usage == GxPoolUsage_Stream) {
        d3dUsage |= D3DUSAGE_DYNAMIC;
        d3dPool = D3DPOOL_DEFAULT;
    }

    LPDIRECT3DVERTEXBUFFER9 vertexBuf = nullptr;

    if (SUCCEEDED(this->m_d3dDevice->CreateVertexBuffer(size, d3dUsage, 0, d3dPool, &vertexBuf, nullptr))) {
        return vertexBuf;
    }

    return nullptr;
}

LPDIRECT3DVERTEXDECLARATION9 CGxDeviceD3d::ICreateD3dVertexDecl(D3DVERTEXELEMENT9 elements[], uint32_t count) {
    if (this->m_primVertexFormat < GxVertexBufferFormats_Last) {
        for (int32_t i = 0; i < count; i++) {
            auto& element = elements[i];
            auto foo = 1;
        }

        if (!this->m_d3dVertexDecl[this->m_primVertexFormat]) {
            this->m_d3dDevice->CreateVertexDeclaration(elements, &this->m_d3dVertexDecl[this->m_primVertexFormat]);
        }

        return this->m_d3dVertexDecl[this->m_primVertexFormat];
    }

    // TODO new vertex buffer format

    return nullptr;
}

// ref: FUN_00684d70
// Fits a windowed-mode client rect into the primary monitor's work area and centres it. `rect` is
// {left, top, right, bottom} of the client area on the way in and of the client area placed on
// screen on the way out; `adjust` is the frame's extra width and height. When the window has to
// shrink and an aspect is set, it walks down from the largest size that fits to half the asked
// size looking for one whose other side is (within 0.01) a whole number, so the client keeps the
// aspect without a fractional edge; failing that it rounds. Returns 0 only when the monitor
// cannot be queried.
int32_t GxWindowFitToWorkArea(float aspect, const int32_t* adjust, int32_t* rect) {
    POINT origin = { 0, 0 };
    HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO info;
    info.cbSize = sizeof(info);

    if (!GetMonitorInfoA(monitor, &info)) {
        return 0;
    }

    float height = static_cast<float>(rect[3] - rect[1]);
    int32_t width = rect[2] - rect[0];

    rect[1] = info.rcWork.top;
    rect[0] = info.rcWork.left;
    rect[2] = adjust[0] + info.rcWork.left + width;
    rect[3] = adjust[1] + info.rcWork.top + static_cast<int32_t>(height);

    if (info.rcWork.right < rect[2]) {
        rect[2] = info.rcWork.right;

        if (aspect != 0.0f) {
            int32_t outerWidth = info.rcWork.right - info.rcWork.left;
            int32_t maxWidth = outerWidth - adjust[0];
            int32_t minWidth = width / 2;
            float invAspect = 1.0f / aspect;
            float clientHeight = static_cast<float>(maxWidth) / aspect + 0.5f;

            for (int32_t w = maxWidth; minWidth <= w; w--) {
                float h = static_cast<float>(w) * invAspect;

                if (h - floorf(h) < 0.01f) {
                    outerWidth = adjust[0] + w;
                    clientHeight = static_cast<float>(w) * invAspect;
                    break;
                }
            }

            rect[2] = outerWidth + info.rcWork.left;
            rect[3] = static_cast<int32_t>(floorf(clientHeight)) + adjust[1] + info.rcWork.top;
        }
    }

    if (info.rcWork.bottom < rect[3]) {
        rect[3] = info.rcWork.bottom;

        if (aspect != 0.0f) {
            float outerHeight = static_cast<float>(info.rcWork.bottom - info.rcWork.top);
            int32_t minHeight = static_cast<int32_t>(height) / 2;
            int32_t maxHeight = static_cast<int32_t>(outerHeight) - adjust[1];
            int32_t clientWidth = static_cast<int32_t>(floorf(static_cast<float>(maxHeight) * aspect + 0.5f));
            float bottom = outerHeight;

            for (int32_t h = maxHeight; minHeight <= h; h--) {
                float w = static_cast<float>(h) * aspect;

                if (w - floorf(w) < 0.01f) {
                    bottom = static_cast<float>(adjust[1] + h);
                    clientWidth = static_cast<int32_t>(floorf(static_cast<float>(h) * aspect));
                    break;
                }
            }

            rect[2] = clientWidth + adjust[0] + info.rcWork.left;
            rect[3] = static_cast<int32_t>(bottom) + info.rcWork.top;
        }
    }

    int32_t dx = (info.rcWork.right - rect[2]) / 2;
    rect[2] += dx;
    rect[0] = dx + info.rcWork.left;

    int32_t dy = (info.rcWork.bottom - rect[3]) / 2;
    rect[3] += dy;
    rect[1] = info.rcWork.top + dy;

    rect[2] -= adjust[0];
    rect[3] -= adjust[1];

    return 1;
}

// ref: FUN_0068ebb0
// Ported 2026-10-01. What was missing: a windowed format is now fitted into the work area and
// centred (GxWindowFitToWorkArea), holding gxAspect's ratio, and a maximized one (gxMaximize 1)
// takes the screen size; both write back into `format`, which the caller then stores.
bool CGxDeviceD3d::ICreateWindow(CGxFormat& format) {
    auto instance = GetModuleHandle(nullptr);

    DWORD dwStyle;
    if (format.window == 0) {
        dwStyle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_SYSMENU;
    } else if (format.maximize == 1) {
        dwStyle = WS_POPUP | WS_VISIBLE;
    } else if (format.maximize == 2) {
        dwStyle = WS_POPUP;
    } else {
        dwStyle = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    }

    float aspect = 0.0f;

    if (format.aspect && format.size.x && format.size.y) {
        aspect = static_cast<float>(format.size.x) / static_cast<float>(format.size.y);
    }

    RECT clientArea = {
        0,             // left
        0,             // top
        format.size.x, // right
        format.size.y  // bottom
    };
    AdjustWindowRectEx(&clientArea, dwStyle, false, 0);
    CGxDeviceD3d::s_clientAdjustWidth = clientArea.right - format.size.x - clientArea.left;
    CGxDeviceD3d::s_clientAdjustHeight = clientArea.bottom - format.size.y - clientArea.top;

    if (format.window) {
        if (format.maximize == 1) {
            format.pos.x = 0;
            format.pos.y = 0;
            format.size.x = GetSystemMetrics(SM_CXSCREEN);
            format.size.y = GetSystemMetrics(SM_CYSCREEN);
        } else {
            int32_t adjust[2] = { CGxDeviceD3d::s_clientAdjustWidth, CGxDeviceD3d::s_clientAdjustHeight };
            int32_t rect[4] = { 0, 0, format.size.x, format.size.y };

            if (!GxWindowFitToWorkArea(aspect, adjust, rect)) {
                format.pos.x = 0;
                format.pos.y = 0;
            } else {
                format.pos.x = rect[0];
                format.pos.y = rect[1];
                format.size.x = rect[2] - rect[0];
                format.size.y = rect[3] - rect[1];
            }
        }
    }

    int32_t width = format.size.x ? format.size.x : CW_USEDEFAULT;
    int32_t height = format.size.y ? format.size.y : CW_USEDEFAULT;

    if (format.window && format.maximize != 1 && format.size.x && format.size.y) {
        width += CGxDeviceD3d::s_clientAdjustWidth;
        height += CGxDeviceD3d::s_clientAdjustHeight;
    }

    this->m_hwnd = CreateWindowEx(
        WS_EX_APPWINDOW,
        TEXT("GxWindowClassD3d"),
        TEXT("World of Warcraft"),
        dwStyle,
        format.pos.x,
        format.pos.y,
        width,
        height,
        nullptr,
        nullptr,
        instance,
        this
    );

    if (this->m_hwnd && format.maximize != 2) {
        ShowWindow(this->m_hwnd, SW_SHOWNORMAL);
    }

    return this->m_hwnd != nullptr;
}


// ref: FUN_006903b0
// Everything the device created is released before the device itself, so DeviceSetFormat can
// build a new one. This was an empty TODO: a format change created a second device beside the
// first and kept drawing with the first one's textures, buffers and shaders.
//
// Not ported, recorded: the reference first destroys the two default shader sets ICreateD3dDevice
// loads (Shaders\Vertex, Shaders\Pixel), calls the cursor teardown (vtable slot 3,
// FUN_006a00c0), destroys the 8x8 placeholder texture at +0x3b58 and releases the NVAPI stereo
// handle. Frozen creates none of those yet.
void CGxDeviceD3d::IDestroyD3dDevice() {
    this->IReleaseD3dResources(1);

    // FUN_006a5680: the fourteen cached vertex declarations.
    for (uint32_t i = 0; i < GxVertexBufferFormats_Last; i++) {
        if (this->m_d3dVertexDecl[i]) {
            this->m_d3dVertexDecl[i]->Release();
            this->m_d3dVertexDecl[i] = nullptr;
        }
    }

    this->m_d3dCurrentVertexDecl = nullptr;

    if (this->m_d3dDevice) {
        this->m_d3dDevice->Release();
        this->m_d3dDevice = nullptr;
    }
}

// ref: FUN_006a5e40
// On a full release, every pixel and vertex shader drops its D3D object and is marked unloaded;
// the next bind re-creates it on whatever device exists then.
void CGxDeviceD3d::IReleaseD3dShaders(int32_t all) {
    if (!all) {
        return;
    }

    for (auto shader = this->m_shaderList[GxSh_Pixel].Head(); shader; shader = this->m_shaderList[GxSh_Pixel].Next(shader)) {
        if (shader->apiSpecific) {
            static_cast<IUnknown*>(shader->apiSpecific)->Release();
            shader->apiSpecific = nullptr;
            shader->loaded = 0;
        }
    }

    for (auto shader = this->m_shaderList[GxSh_Vertex].Head(); shader; shader = this->m_shaderList[GxSh_Vertex].Next(shader)) {
        if (shader->apiSpecific) {
            static_cast<IUnknown*>(shader->apiSpecific)->Release();
            shader->apiSpecific = nullptr;
            shader->loaded = 0;
        }
    }
}

// The per-pool half of IReleaseD3dPools, lifted out unchanged so that PoolDestroy and the
// device-lost walk release a pool the same way rather than by two copies of the same code.
// ref: FUN_0068e1f0
void CGxDeviceD3d::IPoolRelease(CGxPool* pool) {
    if (pool->m_usage == GxPoolUsage_Stream) {
        pool->unk1C = 0;
    }

    pool->Invalidate();

    if (pool->m_apiSpecific) {
        if (pool->m_target == GxPoolTarget_Vertex) {
            auto d3dBuf = static_cast<LPDIRECT3DVERTEXBUFFER9>(pool->m_apiSpecific);
            d3dBuf->Release();
        } else if (pool->m_target == GxPoolTarget_Index) {
            auto d3dBuf = static_cast<LPDIRECT3DINDEXBUFFER9>(pool->m_apiSpecific);
            d3dBuf->Release();
        }

        pool->m_apiSpecific = nullptr;
    }
}

// ref: FUN_006a1c60
void CGxDeviceD3d::IReleaseD3dPools(int32_t a2) {
    for (auto pool = this->m_poolList.Head(); pool; pool = this->m_poolList.Next(pool)) {
        if (!a2) {
            if (pool->m_usage != GxPoolUsage_Dynamic && pool->m_usage != GxPoolUsage_Stream) {
                continue;
            }
        }

        if (pool->m_usage == GxPoolUsage_Stream) {
            pool->unk1C = 0;
        }

        this->IPoolRelease(pool);
    }
}

// ref: FUN_006a2aa0
// Direct3D destroys every D3DPOOL_DEFAULT resource on a device reset, and a render target has to
// live there. The reference walks the device's texture list and, for every texture holding a D3D
// object -- every one when `all` is set, otherwise only render targets -- releases it, marks it
// for re-creation and a full upload, and tells its owner (command 3) that the contents are gone.
// This replaces a tracker frozen kept of its own for the same purpose (the 2026-09 resize fix:
// a render target that outlived the reset made Reset fail with D3DERR_INVALIDCALL).
//
// One divergence, recorded here: the reference stores the device field at +0x3b58 (the 8x8
// placeholder texture) into the texture where frozen stores null until that texture exists.
void CGxDeviceD3d::IReleaseD3dTextures(int32_t all) {
    static CiRect s_emptyRect = { 0, 0, 0, 0 };

    for (auto texId = this->m_texList.Head(); texId; texId = this->m_texList.Next(texId)) {
        if (!texId->m_apiSpecificData) {
            continue;
        }

        if (!all && !texId->m_flags.m_renderTarget) {
            continue;
        }

        if (!texId->m_needsCreation && (texId->m_apiSpecificData || texId->m_apiSpecificData2)) {
            static_cast<IUnknown*>(texId->m_apiSpecificData)->Release();
        }

        texId->m_apiSpecificData = nullptr;
        texId->m_needsCreation = 1;

        this->TexMarkForUpdate(texId, s_emptyRect, 0);

        // The reference calls it unconditionally; every texture it reaches there has one.
        if (texId->m_userFunc) {
            uint32_t texelStrideInBytes;
            const void* texels;
            texId->m_userFunc(static_cast<EGxTexCommand>(3), texId->m_width, texId->m_height, 0, 0,
                texId->m_userArg, texelStrideInBytes, texels);
        }
    }

    if (this->m_hwnd && this->m_d3dDevice) {
        this->ICallbacksTexturesLost();
    }
}

// ref: FUN_006a2bb0
// Releases the D3D object, then hands the texture to the device to unlink and free. Before this
// override existed every destroyed texture leaked its D3D object.
void CGxDeviceD3d::TexDestroy(CGxTex* texId) {
    if (!texId->m_needsCreation && (texId->m_apiSpecificData || texId->m_apiSpecificData2)) {
        static_cast<IUnknown*>(texId->m_apiSpecificData)->Release();
        texId->m_apiSpecificData = nullptr;
    }

    CGxDevice::TexDestroy(texId);
}

// ref: FUN_00690150
void CGxDeviceD3d::IReleaseD3dResources(int32_t a2) {
    static int32_t releases = 0;

    // Only the first few are interesting; printing every call is what made the runaway retry loop
    // visible in the first place (4847 calls in one session), but it floods the log after that.
    if (++releases <= 8) {
        fprintf(stderr, "IReleaseD3dResources call %d\n", releases);
    }

    // Unbind first. A surface that is still the device's render target or depth-stencil keeps a
    // reference alive no matter how many times its texture is released, and Reset then fails with
    // D3DERR_INVALIDCALL -- which is exactly what was happening: the shadow map stayed bound, the
    // reset failed once, m_context was cleared, and the client rendered for ever without presenting.
    if (this->m_d3dDevice) {
        if (this->m_defColorSurface) {
            this->m_d3dDevice->SetRenderTarget(0, this->m_defColorSurface);
        }

        this->m_d3dDevice->SetDepthStencilSurface(this->m_defDepthSurface);

        // Textures hold references too; drop every stage before releasing the objects behind them.
        for (uint32_t stage = 0; stage < 8; stage++) {
            this->m_d3dDevice->SetTexture(stage, nullptr);
        }

        // Same for the vertex and index buffers: releasing a buffer that is still SET as a stream
        // source or index buffer does not drop its last reference, and Reset then fails with
        // INVALIDCALL exactly as a bound render target does. Clear the bindings, and the device's
        // cached copies of them, so the pool release below really is the last reference.
        for (uint32_t stream = 0; stream < 8; stream++) {
            this->m_d3dDevice->SetStreamSource(stream, nullptr, 0, 0);
            this->m_d3dVertexStreamBuf[stream] = nullptr;
            this->m_d3dVertexStreamOfs[stream] = -1;
            this->m_d3dVertexStreamStride[stream] = -1;
        }

        this->m_d3dDevice->SetIndices(nullptr);
        this->m_d3dCurrentIndexBuf = nullptr;
    }

    // Then the textures the reset destroys, so it leaves no dangling handles, and on a full
    // release the shaders, as the reference does in this order.
    this->IReleaseD3dTextures(a2);
    this->IReleaseD3dShaders(a2);

    // TODO

    this->IReleaseD3dPools(a2);

    memset(this->m_deviceStates, 0xFF, sizeof(this->m_deviceStates));
    this->ResetRsSendCaches();

    if (this->m_defColorSurface) {
        this->m_defColorSurface->Release();
        this->m_defColorSurface = nullptr;
    }

    if (this->m_defDepthSurface) {
        this->m_defDepthSurface->Release();
        this->m_defDepthSurface = nullptr;
    }

    if (this->m_d3dFrameQuery) {
        this->m_d3dFrameQuery->Release();
        this->m_d3dFrameQuery = nullptr;
    }

    this->IReleaseD3dQueries();

    // TODO

    if (this->m_d3dDevice) {
        this->m_d3dDevice->ShowCursor(false);
    }
}

uint32_t CGxDeviceD3d::s_d3dNormalizeNormals = 0xFFFFFFFF;
// Reference 0x00c60760: whether the world matrix last sent to D3D was the identity, so a run of
// identity world transforms is sent once.
uint32_t CGxDeviceD3d::s_d3dWorldIdentity = 0;

void CGxDeviceD3d::ResetRsSendCaches() {
    this->m_d3dSpecularEnable = 0xFFFFFFFF;
    this->m_d3dLighting = 0xFFFFFFFF;
    this->m_d3dFogEnable = 0xFFFFFFFF;
    this->m_d3dPointScaleEnable = 0xFFFFFFFF;
    this->m_d3dColorWrite = 0xFFFFFFFF;
    CGxDeviceD3d::s_d3dNormalizeNormals = 0xFFFFFFFF;
}

// ref: FUN_006a4100
void CGxDeviceD3d::ISetTexCoordIndex(uint32_t tmu, uint32_t texGen, uint32_t index) {
    auto state = static_cast<EDeviceState>(Ds_TssTexCoordIndex0 + tmu);

    switch (texGen) {
        case 0:
            this->DsSet(state, index);
            break;
        case 1:
        case 2:
        case 3:
            this->DsSet(state, tmu | D3DTSS_TCI_CAMERASPACEPOSITION);
            break;
        case 4:
        case 6:
            this->DsSet(state, tmu | D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
            break;
        case 5:
            this->DsSet(state, tmu | D3DTSS_TCI_CAMERASPACENORMAL);
            break;
        default:
            break;
    }
}

// The colour and alpha combiner of a fixed-function stage: an op and its two arguments per
// texture op, out of the reference's tables at 0x00a2f9cc (op) and 0x00a2f9e4 (argument pairs),
// verified against the binary 2026-10-01. Ignored by D3D while shaders are bound.
static const uint32_t s_texOp[] = {
    D3DTOP_MODULATE, D3DTOP_MODULATE2X, D3DTOP_ADD, D3DTOP_SELECTARG2, D3DTOP_BLENDCURRENTALPHA,
    D3DTOP_BLENDDIFFUSEALPHA
};

static const uint32_t s_texArgs[][2] = {
    { D3DTA_TEXTURE, D3DTA_CURRENT }, { D3DTA_TEXTURE, D3DTA_CURRENT }, { D3DTA_TEXTURE, D3DTA_CURRENT },
    { D3DTA_TEXTURE, D3DTA_CURRENT }, { D3DTA_CURRENT, D3DTA_TEXTURE }, { D3DTA_TEXTURE, D3DTA_CURRENT }
};

static void UnpackD3dColor(D3DCOLORVALUE& out, uint32_t argb) {
    // 1/255 is the reference's own constant at 0x00a2f960.
    out.r = static_cast<float>((argb >> 16) & 0xFF) * 0.003921568859368563f;
    out.g = static_cast<float>((argb >> 8) & 0xFF) * 0.003921568859368563f;
    out.b = static_cast<float>(argb & 0xFF) * 0.003921568859368563f;
    out.a = static_cast<float>(argb >> 24) * 0.003921568859368563f;
}

// ref: FUN_006a4250
void CGxDeviceD3d::ISetMaterial(uint32_t diffuse, uint32_t emissive, uint32_t specular, float power) {
    D3DMATERIAL9 material;

    UnpackD3dColor(material.Diffuse, diffuse);
    material.Ambient = material.Diffuse;
    UnpackD3dColor(material.Emissive, emissive);
    UnpackD3dColor(material.Specular, specular);
    material.Power = power;

    this->m_d3dDevice->SetMaterial(&material);
}

// ref: FUN_006a4190
void CGxDeviceD3d::ISetColorOp(uint32_t tmu, uint32_t op) {
    if (tmu < static_cast<uint32_t>(this->m_caps.m_numTmus) && op < 6) {
        this->DsSet(static_cast<EDeviceState>(Ds_TssColorOp0 + tmu), s_texOp[op]);
        this->DsSet(static_cast<EDeviceState>(Ds_TssColorArg10 + tmu), s_texArgs[op][0]);
        this->DsSet(static_cast<EDeviceState>(Ds_TssColorArg20 + tmu), s_texArgs[op][1]);
    }
}

// ref: FUN_006a41f0
void CGxDeviceD3d::ISetAlphaOp(uint32_t tmu, uint32_t op) {
    if (tmu < static_cast<uint32_t>(this->m_caps.m_numTmus) && op < 6) {
        this->DsSet(static_cast<EDeviceState>(Ds_TssAlphaOp0 + tmu), s_texOp[op]);
        this->DsSet(static_cast<EDeviceState>(Ds_TssAlphaArg10 + tmu), s_texArgs[op][0]);
        this->DsSet(static_cast<EDeviceState>(Ds_TssAlphaArg20 + tmu), s_texArgs[op][1]);
    }
}

// ref: FUN_006a4af0
// Picks the stage's coordinate source, then fills the stage's texgen stack. Modes 1 and 2
// generate in camera space, so the matrix carries them back out: the inverse view, and for mode 1
// the inverse world as well. Mode 6 is the sphere map's scale-and-bias. Every other mode loads
// the identity. The mode compares are signed, as the reference's are.
void CGxDeviceD3d::ISetTexGen(uint32_t tmu, uint32_t mode) {
    if (tmu >= static_cast<uint32_t>(this->m_caps.m_numTmus)) {
        return;
    }

    this->ISetTexCoordIndex(tmu, mode,
        static_cast<uint32_t>(this->m_appRenderStates[GxRs_TexCoord0 + tmu].m_value));

    int32_t texGen = static_cast<int32_t>(mode);
    C44Matrix matrix;

    if (texGen > 0 && texGen <= 2) {
        matrix = this->m_xforms[GxXform_View].TopConst();
        matrix = matrix.AffineInverse();

        if (texGen == 1) {
            matrix = matrix * this->m_xforms[GxXform_World].TopConst().Inverse();
        }
    } else if (texGen == 6) {
        matrix.Identity();
        matrix.a0 = 0.5f;
        matrix.b1 = 0.5f;
        matrix.d0 = 0.5f;
        matrix.d1 = 0.5f;
    } else {
        this->m_texGenXforms[tmu].SetIdentity();
        return;
    }

    this->m_texGenXforms[tmu].Top() = matrix;
}

// ref: FUN_006a4ac0
void CGxDeviceD3d::ISetTexCoord(uint32_t tmu, uint32_t index) {
    if (tmu < static_cast<uint32_t>(this->m_caps.m_numTmus)) {
        this->ISetTexCoordIndex(tmu,
            static_cast<uint32_t>(this->m_appRenderStates[GxRs_TexGen0 + tmu].m_value), index);
    }
}

// ref: FUN_006a4c30
void CGxDeviceD3d::IRsSendToHw(EGxRenderState which) {
    auto state = &this->m_appRenderStates[which];

    switch (which) {
    // TODO handle all render states

    case GxRs_BlendingMode: {
        auto blendMode = static_cast<EGxBlend>(static_cast<int32_t>(state->m_value));

        if (blendMode < GxBlend_Alpha) {
            this->DsSet(Ds_AlphaBlendEnable, 0);
        } else {
            this->DsSet(Ds_AlphaBlendEnable, 1);
            this->DsSet(Ds_SrcBlend, CGxDeviceD3d::s_srcBlend[blendMode]);
            this->DsSet(Ds_DstBlend, CGxDeviceD3d::s_dstBlend[blendMode]);
        }

        break;
    }

    case GxRs_AlphaRef: {
        auto alphaRef = static_cast<int32_t>(state->m_value);

        if (alphaRef <= 0) {
            this->DsSet(Ds_AlphaTestEnable, 0);
        } else {
            this->DsSet(Ds_AlphaRef, alphaRef);
            this->DsSet(Ds_AlphaTestEnable, 1);
        }

        break;
    }

    case GxRs_DepthTest:
    case GxRs_DepthFunc: {
        auto depthTest = static_cast<uint32_t>((&this->m_appRenderStates[GxRs_DepthTest])->m_value);
        auto depthFunc = static_cast<uint32_t>((&this->m_appRenderStates[GxRs_DepthFunc])->m_value);

        auto d3dDepthFunc = D3DCMP_ALWAYS;
        if (this->MasterEnable(GxMasterEnable_DepthTest) && depthTest) {
            d3dDepthFunc = CGxDeviceD3d::s_cmpFunc[depthFunc];
        }

        this->DsSet(Ds_ZFunc, d3dDepthFunc);

        this->m_appRenderStates[GxRs_DepthTest].m_dirty = 0;
        this->m_appRenderStates[GxRs_DepthFunc].m_dirty = 0;

        break;
    }

    case GxRs_DepthWrite: {
        auto depthWrite = static_cast<uint32_t>(state->m_value);
        if (!this->MasterEnable(GxMasterEnable_DepthWrite)) {
            depthWrite = 0;
        }

        this->DsSet(Ds_ZWriteEnable, depthWrite);

        break;
    }

    // GxRs_ColorWrite reached nothing at all before this: the enum had Ds_ColorWriteEnable and
    // neither switch had a case for it, so CM2SceneRender::SetupMaterial's request to turn colour
    // writes OFF for an element with flag 0x1 was silently dropped and that pass wrote colour.
    //
    // The bit order is the part worth reading twice. Gx and D3D both use four bits, but the
    // reference's handler remaps the middle two -- Gx 0x2 becomes D3D's BLUE and Gx 0x4 becomes
    // D3D's GREEN -- so Gx orders them R, B, G, A against D3D's R, G, B, A. That matches the BGRA
    // byte order this codebase uses for colours elsewhere. The only two values frozen sets today,
    // 15 and 0, are identical under either reading, so a straight pass-through would have looked
    // right until the first partial mask.
    case GxRs_ColorWrite: {
        uint32_t colorWrite = 0;

        if (this->MasterEnable(GxMasterEnable_ColorWrite)) {
            colorWrite = static_cast<uint32_t>(state->m_value);
        }

        uint32_t mask = 0;

        if (colorWrite & 0x1) {
            mask |= D3DCOLORWRITEENABLE_RED;
        }

        if (colorWrite & 0x4) {
            mask |= D3DCOLORWRITEENABLE_GREEN;
        }

        if (colorWrite & 0x2) {
            mask |= D3DCOLORWRITEENABLE_BLUE;
        }

        if (colorWrite & 0x8) {
            mask |= D3DCOLORWRITEENABLE_ALPHA;
        }

        // Sent directly with its own cache, as the reference does at +0x3e74; DsSet's slot for
        // this state takes the Gx mask instead and remaps it itself.
        if (this->m_d3dColorWrite != mask) {
            this->m_d3dDevice->SetRenderState(D3DRS_COLORWRITEENABLE, mask);
            this->m_d3dColorWrite = mask;
        }

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

        this->DsSet(Ds_CullMode, CGxDeviceD3d::s_cullMode[cullMode]);

        break;
    }

    case GxRs_ScissorTest: {
        auto scissorTestEnable = static_cast<uint32_t>(state->m_value) != 0;
        this->m_d3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, scissorTestEnable);

        break;
    }

    case GxRs_ClipPlaneMask: {
        // Which of the six user clip planes are on, as a bit per plane, straight into
        // D3DRS_CLIPPLANEENABLE (0x98). The reference caches the last value at +0x3e84 and skips
        // the call when it has not changed, which is reproduced here.
        //
        // This state was accepted and dropped before now, like GxRs_Multisample was. It is the
        // last of three missing pieces rather than the first: the planes themselves and their sync
        // landed on 2026-09-23 (CGxDevice::ClipPlaneSet, IStateSyncClipPlanes), but nothing yet
        // RAISES this mask, and the thing that should is CM2SceneRender::SetupLighting. Its block
        // is at 0x0081fd5e in the reference -- gated on the current element's flags & 0x2
        // (M2UseClipPlanes), it copies m_curLighting->m_liquidPlane, negates all four components
        // when m_curPass is 2, calls ClipPlaneSet(0, plane), and then sets this mask to 1 (or to 0
        // down the else path at 0x0081fe4d).
        //
        // That block is NOT ported, and porting it alone would achieve nothing: CM2Lighting's
        // m_liquidPlane is never written, because the liquid-plane work in CM2Scene::Animate is
        // still a TODO -- see the note there about flag 0x40 never being set. So the chain is
        // liquid plane -> SetupLighting -> this state -> the planes, and only the last piece and
        // this one exist. This case is safe in isolation because the mask defaults to 0 and
        // nothing raises it.
        auto clipPlaneMask = static_cast<uint32_t>(state->m_value);

        if (this->m_d3dClipPlaneEnable != clipPlaneMask) {
            this->m_d3dDevice->SetRenderState(D3DRS_CLIPPLANEENABLE, clipPlaneMask);
            this->m_d3dClipPlaneEnable = clipPlaneMask;
        }

        break;
    }

    case GxRs_Multisample: {
        // The reference's case for this reads the state and sends a plain boolean:
        //   `xorl %ecx,%ecx; cmpl %ecx,(%edi); setne %cl; push %ecx; push $0xa1` at 0x006a5126.
        // 0xa1 is D3DRS_MULTISAMPLEANTIALIAS.
        //
        // frozen accepted this state and dropped it: nothing in any backend handled GxRs_Multisample,
        // so CGxDevice's default of 1 never reached the device and the world render's own
        // GxRsSet(GxRs_Multisample, 1) would have been a no-op too. docs/world-render-inventory.md
        // recorded that gap as "missing (GxRsSet 0x13)" on the viewport/state-push row; 0x13 is 19,
        // which is this state.
        //
        // It only does anything when the device was created with a multisample type, so on a client
        // with gxMultisample at 1 this changes nothing.
        auto multisampleEnable = static_cast<uint32_t>(state->m_value) != 0;
        this->m_d3dDevice->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, multisampleEnable);

        break;
    }

    case GxRs_PolygonOffset: {
        // Depth bias for coplanar decals. The reference sets one state, negated, and only when the
        // device reports the capability; it never touches D3DRS_SLOPESCALEDEPTHBIAS. Used only by
        // decal-style draws (footprints, post-liquid decals, projected decals) -- models set it to
        // zero. Until now this state was accepted and silently dropped by every D3D draw.
        if (this->m_caps.m_depthBias) {
            float offset = -static_cast<float>(state->m_value);
            this->m_d3dDevice->SetRenderState(D3DRS_DEPTHBIAS, *reinterpret_cast<DWORD*>(&offset));
        }

        break;
    }

    // Gated by the master enable (0x2) and cached at +0x3e6c, as the reference does it; frozen
    // sent the app value straight through until 2026-10-01.
    case GxRs_Fog: {
        uint32_t fogEnable = 0;

        if (this->MasterEnable(GxMasterEnable_Fog)) {
            fogEnable = static_cast<uint32_t>(state->m_value) != 0 ? 1 : 0;
        }

        if (this->m_d3dFogEnable != fogEnable) {
            this->m_d3dDevice->SetRenderState(D3DRS_FOGENABLE, fogEnable);
            this->m_d3dFogEnable = fogEnable;
        }

        break;
    }

    case GxRs_FogColor: {
        this->m_d3dDevice->SetRenderState(D3DRS_FOGCOLOR, static_cast<uint32_t>(state->m_value));

        break;
    }

    case GxRs_FogStart: {
        float fogStart = static_cast<float>(state->m_value);
        this->m_d3dDevice->SetRenderState(D3DRS_FOGSTART, *reinterpret_cast<DWORD*>(&fogStart));

        break;
    }

    case GxRs_FogEnd: {
        float fogEnd = static_cast<float>(state->m_value);
        this->m_d3dDevice->SetRenderState(D3DRS_FOGEND, *reinterpret_cast<DWORD*>(&fogEnd));

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
        this->IShaderBindVertex(shader);

        break;
    }

    case GxRs_PixelShader: {
        auto shader = static_cast<CGxShader*>(static_cast<void*>(state->m_value));
        this->IShaderBindPixel(shader);

        break;
    }

    // 2026-10-01: the cases below complete the reference's switch (FUN_006a4c30, 77 cases).

    // The fixed-function material, sent whole whenever any of its four states changes, and
    // all four marked clean together. Specular is enabled only for a positive exponent and a
    // non-black specular colour.
    case GxRs_MatDiffuse:
    case GxRs_MatEmissive:
    case GxRs_MatSpecular:
    case GxRs_MatSpecularExp: {
        auto& rs = this->m_appRenderStates;

        this->ISetMaterial(
            static_cast<uint32_t>(rs[GxRs_MatDiffuse].m_value),
            static_cast<uint32_t>(rs[GxRs_MatEmissive].m_value),
            static_cast<uint32_t>(rs[GxRs_MatSpecular].m_value),
            static_cast<float>(rs[GxRs_MatSpecularExp].m_value));

        rs[GxRs_MatDiffuse].m_dirty = 0;
        rs[GxRs_MatEmissive].m_dirty = 0;
        rs[GxRs_MatSpecular].m_dirty = 0;
        rs[GxRs_MatSpecularExp].m_dirty = 0;

        uint32_t specular = (static_cast<float>(rs[GxRs_MatSpecularExp].m_value) > 0.0f
            && static_cast<uint32_t>(rs[GxRs_MatSpecular].m_value) != 0) ? 1 : 0;

        if (this->m_d3dSpecularEnable != specular) {
            this->m_d3dDevice->SetRenderState(D3DRS_SPECULARENABLE, specular);
            this->m_d3dSpecularEnable = specular;
        }

        break;
    }

    case GxRs_NormalizeNormals: {
        auto normalize = static_cast<uint32_t>(state->m_value);

        if (CGxDeviceD3d::s_d3dNormalizeNormals != normalize) {
            this->m_d3dDevice->SetRenderState(D3DRS_NORMALIZENORMALS, normalize);
            CGxDeviceD3d::s_d3dNormalizeNormals = normalize;
        }

        break;
    }

    // Off whenever the master enable says so, whatever the app asked for.
    case GxRs_Lighting: {
        uint32_t lighting = 0;

        if (this->MasterEnable(GxMasterEnable_Lighting)) {
            lighting = static_cast<uint32_t>(state->m_value) != 0 ? 1 : 0;
        }

        if (this->m_d3dLighting != lighting) {
            this->m_d3dDevice->SetRenderState(D3DRS_LIGHTING, lighting);
            this->m_d3dLighting = lighting;
        }

        break;
    }

    case GxRs_ColorOp0:
    case GxRs_ColorOp1:
    case GxRs_ColorOp2:
    case GxRs_ColorOp3:
    case GxRs_ColorOp4:
    case GxRs_ColorOp5:
    case GxRs_ColorOp6:
    case GxRs_ColorOp7: {
        this->ISetColorOp(which - GxRs_ColorOp0, static_cast<uint32_t>(state->m_value));
        break;
    }

    case GxRs_AlphaOp0:
    case GxRs_AlphaOp1:
    case GxRs_AlphaOp2:
    case GxRs_AlphaOp3:
    case GxRs_AlphaOp4:
    case GxRs_AlphaOp5:
    case GxRs_AlphaOp6:
    case GxRs_AlphaOp7: {
        this->ISetAlphaOp(which - GxRs_AlphaOp0, static_cast<uint32_t>(state->m_value));
        break;
    }

    case GxRs_TexGen0:
    case GxRs_TexGen1:
    case GxRs_TexGen2:
    case GxRs_TexGen3:
    case GxRs_TexGen4:
    case GxRs_TexGen5:
    case GxRs_TexGen6:
    case GxRs_TexGen7: {
        this->ISetTexGen(which - GxRs_TexGen0, static_cast<uint32_t>(state->m_value));
        break;
    }

    case GxRs_TexCoord0:
    case GxRs_TexCoord1:
    case GxRs_TexCoord2:
    case GxRs_TexCoord3:
    case GxRs_TexCoord4:
    case GxRs_TexCoord5:
    case GxRs_TexCoord6:
    case GxRs_TexCoord7: {
        this->ISetTexCoord(which - GxRs_TexCoord0, static_cast<uint32_t>(state->m_value));
        break;
    }

    // Point size, in pixels. With attenuation on (the coefficients differ from (1, 0, 0)) it is
    // divided by the viewport's pixel height, which is what D3D's scaled size expects.
    case GxRs_PointScale: {
        float size = static_cast<float>(state->m_value);
        auto& attenuation = this->m_appRenderStates[GxRs_PointScaleAttenuation].m_value;

        if (attenuation.m_data.f[0] != 1.0f || attenuation.m_data.f[1] != 0.0f || attenuation.m_data.f[2] != 0.0f) {
            float height = this->DeviceCurWindow().maxY;
            size = size / ((1.0f - this->m_viewport.y.l) * height - (1.0f - this->m_viewport.y.h) * height);
        }

        this->m_d3dDevice->SetRenderState(D3DRS_POINTSIZE, *reinterpret_cast<DWORD*>(&size));

        break;
    }

    case GxRs_PointScaleAttenuation: {
        auto& v = state->m_value.m_data;
        uint32_t enable = (v.f[0] != 1.0f || v.f[1] != 0.0f || v.f[2] != 0.0f) ? 1 : 0;

        if (this->m_d3dPointScaleEnable != enable) {
            this->m_d3dDevice->SetRenderState(D3DRS_POINTSCALEENABLE, enable);
            this->m_d3dPointScaleEnable = enable;
        }

        if (enable) {
            this->DsSet(Ds_PointScaleA, v.u[0]);
            this->DsSet(Ds_PointScaleB, v.u[1]);
            this->DsSet(Ds_PointScaleC, v.u[2]);
        }

        break;
    }

    case GxRs_PointScaleMin: {
        float size = static_cast<float>(state->m_value);
        this->m_d3dDevice->SetRenderState(D3DRS_POINTSIZE_MIN, *reinterpret_cast<DWORD*>(&size));
        break;
    }

    case GxRs_PointScaleMax: {
        float size = static_cast<float>(state->m_value);
        this->m_d3dDevice->SetRenderState(D3DRS_POINTSIZE_MAX, *reinterpret_cast<DWORD*>(&size));
        break;
    }

    case GxRs_PointSprite: {
        this->m_d3dDevice->SetRenderState(D3DRS_POINTSPRITEENABLE, static_cast<float>(state->m_value) != 0.0f);
        break;
    }

    // The constant blend colour, a grey: one byte, rounded from 0..1, in all four channels.
    case GxRs_BlendFactor: {
        uint32_t grey = static_cast<uint32_t>(static_cast<int32_t>(roundf(static_cast<float>(state->m_value) * 255.0f))) & 0xFF;
        this->m_d3dDevice->SetRenderState(D3DRS_BLENDFACTOR, (((grey << 8) | grey) << 8 | grey) << 8 | grey);
        break;
    }

    default:
        break;
    }
}

// ref: FUN_006a3350
// Recovers a lost device before beginning the scene: once D3D reports the device can be reset,
// everything in the default pool is released, the device is reset with the current format, the
// defaults are re-sent and the window is marked active again. Frozen used to retry on D3D_OK as
// well, to escape a hang whose real cause was a render target surviving the reset; the texture
// list walk releases those now, and retrying on OK would re-enter from IStateSetD3dDefaults.
// Not ported, recorded: the stereo-dirty flag (+0x3acc) and the device-restored callbacks
// (vtable slot 5, FUN_006843b0).
void CGxDeviceD3d::ISceneBegin() {
    if (!this->m_context) {
        HRESULT coop = this->m_d3dDevice ? this->m_d3dDevice->TestCooperativeLevel() : D3DERR_DEVICELOST;

        if (coop == D3DERR_DEVICENOTRESET) {
            this->IReleaseD3dResources(0);

            D3DPRESENT_PARAMETERS d3dpp;
            this->ISetPresentParms(d3dpp, this->m_format);

            if (SUCCEEDED(this->m_d3dDevice->Reset(&d3dpp))) {
                this->IStateSetD3dDefaults();
                this->IWindowActiveSet(1);
                this->m_context = 1;
                this->intF5C = 0;
                this->ICallbacksRestored();
            }
        }

        if (!this->m_context) {
            return;
        }
    }

    this->ShaderConstantsClear();

    if (SUCCEEDED(this->m_d3dDevice->BeginScene())) {
        this->m_inScene = 1;
    }
}

// ref: FUN_0069fe10
// Records whether the window is active (intF64, reference +0xf64), which the frame cap reads; on
// activation the cursor image is re-sent and, in fullscreen, the cursor is clipped to the window.
void CGxDeviceD3d::IWindowActiveSet(int32_t active) {
    this->intF64 = active;

    if (active) {
        this->m_cursorDirty = 1;

        if (!this->m_format.window) {
            RECT windowRect;
            GetWindowRect(this->m_hwnd, &windowRect);
            ClipCursor(&windowRect);
        }
    }
}

// ref: FUN_006a3420
void CGxDeviceD3d::ISceneEnd() {
    if (this->m_inScene) {
        this->m_d3dDevice->EndScene();
        this->m_inScene = 0;
    }
}

// ref: FUN_0068ee20
// Ported 2026-10-01 from the decompilation, field by field against D3DCAPS9. Gained: the
// render-target format checks, vertex shader constant count, colour write, clip planes, hardware
// cursor, occlusion queries, point sprites, the blend factor, the shader-target clamps from the
// format (a fixed-function format clamps both to none), the ps_3_0-needs-vs_3_0 rule, the
// GeForce FX constant cap, the GeForce non-pow2 rule, and the pre-vs_2_0 attribute remap.
void CGxDeviceD3d::ISetCaps(const CGxFormat& format) {
    auto& caps = this->m_caps;
    auto& d3dCaps = this->m_d3dCaps;

    caps.m_numTmus = d3dCaps.MaxSimultaneousTextures > 7 ? 8 : d3dCaps.MaxSimultaneousTextures;
    caps.m_pixelCenterOnEdge = 0;
    caps.m_texelCenterOnEdge = 1;

    uint32_t maxTextureWidth = d3dCaps.MaxTextureWidth;
    caps.m_texMaxSize[GxTex_2d] = std::max(maxTextureWidth, 256u);
    caps.m_texMaxSize[GxTex_CubeMap] = std::max(maxTextureWidth, 256u);
    caps.m_texMaxSize[GxTex_Rectangle] = std::max(maxTextureWidth, 256u);
    caps.m_texMaxSize[GxTex_NonPow2] = std::max(maxTextureWidth, 256u);

    caps.m_maxIndex = d3dCaps.MaxVertexIndex;

    caps.m_texFilterTrilinear = (d3dCaps.TextureFilterCaps & D3DPTFILTERCAPS_MIPFLINEAR) != 0;
    caps.m_texFilterAnisotropic = (d3dCaps.TextureFilterCaps & (D3DPTFILTERCAPS_MINFANISOTROPIC | D3DPTFILTERCAPS_MAGFANISOTROPIC)) != 0;

    if (d3dCaps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC) {
        CGxDeviceD3d::s_filterModes[GxTex_Anisotropic][0] = D3DTEXF_ANISOTROPIC;
    }

    if (d3dCaps.TextureFilterCaps & D3DPTFILTERCAPS_MAGFANISOTROPIC) {
        CGxDeviceD3d::s_filterModes[GxTex_Anisotropic][1] = D3DTEXF_ANISOTROPIC;
    }

    caps.m_maxTexAnisotropy = d3dCaps.MaxAnisotropy;

    if (caps.m_texFilterAnisotropic && d3dCaps.MaxAnisotropy < 2) {
        caps.m_texFilterAnisotropic = 0;
    }

    caps.m_depthBias = (d3dCaps.RasterCaps & D3DPRASTERCAPS_DEPTHBIAS) != 0;
    caps.m_numStreams = d3dCaps.MaxStreams;
    caps.int10 = d3dCaps.Caps2 & 1;

    for (int32_t i = 0; i < GxShTargets_Last; i++) {
        caps.m_shaderTargets[i] = 0;
        caps.m_shaderConsts[i] = 0;
    }

    auto pixelShaderVersion = d3dCaps.PixelShaderVersion;

    if (pixelShaderVersion >= D3DPS_VERSION(3, 0)) {
        caps.m_shaderTargets[GxSh_Pixel] = GxShPS_ps_3_0;
    } else if (pixelShaderVersion >= D3DPS_VERSION(2, 0)) {
        caps.m_shaderTargets[GxSh_Pixel] = GxShPS_ps_2_0;
    } else if (pixelShaderVersion >= D3DPS_VERSION(1, 4)) {
        caps.m_shaderTargets[GxSh_Pixel] = GxShPS_ps_1_4;
    } else if (pixelShaderVersion > D3DPS_VERSION(1, 0)) {
        caps.m_shaderTargets[GxSh_Pixel] = GxShPS_ps_1_1;
    }

    if (caps.m_shaderTargets[GxSh_Pixel] != GxShPS_none) {
        auto vertexShaderVersion = d3dCaps.VertexShaderVersion;

        if (vertexShaderVersion >= D3DVS_VERSION(3, 0)) {
            caps.m_shaderTargets[GxSh_Vertex] = GxShVS_vs_3_0;
        } else if (vertexShaderVersion >= D3DVS_VERSION(2, 0)) {
            caps.m_shaderTargets[GxSh_Vertex] = GxShVS_vs_2_0;
        } else if (vertexShaderVersion == D3DVS_VERSION(1, 1)) {
            caps.m_shaderTargets[GxSh_Vertex] = GxShVS_vs_1_1;
        }

        caps.m_shaderConsts[GxSh_Vertex] = d3dCaps.MaxVertexShaderConst;
    }

    // The format can only lower a target: -1 leaves it, a fixed-function format sets 0.
    if (format.unk48 != -1 && format.unk48 <= caps.m_shaderTargets[GxSh_Pixel]) {
        caps.m_shaderTargets[GxSh_Pixel] = format.unk48;
    }

    if (format.unk38 != -1 && format.unk38 <= caps.m_shaderTargets[GxSh_Vertex]) {
        caps.m_shaderTargets[GxSh_Vertex] = format.unk38;
    }

    if (caps.m_shaderTargets[GxSh_Pixel] == GxShPS_ps_3_0 && caps.m_shaderTargets[GxSh_Vertex] != GxShVS_vs_3_0) {
        caps.m_shaderTargets[GxSh_Pixel] = GxShPS_ps_2_0;
    }

    D3DDEVICE_CREATION_PARAMETERS creation;
    this->m_d3dDevice->GetCreationParameters(&creation);

    D3DADAPTER_IDENTIFIER9 adapter;
    this->m_d3d->GetAdapterIdentifier(creation.AdapterOrdinal, 0, &adapter);

    // GeForce FX (device ids 0x300..0x3ff): no more than 192 vertex shader constants.
    if (adapter.VendorId == 0x10DE && adapter.DeviceId - 0x300 < 0x100 && caps.m_shaderConsts[GxSh_Vertex] > 0xC0) {
        caps.m_shaderConsts[GxSh_Vertex] = 0xC0;
    }

    for (int32_t i = 0; i < GxTexFormats_Last; i++) {
        if (i == GxTex_Unknown) {
            caps.m_texFmt[i] = 0;
        } else {
            caps.m_texFmt[i] = this->m_d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, this->m_devAdapterFormat, 0,
                D3DRTYPE_TEXTURE, CGxDeviceD3d::s_GxTexFmtToD3dFmt[i]) == D3D_OK;
        }
    }

    caps.m_generateMipMaps = (d3dCaps.Caps2 & D3DCAPS2_CANAUTOGENMIPMAP) != 0;

    // Before vs_2_0 the two byte-vector attribute types travel as D3DCOLOR.
    if (caps.m_shaderTargets[GxSh_Vertex] < GxShVS_vs_2_0) {
        CGxDeviceD3d::s_gxAttribToD3dAttribType[1] = D3DDECLTYPE_D3DCOLOR;
        CGxDeviceD3d::s_gxAttribToD3dAttribType[2] = D3DDECLTYPE_D3DCOLOR;
    }

    auto checkRtt = [&](uint32_t usage, D3DFORMAT d3dFormat) {
        return this->m_d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, this->m_devAdapterFormat, usage, D3DRTYPE_TEXTURE, d3dFormat) == D3D_OK;
    };

    caps.m_texFmtRtt[GxTex_Argb8888] = checkRtt(D3DUSAGE_RENDERTARGET, CGxDeviceD3d::s_GxTexFmtToD3dFmt[GxTex_Argb8888]);
    caps.m_texFmtRtt[GxTex_Rgb565] = checkRtt(D3DUSAGE_RENDERTARGET, CGxDeviceD3d::s_GxTexFmtToD3dFmt[GxTex_Rgb565]);
    caps.m_texFmtRtt[GxTex_Gr1616F] = checkRtt(D3DUSAGE_RENDERTARGET, CGxDeviceD3d::s_GxTexFmtToD3dFmt[GxTex_Gr1616F]);
    caps.m_texFmtRtt[GxTex_R32F] = checkRtt(D3DUSAGE_RENDERTARGET, CGxDeviceD3d::s_GxTexFmtToD3dFmt[GxTex_R32F]);
    caps.m_texFmtRtt[GxTex_D24X8] = checkRtt(D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24X8);

    caps.m_colorWrite = (d3dCaps.PrimitiveMiscCaps & D3DPMISCCAPS_COLORWRITEENABLE) != 0;
    caps.m_hardwareCursor = (d3dCaps.CursorCaps & D3DCURSORCAPS_COLOR) != 0;
    caps.m_maxClipPlanes = d3dCaps.MaxUserClipPlanes;

    caps.m_texTarget[GxTex_2d] = 1;
    caps.m_texTarget[GxTex_CubeMap] = (d3dCaps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) != 0;
    caps.m_texTarget[GxTex_Rectangle] = 0;
    caps.m_texTarget[GxTex_NonPow2] = !(!(d3dCaps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) && (d3dCaps.TextureCaps & D3DPTEXTURECAPS_POW2));

    caps.m_texNonPow2Conditional = 1;

    if (caps.m_texTarget[GxTex_NonPow2]) {
        caps.m_texNonPow2Conditional = (d3dCaps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) != 0;

        if (adapter.VendorId == 0x10DE) {
            caps.m_texNonPow2Conditional = 0;
        }
    }

    LPDIRECT3DQUERY9 query = nullptr;
    caps.m_occlusionQuery = this->m_d3dDevice->CreateQuery(D3DQUERYTYPE_OCCLUSION, &query) == D3D_OK;

    if (query) {
        query->Release();
    }

    caps.m_pointSprites = d3dCaps.MaxPointSize > 1.0f;
    caps.m_maxPointSize = d3dCaps.MaxPointSize;
    caps.m_pointScale = d3dCaps.MaxPointSize > 1.0f;
    caps.m_blendFactor = (d3dCaps.SrcBlendCaps & D3DPBLENDCAPS_BLENDFACTOR) != 0;

    caps.int130b = 1;

    uint32_t notPs30 = caps.m_shaderTargets[GxSh_Pixel] != GxShPS_ps_3_0;

    for (auto& unknown : caps.int114) {
        unknown = 0;
    }

    caps.m_notPs30a = notPs30;
    caps.m_notPs30b = notPs30;
}

// ref: FUN_0068e250
// Two things were missing here: the back-buffer count, which is gxTripleBuffer's whenever vsync
// is on (it was always 1), and the multisample quality, which the reference sets to the top
// quality level D3D reports for the format scaled by gxMultisampleQuality (it was left at 0).
void CGxDeviceD3d::ISetPresentParms(D3DPRESENT_PARAMETERS& d3dpp, const CGxFormat& format) {
    memset(&d3dpp, 0, sizeof(d3dpp));

    if (!format.window) {
        d3dpp.BackBufferWidth = format.size.x;
        d3dpp.BackBufferHeight = format.size.y;
        d3dpp.BackBufferFormat = CGxDeviceD3d::s_GxFormatToD3dFormat[format.colorFormat];
        d3dpp.BackBufferCount = format.vsync ? format.backBufferCount : 1;
        d3dpp.FullScreen_RefreshRateInHz = format.refreshRate;
    } else {
        D3DDISPLAYMODE currentMode;
        D3DFORMAT backBufferFormat;

        if (FAILED(this->m_d3d->GetAdapterDisplayMode(0, &currentMode))) {
            backBufferFormat = this->m_desktopDisplayMode.Format;
        } else {
            backBufferFormat = currentMode.Format;
        }

        auto& windowRect = this->DeviceCurWindow();

        d3dpp.Windowed = true;
        d3dpp.BackBufferWidth = static_cast<UINT>(windowRect.maxX);
        d3dpp.BackBufferFormat = backBufferFormat;
        d3dpp.BackBufferHeight = static_cast<UINT>(windowRect.maxY);
        d3dpp.BackBufferCount = format.vsync ? format.backBufferCount : 1;
        d3dpp.FullScreen_RefreshRateInHz = 0;
    }

    d3dpp.hDeviceWindow = this->m_hwnd;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.EnableAutoDepthStencil = true;
    d3dpp.AutoDepthStencilFormat = CGxDeviceD3d::s_GxFormatToD3dFormat[format.depthFormat];

    switch (format.vsync) {
        case 1:
            d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
            break;
        case 2:
            d3dpp.PresentationInterval = format.window ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_TWO;
            break;
        case 3:
            d3dpp.PresentationInterval = format.window ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_THREE;
            break;
        case 4:
            d3dpp.PresentationInterval = format.window ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_FOUR;
            break;
        default:
            d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
            break;
    }

    if (format.multisampleCount < 2) {
        d3dpp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
        return;
    }

    d3dpp.MultiSampleType = static_cast<D3DMULTISAMPLE_TYPE>(format.multisampleCount);

    DWORD qualityLevels = 0;

    if (FAILED(this->m_d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, d3dpp.BackBufferFormat,
            format.window, d3dpp.MultiSampleType, &qualityLevels)) || qualityLevels < 2) {
        qualityLevels = 1;
    }

    d3dpp.MultiSampleQuality = static_cast<DWORD>(static_cast<float>(qualityLevels - 1) * format.multisampleQuality);
}

// ref: FUN_006a4900
void CGxDeviceD3d::ISetTexture(uint32_t tmu, CGxTex* texId) {
    if (tmu > 15) {
        return;
    }

    if (texId) {
        this->ITexMarkAsUpdated(texId);
        this->m_d3dDevice->SetTexture(tmu, static_cast<LPDIRECT3DBASETEXTURE9>(texId->m_apiSpecificData));

        // Texture filters
        auto& filters = CGxDeviceD3d::s_filterModes[texId->m_flags.m_filter];
        this->DsSet(static_cast<EDeviceState>(Ds_TssMinFilter0 + tmu), filters[0]);
        this->DsSet(static_cast<EDeviceState>(Ds_TssMagFilter0 + tmu), filters[1]);
        this->DsSet(static_cast<EDeviceState>(Ds_TssMipFilter0 + tmu), filters[2]);

        // Texture addressing
        this->DsSet(static_cast<EDeviceState>(Ds_TssWrapU0 + tmu), CGxDeviceD3d::s_wrapModes[texId->m_flags.m_wrapU]);
        this->DsSet(static_cast<EDeviceState>(Ds_TssWrapV0 + tmu), CGxDeviceD3d::s_wrapModes[texId->m_flags.m_wrapV]);

        // Max anisotropy
        this->DsSet(static_cast<EDeviceState>(Ds_TssMaxAnisotropy0 + tmu), texId->m_flags.m_maxAnisotropy);

        // A stage that gains a texture with no pixel shader bound gets the app's combiners back.
        if (tmu < 8) {
            if (!this->m_appRenderStates[GxRs_PixelShader].m_value.m_data.p && !this->m_stageTextured[tmu]) {
                this->ISetColorOp(tmu, static_cast<uint32_t>(this->m_appRenderStates[GxRs_ColorOp0 + tmu].m_value));
                this->ISetAlphaOp(tmu, static_cast<uint32_t>(this->m_appRenderStates[GxRs_AlphaOp0 + tmu].m_value));
                this->m_appRenderStates[GxRs_ColorOp0 + tmu].m_dirty = 0;
                this->m_appRenderStates[GxRs_AlphaOp0 + tmu].m_dirty = 0;
            }

            this->m_stageTextured[tmu] = 1;
        }
    } else {
        this->m_d3dDevice->SetTexture(tmu, nullptr);

        // And one that loses it has them disabled.
        if (tmu < 8) {
            if (!this->m_appRenderStates[GxRs_PixelShader].m_value.m_data.p && this->m_stageTextured[tmu]) {
                this->DsSet(static_cast<EDeviceState>(Ds_TssColorOp0 + tmu), D3DTOP_DISABLE);
                this->DsSet(static_cast<EDeviceState>(Ds_TssAlphaOp0 + tmu), D3DTOP_DISABLE);
            }

            this->m_stageTextured[tmu] = 0;
        }
    }
}

void CGxDeviceD3d::ISetVertexBuffer(uint32_t stream, LPDIRECT3DVERTEXBUFFER9 buffer, uint32_t offset, uint32_t stride) {
    if (!this->m_caps.int10) {
        offset = 0;
    }

    if (this->m_d3dVertexStreamBuf[stream] != buffer || this->m_d3dVertexStreamOfs[stream] != offset || this->m_d3dVertexStreamStride[stream] != stride) {
        this->m_d3dDevice->SetStreamSource(stream, buffer, offset, stride);

        this->m_d3dVertexStreamBuf[stream] = buffer;
        this->m_d3dVertexStreamOfs[stream] = offset;
        this->m_d3dVertexStreamStride[stream] = stride;
    }
}

// ref: FUN_006a5c70
void CGxDeviceD3d::IShaderBindPixel(CGxShader* shader) {
    if (!shader) {
        this->m_d3dDevice->SetPixelShader(nullptr);

        // Back to fixed function: every stage without a texture has its combiners disabled, every
        // stage with one gets the app's back, and the op states are marked clean.
        for (uint32_t tmu = 0; tmu < 8; tmu++) {
            if (!this->m_stageTextured[tmu]) {
                this->DsSet(static_cast<EDeviceState>(Ds_TssColorOp0 + tmu), D3DTOP_DISABLE);
                this->DsSet(static_cast<EDeviceState>(Ds_TssAlphaOp0 + tmu), D3DTOP_DISABLE);
            } else {
                this->ISetColorOp(tmu, static_cast<uint32_t>(this->m_appRenderStates[GxRs_ColorOp0 + tmu].m_value));
                this->ISetAlphaOp(tmu, static_cast<uint32_t>(this->m_appRenderStates[GxRs_AlphaOp0 + tmu].m_value));
            }

            this->m_appRenderStates[GxRs_ColorOp0 + tmu].m_dirty = 0;
            this->m_appRenderStates[GxRs_AlphaOp0 + tmu].m_dirty = 0;
        }

        return;
    }

    if (!shader->loaded) {
        this->IShaderCreatePixel(shader);
    }

    auto d3dShader = static_cast<LPDIRECT3DPIXELSHADER9>(shader->apiSpecific);
    this->m_d3dDevice->SetPixelShader(d3dShader);
}

// ref: FUN_006aa2f0
void CGxDeviceD3d::IShaderBindVertex(CGxShader* shader) {
    if (!shader) {
        this->m_d3dDevice->SetVertexShader(nullptr);
        return;
    }

    if (!shader->loaded) {
        this->IShaderCreateVertex(shader);
    }

    auto d3dShader = static_cast<LPDIRECT3DVERTEXSHADER9>(shader->apiSpecific);
    this->m_d3dDevice->SetVertexShader(d3dShader);
}

void CGxDeviceD3d::IShaderConstantsFlush() {
    // Vertex shader constants
    auto vsConst = &CGxDevice::s_shadowConstants[1];
    if (vsConst->unk2 <= vsConst->unk1) {
        this->m_d3dDevice->SetVertexShaderConstantF(
            vsConst->unk2,
            reinterpret_cast<float*>(&vsConst->constants[vsConst->unk2]),
            vsConst->unk1 - vsConst->unk2 + 1
        );
    }
    vsConst->unk2 = 255;
    vsConst->unk1 = 0;

    // Pixel shader constants
    auto psConst = &CGxDevice::s_shadowConstants[0];
    if (psConst->unk2 <= psConst->unk1) {
        this->m_d3dDevice->SetPixelShaderConstantF(
            psConst->unk2,
            reinterpret_cast<float*>(&psConst->constants[psConst->unk2]),
            psConst->unk1 - psConst->unk2 + 1
        );
    }
    psConst->unk2 = 255;
    psConst->unk1 = 0;
}

// ref: FUN_006a5e10
void CGxDeviceD3d::IShaderCreate(CGxShader* shader) {
    if (shader->target == GxSh_Vertex) {
        this->IShaderCreateVertex(shader);
    } else if (shader->target == GxSh_Pixel) {
        this->IShaderCreatePixel(shader);
    }
}

// ref: FUN_006aa070
void CGxDeviceD3d::IShaderCreatePixel(CGxShader* shader) {
    shader->valid = 0;

    if (!this->m_context) {
        return;
    }

    shader->loaded = 1;

    if (shader->code.Count() == 0) {
        return;
    }

    LPDIRECT3DPIXELSHADER9 d3dShader;
    if (SUCCEEDED(this->m_d3dDevice->CreatePixelShader(reinterpret_cast<DWORD*>(shader->code.Ptr()), &d3dShader))) {
        shader->apiSpecific = d3dShader;
        shader->valid = 1;
    }
}

void CGxDeviceD3d::IShaderCreateVertex(CGxShader* shader) {
    shader->valid = 0;

    if (!this->m_context) {
        return;
    }

    shader->loaded = 1;

    if (shader->code.Count() == 0) {
        return;
    }

    LPDIRECT3DVERTEXSHADER9 d3dShader;
    if (SUCCEEDED(this->m_d3dDevice->CreateVertexShader(reinterpret_cast<DWORD*>(shader->code.Ptr()), &d3dShader))) {
        shader->apiSpecific = d3dShader;
        shader->valid = 1;
    }
}

// ref: FUN_006a3a60
// One recorded divergence: frozen also sets D3DRS_FOGTABLEMODE to linear, which the reference
// never does, because frozen's own terrain shaders do not write a vertex fog value; pixel fog
// by view depth stands in for it. Remove it with the move to the archived shaders.
void CGxDeviceD3d::IStateSetD3dDefaults() {
    this->m_d3dDevice->SetRenderState(D3DRS_ZENABLE, 1);
    this->m_d3dDevice->SetRenderState(D3DRS_LOCALVIEWER, 1);
    this->m_d3dDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    this->m_d3dDevice->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
    this->m_d3dDevice->SetRenderState(D3DRS_FOGDENSITY, 0);
    this->m_d3dDevice->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);

    for (uint32_t tmu = 0; tmu < 16; tmu++) {
        this->m_d3dDevice->SetSamplerState(tmu, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP);
    }

    this->IRsForceUpdate();
    this->IRsSync(0);

    this->m_primVertexDirty = -1;
    this->m_primIndexDirty = 0;

    this->m_d3dDevice->SetIndices(nullptr);

    this->m_d3dCurrentVertexDecl = nullptr;
    this->m_d3dCurrentIndexBuf = nullptr;

    for (uint32_t i = 0; i < 8; i++) {
        this->m_d3dVertexStreamBuf[i] = nullptr;
        this->m_d3dVertexStreamOfs[i] = -1;
        this->m_d3dVertexStreamStride[i] = -1;
    }

    this->m_d3dDevice->GetRenderTarget(0, &this->m_defColorSurface);
    this->m_d3dDevice->GetDepthStencilSurface(&this->m_defDepthSurface);

    this->ILightsInvalidate();

    if (!this->m_d3dFrameQuery) {
        this->m_d3dDevice->CreateQuery(D3DQUERYTYPE_EVENT, &this->m_d3dFrameQuery);
    }

    this->ISceneBegin();
}

void CGxDeviceD3d::IStateSync() {
    // TODO

    this->IShaderConstantsFlush();
    this->IRsSync(0);

    if (this->m_hwRenderStates[GxRs_VertexShader] == nullptr && this->m_appRenderStates[GxRs_VertexShader].m_value == nullptr) {
        this->IStateSyncLights();
        this->IStateSyncMaterial();
        this->IStateSyncXforms();
    }

    this->IStateSyncEnables();
    this->IStateSyncClipPlanes();
    this->IStateSyncScissorRect();

    this->IStateSyncVertexPtrs();
    this->IStateSyncIndexPtr();

    // TODO

    if (this->intF6C) {
        this->IXformSetViewport();
    }
}

// Tests one master-enable bit for a change between the app-side and hardware-side masks and
// reports what it is now. The two extra masks are force-off masks: a bit set in one forces that
// enable to read as off on that side. Both call sites in the reference pass zero for them, so
// nothing forces anything off today -- they are kept rather than dropped, because dropping them
// would hide that the reference has the capability at all.
//
// The reference shares this between two device backends (00683835 in the D3D state sync below, and
// 006921fc in another), so if the GL backends ever grow a master-enable sync this should move to
// CGxDevice instead of being copied.
// ref: FUN_006830b0
static int32_t MasterEnableChanged(uint32_t appEnables, uint32_t hwEnables, uint32_t appForceOff,
                                   uint32_t hwForceOff, EGxMasterEnables which, int32_t* nowEnabled) {
    uint32_t bit = 1u << which;
    int32_t now = (appEnables & ~appForceOff & bit) != 0;
    int32_t was = (hwEnables & ~hwForceOff & bit) != 0;

    *nowEnabled = now;

    return now != was;
}

// Pushes the master enables to the device. Only ONE of the nine reaches D3D here, and that is not
// an omission: MasterEnableSet routes Lighting, Fog, DepthTest, DepthWrite, ColorWrite and Culling
// through IRsForceUpdate, so they travel the ordinary render-state path and arrive via IRsSync.
// PolygonFill has no GxRs of its own, so it is the only one left to send directly, and the
// reference sends it exactly here.
//
// This costs nothing until something asks for wireframe: both masks start at 511, so the equality
// test returns immediately, and D3D's own default fill mode is already solid.
// ref: FUN_006a3810
void CGxDeviceD3d::IStateSyncEnables() {
    if (this->m_appMasterEnables == this->m_hwMasterEnables) {
        return;
    }

    int32_t fill;

    if (MasterEnableChanged(this->m_appMasterEnables, this->m_hwMasterEnables, 0, 0, GxMasterEnable_PolygonFill, &fill)) {
        this->m_d3dDevice->SetRenderState(D3DRS_FILLMODE, fill ? D3DFILL_SOLID : D3DFILL_WIREFRAME);
    }

    this->m_hwMasterEnables = this->m_appMasterEnables;
}

// Pushes the six user clip planes that changed since the last sync. The reference walks the mask
// bit by bit rather than testing plane against plane, because CGxDevice::ClipPlaneSet has already
// done the comparison and recorded the answer.
//
// SetClipPlane is vtable offset 0xdc, and 0xdc / 4 = 55, which is its index on IDirect3DDevice9 --
// the same arithmetic that identified SetRenderState at 0xe4.
//
// This costs nothing today and is not a stub while it does: nothing in frozen calls ClipPlaneSet
// yet, so the mask is zero and the first test returns. GxRs_ClipPlaneMask is a separate thing --
// it enables planes, and travels the ordinary render-state path as Ds_ClipPlaneEnable -- so the
// two halves can land independently.
// ref: FUN_006a3870
void CGxDeviceD3d::IStateSyncClipPlanes() {
    if (!this->m_clipPlaneDirty) {
        return;
    }

    for (uint32_t i = 0; i < 6; i++) {
        if (this->m_clipPlaneDirty & (1 << i)) {
            this->m_d3dDevice->SetClipPlane(i, reinterpret_cast<const float*>(&this->m_clipPlanes[i]));
        }
    }

    this->m_clipPlaneDirty = 0;
}

// Turns the normalised scissor rectangle into pixels and sends it. SetScissorRect is vtable
// 0x12c and 0x12c / 4 = 75, its IDirect3DDevice9 method index.
//
// Horizontal edges scale by the current window's maxX and vertical by its maxY -- the reference
// reaches both through FUN_00682d70, which is `return &this->[0x174]`, i.e. DeviceCurWindow. The
// left and top edges take +0.5 (the constant at 0x009e2ec4, read out of .rdata) and the right and
// bottom edges +1.0, which is the usual rounding for a half-open rectangle.
//
// Vertical orientation flips depending on where the frame is going. With neither render target
// bound the frame is the back buffer and y is measured from the bottom, so top comes from maxY
// and bottom from minY, each subtracted from 1.0; with a target bound they are used directly.
// IXformSetViewport already flips y the same way.
//
// DIVERGENCE, deliberate. The reference spells that test as `+0x2918 != 0 || +0x2924 != 0`, and
// those are NOT the m_texture fields. Device create zeroes six consecutive dwords from +0x2910 to
// +0x2924, which is exactly TextureTarget m_textureTarget[2] at base +0x2910, so the entries are
// {m_texture, m_plane, m_apiSpecific} at +0x2910/+0x2914/+0x2918 and +0x291c/+0x2920/+0x2924.
// +0x2918 and +0x2924 are therefore the two m_apiSpecific fields -- the bound API surfaces -- and
// the reference stores one there at 0x0068b929, indexing `0x2918(%edi,%eax,4)` with eax = i * 3.
//
// frozen tests m_texture instead, because CGxDevice::RenderTargetSet fills m_texture and m_plane
// and never fills m_apiSpecific: IRenderTargetSet takes the surface, binds it and releases it
// again without storing it. Testing the faithful field here would make this branch permanently
// false and the render-to-texture case dead, which is worse than the divergence. The real fix is
// to have IRenderTargetSet keep the surface in m_apiSpecific, and that is a lifetime change (the
// device holds its own reference today) that wants a run behind it. Until then this reads the
// field that actually tracks the binding.
//
// The dirty flag starts at 1 with an all-zero rectangle, because device create sets it from a
// register holding 1 (0x00688e64 loads it, and the same register initialises intF6C). So the
// first sync really does send an empty rectangle. That is harmless and it is what the reference
// does: D3DRS_SCISSORTESTENABLE follows GxRs_ScissorTest, which defaults to 0, so nothing is
// clipped until something turns the test on -- and whatever turns it on sets a rectangle first.
// ref: FUN_006a38d0
void CGxDeviceD3d::IStateSyncScissorRect() {
    if (!this->m_scissorDirty) {
        return;
    }

    const CRect& window = this->DeviceCurWindow();
    const CRect& scissor = this->m_scissorRect;

    RECT rect;

    rect.left = static_cast<LONG>(scissor.minX * window.maxX + 0.5f);
    rect.right = static_cast<LONG>(scissor.maxX * window.maxX + 1.0f);

    if (this->m_textureTarget[GxBuffers_Color].m_texture || this->m_textureTarget[GxBuffers_Depth].m_texture) {
        rect.top = static_cast<LONG>(0.5f + scissor.minY * window.maxY);
        rect.bottom = static_cast<LONG>(1.0f + scissor.maxY * window.maxY);
    } else {
        rect.top = static_cast<LONG>(0.5f + (1.0f - scissor.maxY) * window.maxY);
        rect.bottom = static_cast<LONG>(1.0f + (1.0f - scissor.minY) * window.maxY);
    }

    if (rect.left < 0) {
        rect.left = 0;
    }

    if (rect.top < 0) {
        rect.top = 0;
    }

    if (rect.right > static_cast<LONG>(window.maxX)) {
        rect.right = static_cast<LONG>(window.maxX);
    }

    if (rect.bottom > static_cast<LONG>(window.maxY)) {
        rect.bottom = static_cast<LONG>(window.maxY);
    }

    this->m_d3dDevice->SetScissorRect(&rect);

    this->m_scissorDirty = 0;
}

void CGxDeviceD3d::IStateSyncIndexPtr() {
    if (!this->m_primIndexDirty) {
        return;
    }

    this->m_primIndexDirty = 0;

    auto d3dIndexBuf = static_cast<LPDIRECT3DINDEXBUFFER9>(this->m_primIndexBuf->m_pool->m_apiSpecific);

    if (this->m_d3dCurrentIndexBuf != d3dIndexBuf) {
        this->m_d3dDevice->SetIndices(d3dIndexBuf);
        this->m_d3dCurrentIndexBuf = d3dIndexBuf;
    }
}

// Send the four fixed-function lights, and decide whether fixed-function lighting is on at all.
//
// The interesting part is the middle branch, which has nothing to do with lights. When the app
// turns GxRs_Lighting OFF, D3D would normally be told D3DRS_LIGHTING false -- and then it takes
// the vertex colour as the final colour and ignores the material entirely. That is wrong for
// geometry whose vertices carry NO colour but whose material diffuse is not white -- the state
// defaults to 0xFFFFFFFF, so `!= -1` is exactly "somebody tinted this". Such geometry would come
// out untinted. So when both of those hold, the reference turns D3D lighting ON with every light
// disabled and D3DRS_AMBIENT set to white, which makes the fixed-function equation collapse to
// the material's ambient/diffuse term and reproduces the unlit colour the app meant. The
// m_lightingEmulated flag exists only to disable the four lights once on entering that state
// instead of every frame.
//
// The light loop's asymmetry is deliberate and reproduced: disabling a light clears ONLY the
// enabled bit, leaving the rest of the dirty mask standing so the light is re-sent in full when
// it comes back, while sending a light clears the mask entirely. A light that is enabled and
// dirty for any other reason is re-sent without touching LightEnable.
//
// Range is the 10000.0 at 0x00a2f95c, read out of the binary rather than guessed. Falloff is 1.0.
// Theta and Phi are never written, because the reference never writes them -- it has no spot
// lights, and D3D reads neither for the two types it does use.
//
// **Built, not seen running.**
// ref: FUN_006a43d0
void CGxDeviceD3d::IStateSyncLights() {
    uint32_t index = 0;

    if (this->m_appRenderStates[GxRs_Lighting].m_value.m_data.i[0] == 0) {
        if ((this->m_primVertexMask & (1 << GxVA_Color0)) == 0
                && this->m_appRenderStates[GxRs_MatDiffuse].m_value.m_data.i[0] != -1) {
            if (this->m_lightingEmulated == 0) {
                this->m_lightingEmulated = 1;

                for (index = 0; index < 4; index++) {
                    this->m_d3dDevice->LightEnable(index, FALSE);

                    // The slot keeps its enabled flag; only the dirty bit moves, so that whatever
                    // the app had asked for is re-sent the moment emulation stops.
                    if (this->m_lights[index].m_enabled == 0) {
                        this->m_lights[index].m_dirty &= 0xfffe;
                    } else {
                        this->m_lights[index].m_dirty |= 0x1;
                    }
                }
            }

            if (this->m_d3dLighting != 1) {
                this->m_d3dDevice->SetRenderState(D3DRS_LIGHTING, 1);
                this->m_d3dLighting = 1;
            }

            if (this->m_d3dAmbient != 0xffffffff) {
                this->m_d3dDevice->SetRenderState(D3DRS_AMBIENT, 0xffffffff);
                this->m_d3dAmbient = 0xffffffff;
            }
        } else {
            this->m_lightingEmulated = 0;

            if (this->m_d3dLighting != 0) {
                this->m_d3dDevice->SetRenderState(D3DRS_LIGHTING, 0);
                this->m_d3dLighting = 0;
            }

            if (this->m_d3dAmbient != 0) {
                this->m_d3dDevice->SetRenderState(D3DRS_AMBIENT, 0);
                this->m_d3dAmbient = 0;
            }
        }

        return;
    }

    this->m_lightingEmulated = 0;

    uint32_t lighting = this->m_appMasterEnables & 0x1;

    if (this->m_d3dLighting != lighting) {
        this->m_d3dDevice->SetRenderState(D3DRS_LIGHTING, lighting);
        this->m_d3dLighting = lighting;
    }

    if (this->m_d3dAmbient != 0) {
        this->m_d3dDevice->SetRenderState(D3DRS_AMBIENT, 0);
        this->m_d3dAmbient = 0;
    }

    for (index = 0; index < 4; index++) {
        CGxLightState& state = this->m_lights[index];

        if (state.m_dirty == 0) {
            continue;
        }

        if ((state.m_dirty & 0x1) == 0) {
            if (state.m_enabled == 0) {
                continue;
            }
        } else if (state.m_enabled == 0) {
            state.m_dirty &= 0xfffe;
            this->m_d3dDevice->LightEnable(index, FALSE);
            continue;
        } else {
            this->m_d3dDevice->LightEnable(index, TRUE);
        }

        if (state.m_posOrDir.w == 1.0f) {
            this->m_d3dLight.Type = D3DLIGHT_POINT;
            this->m_d3dLight.Position.x = state.m_posOrDir.x;
            this->m_d3dLight.Position.y = state.m_posOrDir.y;
            this->m_d3dLight.Position.z = state.m_posOrDir.z;
        } else {
            this->m_d3dLight.Type = D3DLIGHT_DIRECTIONAL;
            this->m_d3dLight.Direction.x = state.m_posOrDir.x;
            this->m_d3dLight.Direction.y = state.m_posOrDir.y;
            this->m_d3dLight.Direction.z = state.m_posOrDir.z;
        }

        this->m_d3dLight.Diffuse.r = state.m_diffuse.x;
        this->m_d3dLight.Diffuse.g = state.m_diffuse.y;
        this->m_d3dLight.Diffuse.b = state.m_diffuse.z;
        this->m_d3dLight.Diffuse.a = 1.0f;

        this->m_d3dLight.Ambient.r = state.m_ambient.x;
        this->m_d3dLight.Ambient.g = state.m_ambient.y;
        this->m_d3dLight.Ambient.b = state.m_ambient.z;
        this->m_d3dLight.Ambient.a = 0.0f;

        this->m_d3dLight.Specular.r = state.m_specular.x;
        this->m_d3dLight.Specular.g = state.m_specular.y;
        this->m_d3dLight.Specular.b = state.m_specular.z;
        this->m_d3dLight.Specular.a = 0.0f;

        this->m_d3dLight.Range = 10000.0f;
        this->m_d3dLight.Falloff = 1.0f;

        this->m_d3dLight.Attenuation0 = state.m_attenuation.x;
        this->m_d3dLight.Attenuation1 = state.m_attenuation.y;
        this->m_d3dLight.Attenuation2 = state.m_attenuation.z;

        this->m_d3dDevice->SetLight(index, &this->m_d3dLight);

        state.m_dirty = 0;
    }
}

// Despite the name this never calls SetMaterial. It drives the four *MATERIALSOURCE render
// states -- D3DRS_AMBIENTMATERIALSOURCE 0x93, DIFFUSE 0x91, SPECULAR 0x92, EMISSIVE 0x94 -- which
// say, per channel, whether the fixed-function lighting equation takes that channel from the
// material or from the vertex colour.
//
// GxRs_ColorMaterial picks which single channel the vertex colour feeds: 0 gives it to ambient and
// diffuse, 1 to specular, 2 to emissive, and every channel that does not win takes D3DMCS_MATERIAL.
// When the bound vertex format carries no colour at all the question does not arise and all four
// take the material.
//
// The offsets behind this were confirmed rather than guessed, which is worth recording because the
// reference's fields are nothing like frozen's. The state array at +0x28f4 is indexed 24 bytes to
// the entry: the vertex-shader test in IStateSync reads +0x738, and 0x738 / 24 = 77 =
// GxRs_VertexShader, while this function reads +0x7f8, and 0x7f8 / 24 = 85 = GxRs_ColorMaterial.
// The mask at +0x28a8 is m_primVertexMask -- its setter at 0x00682eb0 is CGxDevice::PrimVertexMask
// statement for statement, down to storing GxVAs_Last (0xe) into m_primVertexFormat -- so bit 0x10
// is 1 << GxVA_Color0, and the per-attribute buffer array at +0x2870 ends exactly where the mask
// begins.
//
// Two notes on what a run should show, because this replaces an empty body and so changes
// behaviour on every fixed-function draw that has a colour stream:
//
// * Nothing in frozen ever sets GxRs_ColorMaterial and its default is 0, so today the live case is
//   always "ambient and diffuse from the vertex colour". Against D3D's own defaults that moves
//   ambient from MATERIAL to COLOR1 and specular from COLOR2 to MATERIAL; diffuse and emissive
//   already agreed.
// * The caches start at zero while two of D3D's defaults do not, so a first sync that computes
//   zero sends nothing and leaves D3D on its default. That is a real gap and it is the reference's
//   gap -- nothing else in the binary writes +0x3e4c..+0x3e58 -- so it is reproduced rather than
//   fixed. Do not "correct" it without checking the reference again.
//
// Only the fixed-function path reaches this: IStateSync calls it solely when no vertex shader is
// bound, which today means UI and 2D rather than the world.
//
// **Built, not seen running.**
// ref: FUN_006a4700
void CGxDeviceD3d::IStateSyncMaterial() {
    uint32_t ambient;
    uint32_t diffuse;
    uint32_t specular;
    uint32_t emissive;

    if (this->m_primVertexMask & (1 << GxVA_Color0)) {
        uint32_t which = this->m_appRenderStates[GxRs_ColorMaterial].m_value.m_data.u[0];

        ambient = which == 0;
        diffuse = which == 0;
        specular = which == 1;
        emissive = which == 2;
    } else {
        ambient = 0;
        diffuse = 0;
        specular = 0;
        emissive = 0;
    }

    if (this->m_d3dAmbientMaterialSource != ambient) {
        this->m_d3dDevice->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, ambient);
        this->m_d3dAmbientMaterialSource = ambient;
    }

    if (this->m_d3dDiffuseMaterialSource != diffuse) {
        this->m_d3dDevice->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, diffuse);
        this->m_d3dDiffuseMaterialSource = diffuse;
    }

    if (this->m_d3dSpecularMaterialSource != specular) {
        this->m_d3dDevice->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, specular);
        this->m_d3dSpecularMaterialSource = specular;
    }

    if (this->m_d3dEmissiveMaterialSource != emissive) {
        this->m_d3dDevice->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, emissive);
        this->m_d3dEmissiveMaterialSource = emissive;
    }
}

void CGxDeviceD3d::IStateSyncVertexPtrs() {
    if (this->m_primVertexFormat < GxVertexBufferFormats_Last && this->m_d3dVertexDecl[this->m_primVertexFormat]) {
        auto d3dVertexDecl = this->m_d3dVertexDecl[this->m_primVertexFormat];

        if (this->m_d3dCurrentVertexDecl != d3dVertexDecl) {
            this->m_d3dDevice->SetVertexDeclaration(d3dVertexDecl);
            this->m_d3dCurrentVertexDecl = d3dVertexDecl;
        }

        this->ISetVertexBuffer(
            0,
            static_cast<LPDIRECT3DVERTEXBUFFER9>(this->m_primVertexBuf->m_pool->m_apiSpecific),
            this->m_primVertexBuf->m_index,
            this->m_primVertexSize
        );

        return;
    }

    CGxBuf* streamBufs[GxVAs_Last] = { 0 };
    uint32_t streamSizes[GxVAs_Last] = { 0 };

    D3DVERTEXELEMENT9 elements[GxVAs_Last + 1];
    uint32_t elementCount = 0;
    uint32_t streamCount = 0;

    for (uint32_t i = 0; i < GxVAs_Last; i++) {
        if ((1 << i) & this->m_primVertexMask) {
            uint32_t stream = 0;

            if (streamCount) {
                do {
                    if (streamBufs[stream] == this->m_primVertexFormatBuf[i]) {
                        break;
                    }
                    stream++;
                } while (stream < streamCount);
            }

            if (stream == streamCount) {
                streamBufs[stream] = this->m_primVertexFormatBuf[i];
                streamCount++;
            }

            auto& attrib = this->m_primVertexFormatAttrib[i];

            streamSizes[stream] += CGxDeviceD3d::s_gxAttribToD3dAttribSize[attrib.type];

            elements[elementCount].Stream = stream;
            elements[elementCount].Offset = attrib.offset;
            elements[elementCount].Type = CGxDeviceD3d::s_gxAttribToD3dAttribType[attrib.type];
            elements[elementCount].Method = D3DDECLMETHOD_DEFAULT;
            elements[elementCount].Usage = CGxDeviceD3d::s_gxAttribToD3dAttribUsage[attrib.attrib];
            elements[elementCount].UsageIndex = CGxDeviceD3d::s_gxAttribToD3dAttribUsageIndex[attrib.attrib];

            elementCount++;
        }
    }

    elements[elementCount] = D3DDECL_END();
    elementCount++;

    auto d3dVertexDecl = this->ICreateD3dVertexDecl(elements, elementCount);
    if (this->m_d3dCurrentVertexDecl != d3dVertexDecl) {
        this->m_d3dDevice->SetVertexDeclaration(d3dVertexDecl);
        this->m_d3dCurrentVertexDecl = d3dVertexDecl;
    }

    for (uint32_t stream = 0; stream < streamCount; stream++) {
        auto streamBuf = streamBufs[stream];

        this->ISetVertexBuffer(
            stream,
            static_cast<LPDIRECT3DVERTEXBUFFER9>(streamBuf->m_pool->m_apiSpecific),
            streamBuf->m_index,
            streamSizes[stream]
        );
    }
}

// ref: FUN_006a4850
void CGxDeviceD3d::IStateSyncXforms() {
    if (this->m_xforms[GxXform_Projection].m_dirty) {
        this->m_d3dDevice->SetTransform(D3DTS_PROJECTION, reinterpret_cast<D3DMATRIX*>(&this->m_projNative));
        this->m_xforms[GxXform_Projection].m_dirty = 0;
    }

    if (this->m_xforms[GxXform_View].m_dirty) {
        this->m_d3dDevice->SetTransform(D3DTS_VIEW, reinterpret_cast<const D3DMATRIX*>(&this->m_xforms[GxXform_View].TopConst()));
        this->m_xforms[GxXform_View].m_dirty = 0;
    }

    if (this->m_xforms[GxXform_World].m_dirty) {
        this->IStateSyncWorldXform();
    }

    // One texture transform per stage the card has, re-sent when either the application's
    // stack or the stage's texgen stack changed. This loop is CGxCaps::m_numTmus's only reader.
    for (uint32_t tmu = 0; tmu < static_cast<uint32_t>(this->m_caps.m_numTmus); tmu++) {
        if (this->m_xforms[GxXform_Tex0 + tmu].m_dirty || this->m_texGenXforms[tmu].m_dirty) {
            this->IStateSyncTexXform(tmu);
        }
    }
}

// ref: FUN_006a5a30
// Fixed-function only: D3DTS_WORLD means nothing while a vertex shader is bound. Skips the send
// only when both this and the last world sent are identities.
void CGxDeviceD3d::IStateSyncWorldXform() {
    auto& world = this->m_xforms[GxXform_World];
    uint32_t identity = world.m_flags[world.m_level] & CGxMatrixStack::F_Identity;

    if (!CGxDeviceD3d::s_d3dWorldIdentity || !identity) {
        this->m_d3dDevice->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(&world.TopConst()));
    }

    world.m_dirty = 0;
    CGxDeviceD3d::s_d3dWorldIdentity = identity;
}

// ref: FUN_006a5aa0
// Sends one stage's texture transform and the matching texture-transform flags. The per-stage
// state at GxRs_Unk61 + tmu chooses the form: 0 sends the texgen matrix alone (counting three
// coordinates unless it is the identity), 1 sends texgen times the application's matrix (two
// coordinates when the stage has no texgen, with the translation row moved up to where D3D reads
// it for two-component coordinates), 2 sends the product projected. Any other value disables the
// transform. The reference reads that state through the global device; it is the same object.
void CGxDeviceD3d::IStateSyncTexXform(uint32_t tmu) {
    auto& texGen = this->m_texGenXforms[tmu];
    auto& app = this->m_xforms[GxXform_Tex0 + tmu];
    auto d3dState = static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0 + tmu);
    uint32_t flags = D3DTTFF_DISABLE;

    switch (static_cast<uint32_t>(this->m_appRenderStates[GxRs_Unk61 + tmu].m_value)) {
        case 0: {
            if (!(texGen.m_flags[texGen.m_level] & CGxMatrixStack::F_Identity)) {
                flags = D3DTTFF_COUNT3;
            }

            this->m_d3dDevice->SetTransform(d3dState, reinterpret_cast<const D3DMATRIX*>(&texGen.TopConst()));
            break;
        }

        case 1: {
            C44Matrix matrix = texGen.TopConst() * app.TopConst();

            if (static_cast<uint32_t>(this->m_appRenderStates[GxRs_TexGen0 + tmu].m_value) == 0) {
                flags = D3DTTFF_COUNT2;
                matrix.c0 = matrix.d0;
                matrix.c1 = matrix.d1;
            } else {
                flags = D3DTTFF_COUNT3;
            }

            this->m_d3dDevice->SetTransform(d3dState, reinterpret_cast<const D3DMATRIX*>(&matrix));
            break;
        }

        case 2: {
            C44Matrix matrix = texGen.TopConst() * app.TopConst();
            this->m_d3dDevice->SetTransform(d3dState, reinterpret_cast<const D3DMATRIX*>(&matrix));
            flags = D3DTTFF_COUNT3 | D3DTTFF_PROJECTED;
            break;
        }

        default:
            break;
    }

    this->DsSet(static_cast<EDeviceState>(Ds_TssTTF0 + tmu), flags);

    app.m_dirty = 0;
    texGen.m_dirty = 0;
}

// ref: FUN_006a2c00
// Render targets go in D3DPOOL_DEFAULT, everything else in D3DPOOL_MANAGED. A depth format is
// always one level of D3DFMT_D24X8. When an ordinary texture is refused the format is demoted for
// good through s_tolerableTexFmtMapping and tried once more; a refused render target is not
// retried. The three format tables were checked byte for byte against the reference's data at
// 0x00a2f81c, 0x00a2f7e8 and 0x00ad8ef0 on 2026-10-01.
void CGxDeviceD3d::ITexCreate(CGxTex* texId) {
    uint32_t width, height, startLevel, endLevel;
    this->ITexWHDStartEnd(texId, width, height, startLevel, endLevel);

    auto format = CGxDeviceD3d::s_GxTexFmtToUse[texId->m_format];
    texId->m_format = format;

    uint32_t d3dUsage = texId->m_flags.m_renderTarget ? D3DUSAGE_RENDERTARGET : 0;
    D3DPOOL d3dPool = texId->m_flags.m_renderTarget ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;

    if (texId->m_flags.m_generateMipMaps) {
        d3dUsage |= D3DUSAGE_AUTOGENMIPMAP;
    }

    HRESULT result;
    void* d3dTexture = nullptr;

    if (texId->m_target == GxTex_CubeMap) {
        result = this->m_d3dDevice->CreateCubeTexture(width, endLevel - startLevel, d3dUsage,
            CGxDeviceD3d::s_GxTexFmtToD3dFmt[format], d3dPool,
            reinterpret_cast<LPDIRECT3DCUBETEXTURE9*>(&d3dTexture), nullptr);
    } else if (format == GxTex_D24X8) {
        result = this->m_d3dDevice->CreateTexture(width, height, 1, D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24X8,
            d3dPool, reinterpret_cast<LPDIRECT3DTEXTURE9*>(&d3dTexture), nullptr);
    } else {
        result = this->m_d3dDevice->CreateTexture(width, height, endLevel - startLevel, d3dUsage,
            CGxDeviceD3d::s_GxTexFmtToD3dFmt[format], d3dPool,
            reinterpret_cast<LPDIRECT3DTEXTURE9*>(&d3dTexture), nullptr);

        if (FAILED(result)) {
            if (texId->m_flags.m_renderTarget) {
                return;
            }

            CGxDeviceD3d::s_GxTexFmtToUse[texId->m_format] = CGxDeviceD3d::s_tolerableTexFmtMapping[texId->m_format];
            texId->m_format = CGxDeviceD3d::s_GxTexFmtToUse[texId->m_format];

            result = this->m_d3dDevice->CreateTexture(width, height, endLevel - startLevel, d3dUsage,
                CGxDeviceD3d::s_GxTexFmtToD3dFmt[texId->m_format], d3dPool,
                reinterpret_cast<LPDIRECT3DTEXTURE9*>(&d3dTexture), nullptr);
        }
    }

    if (FAILED(result)) {
        return;
    }

    texId->m_apiSpecificData = d3dTexture;
    texId->m_needsCreation = 0;
}

// Point the device at a texture's surface, or back at the frame buffer when the texture is null.
// The default surfaces are captured at device creation (m_defColorSurface / m_defDepthSurface), so
// restoring never has to guess. A depth target is bound as the depth-stencil surface; a colour
// target goes to slot 0.
void CGxDeviceD3d::IRenderTargetSet(EGxBuffer buffer, CGxTex* texId, uint32_t plane) {
    if (!this->m_d3dDevice) {
        return;
    }

    LPDIRECT3DSURFACE9 surface = nullptr;

    if (texId) {
        if (texId->m_needsCreation) {
            this->ITexCreate(texId);

            fprintf(stderr, "IRenderTargetSet: rebuilt texture, handle now %p\n",
                    texId->m_apiSpecificData);
        }

        auto d3dTexture = static_cast<LPDIRECT3DTEXTURE9>(texId->m_apiSpecificData);

        if (!d3dTexture || FAILED(d3dTexture->GetSurfaceLevel(plane, &surface))) {
            return;
        }
    }

    if (buffer == GxBuffers_Depth) {
        this->m_d3dDevice->SetDepthStencilSurface(surface ? surface : this->m_defDepthSurface);
    } else {
        this->m_d3dDevice->SetRenderTarget(0, surface ? surface : this->m_defColorSurface);

        // THE CURRENT WINDOW RECT FOLLOWS THE COLOUR TARGET, and nothing here used to move it.
        //
        // IXformSetViewport turns the normalized viewport into pixels by scaling it by
        // DeviceCurWindow(), so while that still reported the BACK BUFFER a full 0..1 viewport on a
        // 1024x1024 shadow map covered 1024x768 of it. The bottom quarter of the map was never
        // cleared and never drawn into -- visible as an exact 1024x256 band of zeroes in a dump of
        // it, which is how this was found rather than reasoned about.
        //
        // A zero there is the NEAREST possible depth, so every lookup landing in that band reads
        // 'something is right in front of the light' and shadows everything. It is not a cosmetic
        // quarter of a texture.
        //
        // Restoring the default window on unbind is the other half: leaving the map's size in place
        // would shrink the next frame's viewport to the top-left of the back buffer.
        if (texId) {
            CRect targetRect;
            targetRect.minX = 0.0f;
            targetRect.minY = 0.0f;
            targetRect.maxX = static_cast<float>(texId->m_width);
            targetRect.maxY = static_cast<float>(texId->m_height);

            this->DeviceSetCurWindow(targetRect);
        } else {
            this->DeviceSetCurWindow(this->m_defWindowRect);
        }

        // The viewport is expressed against the rect that just changed, so it has to be pushed
        // again even though its own values did not move.
        this->intF6C = 1;
    }

    if (surface) {
        surface->Release(); // the device holds its own reference
    }
}

// Debug only: pull a render target back into system memory and write it out as a greyscale TGA.
// D3D9 cannot lock a D3DPOOL_DEFAULT render target directly, so the surface has to be copied into
// an offscreen plain surface first. R32F is the shadow map's own format and carries a normalized
// depth, so it is scaled straight to 0..255; Argb8888 is reduced to its red channel so the same
// viewer works for both.
// Capture the back buffer to an uncompressed 24-bit TGA.
//
// This is the only honest way to see what the client actually drew. Grabbing the desktop with a
// screen-capture API returns whatever window happens to be on top -- which, on a machine someone is
// using, is not this one.
//
// GetRenderTargetData is the documented route off a D3DPOOL_DEFAULT surface: it needs a
// system-memory staging surface of the same size and format, and it is the reason a back buffer
// cannot simply be locked. GetBackBuffer rather than GetRenderTarget, so a pass that left an
// offscreen target bound cannot redirect the capture.
int32_t CGxDeviceD3d::IScreenShot(const char* path) {
    if (!this->m_d3dDevice || !path) {
        return 0;
    }

    LPDIRECT3DSURFACE9 source = nullptr;

    if (FAILED(this->m_d3dDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &source)) || !source) {
        return 0;
    }

    D3DSURFACE_DESC desc;
    source->GetDesc(&desc);

    LPDIRECT3DSURFACE9 staging = nullptr;

    HRESULT hr = this->m_d3dDevice->CreateOffscreenPlainSurface(
        desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &staging, nullptr);

    if (FAILED(hr)) {
        fprintf(stderr, "ScreenShot: CreateOffscreenPlainSurface(%ux%u fmt %u) failed 0x%08lX\n",
                desc.Width, desc.Height, static_cast<unsigned>(desc.Format), hr);
        source->Release();
        return 0;
    }

    hr = this->m_d3dDevice->GetRenderTargetData(source, staging);

    if (FAILED(hr)) {
        fprintf(stderr, "ScreenShot: GetRenderTargetData failed 0x%08lX\n", hr);
        staging->Release();
        source->Release();
        return 0;
    }

    D3DLOCKED_RECT locked;
    int32_t result = 0;

    if (SUCCEEDED(staging->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
        FILE* out = fopen(path, "wb");

        if (out) {
            uint32_t w = desc.Width;
            uint32_t h = desc.Height;

            unsigned char header[18] = { 0 };
            header[2] = 2; // uncompressed true-colour
            header[12] = static_cast<unsigned char>(w & 0xFF);
            header[13] = static_cast<unsigned char>((w >> 8) & 0xFF);
            header[14] = static_cast<unsigned char>(h & 0xFF);
            header[15] = static_cast<unsigned char>((h >> 8) & 0xFF);
            header[16] = 24;
            header[17] = 0x20; // top-down, so the file reads the way the frame was drawn
            fwrite(header, 1, sizeof(header), out);

            // The back buffer is X8R8G8B8 or A8R8G8B8; both are BGRA in memory, which is already
            // the byte order TGA wants, so the three colour bytes copy straight across.
            for (uint32_t y = 0; y < h; y++) {
                auto row = static_cast<const unsigned char*>(locked.pBits)
                    + static_cast<size_t>(y) * locked.Pitch;

                for (uint32_t x = 0; x < w; x++) {
                    fwrite(row + x * 4, 1, 3, out);
                }
            }

            fclose(out);
            result = 1;
        } else {
            fprintf(stderr, "ScreenShot: could not open %s for writing\n", path);
        }

        staging->UnlockRect();
    }

    staging->Release();
    source->Release();

    return result;
}

int32_t CGxDeviceD3d::IRenderTargetDump(CGxTex* texId, const char* path) {
    if (!this->m_d3dDevice || !texId) {
        return 0;
    }

    // Create it first if it is pending, exactly as IRenderTargetSet does. Every other consumer of a
    // CGxTex honours m_needsCreation; this one did not, so it read a null handle and bailed while
    // the texture was merely waiting to be rebuilt after the device reset.
    if (texId->m_needsCreation) {
        this->ITexCreate(texId);
    }

    auto d3dTexture = static_cast<LPDIRECT3DTEXTURE9>(texId->m_apiSpecificData);

    if (!d3dTexture) {
        return 0;
    }

    LPDIRECT3DSURFACE9 source = nullptr;

    fprintf(stderr, "RenderTargetDump: tex %p needsCreation %u %ux%u fmt %u target %u levels %u\n",
            static_cast<void*>(d3dTexture), texId->m_needsCreation, texId->m_width, texId->m_height,
            static_cast<unsigned>(texId->m_format), static_cast<unsigned>(texId->m_target),
            d3dTexture->GetLevelCount());

    HRESULT hr = d3dTexture->GetSurfaceLevel(0, &source);

    if (FAILED(hr)) {
        fprintf(stderr, "RenderTargetDump: GetSurfaceLevel failed 0x%08lX\n", hr);
        return 0;
    }

    D3DSURFACE_DESC desc;
    source->GetDesc(&desc);

    LPDIRECT3DSURFACE9 staging = nullptr;

    hr = this->m_d3dDevice->CreateOffscreenPlainSurface(
        desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &staging, nullptr);

    if (FAILED(hr)) {
        fprintf(stderr, "RenderTargetDump: CreateOffscreenPlainSurface(%ux%u fmt %u) failed 0x%08lX\n",
                desc.Width, desc.Height, static_cast<unsigned>(desc.Format), hr);
        source->Release();
        return 0;
    }

    hr = this->m_d3dDevice->GetRenderTargetData(source, staging);

    if (FAILED(hr)) {
        fprintf(stderr, "RenderTargetDump: GetRenderTargetData failed 0x%08lX\n", hr);
        staging->Release();
        source->Release();
        return 0;
    }

    D3DLOCKED_RECT locked;
    int32_t result = 0;

    if (SUCCEEDED(staging->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
        FILE* out = fopen(path, "wb");

        if (out) {
            uint32_t w = desc.Width;
            uint32_t h = desc.Height;

            // Uncompressed 8-bit greyscale TGA.
            unsigned char header[18] = { 0 };
            header[2] = 3;
            header[12] = static_cast<unsigned char>(w & 0xFF);
            header[13] = static_cast<unsigned char>((w >> 8) & 0xFF);
            header[14] = static_cast<unsigned char>(h & 0xFF);
            header[15] = static_cast<unsigned char>((h >> 8) & 0xFF);
            header[16] = 8;
            header[17] = 0x20; // top-down, so the image reads the way the map was rendered
            fwrite(header, 1, sizeof(header), out);

            for (uint32_t y = 0; y < h; y++) {
                auto row = static_cast<const unsigned char*>(locked.pBits) + static_cast<size_t>(y) * locked.Pitch;

                for (uint32_t x = 0; x < w; x++) {
                    float value;

                    if (desc.Format == D3DFMT_R32F) {
                        value = reinterpret_cast<const float*>(row)[x];
                    } else {
                        value = row[x * 4 + 2] / 255.0f;
                    }

                    value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
                    unsigned char texel = static_cast<unsigned char>(value * 255.0f);
                    fwrite(&texel, 1, 1, out);
                }
            }

            fclose(out);
            result = 1;
        }

        staging->UnlockRect();
    }

    staging->Release();
    source->Release();

    return result;
}

// ref: FUN_006a3070
void CGxDeviceD3d::ITexMarkAsUpdated(CGxTex* texId) {
    if (!texId->m_needsUpdate || !this->m_context) {
        return;
    }

    if (texId->m_needsCreation || (!texId->m_apiSpecificData && !texId->m_apiSpecificData2)) {
        this->ITexCreate(texId);
    }

    if (!texId->m_needsCreation && (texId->m_apiSpecificData || texId->m_apiSpecificData2)) {
        if (texId->m_userFunc) {
            this->ITexUpload(texId);
        }

        CGxDevice::ITexMarkAsUpdated(texId);
    }
}

// ref: FUN_006a2d80
// Ported from the decompilation on 2026-10-01; three behaviours changed with it.
//  - The surface is asked for by its index in the D3D texture, counted from the first uploaded
//    level, not by the absolute mip level. They differ whenever a base mip level is skipped
//    (ITexWHDStartEnd), because ITexCreate only allocates the levels it uploads.
//  - On a compressed format the 4-aligned rect is clamped to THIS level's size. It was clamped
//    to the first level's, so a 2x2 DXT mip asked for a 4x4 lock, the lock failed, and the
//    failure ended the upload: the smallest mips of every DXT texture were never written.
//  - A level the owner latches no texels for is skipped, as is every level of a render target,
//    instead of asserting.
void CGxDeviceD3d::ITexUpload(CGxTex* texId) {
    uint32_t texelStrideInBytes;
    const void* texels = nullptr;

    texId->m_userFunc(GxTex_Lock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);

    uint32_t width;
    uint32_t height;
    uint32_t startLevel;
    uint32_t endLevel;
    this->ITexWHDStartEnd(texId, width, height, startLevel, endLevel);

    bool srcCompressed = texId->m_dataFormat == GxTex_Dxt1 || texId->m_dataFormat == GxTex_Dxt3 || texId->m_dataFormat == GxTex_Dxt5;
    uint32_t numFace = texId->m_target == GxTex_CubeMap ? 6 : 1;

    for (uint32_t face = 0; face < numFace; face++) {
        uint32_t levelWidth = width;
        uint32_t levelHeight = height;
        uint32_t surfaceLevel = 0;

        for (uint32_t level = startLevel; level != endLevel; level++, surfaceLevel++) {
            texels = nullptr;

            texId->m_userFunc(GxTex_Latch, levelWidth, levelHeight, face, level, texId->m_userArg, texelStrideInBytes, texels);

            if (texels && !texId->m_flags.m_renderTarget) {
                LPDIRECT3DSURFACE9 surface = nullptr;
                HRESULT surfaceResult;

                if (texId->m_target == GxTex_CubeMap) {
                    auto d3dTexture = static_cast<LPDIRECT3DCUBETEXTURE9>(texId->m_apiSpecificData);
                    surfaceResult = d3dTexture->GetCubeMapSurface(CGxDeviceD3d::s_faceTypes[face], surfaceLevel, &surface);
                } else {
                    auto d3dTexture = static_cast<LPDIRECT3DTEXTURE9>(texId->m_apiSpecificData);
                    surfaceResult = d3dTexture->GetSurfaceLevel(surfaceLevel, &surface);
                }

                if (FAILED(surfaceResult)) {
                    goto UNLOCK;
                }

                RECT rect;
                rect.left = texId->m_updateRect.minX >> level;
                rect.top = texId->m_updateRect.minY >> level;
                rect.right = std::max(rect.left + 1, static_cast<LONG>(texId->m_updateRect.maxX >> level));
                rect.bottom = std::max(rect.top + 1, static_cast<LONG>(texId->m_updateRect.maxY >> level));

                if (texId->m_format == GxTex_Dxt1 || texId->m_format == GxTex_Dxt3 || texId->m_format == GxTex_Dxt5) {
                    rect.left &= ~3;
                    rect.top &= ~3;
                    rect.right = (rect.right + 3) & ~3;
                    rect.bottom = (rect.bottom + 3) & ~3;

                    rect.bottom = std::min(rect.bottom, static_cast<LONG>(levelHeight));
                    rect.right = std::min(rect.right, static_cast<LONG>(levelWidth));
                }

                D3DLOCKED_RECT lockedRect;
                if (FAILED(surface->LockRect(&lockedRect, &rect, 0x0))) {
                    surface->Release();
                    goto UNLOCK;
                }

                // The latched texels cover the whole level; step to the update rect, unless the
                // owner set bit 15 to say it latched the rect alone.
                auto srcTexels = static_cast<const uint8_t*>(texels);

                if (!texId->m_flags.m_bit15) {
                    if (srcCompressed) {
                        uint32_t blockBytes = GxCalcTexelStrideInBytes(texId->m_dataFormat, 4);
                        srcTexels += (rect.top >> 2) * texelStrideInBytes + (rect.left >> 2) * blockBytes;
                    } else {
                        uint32_t texelBytes = GxCalcTexelStrideInBytes(texId->m_dataFormat, 1);
                        srcTexels += rect.top * texelStrideInBytes + rect.left * texelBytes;
                    }
                }

                C2iVector size = { rect.right - rect.left, rect.bottom - rect.top };
                auto dstFormat = GxGetBlitFormat(texId->m_format);
                auto srcFormat = GxGetBlitFormat(texId->m_dataFormat);

                Blit(size, BlitAlpha_0, srcTexels, texelStrideInBytes, srcFormat, lockedRect.pBits, lockedRect.Pitch, dstFormat);

                surface->UnlockRect();
                surface->Release();
            }

            levelHeight >>= 1;
            levelWidth >>= 1;

            if (levelWidth == 0) {
                levelWidth = 1;
            }

            if (levelHeight == 0) {
                levelHeight = 1;
            }
        }
    }

UNLOCK:
    texels = nullptr;
    texId->m_userFunc(GxTex_Unlock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);

    if (!texId->m_flags.m_renderTarget) {
        static_cast<LPDIRECT3DBASETEXTURE9>(texId->m_apiSpecificData)->PreLoad();
    }
}


// The last call IStateSync makes that was not linked, found by running --diff on it: every other
// callee matched in order and this one showed as `- 006a99e0`.
//
// Identified from four things that agree. It calls DeviceCurWindow; it reads six consecutive floats
// at +0xf70..+0xf84, which is m_viewport as {x.l, x.h, y.l, y.h, z.l, z.h}, and the first two terms
// it forms are x.l * maxX + 0.5 and (1.0 - y.h) * maxY + 0.5 -- the X and Y below, constant
// included; it calls vtable 0xbc, and 0xbc / 4 = 47 = SetViewport; and it ends by storing 0 to
// +0xf6c, which is intF6C, the same flag this function clears and IStateSync tests before calling
// it.
// ref: FUN_006a99e0
void CGxDeviceD3d::IXformSetViewport() {
    const auto& gxViewport = this->m_viewport;
    auto windowRect = this->DeviceCurWindow();

    D3DVIEWPORT9 d3dViewport;

    d3dViewport.X = (gxViewport.x.l * windowRect.maxX) + 0.5;
    d3dViewport.Y = ((1.0 - gxViewport.y.h) * windowRect.maxY) + 0.5;

    // A `// TODO account for negative X value` used to stand here. It is already accounted for.
    // The reference reloads X and Y as SIGNED 32-bit and adds 4294967296.0 -- 2^32, the constant at
    // 0x009e23ac -- when the value is negative, which is nothing but the x87 idiom a compiler emits
    // to convert a DWORD to floating point. X and Y are DWORDs here, so the conversions below do
    // exactly that on their own. Two of the reference's four branches are that idiom and should not
    // be reproduced as branches.
    d3dViewport.Width = (gxViewport.x.h * windowRect.maxX) - d3dViewport.X + 0.5;
    d3dViewport.Height = ((1.0 - gxViewport.y.l) * windowRect.maxY) - d3dViewport.Y + 0.5;

    d3dViewport.MinZ = gxViewport.z.l;
    d3dViewport.MaxZ = gxViewport.z.h;

    // The other two branches are real, and this is them: rendering into a texture removes the
    // y-flip, so Y is recomputed from the LOW edge instead of one minus the high edge. Note it
    // truncates with no +0.5, unlike the rounded Y above -- the reference sets the control word to
    // truncate and stores the result straight back.
    //
    // Same divergence as IStateSyncScissorRect, for the same reason and with the same fix pending:
    // the reference tests m_textureTarget's m_apiSpecific fields (+0x2918 and +0x2924 against a
    // base of +0x2910), but frozen never stores a surface there, so m_texture is what actually
    // tracks the binding here. See the note on IStateSyncScissorRect.
    if (this->m_textureTarget[GxBuffers_Color].m_texture || this->m_textureTarget[GxBuffers_Depth].m_texture) {
        d3dViewport.Y = windowRect.maxY * gxViewport.y.l;
    }

    this->m_d3dDevice->SetViewport(&d3dViewport);

    this->intF6C = 0;
}

// ref: FUN_0069ff80
// Releases the pool's buffer and builds one at the new size. Frozen had its own version with a
// size guard and an inline release, written when the empty stub behind it caused the null-lock
// crashes; the null case is the scratch fallback's job now (IBufLock).
void CGxDeviceD3d::PoolSizeSet(CGxPool* pool, uint32_t size) {
    this->IPoolRelease(pool);
    pool->m_size = size;

    if (pool->m_target == GxPoolTarget_Vertex) {
        pool->m_apiSpecific = this->ICreateD3dVB(pool->m_usage, size);
    } else if (pool->m_target == GxPoolTarget_Index) {
        pool->m_apiSpecific = this->ICreateD3dIB(pool->m_usage, size);
    }
}

// ref: FUN_006a74b0
void CGxDeviceD3d::SceneClear(uint32_t mask, CImVector color) {
    CGxDevice::SceneClear(mask, color);

    if (!this->m_context) {
        return;
    }

    uint32_t flags = 0x0;
    if (mask & 0x1) {
        flags |= 0x1;
    }
    if (mask & 0x2) {
        flags |= 0x2;
    }

    if (this->intF6C) {
        this->IXformSetViewport();
    }

    D3DCOLOR d3dColor = ((color.a << 8 | color.r) << 8 | color.g) << 8 | color.b;

    this->m_d3dDevice->Clear(0, nullptr, flags, d3dColor, 1.0f, 0);
}

// ref: FUN_006a3450
// Ported 2026-10-01. Gained: gxFixLag (wait on an event query issued for the frame, or, with no
// query, lock the back buffer, which forces the same wait) and the maxFPS / maxFPSBk cap before
// the present, neither of which frozen had. Not ported, recorded: the hardware cursor upload
// (vtable slot 4, FUN_0068e810), the frame read-back the one-shot request at +0x2934 asks for
// (FUN_006841d0), and the NVAPI stereo convergence and separation updates.
void CGxDeviceD3d::ScenePresent() {
    if (this->m_context) {
        CGxDevice::ScenePresent();
        this->ISceneEnd();

        if (this->m_format.fixLag) {
            if (!this->m_d3dFrameQuery) {
                LPDIRECT3DSURFACE9 backBuffer;

                if (SUCCEEDED(this->m_d3dDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer))) {
                    D3DSURFACE_DESC desc;
                    backBuffer->GetDesc(&desc);

                    D3DLOCKED_RECT locked;
                    if (SUCCEEDED(backBuffer->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
                        backBuffer->UnlockRect();
                    }

                    backBuffer->Release();
                }
            } else {
                this->m_d3dFrameQuery->Issue(D3DISSUE_END);

                while (this->m_d3dFrameQuery->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {
                    OsSleep(0);
                }
            }
        }

        this->ILimitFrameRate();

        if (FAILED(this->m_d3dDevice->Present(nullptr, nullptr, nullptr, nullptr))) {
            this->m_context = 0;
        }
    }

    this->ISceneBegin();
}

// ref: FUN_006aa130
void CGxDeviceD3d::ShaderCreate(CGxShader* shaders[], EGxShTarget target, const char* a4, const char* a5, int32_t permutations) {
    CGxDevice::ShaderCreate(shaders, target, a4, a5, permutations);

    if (permutations == 1 && !shaders[0]->loaded) {
        this->IShaderCreate(shaders[0]);
    }
}

int32_t CGxDeviceD3d::StereoEnabled() {
    // TODO
    return 0;
}

// ref: FUN_006a9b40
// Stores the application projection, then derives the native one: normalized by _34, the depth
// rows remapped from [-1, 1] to D3D's [0, 1], and scaled by 0.2 unless NormalProjection is on.
void CGxDeviceD3d::XformSetProjection(const C44Matrix& matrix) {
    this->m_projection = matrix;
    DirectX::XMMATRIX projNative;
    memcpy(&projNative, &matrix, sizeof(projNative));

    if (NotEqual(projNative._34, 1.0f, WHOA_EPSILON_1) && NotEqual(projNative._34, 0.0f, WHOA_EPSILON_1)) {
        projNative /= projNative._34;
    }

    if (projNative._44 == 0.0f) {
        auto v5 = -(projNative._43 / (projNative._33 + 1.0f));
        auto v6 = -(projNative._43 / (projNative._33 - 1.0f));
        projNative._33 = v6 / (v6 - v5);
        projNative._43 = v6 * v5 / (v5 - v6);
    } else {
        auto v8 = 1.0f / projNative._33;
        auto v9 = (-1.0f - projNative._43) * v8;
        auto v10 = v8 * (1.0f - projNative._43);
        projNative._33 = 1.0f / (v10 - v9);
        projNative._43 = v9 / (v9 - v10);
    }

    if (!this->MasterEnable(GxMasterEnable_NormalProjection) && projNative._44 != 1.0f) {
        DirectX::XMMATRIX shrink = {
            0.2f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.2f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.2f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

        projNative *= shrink;
    }

    this->m_xforms[GxXform_Projection].m_dirty = 1;
    memcpy(&this->m_projNative, &projNative, sizeof(this->m_projNative));
}

// ref: FUN_006a9e00
void CGxDeviceD3d::XformSetView(const C44Matrix& matrix) {
    CGxDevice::XformSetView(matrix);
}

// ref: FUN_006aa190
// The last reference to a shader releases its D3D object before the device frees the shader.
// Frozen had no override, so every destroyed shader leaked its D3D object.
void CGxDeviceD3d::ShaderDestroy(CGxShader** shader) {
    auto s = *shader;

    if ((s->target == GxSh_Vertex || s->target == GxSh_Pixel) && s->refCount == 1) {
        if (s->apiSpecific) {
            static_cast<IUnknown*>(s->apiSpecific)->Release();
            s->apiSpecific = nullptr;
        }

        s->loaded = 0;
    }

    CGxDevice::ShaderDestroy(shader);
}

// ref: FUN_006a5d50
// Reloads a vertex or pixel shader from its file and re-creates it: the last reference first drops
// its D3D object.
void CGxDeviceD3d::ShaderReload(CGxShader* shader, const char* path, const char* name) {
    if (shader->target != GxSh_Vertex && shader->target != GxSh_Pixel) {
        return;
    }

    if (shader->refCount == 1) {
        if (shader->apiSpecific) {
            static_cast<IUnknown*>(shader->apiSpecific)->Release();
            shader->apiSpecific = nullptr;
        }

        shader->loaded = 0;
    }

    this->IShaderLoad(&shader, static_cast<EGxShTarget>(shader->target), path, name, 1);

    if (shader->target == GxSh_Vertex) {
        this->IShaderCreateVertex(shader);
    } else {
        this->IShaderCreatePixel(shader);
    }
}

// ref: FUN_0069fe80
// Fullscreen only: D3D9 applies a gamma ramp to a fullscreen swap chain and ignores it windowed.
void CGxDeviceD3d::DeviceSetGammaRamp(const CGxGammaRamp& ramp) {
    CGxDevice::DeviceSetGammaRamp(ramp);

    if ((this->m_d3dCaps.Caps2 & D3DCAPS2_FULLSCREENGAMMA) && !this->IDevIsWindowed()) {
        this->m_d3dDevice->SetGammaRamp(0, 0, reinterpret_cast<const D3DGAMMARAMP*>(&this->m_gammaRamp));
    }
}

// ref: FUN_0068e4c0
void CGxDeviceD3d::DeviceSetGamma(float gamma) {
    CGxDevice::DeviceSetGamma(gamma);

    if ((this->m_d3dCaps.Caps2 & D3DCAPS2_FULLSCREENGAMMA) && !this->IDevIsWindowed()) {
        this->m_d3dDevice->SetGammaRamp(0, 0, reinterpret_cast<const D3DGAMMARAMP*>(&this->m_gammaRamp));
    }
}

// ref: FUN_0069ff40
// Override 0 caps the pixel shader target; override 7 sets the non-pow2 restriction.
void CGxDeviceD3d::DeviceOverride(int32_t which, uint32_t value) {
    CGxDevice::DeviceOverride(which, value);

    if (which == 0) {
        this->m_caps.m_shaderTargets[GxSh_Pixel] = value;
    } else if (which == 7) {
        this->m_caps.m_texNonPow2Conditional = value != 0;
    }
}

// ref: FUN_0068e720
// Releases the D3D buffer and frees the pool. Its buffers are not freed here: they belong to
// whoever made them, who destroys them through BufDestroy.
void CGxDeviceD3d::PoolDestroy(CGxPool* pool) {
    if (pool) {
        this->IPoolRelease(pool);
        pool->~CGxPool();
        SMemFree(pool, __FILE__, __LINE__, 0x0);
    }
}

// ref: FUN_0068e9c0
void CGxDeviceD3d::QueryCreate(CGxQuery*& query, uint32_t type) {
    CGxDevice::QueryCreate(query, type);

    if (!query->m_apiSpecific) {
        LPDIRECT3DQUERY9 d3dQuery;

        if (this->m_d3dDevice->CreateQuery(s_gxQueryToD3dQuery[query->m_type], &d3dQuery) == D3D_OK) {
            query->m_apiSpecific = d3dQuery;
        }
    }
}

// ref: FUN_006a0190
void CGxDeviceD3d::QueryDestroy(CGxQuery*& query) {
    if (query->m_apiSpecific) {
        static_cast<LPDIRECT3DQUERY9>(query->m_apiSpecific)->Release();
    }

    query->m_apiSpecific = nullptr;

    CGxDevice::QueryDestroy(query);
}

// ref: FUN_0068ea10
// A query whose object went with a reset is made again here.
int32_t CGxDeviceD3d::QueryBegin(CGxQuery* query) {
    if (!this->m_context) {
        return 0;
    }

    if (!query->m_apiSpecific) {
        LPDIRECT3DQUERY9 d3dQuery;

        if (this->m_d3dDevice->CreateQuery(s_gxQueryToD3dQuery[query->m_type], &d3dQuery) == D3D_OK) {
            query->m_apiSpecific = d3dQuery;
        }

        if (!query->m_apiSpecific) {
            return 0;
        }
    }

    HRESULT result = D3D_OK;

    if (query->m_type == 0) {
        result = static_cast<LPDIRECT3DQUERY9>(query->m_apiSpecific)->Issue(D3DISSUE_BEGIN);
    }

    return result == D3D_OK;
}

// ref: FUN_006a0240
int32_t CGxDeviceD3d::QueryEnd(CGxQuery* query) {
    if (!this->m_context || !query->m_apiSpecific) {
        return 0;
    }

    HRESULT result = D3D_OK;

    if (query->m_type == 0) {
        result = static_cast<LPDIRECT3DQUERY9>(query->m_apiSpecific)->Issue(D3DISSUE_END);
    }

    return result == D3D_OK;
}

// ref: FUN_0068ea90
// 0 asks for the size of the result; 1 asks whether it has arrived, without waiting.
int32_t CGxDeviceD3d::QueryGetParam(CGxQuery* query, uint32_t which, uint32_t& value) {
    if (!this->m_context) {
        return 0;
    }

    if (!query->m_apiSpecific) {
        LPDIRECT3DQUERY9 d3dQuery;

        if (this->m_d3dDevice->CreateQuery(s_gxQueryToD3dQuery[query->m_type], &d3dQuery) == D3D_OK) {
            query->m_apiSpecific = d3dQuery;
        }

        if (!query->m_apiSpecific) {
            return 0;
        }
    }

    auto d3dQuery = static_cast<LPDIRECT3DQUERY9>(query->m_apiSpecific);

    if (which == 0) {
        value = d3dQuery->GetDataSize();
        return 1;
    }

    if (which == 1) {
        value = d3dQuery->GetData(nullptr, 0, 0) == S_OK;
    }

    return 0;
}

// ref: FUN_006a0310
int32_t CGxDeviceD3d::QueryGetData(CGxQuery* query, void* data) {
    if (!this->m_context) {
        return 0;
    }

    auto d3dQuery = static_cast<LPDIRECT3DQUERY9>(query->m_apiSpecific);

    if (!d3dQuery) {
        return 0;
    }

    HRESULT result;

    do {
        result = d3dQuery->GetData(data, d3dQuery->GetDataSize(), D3DGETDATA_FLUSH);
    } while (result == S_FALSE);

    return result == S_OK;
}

// ref: FUN_006a1690
// A reset destroys every query object; the queries themselves stay and re-create on next use.
void CGxDeviceD3d::IReleaseD3dQueries() {
    for (auto query = this->m_queryList.Head(); query; query = this->m_queryList.Next(query)) {
        if (query->m_apiSpecific) {
            static_cast<LPDIRECT3DQUERY9>(query->m_apiSpecific)->Release();
        }

        query->m_apiSpecific = nullptr;
    }
}
