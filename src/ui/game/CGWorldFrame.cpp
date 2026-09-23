#include "model/CM2Model.hpp"
#include <vector>
#include "model/CM2Shared.hpp"
#include "model/M2Data.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/ClntObjMgr.hpp"
#include "world/CWorld.hpp"
#include "world/Terrain.hpp"
#include "world/OverheadIcons.hpp"
#include "gx/Texture.hpp"
#include "component/CCharacterComponent.hpp"
#include "object/client/CGPlayer_C.hpp"
#include <common/Time.hpp>
#include <cstdio>
#include "object/client/QuestStatusCache.hpp"
#include "object/client/UnitVisuals.hpp"
#include "world/MapShadow.hpp"
#include "world/ParticleFx.hpp"
#include "model/CM2Scene.hpp"
#include "gx/shader/CShaderEffect.hpp"
#include "gx/Screen.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "ui/game/CGWorldFrame.hpp"
#include "gx/Coordinate.hpp"
#include "gx/Shader.hpp"
#include "gx/Transform.hpp"
#include "object/Client.hpp"
#include "ui/game/CGCamera.hpp"
#include "event/CEvent.hpp"
#include "ui/game/PlayerName.hpp"
#include "world/World.hpp"
#include <storm/Memory.hpp>
#include <tempest/Matrix.hpp>

CSimpleFrame* CGWorldFrame::Create(CSimpleFrame* parent) {
    // TODO use CDataAllocator

    return STORM_NEW(CGWorldFrame)(parent);
}

// Test hook: FROZEN_SCREENSHOT=<path> captures one frame after FROZEN_SCREENSHOT_FRAME frames in world
// (default 300) and writes it there.
//
// Verifying a render means looking at it, and the only safe way to do that on a machine someone else
// is using is to have the client hand over its own back buffer -- a desktop capture returns whichever
// window is on top. Counting frames rather than seconds so the capture lands after the world has had
// time to stream in regardless of how fast the machine is.
static void AutoScreenShot() {
    static int32_t remaining = -1;
    static bool done = false;

    if (done) {
        return;
    }

    const char* path = getenv("FROZEN_SCREENSHOT");

    if (!path || !*path) {
        done = true;
        return;
    }

    if (remaining < 0) {
        const char* frames = getenv("FROZEN_SCREENSHOT_FRAME");
        remaining = frames && *frames ? atoi(frames) : 300;
    }

    if (--remaining > 0) {
        return;
    }

    SStrCopy(Screen::s_capturePath, path, sizeof(Screen::s_capturePath));
    Screen::s_captureScreen = 1;
    done = true;

    fprintf(stderr, "AutoScreenShot: capturing to %s\n", path);
}

void CGWorldFrame::RenderWorld(void* param) {
    auto frame = reinterpret_cast<CGWorldFrame*>(param);

    AutoScreenShot();

    C44Matrix savedProj;
    GxXformProjection(savedProj);

    C44Matrix savedView;
    GxXformView(savedView);

    frame->OnWorldUpdate();
    PlayerNameUpdateWorldText();

    frame->OnWorldRender();
    PlayerNameRenderWorldText();

    GxXformSetProjection(savedProj);
    GxXformSetView(savedView);

    CShaderEffect::UpdateProjMatrix();
}

CGWorldFrame::CGWorldFrame(CSimpleFrame* parent) : CSimpleFrame(parent) {
    // TODO

    CGWorldFrame::s_currentWorldFrame = this;

    // TODO

    this->SetFrameStrata(FRAME_STRATA_WORLD);

    this->EnableEvent(SIMPLE_EVENT_KEY, -1);
    this->EnableEvent(SIMPLE_EVENT_MOUSE, -1);
    this->EnableEvent(SIMPLE_EVENT_MOUSEWHEEL, -1);

    // TODO

    this->m_camera = STORM_NEW(CGCamera);

    // TODO
}

void CGWorldFrame::OnFrameRender(CRenderBatch* batch, uint32_t layer) {
    this->CSimpleFrame::OnFrameRender(batch, layer);

    if (layer == DRAWLAYER_BACKGROUND) {
        batch->QueueCallback(&CGWorldFrame::RenderWorld, this);
    }
}

