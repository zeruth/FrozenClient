#include "gx/d3d/CGxDeviceD3d.hpp"
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

CGxDeviceD3d::CGxDeviceD3d() : CGxDevice() {
    // TODO

    this->m_api = GxApi_D3d9;

    // TODO

    memset(this->m_deviceStates, 0xFF, sizeof(this->m_deviceStates));

    // TODO

    this->DeviceCreatePools();
    this->DeviceCreateStreamBufs();
}

char* CGxDeviceD3d::BufLock(CGxBuf* buf) {
    CGxDevice::BufLock(buf);
    return this->IBufLock(buf);
}

int32_t CGxDeviceD3d::BufUnlock(CGxBuf* buf, uint32_t size) {
    CGxDevice::BufUnlock(buf, size);
    this->IBufUnlock(buf);

    return 1;
}

void CGxDeviceD3d::BufData(CGxBuf* buf, const void* data, size_t size, uintptr_t offset) {
    CGxDevice::BufData(buf, data, size, offset);

    auto bufData = this->IBufLock(buf);

    // A failed lock returns null, and &bufData[offset] is then a small address rather than an
    // obviously bad one: this crashed as a write to 0x20, which is simply offset 0x20 from null.
    // That is the third place in this backend where an unchecked lock result was written through.
    if (!bufData) {
        return;
    }

    memcpy(&bufData[offset], data, size);
    this->IBufUnlock(buf);
}

void CGxDeviceD3d::CapsWindowSize(CRect& dst) {
    dst = this->DeviceCurWindow();
}

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

int32_t CGxDeviceD3d::CreatePoolAPI(CGxPool* pool) {
    if (pool->m_target == GxPoolTarget_Vertex) {
        pool->m_apiSpecific = this->ICreateD3dVB(pool->m_usage, pool->m_size);
    } else if (pool->m_target == GxPoolTarget_Index) {
        pool->m_apiSpecific = this->ICreateD3dIB(pool->m_usage, pool->m_size);
    }

    return 1;
}

int32_t CGxDeviceD3d::DeviceCreate(int32_t (*windowProc)(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam), const CGxFormat& format) {
    this->m_ownhwnd = 1;

    // TODO gamma ramp

    this->m_hwndClass = WindowClassCreate();

    if (this->m_hwndClass) {
        if (this->ICreateD3d() && this->CGxDevice::DeviceCreate(windowProc, format)) {
            return 1;
        } else {
            // TODO
            // this->DeviceDestroy();
            return 0;
        }
    }

    // TODO CGxDevice::Log("CGxDeviceD3d::DeviceCreate(): WindowClassCreate() failed: %s", OsGetLastErrorStr());
    // TODO this->DeviceDestroy();

    return 0;
}

int32_t CGxDeviceD3d::DeviceSetFormat(const CGxFormat& format) {
    CGxDevice::Log("CGxDeviceD3d::DeviceSetFormat():");
    CGxDevice::Log(format);

    if (this->m_hwnd) {
        ShowWindow(this->m_hwnd, 0);
    }

    // TODO

    if (this->m_hwnd) {
        DestroyWindow(this->m_hwnd);
    }

    this->m_hwnd = nullptr;

    this->m_format = format;

    CGxFormat createFormat = format;

    if (this->ICreateWindow(createFormat) && this->ICreateD3dDevice(createFormat) && this->CGxDevice::DeviceSetFormat(format)) {
        this->intF64 = 1;

        // TODO

        if (this->m_format.window == 0) {
            RECT windowRect;
            GetWindowRect(this->m_hwnd, &windowRect);
            ClipCursor(&windowRect);
        }

        return 1;
    }
}

void* CGxDeviceD3d::DeviceWindow() {
    return this->m_hwnd;
}

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
                    // TODO

                    this->m_context = 1;
                    this->intF5C = 0;

                    // TODO

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

