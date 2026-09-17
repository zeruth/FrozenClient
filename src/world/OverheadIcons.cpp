#include "world/OverheadIcons.hpp"

#include "gx/Coordinate.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/Shader.hpp"
#include "gx/shader/CGxShader.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Data.hpp"
#include "object/Types.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/ClntObjMgr.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/QuestStatusCache.hpp"
#include "world/CWorld.hpp"
#include "world/Terrain.hpp"
#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>

namespace {

// 1 available, 2 incomplete, 3 complete -- the order QuestStatusIconFor returns.
const char* const ICON_PATHS[] = {
    nullptr,
    "Interface\\GossipFrame\\AvailableQuestIcon.blp",
    "Interface\\GossipFrame\\IncompleteQuestIcon.blp",
    "Interface\\GossipFrame\\ActiveQuestIcon.blp",
};

const int32_t ICON_COUNT = sizeof(ICON_PATHS) / sizeof(ICON_PATHS[0]);

HTEXTURE s_icons[ICON_COUNT] = {};
bool s_tried[ICON_COUNT] = {};

// Yards. The marker hangs above the model's own bounding box so a tall creature does not wear it on
// its face, and is sized in world units so it shrinks with distance like everything else.
const float ICON_SIZE = 0.9f;
const float ICON_GAP = 0.6f;

HTEXTURE IconTexture(int32_t index) {
    if (index <= 0 || index >= ICON_COUNT) {
        return nullptr;
    }

    if (!s_tried[index]) {
        s_tried[index] = true;

        CStatus status;
        s_icons[index] = TextureCreate(ICON_PATHS[index],
                                       CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1), &status, 0);
    }

    return s_icons[index];
}

// Top of the unit in world space: its feet plus the model's own height, so the marker clears a
// tauren as well as a gnome.
float UnitTop(CGObject_C* object) {
    float height = 2.0f;

    auto model = object->m_model;

    if (model && model->m_shared && model->m_shared->m_m2DataLoaded && model->m_shared->m_data) {
        const M2Bounds& b = model->m_shared->m_data->bounds;
        float scale = object->GetScale();

        if (object->IsA(TYPE_UNIT)) {
            scale *= static_cast<CGUnit_C*>(object)->GetModelScale();
        }

        float modelTop = b.extent.t.z * scale;

        if (modelTop > 0.1f) {
            height = modelTop;
        }
    }

    return object->GetPosition().z + height + ICON_GAP;
}

} // namespace

void OverheadIconsRender() {
    auto objMgr = ClntObjMgrGetCurrent();

    if (!objMgr) {
        return;
    }

    // Camera-facing basis: the icon is a quad spanned by the view's right and up axes, so it always
    // faces the viewer without needing a per-icon matrix.
    C44Matrix view;
    GxXformView(view);

    C3Vector right = { view.a0, view.b0, view.c0 };
    C3Vector up = { view.a1, view.b1, view.c1 };

    CGxShader* vs = nullptr;
    CGxShader* ps = nullptr;
    TerrainUiShaders(vs, ps);

    if (!vs || !ps) {
        return;
    }

    bool pushed = false;

    for (auto object = objMgr->m_visibleObjects.Head(); object; object = objMgr->m_visibleObjects.Next(object)) {
        // Only units carry quest markers; a game object questgiver uses the same status but the
        // reference does not float an icon over it.
        if (!object->IsA(TYPE_UNIT)) {
            continue;
        }

        int32_t icon = QuestStatusIconFor(object->GetGUID());

        if (!icon) {
            continue;
        }

        HTEXTURE texture = IconTexture(icon);

        if (!texture) {
            continue;
        }

        if (!pushed) {
            pushed = true;

            GxRsPush();
            GxRsSet(GxRs_DepthTest, 1);
            GxRsSet(GxRs_DepthFunc, 0);   // less-equal: hidden behind geometry, as the reference does
            GxRsSet(GxRs_DepthWrite, 0);
            GxRsSet(GxRs_Culling, 0);
            GxRsSet(GxRs_Lighting, 0);
            GxRsSet(GxRs_Fog, 0);
            GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
            GxRsSet(GxRs_VertexShader, vs);
            GxRsSet(GxRs_PixelShader, ps);

            const C44Matrix& viewProjT = TerrainViewProjT();
            GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&viewProjT), 4);
        }

        C3Vector centre = object->GetPosition();
        centre.z = UnitTop(object);

        // The terrain vertex program expects positions relative to the camera, the same convention
        // every other world pass uses.
        C3Vector camera = CWorld::GetCameraPos();
        centre.x -= camera.x;
        centre.y -= camera.y;
        centre.z -= camera.z;

        float h = ICON_SIZE * 0.5f;

        C3Vector pos[4];
        C2Vector uv[4];
        CImVector col[4];

        for (int32_t i = 0; i < 4; i++) {
            float sx = (i == 0 || i == 3) ? -h : h;
            float sy = (i < 2) ? h : -h;

            pos[i].x = centre.x + right.x * sx + up.x * sy;
            pos[i].y = centre.y + right.y * sx + up.y * sy;
            pos[i].z = centre.z + right.z * sx + up.z * sy;

            uv[i].x = (i == 0 || i == 3) ? 0.0f : 1.0f;
            uv[i].y = (i < 2) ? 0.0f : 1.0f;

            col[i].r = col[i].g = col[i].b = col[i].a = 0xFF;
        }

        static const uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };

        GxRsSet(GxRs_Texture0, TextureGetGxTex(texture, 0, nullptr));

        GxPrimLockVertexPtrs(
            4,
            pos, sizeof(C3Vector),
            nullptr, 0,
            col, sizeof(CImVector),
            nullptr, 0,
            uv, sizeof(C2Vector),
            nullptr, 0
        );
        GxDrawLockedElements(GxPrim_Triangles, 6, indices);
        GxPrimUnlockVertexPtrs();
    }

    if (pushed) {
        GxRsPop();
    }
}

void OverheadIconsRelease() {
    for (int32_t i = 0; i < ICON_COUNT; i++) {
        s_icons[i] = nullptr;
        s_tried[i] = false;
    }
}