void CGWorldFrame::OnFrameSizeChanged(const CRect& rect) {
    this->CSimpleFrame::OnFrameSizeChanged(rect);

    // Screen rect (DDC)
    this->m_screenRect.minX = std::max(this->m_rect.minX, 0.0f);
    this->m_screenRect.minY = std::max(this->m_rect.minY, 0.0f);
    this->m_screenRect.maxX = std::min(this->m_rect.maxX, NDCToDDCWidth(1.0f));
    this->m_screenRect.maxY = std::min(this->m_rect.maxY, NDCToDDCHeight(1.0f));

    // Camera aspect ratio
    if (this->m_camera) {
        this->m_camera->SetScreenAspect(this->m_screenRect);
    }

    // Viewport (NDC)
    DDCToNDC(this->m_rect.minX, this->m_rect.minY, &this->m_viewport.minX, &this->m_viewport.minY);
    DDCToNDC(this->m_rect.maxX, this->m_rect.maxY, &this->m_viewport.maxX, &this->m_viewport.maxY);
    this->m_viewport.minX = std::max(this->m_viewport.minX, 0.0f);
    this->m_viewport.minY = std::max(this->m_viewport.minY, 0.0f);
    this->m_viewport.maxX = std::min(this->m_viewport.maxX, 1.0f);
    this->m_viewport.maxY = std::min(this->m_viewport.maxY, 1.0f);
}

// Right or left drag rotates the camera; the wheel zooms it
int32_t CGWorldFrame::OnLayerMouseDown(const CMouseEvent& evt, const char* btn) {
    if (btn) {
        return this->CSimpleFrame::OnLayerMouseDown(evt, btn);
    }

    this->m_cameraDragging = 1;
    this->m_dragLastX = evt.x;
    this->m_dragLastY = evt.y;

    return this->CSimpleFrame::OnLayerMouseDown(evt, btn);
}

int32_t CGWorldFrame::OnLayerMouseUp(const CMouseEvent& evt, const char* btn) {

    this->m_cameraDragging = 0;

    return this->CSimpleFrame::OnLayerMouseUp(evt, btn);
}

int32_t CGWorldFrame::OnLayerTrackUpdate(const CMouseEvent& evt) {

    if (this->m_cameraDragging && this->m_camera) {
        // The event position is normalized to the window; a full sweep turns roughly one turn
        float deltaX = evt.x - this->m_dragLastX;
        float deltaY = evt.y - this->m_dragLastY;

        this->m_dragLastX = evt.x;
        this->m_dragLastY = evt.y;

        // Screen y runs bottom to top, so dragging up should pitch the view up
        this->m_camera->Rotate(-deltaX * 6.28318f, deltaY * 3.14159f);
    }

    return this->CSimpleFrame::OnLayerTrackUpdate(evt);
}

int32_t CGWorldFrame::OnLayerMouseWheel(const CMouseEvent& evt) {

    if (this->m_camera) {
        // One wheel notch is a couple of yards
        this->m_camera->Zoom(evt.wheelDistance * -2.0f);
    }

    return 1;
}

CGWorldFrame* CGWorldFrame::s_currentWorldFrame = nullptr;

const CRect* CGWorldFrame::GetWorldViewport() {
    auto frame = CGWorldFrame::s_currentWorldFrame;
    return frame ? &frame->m_viewport : nullptr;
}

