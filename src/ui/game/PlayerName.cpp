#include "ui/game/PlayerName.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Data.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/ClntObjMgr.hpp"
#include "object/client/NameCache.hpp"
#include "object/Client.hpp"
#include "world/CWorld.hpp"
#include "gx/Transform.hpp"
#include "gx/Coordinate.hpp"
#include "gx/font/GxuFont.hpp"
#include "gx/font/TextBlock.hpp"
#include <cstdlib>
#include "gx/font/CGxString.hpp"
#include "gx/font/CGxStringBatch.hpp"
#include "util/Filesystem.hpp"
#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>
#include <cmath>
#include <map>
#include <string>
#include <vector>

// World text (the reference's WorldText.cpp region: FUN_007e6480 update, FUN_007e7490 render):
// unit names drawn as screen-space text above the model's head. The reference keeps per-text
// resources across frames; this stand-in rebuilds the strings each frame from the name cache.

namespace {

const float NAME_HEIGHT = 0.016f;      // NDC text height
const float NAME_BLOCK_WIDTH = 0.4f;   // centred block the name is justified within
const float NAME_MAX_DISTANCE = 60.0f; // yards

HTEXTFONT s_font = nullptr;
bool s_fontTried = false;
CGxStringBatch* s_batch = nullptr;

// One cached string per unit. Rebuilding every visible name every frame meant creating and
// destroying a CGxString (and the whole batch) per unit per frame, which rasterises glyphs and
// churns the font caches for text that almost never changes. A cached entry is rebuilt only when
// the name text itself changes; moving a name just re-places it.
struct CachedText {
    CGxString* str = nullptr;
    std::string text;
    uint32_t lastFrame = 0;
};

std::map<WOWGUID, CachedText> s_texts;
uint32_t s_frame = 0;

void DestroyText(CachedText& entry) {
    if (entry.str) {
        GxuFontDestroyString(entry.str);
        entry.str = nullptr;
    }
}

// Drop the strings of units that were not drawn this frame (despawned, culled or out of range)
void ExpireTexts() {
    for (auto it = s_texts.begin(); it != s_texts.end();) {
        if (it->second.lastFrame != s_frame) {
            DestroyText(it->second);
            it = s_texts.erase(it);
        } else {
            ++it;
        }
    }
}

bool EnsureFont() {
    if (s_fontTried) {
        return s_font != nullptr;
    }

    s_fontTried = true;

    char fontFile[260];
    OsBuildFontFilePath("FRIZQT__.TTF", fontFile, sizeof(fontFile));
    s_font = TextBlockGenerateFont(fontFile, 0x1, NDCToDDCHeight(NAME_HEIGHT));

    return s_font != nullptr;
}

// Project a world position through the world view/projection into 0..1 screen coordinates.
// The scene is drawn camera-relative (the view sits at the origin), so the camera position is
// subtracted first. Returns false when the point is behind the camera or off screen.
bool ProjectToScreen(const C3Vector& world, const C44Matrix& viewProj, const C3Vector& cameraPos, float& sx, float& sy) {
    float x = world.x - cameraPos.x;
    float y = world.y - cameraPos.y;
    float z = world.z - cameraPos.z;

    float cx = x * viewProj.a0 + y * viewProj.b0 + z * viewProj.c0 + viewProj.d0;
    float cy = x * viewProj.a1 + y * viewProj.b1 + z * viewProj.c1 + viewProj.d1;
    float cw = x * viewProj.a3 + y * viewProj.b3 + z * viewProj.c3 + viewProj.d3;

    if (cw <= 0.001f) {
        return false;
    }

    sx = (cx / cw) * 0.5f + 0.5f;
    sy = (cy / cw) * 0.5f + 0.5f;

    return sx > -0.2f && sx < 1.2f && sy > -0.1f && sy < 1.1f;
}

} // namespace

void PlayerNameUpdateWorldText() {
    // Names are queried lazily by the render pass through the name cache
}