void CGxDeviceD3d::Draw(CGxBatch* batch, int32_t indexed) {

    if (!this->m_context || this->intF5C) {
        return;
    }

    this->IStateSync();

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

void CGxDeviceD3d::DsSet(EDeviceState state, uint32_t val) {
    if (this->m_deviceStates[state] == val) {
        return;
    }

    switch (state) {
    // TODO handle other device states

    case Ds_SrcBlend: {
        this->m_d3dDevice->SetRenderState(D3DRS_SRCBLEND, val);
        break;
    }

    case Ds_DstBlend: {
        this->m_d3dDevice->SetRenderState(D3DRS_DESTBLEND, val);
        break;
    }

    case Ds_TssMagFilter0:
    case Ds_TssMagFilter1:
    case Ds_TssMagFilter2:
    case Ds_TssMagFilter3:
    case Ds_TssMagFilter4:
    case Ds_TssMagFilter5:
    case Ds_TssMagFilter6:
    case Ds_TssMagFilter7:
    case Ds_TssMagFilter8:
    case Ds_TssMagFilter9:
    case Ds_TssMagFilter10:
    case Ds_TssMagFilter11:
    case Ds_TssMagFilter12:
    case Ds_TssMagFilter13:
    case Ds_TssMagFilter14:
    case Ds_TssMagFilter15: {
        auto tmu = state - Ds_TssMagFilter0;
        this->m_d3dDevice->SetSamplerState(tmu, D3DSAMP_MAGFILTER, val);

        break;
    }

    case Ds_TssMinFilter0:
    case Ds_TssMinFilter1:
    case Ds_TssMinFilter2:
    case Ds_TssMinFilter3:
    case Ds_TssMinFilter4:
    case Ds_TssMinFilter5:
    case Ds_TssMinFilter6:
    case Ds_TssMinFilter7:
    case Ds_TssMinFilter8:
    case Ds_TssMinFilter9:
    case Ds_TssMinFilter10:
    case Ds_TssMinFilter11:
    case Ds_TssMinFilter12:
    case Ds_TssMinFilter13:
    case Ds_TssMinFilter14:
    case Ds_TssMinFilter15: {
        auto tmu = state - Ds_TssMinFilter0;
        this->m_d3dDevice->SetSamplerState(tmu, D3DSAMP_MINFILTER, val);

        break;
    }

    case Ds_TssMipFilter0:
    case Ds_TssMipFilter1:
    case Ds_TssMipFilter2:
    case Ds_TssMipFilter3:
    case Ds_TssMipFilter4:
    case Ds_TssMipFilter5:
    case Ds_TssMipFilter6:
    case Ds_TssMipFilter7:
    case Ds_TssMipFilter8:
    case Ds_TssMipFilter9:
    case Ds_TssMipFilter10:
    case Ds_TssMipFilter11:
    case Ds_TssMipFilter12:
    case Ds_TssMipFilter13:
    case Ds_TssMipFilter14:
    case Ds_TssMipFilter15: {
        auto tmu = state - Ds_TssMipFilter0;
        this->m_d3dDevice->SetSamplerState(tmu, D3DSAMP_MIPFILTER, val);

        break;
    }

    case Ds_TssWrapU0:
    case Ds_TssWrapU1:
    case Ds_TssWrapU2:
    case Ds_TssWrapU3:
    case Ds_TssWrapU4:
    case Ds_TssWrapU5:
    case Ds_TssWrapU6:
    case Ds_TssWrapU7:
    case Ds_TssWrapU8:
    case Ds_TssWrapU9:
    case Ds_TssWrapU10:
    case Ds_TssWrapU11:
    case Ds_TssWrapU12:
    case Ds_TssWrapU13:
    case Ds_TssWrapU14:
    case Ds_TssWrapU15: {
        auto tmu = state - Ds_TssWrapU0;
        this->m_d3dDevice->SetSamplerState(tmu, D3DSAMP_ADDRESSU, val);

        break;
    }

    case Ds_TssWrapV0:
    case Ds_TssWrapV1:
    case Ds_TssWrapV2:
    case Ds_TssWrapV3:
    case Ds_TssWrapV4:
    case Ds_TssWrapV5:
    case Ds_TssWrapV6:
    case Ds_TssWrapV7:
    case Ds_TssWrapV8:
    case Ds_TssWrapV9:
    case Ds_TssWrapV10:
    case Ds_TssWrapV11:
    case Ds_TssWrapV12:
    case Ds_TssWrapV13:
    case Ds_TssWrapV14:
    case Ds_TssWrapV15: {
        auto tmu = state - Ds_TssWrapV0;
        this->m_d3dDevice->SetSamplerState(tmu, D3DSAMP_ADDRESSV, val);

        break;
    }

    case Ds_AlphaBlendEnable: {
        this->m_d3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, val);
        break;
    }

    case Ds_AlphaTestEnable: {
        this->m_d3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, val);
        break;
    }

    case Ds_ColorWriteEnable: {
        this->m_d3dDevice->SetRenderState(D3DRS_COLORWRITEENABLE, val);
        break;
    }

    case Ds_AlphaRef: {
        this->m_d3dDevice->SetRenderState(D3DRS_ALPHAREF, val);
        break;
    }

    case Ds_ZWriteEnable: {
        this->m_d3dDevice->SetRenderState(D3DRS_ZWRITEENABLE, val);
        break;
    }

    case Ds_CullMode: {
        this->m_d3dDevice->SetRenderState(D3DRS_CULLMODE, val);
        break;
    }

    case Ds_ZFunc: {
        this->m_d3dDevice->SetRenderState(D3DRS_ZFUNC, val);
        break;
    }
    }

    this->m_deviceStates[state] = val;
}

