#include "world/CWFrustum.hpp"

#include <storm/Memory.hpp>

// The records handed out to visible groups, returned at the end of the map object pass. The
// reference keeps this as a WorldScene.cpp file static at 0x00adf6a8.
static STORM_EXPLICIT_LIST(CWFrustum, link) s_freeFrustums;

// ref: FUN_007983b0
CWFrustum* CWFrustum::Alloc() {
    auto frustum = s_freeFrustums.Head();

    if (!frustum) {
        frustum = STORM_NEW(CWFrustum);

        if (!frustum) {
            return nullptr;
        }

        return frustum;
    }

    s_freeFrustums.UnlinkNode(frustum);

    return frustum;
}

void CWFrustum::Free(CWFrustum* frustum) {
    if (!frustum) {
        return;
    }

    s_freeFrustums.LinkToTail(frustum);
}
