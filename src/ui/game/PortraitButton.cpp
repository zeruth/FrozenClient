#include "ui/game/PortraitButton.hpp"
#include "ui/game/ScriptEvents.hpp"
#include "ui/game/Types.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/Gx.hpp"
#include <storm/Array.hpp>
#include <tempest/Rect.hpp>
#include <tempest/Vector.hpp>

int32_t g_portraitAlphaSupported;

// ref: FUN_00616dc0
// Clear the back buffer to transparent black and read a 64 x 64 corner of it back: any texel that
// did not come back opaque means the buffer keeps alpha. Only asked of an OpenGL device, or one
// whose caps say it is worth asking (caps +0x128, which no Direct3D 9 device sets).
int32_t PortraitTestBackBufferAlpha() {
    if (GxDevApi() != GxApi_OpenGl && !GxCaps().int128) {
        return 0;
    }

    int32_t keepsAlpha = 0;

    GxSceneClear(3, { 0, 0, 0, 0 });

    CiRect rect = { 0, 0, 64, 64 };
    TSGrowableArray<uint32_t> bits;

    g_theGxDevicePtr->ICaptureRead(rect, bits);

    for (uint32_t i = 0; i < bits.Count(); i++) {
        if (reinterpret_cast<const uint8_t*>(&bits[i])[3] != 0xFF) {
            keepsAlpha = 1;
            break;
        }
    }

    return keepsAlpha;
}

// ref: FUN_00617070
// Every portrait must be drawn again (the base mip level changed under them): test the back buffer
// again, mark every cached portrait and portrait texture for a redraw, and tell the UI.
//
// DIVERGED past the first line. The reference then walks the two portrait caches (the hash tables
// at 0x00c5ce08 and 0x00c5ce30, setting +0x20 and +0x18 on each entry) and queues
// UNIT_PORTRAIT_UPDATE for every unit it shows a portrait of through the unit event queue
// (FUN_006144d0 -> FUN_006143f0). Frozen has neither cache -- the portrait texture builder behind
// SetPortraitToTexture is recorded as diverged (0x00619330 in overrides.json) -- nor the unit event
// queue, so there is nothing to mark and nothing to queue through.
void PortraitButtonInvalidateAll() {
    g_portraitAlphaSupported = PortraitTestBackBufferAlpha();
}

// ref: FUN_00618110
// DIVERGED in its first step: the reference marks the unit's entry in the portrait cache
// (0x00c5ce08, +0x20) stale, and frozen has no portrait cache (see PortraitButtonInvalidateAll).
// The events are the reference's.
void PortraitRefresh(const WOWGUID& guid, uint32_t flags) {
    if (flags & 0x2) {
        ScriptEventsQueueUnitEvent(guid, SCRIPT_UNIT_PORTRAIT_UPDATE);
    }

    if (flags & 0x1) {
        ScriptEventsQueueUnitEvent(guid, SCRIPT_UNIT_MODEL_CHANGED);
    }
}