char* CGxDeviceD3d::IBufLock(CGxBuf* buf) {
    if (!this->m_context) {
        // TODO
        return nullptr;
    }

    auto pool = buf->m_pool;
    uint32_t lockFlags = 0x0;

    if (pool->m_usage == GxPoolUsage_Stream) {
        auto v6 = buf->m_itemSize + pool->unk1C - 1 - (buf->m_itemSize + pool->unk1C - 1) % buf->m_itemSize;
        if (buf->m_size + v6 <= pool->m_size) {
            lockFlags = D3DLOCK_NOOVERWRITE;
            buf->m_index = v6;
            pool->unk1C = buf->m_size + v6;
        } else {
            lockFlags = D3DLOCK_DISCARD;
            pool->Discard();
            buf->m_index = 0;
            pool->unk1C = buf->m_size;
        }
    } else if (pool->m_usage == GxPoolUsage_Dynamic) {
        lockFlags = D3DLOCK_NOOVERWRITE;
    }

    if (!pool->m_apiSpecific) {
        this->CreatePoolAPI(pool);
    }

    if (!pool->m_apiSpecific) {
        // TODO
        return nullptr;
    }

    // Invalid target
    if (pool->m_target >= GxPoolTargets_Last) {
        return nullptr;
    }

    char* data = nullptr;
    HRESULT lockResult = S_OK;

    if (pool->m_target == GxPoolTarget_Vertex) {
        auto d3dBuf = static_cast<LPDIRECT3DVERTEXBUFFER9>(pool->m_apiSpecific);
        lockResult = d3dBuf->Lock(buf->m_index, buf->m_size, reinterpret_cast<void**>(&data), lockFlags);
    } else if (pool->m_target == GxPoolTarget_Index) {
        auto d3dBuf = static_cast<LPDIRECT3DINDEXBUFFER9>(pool->m_apiSpecific);
        lockResult = d3dBuf->Lock(buf->m_index, buf->m_size, reinterpret_cast<void**>(&data), lockFlags);
    }

    if (SUCCEEDED(lockResult)) {
        if (buf->m_size) {
            // TODO

            if (pool->m_usage == GxPoolUsage_Stream) {
                *data = 0;
            } else {
                *data = *data;
            }

            // TODO
        }
    } else {
        this->IBufUnlock(buf);

        // TODO
        return nullptr;
    }

    return data;
}

void CGxDeviceD3d::IBufUnlock(CGxBuf* buf) {
    // TODO

    auto pool = buf->m_pool;

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

    this->IDestroyD3d();

    return 0;
}

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

LPDIRECT3DVERTEXBUFFER9 CGxDeviceD3d::ICreateD3dVB(EGxPoolUsage usage, uint32_t size) {
    uint32_t d3dUsage = this->m_d3dIsHwDevice ? D3DUSAGE_WRITEONLY : D3DUSAGE_SOFTWAREPROCESSING;
    D3DPOOL d3dPool = D3DPOOL_MANAGED;

    if (usage == GxPoolUsage_Dynamic || usage == GxPoolUsage_Stream) {
        d3dUsage |= D3DUSAGE_DYNAMIC;
        d3dPool = D3DPOOL_DEFAULT;
    }

    LPDIRECT3DVERTEXBUFFER9 vertexBuf = nullptr;

    if (SUCCEEDED(this->m_d3dDevice->CreateVertexBuffer(size, d3dUsage, D3DFMT_INDEX16, d3dPool, &vertexBuf, nullptr))) {
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

    // TODO

    RECT clientArea = {
        0,             // left
        0,             // top
        format.size.x, // right
        format.size.y  // bottom
    };
    AdjustWindowRectEx(&clientArea, dwStyle, false, 0);
    CGxDeviceD3d::s_clientAdjustWidth = clientArea.right - format.size.x - clientArea.left;
    CGxDeviceD3d::s_clientAdjustHeight = clientArea.bottom - format.size.y - clientArea.top;

    // TODO

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

void CGxDeviceD3d::IDestroyD3d() {
    this->IDestroyD3dDevice();
    CGxDeviceD3d::IUnloadD3dLib(this->m_d3dLib, this->m_d3d);
}

void CGxDeviceD3d::IDestroyD3dDevice() {
    // TODO
}

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
}

namespace {

// Direct3D destroys every D3DPOOL_DEFAULT resource on a device reset, and a render target texture
// has to be in that pool. Nothing here tracked them, so after the first window resize their CGxTex
// still held the dead pointer with m_needsCreation clear, and every later bind and read-back used
// it. The symptom was baffling: the texture reported 1024x1024 in the right format, yet
// GetLevelCount() answered 0 and GetSurfaceLevel returned D3DERR_INVALIDCALL, because the object
// behind the pointer was gone. The shadow map had therefore not rendered at all since that reset.
//
// A flat array is enough: there are only ever a handful of render targets, and they are created
// once.
const int32_t MAX_TRACKED_TARGETS = 32;
CGxTex* s_renderTargets[MAX_TRACKED_TARGETS] = { nullptr };

void TrackRenderTarget(CGxTex* texId) {
    if (!texId->m_flags.m_renderTarget) {
        return;
    }

    for (int32_t i = 0; i < MAX_TRACKED_TARGETS; i++) {
        if (s_renderTargets[i] == texId) {
            return;
        }

        if (!s_renderTargets[i]) {
            s_renderTargets[i] = texId;
            return;
        }
    }

    fprintf(stderr, "ITexCreate: more than %d render targets; the rest will not survive a device reset\n",
            MAX_TRACKED_TARGETS);
}

void ForgetRenderTargets() {
    for (int32_t i = 0; i < MAX_TRACKED_TARGETS; i++) {
        auto texId = s_renderTargets[i];

        if (!texId) {
            continue;
        }

        if (texId->m_apiSpecificData) {
            // Through IUnknown: the tracker holds plain, cube and depth-stencil textures, and they
            // are not all LPDIRECT3DTEXTURE9. Release lives on IUnknown, so this is correct for all
            // three instead of relying on the vtables happening to line up.
            static_cast<IUnknown*>(texId->m_apiSpecificData)->Release();
            texId->m_apiSpecificData = nullptr;
        }

        // Rebuilt lazily: every bind already calls ITexCreate when this is set.
        texId->m_needsCreation = 1;
    }
}

} // namespace

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

    // Then drop the tracked render-target textures, so the reset leaves no dangling handles.
    ForgetRenderTargets();

    // TODO

    this->IReleaseD3dPools(a2);

    memset(this->m_deviceStates, 0xFF, sizeof(this->m_deviceStates));

    if (this->m_defColorSurface) {
        this->m_defColorSurface->Release();
        this->m_defColorSurface = nullptr;
    }

    if (this->m_defDepthSurface) {
        this->m_defDepthSurface->Release();
        this->m_defDepthSurface = nullptr;
    }

    // TODO

    if (this->m_d3dDevice) {
        this->m_d3dDevice->ShowCursor(false);
    }
}

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

        this->DsSet(Ds_ColorWriteEnable, mask);

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

    case GxRs_Fog: {
        auto fogEnable = static_cast<uint32_t>(state->m_value) != 0;
        this->m_d3dDevice->SetRenderState(D3DRS_FOGENABLE, fogEnable);

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

    default:
        break;
    }
}