void CGWorldFrame::OnWorldRender() {
    // The reference (FUN_004f8ea0) pushes the device viewport and sets the frame's own rect for the
    // world; the world therefore only ever draws inside the WorldFrame, and the UI's viewport is
    // restored afterwards.
    float savedMinX, savedMaxX, savedMinY, savedMaxY, savedMinZ, savedMaxZ;
    GxXformViewport(savedMinX, savedMaxX, savedMinY, savedMaxY, savedMinZ, savedMaxZ);
    GxXformSetViewport(this->m_viewport.minX, this->m_viewport.maxX, this->m_viewport.minY, this->m_viewport.maxY, 0.0f, 1.0f);

    // The reference brackets the whole world render in a render-state push and turns multisampling
    // on inside it: `calll 0x409670` (GxRsPush) then `push $0x1; push $0x13; calll 0x408bf0`
    // (GxRsSet) at 0x004f8f2a, with the matching GxRsPop in its post stage. 0x13 is 19, which is
    // GxRs_Multisample, so the world is drawn antialiased and the UI is not.
    //
    // docs/world-render-inventory.md carried this as "missing (GxRsSet 0x13)" on the viewport row,
    // together with a note that frozen had no push/pop around the world render. Both halves land
    // here; the state itself only reached the device once IRsSendToHw learned to send it, which is
    // in the same change.
    GxRsPush();
    GxRsSet(GxRs_Multisample, 1);

    // TODO terrain, map objects, sky, and lighting; for now the scene is cleared and the models
    // in the world drawn

    // Clear the below-horizon backdrop to the distance-fog colour when fog is active, so far terrain
    // (which fades to that same fog colour) blends seamlessly into the horizon instead of ending on a
    // sky-coloured seam. With no fog, fall back to the horizon sky colour. The sky dome covers the
    // whole upper hemisphere, so this clear only shows at and below the horizon, like the reference.
    // The reference clears to BLACK under an open sky and only uses a colour when there is no sky
    // to draw, when an interior lighting override is active, or when the camera is under liquid
    // (CMap::Render's clear). The sky dome is then ADDED over that black, so the dome's own colour
    // is the sky; clearing to the sky colour as well would double it.
    bool underLiquid = CWorld::IsCameraUnderLiquid();
    const C3Vector& backdrop = CWorld::GetFogColor();
    CImVector clearColor = { 0x00, 0x00, 0x00, 0xFF };

    if (underLiquid) {
        clearColor.b = static_cast<uint8_t>(backdrop.z * 255.0f);
        clearColor.g = static_cast<uint8_t>(backdrop.y * 255.0f);
        clearColor.r = static_cast<uint8_t>(backdrop.x * 255.0f);
    }
    GxSceneClear(0x3, clearColor);

    CShaderEffect::UpdateProjMatrix();

    // Reference order (CMap::Render FUN_0079a870): terrain chunks, WMO groups, then the sky through
    // the far-depth viewport, so the sky fills only what the opaque world left untouched.
    TerrainRender();
    SkyRender();

    // Frustum-cull entities: TerrainRender has refreshed the frustum, so only in-view objects
    // animate and draw, matching the reference (the scene itself does no view culling). Server
    // positions are always valid, so there is no visibility/animation deadlock.
    auto objMgr = ClntObjMgrGetCurrent();

    if (objMgr) {
        WOWGUID activePlayer = ClntObjMgrGetActivePlayer();

        for (auto object = objMgr->m_visibleObjects.Head(); object; object = objMgr->m_visibleObjects.Next(object)) {
            if (object->m_model) {
                // The local player is always drawn; a camera angle should never cull your own model.
                if (object->GetGUID() == activePlayer) {
                    object->m_model->SetVisible(1);
                    object->m_model->SetAnimating(1);

                    continue;
                }

                // Cull against the model's own bounding sphere (model radius x world scale) so large
                // creatures never pop at screen edges, like the reference. The bounding radius is
                // static model data, so this stays valid even while the object is culled.
                float radius = 30.0f;
                C3Vector center = object->GetPosition();

                if (object->m_model->m_shared && object->m_model->m_shared->m_m2DataLoaded && object->m_model->m_shared->m_data) {
                    float scale = object->GetScale();

                    if (object->IsA(TYPE_UNIT)) {
                        scale *= static_cast<CGUnit_C*>(object)->GetModelScale();
                    }

                    const M2Bounds& b = object->m_model->m_shared->m_data->bounds;
                    radius = b.radius * scale;

                    if (radius < 2.0f) {
                        radius = 2.0f;
                    }

                    // Emitters reach past the mesh (a fire's sphere is its base, not its flames), and
                    // they are only stepped for models that pass this test.
                    radius += ParticleFxCullExtent(object->m_model, scale);

                    // The bounding sphere is centred on the mesh (usually mid-height), not the feet
                    // where the object sits, so offset the cull centre by the model-space box centre
                    // rotated by the facing and scaled. Without this a tall model pops out when its
                    // feet leave the screen even though its body is still in view.
                    C3Vector local = {
                        (b.extent.b.x + b.extent.t.x) * 0.5f,
                        (b.extent.b.y + b.extent.t.y) * 0.5f,
                        (b.extent.b.z + b.extent.t.z) * 0.5f
                    };
                    float f = object->GetFacing();
                    float cf = cosf(f);
                    float sf = sinf(f);
                    center.x += (local.x * cf - local.y * sf) * scale;
                    center.y += (local.x * sf + local.y * cf) * scale;
                    center.z += local.z * scale;
                }

                bool vis = TerrainSphereVisible(center, radius);

                object->m_model->SetVisible(vis ? 1 : 0);
                object->m_model->SetAnimating(vis ? 1 : 0);
            }
        }

        // Blob shadows under the visible units (reference: CMap::Render draws them after the sky
        // and before the M2 passes). The footprint follows the model's bounding radius so a large
        // creature casts a proportionally larger blob.
        BlobShadowsBegin();

        for (auto object = objMgr->m_visibleObjects.Head(); object; object = objMgr->m_visibleObjects.Next(object)) {
            if (!object->m_model || !object->IsA(TYPE_UNIT) || !object->m_model->m_flag10000) {
                continue;
            }

            if (object->GetGUID() != activePlayer && !object->m_model->m_flag8) {
                continue;
            }

            float scale = object->GetScale() * static_cast<CGUnit_C*>(object)->GetModelScale();
            float radius = 1.0f;

            // Prefer the current animation's bounds (the reference sizes the blob from the
            // animated box); fall back to the model's global box when a sequence has none.
            float extent = static_cast<CGUnit_C*>(object)->GetAnimFootprint();

            if (extent <= 0.0f && object->m_model->m_shared && object->m_model->m_shared->m_m2DataLoaded && object->m_model->m_shared->m_data) {
                const M2Bounds& b = object->m_model->m_shared->m_data->bounds;
                float ex = (b.extent.t.x - b.extent.b.x) * 0.5f;
                float ey = (b.extent.t.y - b.extent.b.y) * 0.5f;
                extent = ex > ey ? ex : ey;
            }

            if (extent > 0.0f) {
                radius = extent * scale * 0.9f;
            }

            if (radius < 0.5f) {
                radius = 0.5f;
            } else if (radius > 8.0f) {
                radius = 8.0f;
            }

            BlobShadowDraw(object->GetPosition(), radius);
            BlobShadowDrawWmo(object->GetPosition(), radius);
        }

        // Doodads cast too: the reference walks every scene entity with a model, not just units
        // (FUN_00793980). Terrain and WMO props are scene models, so they come through here; the
        // batched ground effects are not scene entities and correctly cast nothing. Bound the cost
        // with a distance cap and skip small props, standing in for the reference's shadow LOD.
        {
            struct CasterArg { C3Vector eye; } arg = { CWorld::GetCameraPos() };

            TerrainForEachDoodad([](CM2Model* model, void* a) {
                auto& ctx = *static_cast<CasterArg*>(a);

                if (!model->m_flag8 || !model->m_shared || !model->m_shared->m_m2DataLoaded || !model->m_shared->m_data) {
                    return;
                }

                const C44Matrix& M = model->matrixB4; // world placement, scale baked in
                C3Vector pos = { M.d0, M.d1, M.d2 };
                float dx = pos.x - ctx.eye.x;
                float dy = pos.y - ctx.eye.y;
                float dz = pos.z - ctx.eye.z;

                if (dx * dx + dy * dy + dz * dz > 60.0f * 60.0f) {
                    return;
                }

                float scale = sqrtf(M.a0 * M.a0 + M.a1 * M.a1 + M.a2 * M.a2);
                const M2Bounds& b = model->m_shared->m_data->bounds;
                float ex = (b.extent.t.x - b.extent.b.x) * 0.5f;
                float ey = (b.extent.t.y - b.extent.b.y) * 0.5f;
                float radius = (ex > ey ? ex : ey) * scale * 0.9f;

                if (radius < 1.0f) {
                    return; // pebbles and tufts: not worth a pass over the chunk mesh
                }

                if (radius > 8.0f) {
                    radius = 8.0f;
                }

                BlobShadowDraw(pos, radius);
                BlobShadowDrawWmo(pos, radius);
            }, &arg);
        }

        BlobShadowsEnd();
    }

    // NOTE: only the M2 scene draws may sit behind this guard. Detail doodads, liquids, weather,
    // particles, blob shadows and the underwater overlay are all independent of the model scene,
    // and having them inside it meant a null scene silently dropped half the world.
    auto scene = CWorld::GetM2Scene();

    {
        // Fog the models with the same data-driven fog the terrain and WMOs use (TerrainRender has
        // already set the fog colour/distances); the guard keeps clear zones unfogged.
        bool useFog = CWorld::GetFogEnd() > 1.0f && CWorld::GetFogStart() < CWorld::GetFarClip();

        if (useFog) {
            GxRsSet(GxRs_Fog, 1);
        }

        // Which models are drawing this frame, captured BEFORE Animate.
        //
        // m_flag8 is the "queued for drawing" flag, and CM2Scene::Animate CLEARS it on every model
        // as it walks the draw list. The particle update below used to test it afterwards, so it was
        // always 0 and no emitter was ever stepped -- no fires, no braziers, no torches. Same trap
        // that stopped the skybox drawing. The update still runs after Animate, so the bone sequence
        // state it samples is current; only the visibility answer is taken from before.
        static std::vector<CM2Model*> s_emitterModels;
        s_emitterModels.clear();

        for (auto object = objMgr ? objMgr->m_visibleObjects.Head() : nullptr; object; object = objMgr->m_visibleObjects.Next(object)) {
            if (object->m_model && object->m_model->m_flag8) {
                s_emitterModels.push_back(object->m_model);
            }
        }

        TerrainForEachDoodad([](CM2Model* model, void* arg) {
            if (model->m_flag8) {
                static_cast<std::vector<CM2Model*>*>(arg)->push_back(model);
            }
        }, &s_emitterModels);

        if (scene) {
            scene->AdvanceTime(CWorld::GetTickTimeMs());
            scene->Animate(this->m_camera->Position());

            // Cast the animated models into the shadow map. The reference renders this before the
            // terrain pass so terrain can sample the same frame's map; frozen cannot yet, because
            // Animate depends on the visibility the terrain pass establishes. The map is therefore
            // one frame behind what terrain will read in S4. Closing that gap means hoisting
            // TerrainUpdateView and the visibility sweep above this block, which is why
            // TerrainUpdateView was split out of TerrainRender.
            auto player = ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_UNIT, __FILE__, __LINE__);

            if (player) {
                MapShadowSetup(player->GetPosition());

                if (MapShadowBegin()) {
                    scene->DrawShadowCasters(MapShadowLightView());
                    MapShadowEnd();
                }
            }
        }

        // Particle emitters of the visible models (units and doodads) step after Animate so their
        // bone sequence state is current; the quads draw in the transparent block below.
        {
            float dt = static_cast<float>(CWorld::GetTickTimeMs()) * 0.001f;

            if (dt > 0.1f) {
                dt = 0.1f;
            }

            for (auto model : s_emitterModels) {
                ParticleFxUpdateModel(model, dt);
            }
        }

        // Reference (CGWorldFrame::OnWorldRender FUN_004f8ea0): opaque pass 0 after the map, then
        // the transparent block draws pass 2 before pass 1 with the camera above liquid (the order
        // flips underwater, which is not ported yet). Pass 2 was never drawn before.
        if (scene) {
            scene->Draw(M2PASS_0);
        }

        // Detail doodads after the opaque models (reference FUN_007984a0)
        DetailDoodadRender();

        // Transparent block (FUN_004f8ea0): above liquid it is pass 2, liquid, weather, barriers,
        // pass 1; under liquid the reference reverses it so the water surface is composited last:
        // barriers, pass 1, weather, liquid, pass 2. Weather and barriers are not ported yet.
        if (CWorld::IsCameraUnderLiquid()) {
            if (scene) { scene->Draw(M2PASS_1); }
            WeatherRender();
            LiquidRender(1);
            if (scene) { scene->Draw(M2PASS_2); }
            ParticleFxRender();
            OverheadIconsRender();
        } else {
            if (scene) { scene->Draw(M2PASS_2); }
            ParticleFxRender();
            OverheadIconsRender(); // the emitters' quads belong with pass 2 in the reference
            LiquidRender(1); // transparent water/ocean, sorted farthest first (liquid bucket 1)
            WeatherRender();
            if (scene) { scene->Draw(M2PASS_1); }
        }

        ParticleFxEndFrame();

        // Underwater overlay last in the world (reference FUN_0077f9d0 -> CMap FUN_0079ca70)
        UnderwaterOverlayRender();

        if (useFog) {
            GxRsSet(GxRs_Fog, 0);
        }
    }

    GxRsPop();

    GxXformSetViewport(savedMinX, savedMaxX, savedMinY, savedMaxY, savedMinZ, savedMaxZ);
}

