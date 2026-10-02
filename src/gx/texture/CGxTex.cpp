#include <cstring>
#include "gx/Device.hpp"
#include "gx/texture/CGxTex.hpp"
#include "gx/Gx.hpp"
#include <algorithm>

// ref: FUN_00681be0
// Bit 8 is a flag of its own and the anisotropy sits in bits 9..13 (frozen had it one bit low);
// the anisotropy is clamped to what the device supports.
CGxTexFlags::CGxTexFlags(EGxTexFilter filter, uint32_t wrapU, uint32_t wrapV, uint32_t force, uint32_t generateMipMaps, uint32_t renderTarget, uint32_t maxAnisotropy, uint32_t bit8, uint32_t bit14, uint32_t bit15) {
    *reinterpret_cast<uint32_t*>(this) = 0;

    this->m_filter = filter;
    this->m_wrapU = wrapU;
    this->m_wrapV = wrapV;
    this->m_forceMipTracking = force;
    this->m_generateMipMaps = generateMipMaps;
    this->m_renderTarget = renderTarget;

    if (g_theGxDevicePtr->Caps().m_maxTexAnisotropy <= maxAnisotropy) {
        maxAnisotropy = g_theGxDevicePtr->Caps().m_maxTexAnisotropy;
    }

    this->m_bit8 = bit8;
    this->m_maxAnisotropy = maxAnisotropy;
    this->m_bit14 = bit14;
    this->m_bit15 = bit15;

    // The reference asks for the caps once more for an anisotropic filter and does nothing with
    // them.
    if (filter == GxTex_Anisotropic) {
        g_theGxDevicePtr->Caps();
    }
}

// ref: FUN_00683ae0
// The whole flags word compared as bytes.
bool CGxTexFlags::operator!=(const CGxTexFlags& texFlags) const {
    return memcmp(this, &texFlags, sizeof(*this)) != 0;
}

// ref: FUN_004b5820
// The same bytes compared, the other way round.
bool CGxTexFlags::operator==(const CGxTexFlags& texFlags) {
    return !(*this != texFlags);
}

// ref: FUN_006852c0
CGxTex::CGxTex(EGxTexTarget target, uint32_t width, uint32_t height, uint32_t depth, EGxTexFormat format, EGxTexFormat dataFormat, CGxTexFlags flags, void* userArg, void (*userFunc)(EGxTexCommand, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t&, const void*&), const char* name) {
    this->m_updateRect = { 0, 0, static_cast<int32_t>(height), static_cast<int32_t>(width) };
    this->m_target = target;
    this->m_width = width;
    this->m_height = height;
    this->m_dataFormat = dataFormat;
    this->m_format = format;
    this->m_depth = depth;
    this->m_userFunc = userFunc;
    this->m_userArg = userArg;
    this->m_flags = flags;
    this->m_apiSpecificData = nullptr;
    this->m_apiSpecificData2 = nullptr;
    this->m_needsUpdate = 1;
    this->m_needsFlagUpdate = 1;
    this->m_needsCreation = 1;
    this->m_filterUnchanged = 1;
}

// ref: FUN_00685dc0
CGxTex::~CGxTex() {
    this->m_link2.Unlink();
    this->m_link.Unlink();
}

float CGxTex::GetHeight() {
    return this->m_height;
}

float CGxTex::GetWidth() {
    return this->m_width;
}