void CGxDeviceD3d::ISceneBegin() {
    if (this->m_context) {
        this->ShaderConstantsClear();

        if (SUCCEEDED(this->m_d3dDevice->BeginScene())) {
            this->m_inScene = 1;
        }

        return;
    }

    // TODO
}

void CGxDeviceD3d::ISceneEnd() {
    if (this->m_inScene) {
        this->m_d3dDevice->EndScene();
        this->m_inScene = 0;
    }
}

void CGxDeviceD3d::ISetCaps(const CGxFormat& format) {
    // Texture stages

    int32_t maxSimultaneousTextures = this->m_d3dCaps.MaxSimultaneousTextures;
    this->m_caps.m_numTmus = std::min(maxSimultaneousTextures, 8);

    // Rasterization rules

    this->m_caps.m_pixelCenterOnEdge = 0;
    this->m_caps.m_texelCenterOnEdge = 1;

    // Max texture size

    uint32_t maxTextureWidth = this->m_d3dCaps.MaxTextureWidth;
    this->m_caps.m_texMaxSize[GxTex_2d] = std::max(maxTextureWidth, 256u);
    this->m_caps.m_texMaxSize[GxTex_CubeMap] = std::max(maxTextureWidth, 256u);
    this->m_caps.m_texMaxSize[GxTex_Rectangle] = std::max(maxTextureWidth, 256u);
    this->m_caps.m_texMaxSize[GxTex_NonPow2] = std::max(maxTextureWidth, 256u);

    // Max vertex index

    this->m_caps.m_maxIndex = this->m_d3dCaps.MaxVertexIndex;

    // Trilinear filtering

    this->m_caps.m_texFilterTrilinear =
        (this->m_d3dCaps.TextureFilterCaps & D3DPTFILTERCAPS_MIPFLINEAR) != 0;

    // Anisotropic filtering

    this->m_caps.m_texFilterAnisotropic =
        (this->m_d3dCaps.TextureFilterCaps & (D3DPTFILTERCAPS_MINFANISOTROPIC | D3DPTFILTERCAPS_MAGFANISOTROPIC)) != 0;

    if (this->m_d3dCaps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC) {
        CGxDeviceD3d::s_filterModes[GxTex_Anisotropic][0] = D3DTEXF_ANISOTROPIC;
    }

    if (this->m_d3dCaps.TextureFilterCaps & D3DPTFILTERCAPS_MAGFANISOTROPIC) {
        CGxDeviceD3d::s_filterModes[GxTex_Anisotropic][1] = D3DTEXF_ANISOTROPIC;
    }

    this->m_caps.m_maxTexAnisotropy = this->m_d3dCaps.MaxAnisotropy;

    if (this->m_caps.m_texFilterAnisotropic && this->m_d3dCaps.MaxAnisotropy < 2) {
        this->m_caps.m_texFilterAnisotropic = 0;
    }

    // Misc capabilities

    this->m_caps.m_depthBias = (this->m_d3dCaps.RasterCaps & D3DPRASTERCAPS_DEPTHBIAS) != 0;
    this->m_caps.m_numStreams = this->m_d3dCaps.MaxStreams;
    this->m_caps.int10 = (this->m_d3dCaps.Caps2 & 1) != 0; // unknown caps flag

    // Shader targets

    auto pixelShaderVersion = this->m_d3dCaps.PixelShaderVersion;

    if (pixelShaderVersion >= D3DPS_VERSION(3, 0)) {
        this->m_caps.m_shaderTargets[GxSh_Pixel] = GxShPS_ps_3_0;
    } else if (pixelShaderVersion >= D3DPS_VERSION(2, 0)) {
        this->m_caps.m_shaderTargets[GxSh_Pixel] = GxShPS_ps_2_0;
    } else if (pixelShaderVersion >= D3DPS_VERSION(1, 4)) {
        this->m_caps.m_shaderTargets[GxSh_Pixel] = GxShPS_ps_1_4;
    } else if (pixelShaderVersion >= D3DPS_VERSION(1, 1)) {
        this->m_caps.m_shaderTargets[GxSh_Pixel] = GxShPS_ps_1_1;
    }

    if (this->m_caps.m_shaderTargets[GxSh_Pixel] != GxShPS_none) {
        auto vertexShaderVersion = this->m_d3dCaps.VertexShaderVersion;

        if (vertexShaderVersion >= D3DVS_VERSION(3, 0)) {
            this->m_caps.m_shaderTargets[GxSh_Vertex] = GxShVS_vs_3_0;
        } else if (vertexShaderVersion >= D3DVS_VERSION(2, 0)) {
            this->m_caps.m_shaderTargets[GxSh_Vertex] = GxShVS_vs_2_0;
        } else if (vertexShaderVersion == D3DVS_VERSION(1, 1)) {
            this->m_caps.m_shaderTargets[GxSh_Vertex] = GxShVS_vs_1_1;
        }

        // TODO maxVertexShaderConst
    }

    // TODO modify shader targets based on format

    // Texture formats

    for (int32_t i = 0; i < GxTexFormats_Last; i++) {
        if (i == GxTex_Unknown) {
            this->m_caps.m_texFmt[i] = 0;
        } else {
            this->m_caps.m_texFmt[i] = this->m_d3d->CheckDeviceFormat(
                0,
                D3DDEVTYPE_HAL,
                this->m_devAdapterFormat,
                0,
                D3DRTYPE_TEXTURE,
                CGxDeviceD3d::s_GxTexFmtToD3dFmt[i]
            ) == D3D_OK;
        }
    }

    this->m_caps.m_generateMipMaps = (this->m_d3dCaps.Caps2 & D3DCAPS2_CANAUTOGENMIPMAP) != 0;

    // TODO

    // Texture targets

    this->m_caps.m_texTarget[GxTex_2d] = 1;
    this->m_caps.m_texTarget[GxTex_CubeMap] = (this->m_d3dCaps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) != 0;
    this->m_caps.m_texTarget[GxTex_Rectangle] = 0;
    this->m_caps.m_texTarget[GxTex_NonPow2] =
        (this->m_d3dCaps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) != 0 || (this->m_d3dCaps.TextureCaps & D3DPTEXTURECAPS_POW2) == 0;

    // TODO
}