void CGWorldFrame::OnWorldUpdate() {
    // The camera follows the active player until targeting is driven by the game
    if (!this->m_camera->GetTarget()) {
        this->m_camera->SetTarget(ClntObjMgrGetActivePlayer());
    }

    auto target = ClntObjMgrObjectPtr(this->m_camera->GetTarget(), TYPE_OBJECT, __FILE__, __LINE__);

    // TODO

    CGCamera::UpdateCallback(nullptr, this->m_camera);

    // TODO

    // The camera latches near/far at construction, so without this the projection never follows a
    // farclip change or a map load -- and a world frame built before the first LoadMap would keep
    // far = 0. Refresh both from the world before the projection is built.
    this->m_camera->SetNearZ(CWorld::GetNearClip());
    // The horizon distance, not farclip: fog still ends at farclip, but the geometry behind it has
    // to be drawn or it is clipped away in a hard ring instead of fading into the haze.
    this->m_camera->SetFarZ(CWorld::GetHorizonFarClip());

    this->m_camera->SetupWorldProjection(this->m_screenRect);

    // TODO

    auto targetPos = target && !this->m_camera->HasModel()
        ? target->GetPosition()
        : this->m_camera->Position();

    CWorld::Update(this->m_camera->Position(), this->m_camera->Target(), targetPos);

    TerrainUpdate(this->m_camera->Position());

    // Poll the server for questgiver status; nothing populates the overhead markers otherwise.
    QuestStatusUpdate(OsGetAsyncTimeMs());

    // TODO the map entities carry this in the original; until CMap is ported every visible object
    // places its model itself
    auto objMgr = ClntObjMgrGetCurrent();

    if (objMgr) {
        for (auto object = objMgr->m_visibleObjects.Head(); object; object = objMgr->m_visibleObjects.Next(object)) {
            if (object->m_model) {
                float scale = object->GetScale();

                if (object->IsA(TYPE_UNIT)) {
                    scale *= static_cast<CGUnit_C*>(object)->GetModelScale();

                    // Keep the looping idle pose in sync with the unit's state each frame, so a unit
                    // that sits, stands, dies or emotes after spawn updates instead of holding its
                    // spawn-time pose. UpdateIdleAnimation only re-issues the sequence on a change.
                    static_cast<CGUnit_C*>(object)->UpdateIdleAnimation();

                    // Keep the unit's aura visuals (spell state kits) attached to match its auras.
                    UnitVisualsUpdate(static_cast<CGUnit_C*>(object));
                }

                object->m_model->SetWorldTransform(object->GetPosition(), object->GetFacing(), scale);

                // A world model is drawn when it is animating, visible, and flagged for draw,
                // the same set the character preview uses; the animate list is drained each frame
                // so this runs every update
                object->m_model->SetAnimating(1);
                object->m_model->SetVisible(1);

                if (object->m_model->m_attachParent) {
                    object->m_model->m_flag20000 = 1;
                } else {
                    object->m_model->m_flag10000 = 1;
                }
            }
        }
    }
}