void PlayerNameRenderWorldText() {
    // Temporary attribution switch for the crash that kills the client 10-15 seconds after entering
    // the world, faulting inside CGxString::InitializeViewTranslation. Set FROZEN_NO_NAMES=1 to skip
    // this system entirely: if the client then survives, the fault is here; if it still dies, this
    // is not the culprit and the search moves elsewhere. Remove once the cause is known.
    static const bool disabled = getenv("FROZEN_NO_NAMES") != nullptr;

    if (disabled) {
        return;
    }

    s_frame++;

    if (s_batch) {
        GxuFontDestroyBatch(s_batch);
        s_batch = nullptr;
    }

    auto objMgr = ClntObjMgrGetCurrent();

    if (!objMgr || !EnsureFont()) {
        return;
    }

    CGxFont* face = TextBlockGetFontPtr(s_font);

    if (!face) {
        return;
    }

    C44Matrix view;
    GxXformView(view);
    C44Matrix proj;
    GxXformProjection(proj);
    C44Matrix viewProj = view * proj;

    const C3Vector& cameraPos = CWorld::GetCameraPos();
    WOWGUID activePlayer = ClntObjMgrGetActivePlayer();

    for (auto object = objMgr->m_visibleObjects.Head(); object; object = objMgr->m_visibleObjects.Next(object)) {
        if (!object->m_model || !object->IsA(TYPE_UNIT) || object->GetGUID() == activePlayer) {
            continue;
        }

        if (!object->m_model->m_flag8) {
            continue; // culled this frame
        }

        C3Vector pos = object->GetPosition();
        float dx = pos.x - cameraPos.x;
        float dy = pos.y - cameraPos.y;
        float dz = pos.z - cameraPos.z;

        if (dx * dx + dy * dy + dz * dz > NAME_MAX_DISTANCE * NAME_MAX_DISTANCE) {
            continue;
        }

        const char* name = NameCacheGetName(object);

        if (!name || !*name) {
            continue;
        }

        // Above the head: the model box top, scaled, plus a small gap
        float scale = object->GetScale() * static_cast<CGUnit_C*>(object)->GetModelScale();
        float top = 2.0f;

        if (object->m_model->m_shared && object->m_model->m_shared->m_m2DataLoaded && object->m_model->m_shared->m_data) {
            top = object->m_model->m_shared->m_data->bounds.extent.t.z;
        }

        C3Vector head = { pos.x, pos.y, pos.z + top * scale + 0.4f };
        float sx, sy;

        if (!ProjectToScreen(head, viewProj, cameraPos, sx, sy)) {
            continue;
        }

        // ProjectToScreen yields 0..1 across the WORLD viewport, but the text batch maps NDC over
        // the whole window and OnWorldRender has already restored the UI viewport by now. Map into
        // the world frame's rect so names stay over their units when the WorldFrame is inset.
        const CRect* vp = CGWorldFrame::GetWorldViewport();

        if (vp) {
            sx = vp->minX + sx * (vp->maxX - vp->minX);
            sy = vp->minY + sy * (vp->maxY - vp->minY);
        }

        C3Vector position = { sx - NAME_BLOCK_WIDTH * 0.5f, sy, 1.0f };
        CachedText& entry = s_texts[object->GetGUID()];
        entry.lastFrame = s_frame;

        // Rebuild only when the text changed; otherwise just move the existing string.
        if (!entry.str || entry.text != name) {
            DestroyText(entry);

            CImVector color = { 0xFF, 0xFF, 0xFF, 0xFF };
            CGxString* str = nullptr;

            GxuFontCreateString(
                face,
                name,
                NAME_HEIGHT,
                position,
                NAME_BLOCK_WIDTH,
                NAME_HEIGHT * 1.5f,
                0.0f,
                str,
                GxVJ_Bottom,
                GxHJ_Center,
                0x0,
                color,
                0.0f,
                1.0f
            );

            if (!str) {
                continue;
            }

            CImVector shadowColor = { 0x00, 0x00, 0x00, 0xFF };
            C2Vector shadowOffset = { 0.001f, -0.001f };
            GxuFontAddShadow(str, shadowColor, shadowOffset);

            entry.str = str;
            entry.text = name;
        } else {
            entry.str->SetStringPosition(position);
        }

        if (!s_batch) {
            s_batch = GxuFontCreateBatch(false, false);
        }

        GxuFontAddToBatch(s_batch, entry.str);
    }

    if (s_batch) {
        GxuFontRenderBatch(s_batch);

        // Release the batch every frame. It was created once and then added to forever, so it kept
        // pointers to strings that ExpireTexts later destroyed, and the next frame's render walked
        // freed memory: an access violation in CGxString::InitializeViewTranslation about ten
        // seconds after entering the world, which is when the first unit goes out of range. It also
        // meant the same strings were re-added on every frame and the list grew without bound.
        //
        // This does NOT destroy the strings themselves; GxuFontDestroyBatch clears the batch's list
        // and returns the batch to a free pool, so the cache keeps ownership.
        GxuFontDestroyBatch(s_batch);
        s_batch = nullptr;
    }

    ExpireTexts();
}
