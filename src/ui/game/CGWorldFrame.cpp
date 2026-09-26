#include "model/CM2Model.hpp"
#include <vector>
#include "model/CM2Shared.hpp"
#include "model/M2Data.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/ClntObjMgr.hpp"
#include "world/CWorld.hpp"
#include "world/Weather.hpp"
#include "world/DayNight.hpp"
#include "world/CWorldScene.hpp"
#include "world/map/CMap.hpp"
#include "world/OverheadIcons.hpp"
#include "gx/Texture.hpp"
#include "component/CCharacterComponent.hpp"
#include "object/client/CGPlayer_C.hpp"
#include <common/Time.hpp>
#include <cstdio>
#include "object/client/QuestStatusCache.hpp"
#include "object/client/UnitVisuals.hpp"
#include "world/MapShadow.hpp"
#include "world/ShadowMap.hpp"
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

    // THE REFERENCE'S ORDER, read off FUN_004f8ea0's call sites on 2026-09-26 rather than inferred.
    // This is queue item 11's target; every line is what the reference does, in this sequence, with
    // the thunks at 0x0077exxx/0x0077fxxx resolved to what they jump to:
    //
    //   FUN_004f8770                         (pre)
    //   FUN_008c1770
    //   FUN_004f5d90
    //   GxRsPush; GxRsSet(GxRs_Multisample, 1)
    //   GxSceneClear
    //   GxXformViewport / RenderTargetGet / GxXformSetViewport
    //   FUN_004e6f80, FUN_007e5120, FUN_00715380
    //   CShaderEffect::UpdateProjMatrix
    //   CWorld::RenderMap        (0x0077eff0) -> CMap::Render, then FUN_00403fc0
    //   FUN_0079fcc0             (0x0077f070)  a 1181-byte map pass, unported and unreached
    //   ClntObjMgrEnumVisibleObjects
    //   FUN_00715380 / 007153a0 / 007153c0, FUN_00615890, FUN_00725890, FUN_0081ca10
    //   CM2Scene::Draw(M2PASS_0)
    //   FUN_004f8a40
    //   camera pos / FUN_00681ba0 / camera pos / FUN_00682960
    //   CWorldScene::RenderDetailDoodads   (0x0077f010)
    //   CWorld::GetCameraLiquid  (0x00780620)  and then the branch below
    //   ... the transparent block ...
    //   FUN_004f8a40, FUN_007fca30, FUN_007f9ec0, FUN_006fdfb0, FUN_004f6f90
    //   the particulates pass    (0x0077f9d0, behind CWorld enable 0x2000000)
    //   FUN_005eeb70, camera pos / FUN_00681ba0 / camera pos / FUN_00682960
    //   FUN_007f0870, FUN_007e5580, FUN_00401260
    //   GxRsPop
    //   FUN_00615890, FUN_0056c7a0, GxXformSetViewport (restore)
    //   FUN_008c1010, FUN_00747ae0, FUN_006d7ba0, CM2Model::Release
    //
    // The transparent block is an if/else on GetCameraLiquid with a SHARED TAIL -- the above-water
    // arm jumps to the other's last draw at 0x004f91b7, which is why the two read as mirror images:
    //
    //   above water:  Draw(2), liquid bucket 1, weather, FUN_00794b50, Draw(1)
    //   under water:  FUN_00794b50, Draw(1), weather, liquid bucket 1, Draw(2)
    //
    // frozen's version of that block below is already in this order. The one thing absent from both
    // arms is FUN_00794b50 -- 2200 bytes, streamed quads over a model, reached through the wrapper
    // at 0x0077f980 with the float at frame+0xb14 -- which the comment there calls "barriers".
    //
    // So OnWorldRender's low fidelity is NOT an ordering defect: what is missing is the content of
    // the passes, and they are tracked with their own items.

    // Clear the below-horizon backdrop to the distance-fog colour when fog is active, so far terrain
    // (which fades to that same fog colour) blends seamlessly into the horizon instead of ending on a
    // sky-coloured seam. With no fog, fall back to the horizon sky colour. The sky dome covers the
    // whole upper hemisphere, so this clear only shows at and below the horizon, like the reference.
    // The reference clears to BLACK under an open sky and only uses a colour when there is no sky
    // to draw, when an interior lighting override is active, or when the camera is under liquid
    // (CMap::Render's clear). The sky dome is then ADDED over that black, so the dome's own colour
    // is the sky; clearing to the sky colour as well would double it.
    // The clear, the projection refresh and the terrain chunks are CMap::Render's now (the clear
    // colour logic above lives there); the map objects and liquids still come from the stand-in
    // the view state, and the sky follows them as in the reference.
    CMap::Render(this->m_camera->Position(), CWorld::GetTickTimeSec());

    // The frame's view-projection and fog flag, which the sky, the weather, the overhead icons and
    // the particle passes all read. Built after CMap::Render so it sees this frame's transforms.
    if (!CWorldScene::s_viewUpdated) {
        CWorldScene::UpdateWorldView();
    }

    CWorldScene::s_viewUpdated = false;

    SkyRender();

    // Frustum-cull entities: UpdateWorldView has refreshed the frustum, so only in-view objects
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

                bool vis = !CWorldScene::SphereOutsideFrustum(center, radius);

                object->m_model->SetVisible(vis ? 1 : 0);
                object->m_model->SetAnimating(vis ? 1 : 0);
            }
        }

        // The blob decal pass is gone, and with it the whole stand-in that backed it. It could no
        // longer put a decal on anything, for the same reason the WMO half could not (cf7768ff):
        // the decal re-draws the receiver's own triangles and selects with a depth-EQUAL test, so it
        // lands only where its re-draw reproduces the receiver's depth bit for bit -- and the
        // receiver's base pass is CMapRenderChunk's now, which disagrees with it three ways over.
        //
        //   program   the reference pass binds CMap::GetTerrainVertexShader(lights, layers,
        //             specular, colour, chunkSpecular, shadow), a .bls permutation; the decal bound
        //             frozen's own embedded s_terrainVS
        //   constants the reference uploads the whole TerrainConstants block at GxSh_Vertex 0
        //             (view, viewTransposed, proj, three lights, texture scales); the decal wrote a
        //             single 4-register matrix over the same registers, which that shader reads as
        //             its `view` field
        //   streams   the reference streams position + normal (+ MCCV colour) through a device
        //             vertex format; the decal locked position + colour + texcoord
        //
        // Any one of those is enough for the depths to differ, so every pixel failed the test.
        //
        // The way back is the reference's own method, which is not this one: FUN_007e4480 builds a
        // texture PROJECTION matrix and lets the receiver draw itself with an extra stage, which is
        // how it shadows a receiver of any shader without needing to reproduce its depth. That is
        // already the recorded divergence on FUN_007e4480 in overrides.json.

        // The caster enumeration itself is the reference's own pass and stays: it drains the
        // frame's entity list, which is what decides who casts.
        CWorldScene::DrawEntityShadows();
    }

    // NOTE: only the M2 scene draws may sit behind this guard. Detail doodads, liquids, weather,
    // particles, blob shadows and the underwater overlay are all independent of the model scene,
    // and having them inside it meant a null scene silently dropped half the world.
    auto scene = CWorld::GetM2Scene();

    {
        // Fog the models with the same data-driven fog the terrain and WMOs use (UpdateWorldView has
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

        // The buildings' own props, now built on the reference defs by CMap::CreateMapObjDoodads.
        CMap::ForEachMapObjDoodad([](CM2Model* model, void* arg) {
            if (model->m_flag8) {
                static_cast<std::vector<CM2Model*>*>(arg)->push_back(model);
            }
        }, &s_emitterModels);

        CMap::ForEachDoodadModel([](CM2Model* model, void* arg) {
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
            // CWorldScene::UpdateWorldView above this block.
            // Gated on the shadow map quality, which is what the reference gates it on, and which
            // frozen also needs for a plainer reason: NOTHING SAMPLES THE MAP YET. MapShadowTexture
            // and MapShadowTexMatrix have no callers, and every bind point that would use them
            // (ShadowMapBindTerrain, ShadowMapBindMapObj, ShadowMapBindScene in ShadowMap.cpp) is
            // still a TODO. So without this gate the frame re-draws every animated caster into a
            // 1024x1024 render target and then throws the result away -- a whole extra caster pass
            // for nothing, which matters most on Android where the frame rate is already the open
            // problem.
            //
            // g_shadowMapQuality is 0 and nothing assigns it, so today this never runs. It turns
            // itself on as soon as the quality is wired and the sampler lands; do not remove the
            // gate to "enable shadows" without also giving the map a reader.
            auto player = ShadowMapGetQuality() > 0
                ? ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_UNIT, __FILE__, __LINE__)
                : nullptr;

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
        CWorldScene::RenderDetailDoodads();

        // Transparent block (FUN_004f8ea0): above liquid it is pass 2, liquid, weather, barriers,
        // pass 1; under liquid the reference reverses it so the water surface is composited last:
        // barriers, pass 1, weather, liquid, pass 2. This order is confirmed against the
        // disassembly -- see the table at the top of this function, including that the two arms
        // share their final draw.
        //
        // The barrier pass is FUN_00794b50, reached through the wrapper FUN_0077f980 with the float
        // at frame+0xb14; 2200 bytes of streamed quads over a model. Not ported, and it is the only
        // thing missing from this block's order.
        // Liquid bucket 1 is NOT drawn here any more. CMap::Render drains it (a divergence already
        // recorded at that call: the reference drains it from 0x00790a80, which is unported), and
        // once the material draw landed, calling the stand-in here as well drew every water surface
        // TWICE -- which for an alpha-blended surface compounds the blend rather than being
        // invisible.
        if (CWorld::IsCameraUnderLiquid()) {
            if (scene) { scene->Draw(M2PASS_1); }
            WeatherRender();
            CWorldScene::DrawLiquidPass();
            if (scene) { scene->Draw(M2PASS_2); }
            ParticleFxRender();
            OverheadIconsRender();
        } else {
            if (scene) { scene->Draw(M2PASS_2); }
            ParticleFxRender();
            OverheadIconsRender(); // the emitters' quads belong with pass 2 in the reference
            CWorldScene::DrawLiquidPass();
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

    // What the stand-in's per-frame update still did: refresh the outdoor light, and hand the
    // camera to the two modules that build their geometry around it.
    CWorld::UpdateOutdoorLight();
    SkySetCameraState(this->m_camera->Position());
    WeatherSetCameraPos(this->m_camera->Position());
    CWorldScene::s_worldCameraPos = this->m_camera->Position();

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