void CGxDeviceD3d::ISetPresentParms(D3DPRESENT_PARAMETERS& d3dpp, const CGxFormat& format) {
    memset(&d3dpp, 0, sizeof(d3dpp));

    if (format.window) {
        D3DDISPLAYMODE currentMode;
        D3DFORMAT backBufferFormat;
        if (SUCCEEDED(this->m_d3d->GetAdapterDisplayMode(0, &currentMode))) {
            backBufferFormat = currentMode.Format;
        } else {
            backBufferFormat = this->m_desktopDisplayMode.Format;
        }

        auto& windowRect = this->DeviceCurWindow();

        d3dpp.Windowed = true;
        d3dpp.BackBufferWidth = windowRect.maxX;
        d3dpp.BackBufferHeight = windowRect.maxY;
        d3dpp.BackBufferFormat = backBufferFormat;

        if (format.vsync) {
            // TODO d3dpp.BackBufferCount = format.int1C;
            d3dpp.BackBufferCount = 1;
        } else {
            d3dpp.BackBufferCount = 1;
        }

        d3dpp.FullScreen_RefreshRateInHz = 0;
    } else {
        d3dpp.BackBufferWidth = format.size.x;
        d3dpp.BackBufferHeight = format.size.y;
        d3dpp.BackBufferFormat = CGxDeviceD3d::s_GxFormatToD3dFormat[format.colorFormat];
        d3dpp.FullScreen_RefreshRateInHz = format.refreshRate;
    }

    d3dpp.hDeviceWindow = this->m_hwnd;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.EnableAutoDepthStencil = true;
    d3dpp.AutoDepthStencilFormat = CGxDeviceD3d::s_GxFormatToD3dFormat[format.depthFormat];

    switch (format.vsync) {
    case 1:
        d3dpp.PresentationInterval = 1;
        break;
    case 2:
        d3dpp.PresentationInterval = format.window ? 1 : 2;
        break;
    case 3:
        d3dpp.PresentationInterval = format.window ? 1 : 4;
        break;
    case 4:
        d3dpp.PresentationInterval = format.window ? 1 : 8;
        break;
    default:
        d3dpp.PresentationInterval = CW_USEDEFAULT;
        break;
    }

    if (format.multisampleCount <= 1) {
        d3dpp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    } else {
        d3dpp.MultiSampleType = static_cast<D3DMULTISAMPLE_TYPE>(format.multisampleCount);

        // TODO MultiSampleQuality
    }
}

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

        if (tmu < 8) {
            // TODO FFP
        }
    } else {
        this->m_d3dDevice->SetTexture(tmu, nullptr);

        if (tmu < 8) {
            // TODO FFP
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

void CGxDeviceD3d::IShaderBindPixel(CGxShader* shader) {
    if (!shader) {
        this->m_d3dDevice->SetPixelShader(nullptr);

        // TODO FFP handling

        return;
    }

    if (!shader->loaded) {
        this->IShaderCreatePixel(shader);
    }

    auto d3dShader = static_cast<LPDIRECT3DPIXELSHADER9>(shader->apiSpecific);
    this->m_d3dDevice->SetPixelShader(d3dShader);
}

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

void CGxDeviceD3d::IShaderCreate(CGxShader* shader) {
    if (shader->target == GxSh_Vertex) {
        this->IShaderCreateVertex(shader);
    } else if (shader->target == GxSh_Pixel) {
        this->IShaderCreatePixel(shader);
    }
}

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

void CGxDeviceD3d::IStateSetD3dDefaults() {
    this->m_d3dDevice->SetRenderState(D3DRS_ZENABLE, 1);
    this->m_d3dDevice->SetRenderState(D3DRS_LOCALVIEWER, 1);
    this->m_d3dDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    this->m_d3dDevice->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
    this->m_d3dDevice->SetRenderState(D3DRS_FOGDENSITY, 0);
    // Linear pixel (table) fog so fog applies to shader-lit geometry by view depth; only takes
    // effect while D3DRS_FOGENABLE is on (set per-frame during the world render).
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

    // TODO

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

    // TODO

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

void CGxDeviceD3d::IStateSyncLights() {
    // TODO
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

void CGxDeviceD3d::IStateSyncXforms() {
    if (this->m_xforms[GxXform_Projection].m_dirty) {
        this->m_d3dDevice->SetTransform(D3DTS_PROJECTION, reinterpret_cast<D3DMATRIX*>(&this->m_projNative));
        this->m_xforms[GxXform_Projection].m_dirty = 0;
    }

    if (this->m_xforms[GxXform_View].m_dirty) {
        this->m_d3dDevice->SetTransform(D3DTS_VIEW, reinterpret_cast<const D3DMATRIX*>(&this->m_xforms[GxXform_View].TopConst()));
        this->m_xforms[GxXform_View].m_dirty = 0;
    }

    // TODO world

    // TODO tex
}

void CGxDeviceD3d::ITexCreate(CGxTex* texId) {
    uint32_t width, height, startLevel, endLevel;
    this->ITexWHDStartEnd(texId, width, height, startLevel, endLevel);

    texId->m_format = CGxDeviceD3d::s_GxTexFmtToUse[texId->m_format];

    uint32_t d3dUsage = 0;
    D3DPOOL d3dPool = D3DPOOL_MANAGED;

    if (texId->m_flags.m_renderTarget) {
        d3dUsage = D3DUSAGE_RENDERTARGET;
        d3dPool = D3DPOOL_DEFAULT;
    }

    if (texId->m_flags.m_generateMipMaps) {
        d3dUsage |= D3DUSAGE_AUTOGENMIPMAP;
    }

    // Cube map
    if (texId->m_target == GxTex_CubeMap) {
        auto d3dFormat = CGxDeviceD3d::s_GxTexFmtToD3dFmt[texId->m_format];
        LPDIRECT3DCUBETEXTURE9 d3dTexture;

        if (SUCCEEDED(this->m_d3dDevice->CreateCubeTexture(width, endLevel - startLevel, d3dUsage, d3dFormat, d3dPool, &d3dTexture, nullptr))) {
            texId->m_apiSpecificData = d3dTexture;
            texId->m_needsCreation = 0;

            // Anything created in D3DPOOL_DEFAULT has to be tracked, or the reset that destroys it
            // cannot happen: Reset fails with D3DERR_INVALIDCALL while it is still alive.
            if (d3dPool == D3DPOOL_DEFAULT) {
                TrackRenderTarget(texId);
            }
        }

        return;
    }

    // Depth stencil
    if (texId->m_format == GxTex_D24X8) {
        d3dUsage = D3DUSAGE_DEPTHSTENCIL;
        auto d3dFormat = D3DFMT_D24X8;
        LPDIRECT3DTEXTURE9 d3dTexture;

        if (SUCCEEDED(this->m_d3dDevice->CreateTexture(width, height, 1, d3dUsage, d3dFormat, d3dPool, &d3dTexture, nullptr))) {
            texId->m_apiSpecificData = d3dTexture;
            texId->m_needsCreation = 0;

            // THE resize bug: a depth-stencil texture for a render target lives in D3DPOOL_DEFAULT
            // exactly like the colour one, but this branch returned before tracking it. It survived
            // every IReleaseD3dResources, so Reset always answered D3DERR_INVALIDCALL.
            if (d3dPool == D3DPOOL_DEFAULT) {
                TrackRenderTarget(texId);
            }
        }

        return;
    }

    // Ordinary texture
    LPDIRECT3DTEXTURE9 d3dTexture;
    auto d3dFormat = CGxDeviceD3d::s_GxTexFmtToD3dFmt[texId->m_format];

    if (SUCCEEDED(this->m_d3dDevice->CreateTexture(width, height, endLevel - startLevel, d3dUsage, d3dFormat, d3dPool, &d3dTexture, nullptr))) {
        texId->m_apiSpecificData = d3dTexture;
        texId->m_needsCreation = 0;

        TrackRenderTarget(texId);

        return;
    }

    // A render target that fails to create is worth saying out loud: the caller keeps its CGxTex
    // and every later bind and read-back of it fails for reasons that look unrelated to the format
    // the device actually refused.
    if (texId->m_flags.m_renderTarget) {
        fprintf(stderr, "ITexCreate: RENDER TARGET %ux%u format %u (D3D %u) refused; trying fallback\n",
                width, height, static_cast<unsigned>(texId->m_format), static_cast<unsigned>(d3dFormat));
    }

    // TODO flag check SLOBYTE(texId->m_flags)

    // If texture creation failed, try again with a fallback format
    CGxDeviceD3d::s_GxTexFmtToUse[texId->m_format] = CGxDeviceD3d::s_tolerableTexFmtMapping[texId->m_format];
    texId->m_format = CGxDeviceD3d::s_GxTexFmtToUse[texId->m_format];
    d3dFormat = CGxDeviceD3d::s_GxTexFmtToD3dFmt[texId->m_format];

    if (SUCCEEDED(this->m_d3dDevice->CreateTexture(width, height, endLevel - startLevel, d3dUsage, d3dFormat, d3dPool, &d3dTexture, nullptr))) {
        texId->m_apiSpecificData = d3dTexture;
        texId->m_needsCreation = 0;
    } else if (texId->m_flags.m_renderTarget) {
        fprintf(stderr, "ITexCreate: RENDER TARGET fallback format %u ALSO refused; texture unusable\n",
                static_cast<unsigned>(d3dFormat));
    }
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

void CGxDeviceD3d::ITexUpload(CGxTex* texId) {
    uint32_t texelStrideInBytes;
    const void* texels = nullptr;

    texId->m_userFunc(GxTex_Lock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);

    uint32_t width;
    uint32_t height;
    uint32_t startLevel;
    uint32_t endLevel;
    this->ITexWHDStartEnd(texId, width, height, startLevel, endLevel);

    int32_t numFace = texId->m_target == GxTex_CubeMap ? 6 : 1;

    for (int32_t face = 0; face < numFace; face++) {
        for (int32_t level = startLevel; level < endLevel; level++) {
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

            STORM_ASSERT(texels != nullptr || texId->m_flags.m_renderTarget);

            LPDIRECT3DSURFACE9 surface = nullptr;
            HRESULT surfaceResult;

            if (texId->m_target == GxTex_CubeMap) {
                auto d3dTexture = static_cast<LPDIRECT3DCUBETEXTURE9>(texId->m_apiSpecificData);
                surfaceResult = d3dTexture->GetCubeMapSurface(CGxDeviceD3d::s_faceTypes[face], level, &surface);
            } else {
                auto d3dTexture = static_cast<LPDIRECT3DTEXTURE9>(texId->m_apiSpecificData);
                surfaceResult = d3dTexture->GetSurfaceLevel(level, &surface);
            }

            if (FAILED(surfaceResult)) {
                goto UNLOCK;
            }

            RECT rect = {
                texId->m_updateRect.minX >> level,  // left
                texId->m_updateRect.minY >> level,  // top
                texId->m_updateRect.maxX >> level,  // right
                texId->m_updateRect.maxY >> level,  // bottom
            };

            rect.right = std::max(rect.right, rect.left + 1);
            rect.bottom = std::max(rect.bottom, rect.top + 1);

            if (texId->m_format == GxTex_Dxt1 || texId->m_format == GxTex_Dxt3 || texId->m_format == GxTex_Dxt5) {
                rect.left &= 0xFFFFFFFC;
                rect.top &= 0xFFFFFFFC;
                rect.bottom = (rect.bottom + 3) & 0xFFFFFFFC;
                rect.right = (rect.right + 3) & 0xFFFFFFFC;

                rect.bottom = std::min(rect.bottom, static_cast<LONG>(height));
                rect.right = std::min(rect.right, static_cast<LONG>(width));
            }

            D3DLOCKED_RECT lockedRect;
            if (FAILED(surface->LockRect(&lockedRect, &rect, 0x0))) {
                surface->Release();
                goto UNLOCK;
            }

            if (texId->m_flags.m_bit15) {
                // TODO
            }

            C2iVector size = { rect.right - rect.left, rect.bottom - rect.top };

            // The latched texels cover the whole mip level, so step the source to the update rect
            auto srcTexels = static_cast<const uint8_t*>(texels);

            if (srcTexels) {
                bool srcCompressed = texId->m_dataFormat == GxTex_Dxt1 || texId->m_dataFormat == GxTex_Dxt3 || texId->m_dataFormat == GxTex_Dxt5;

                if (srcCompressed) {
                    uint32_t blockBytes = GxCalcTexelStrideInBytes(texId->m_dataFormat, 4);
                    srcTexels += (rect.top >> 2) * texelStrideInBytes + (rect.left >> 2) * blockBytes;
                } else {
                    uint32_t texelBytes = GxCalcTexelStrideInBytes(texId->m_dataFormat, 1);
                    srcTexels += rect.top * texelStrideInBytes + rect.left * texelBytes;
                }
            }

            Blit(
                size,
                BlitAlpha_0,
                srcTexels,
                texelStrideInBytes,
                GxGetBlitFormat(texId->m_dataFormat),
                lockedRect.pBits,
                lockedRect.Pitch,
                GxGetBlitFormat(texId->m_format)
            );

            surface->UnlockRect();
            surface->Release();
        }
    }

UNLOCK:
    texels = nullptr;
    texId->m_userFunc(GxTex_Unlock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);

    if (!texId->m_flags.m_renderTarget) {
        auto d3dTexture = static_cast<LPDIRECT3DTEXTURE9>(texId->m_apiSpecificData);
        d3dTexture->PreLoad();
    }
}

void CGxDeviceD3d::IXformSetProjection(const C44Matrix& matrix) {
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

void CGxDeviceD3d::IXformSetViewport() {
    const auto& gxViewport = this->m_viewport;
    auto windowRect = this->DeviceCurWindow();

    D3DVIEWPORT9 d3dViewport;

    d3dViewport.X = (gxViewport.x.l * windowRect.maxX) + 0.5;
    d3dViewport.Y = ((1.0 - gxViewport.y.h) * windowRect.maxY) + 0.5;

    // TODO account for negative X value

    d3dViewport.Width = (gxViewport.x.h * windowRect.maxX) - d3dViewport.X + 0.5;
    d3dViewport.Height = ((1.0 - gxViewport.y.l) * windowRect.maxY) - d3dViewport.Y + 0.5;

    d3dViewport.MinZ = gxViewport.z.l;
    d3dViewport.MaxZ = gxViewport.z.h;

    // TODO conditionally adjust Y value

    this->m_d3dDevice->SetViewport(&d3dViewport);

    this->intF6C = 0;
}

void CGxDeviceD3d::PoolSizeSet(CGxPool* pool, uint32_t size) {
    // This was an empty stub, and that single omission is the root of every null-buffer crash in
    // this client. BufStream calls it when a draw needs more room than the stream pool has; with
    // the call doing nothing, the pool kept its old size, IBufLock then asked Direct3D to lock a
    // range larger than the buffer actually is, the lock failed, and the null came back to callers
    // that did not check it. It surfaced as writes to address 0 and to 0x20 in four different
    // places -- the vertex submit, the buffer upload, the interface batch and the font batch --
    // which all looked like separate bugs.
    if (!pool || static_cast<int32_t>(size) <= pool->m_size) {
        return;
    }

    // Release the old buffer and build one at the new size. Anything already written into it is
    // discarded, which is correct for a stream pool: its contents only ever live for the draw
    // being assembled, and the caller re-fills it immediately after this returns.
    if (pool->m_apiSpecific) {
        if (pool->m_target == GxPoolTarget_Vertex) {
            static_cast<LPDIRECT3DVERTEXBUFFER9>(pool->m_apiSpecific)->Release();
        } else if (pool->m_target == GxPoolTarget_Index) {
            static_cast<LPDIRECT3DINDEXBUFFER9>(pool->m_apiSpecific)->Release();
        }

        pool->m_apiSpecific = nullptr;
    }

    pool->m_size = size;
    pool->unk1C = 0;

    this->CreatePoolAPI(pool);
}

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

    D3DCOLOR d3dColor = color.b | (color.g | (color.r << 8) << 8);

    this->m_d3dDevice->Clear(0, nullptr, flags, d3dColor, 1.0f, 0);
}

void CGxDeviceD3d::ScenePresent() {
    // A lost context is recoverable and must be retried. Previously one failed Reset cleared
    // m_context and nothing ever set it again: the client kept rendering at full speed while
    // presenting nothing, so the window held its last frame for ever. That reads as a hang.
    if (!this->m_context && this->m_d3dDevice) {
        HRESULT coop = this->m_d3dDevice->TestCooperativeLevel();

        if (coop == D3DERR_DEVICENOTRESET || coop == D3D_OK) {
            this->IReleaseD3dResources(0);

            D3DPRESENT_PARAMETERS d3dpp;
            this->ISetPresentParms(d3dpp, this->m_format);

            if (SUCCEEDED(this->m_d3dDevice->Reset(&d3dpp))) {
                this->IStateSetD3dDefaults();
                this->m_context = 1;
            }
        }
    }

    if (this->m_context) {
        CGxDevice::ScenePresent();
        this->ISceneEnd();

        // TODO

        // TODO fixLag

        // TODO

        HRESULT presented = this->m_d3dDevice->Present(nullptr, nullptr, nullptr, nullptr);

        if (FAILED(presented)) {
            // Recoverable: the retry at the top of this function resets and sets m_context again.
            this->m_context = 0;
        }

        // TODO stereo handling
    }

    this->ISceneBegin();
}

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

void CGxDeviceD3d::XformSetProjection(const C44Matrix& matrix) {
    CGxDevice::XformSetProjection(matrix);
    this->IXformSetProjection(matrix);
}
