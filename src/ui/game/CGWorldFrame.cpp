#include "model/CM2Model.hpp"
#include <vector>
#include "model/CM2Shared.hpp"
#include "model/M2Data.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/ClntObjMgr.hpp"
#include "world/CWorld.hpp"
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
#include "db/Db.hpp"
#include "client/Client.hpp"
#include "world/MapWeather.hpp"
#include "world/DayNightLight.hpp"
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

// ref: FUN_004f5960
CGCamera* CGWorldFrame::GetActiveCamera() {
    return CGWorldFrame::s_currentWorldFrame
        ? CGWorldFrame::s_currentWorldFrame->m_camera
        : nullptr;
}

void CGWorldFrame::OnWorldRender() {
    // The reference (FUN_004f8ea0) pushes the device viewport and sets the frame's own rect for the
    // world; the world therefore only ever draws inside the WorldFrame, and the UI's viewport is
    // restored afterwards.
    float savedMinX, savedMaxX, savedMinY, savedMaxY, savedMinZ, savedMaxZ;
    GxXformViewport(savedMinX, savedMaxX, savedMinY, savedMaxY, savedMinZ, savedMaxZ);
    //
    // The depth range is the saved minimum to 0.94 (DAT_00adeee4, pushed at 0x004f9019), not the
    // whole buffer: everything past 0.94 is held for what draws behind the world -- the
    // low-detail horizon at [0.998, 0.999] and the sky at [0.999, 1.0]. With the world on [0, 1]
    // its far terrain wrote depth above 0.998 and the horizon's mesh won the test against it,
    // drawing a fog-coloured ring along the ground at the far clip.
    GxXformSetViewport(this->m_viewport.minX, this->m_viewport.maxX, this->m_viewport.minY, this->m_viewport.maxY, savedMinZ, 0.94f);

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
    // frozen's version of that block below is in this order. FUN_00794b50 is the load barriers
    // (CWorldScene::RenderBarriers), reached through the wrapper at 0x0077f980 with the float at
    // frame+0xb14.
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

    // Which models draw is the map traversal's answer (CWorldScene::VisitStaticEntity and the
    // unseen-entity pass), made inside CMap::Render before the scene animates. Frozen used to
    // cull every visible object against the frustum here as well, after the fact.
    auto objMgr = ClntObjMgrGetCurrent();

    if (objMgr) {
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

        // The scene's AdvanceTime and Animate, the map shadow render and the particle step all
        // happen inside CMap::Render now, at the reference's position (0x0079ac0f).

        // Reference (CGWorldFrame::OnWorldRender FUN_004f8ea0): opaque pass 0 after the map, then
        // the transparent block draws pass 2 before pass 1 with the camera above liquid (the order
        // flips underwater, which is not ported yet). Pass 2 was never drawn before.
        if (scene) {
            // Which passes the scene may run. The reference does this assignment here too, one
            // instruction before its own Draw (0x004f9117), rather than at scene creation -- so a
            // scene that is never drawn through this frame keeps its constructor's zero.
            scene->m_passMask = CWorld::s_m2PassMask;

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
        // Liquid bucket 1 IS drawn here, and this comment used to say the opposite of the code it sits
        // above -- it claimed CMap::Render drains bucket 1 and that calling DrawLiquidPass here would
        // draw every surface twice, while the call below has been there all along. CMap.cpp's comment
        // at its own Liquid::Draw says the other thing, correctly. The code is right and always was:
        // CMap::Render drains bucket 0 and DrawLiquidPass drains bucket 1, each exactly once, so
        // nothing is drawn twice. Corrected rather than deleted because a note that inverts the truth
        // invites someone to remove a call the renderer needs.
        if (CWorld::IsCameraUnderLiquid()) {
            CWorld::RenderBarriers(CWorld::GetTickTimeSec());
            if (scene) { scene->Draw(M2PASS_1); }
            CWorld::RenderWeather();
            CWorldScene::DrawLiquidPass();
            if (scene) { scene->Draw(M2PASS_2); }
            ParticleFxRender();
            OverheadIconsRender();
        } else {
            if (scene) { scene->Draw(M2PASS_2); }
            ParticleFxRender();
            OverheadIconsRender(); // the emitters' quads belong with pass 2 in the reference
            CWorldScene::DrawLiquidPass();
            CWorld::RenderWeather();
            CWorld::RenderBarriers(CWorld::GetTickTimeSec());
            if (scene) { scene->Draw(M2PASS_1); }
        }

        ParticleFxEndFrame();

        // The underwater motes last in the world (FUN_0077f9d0 -> FUN_0079ca70)
        CWorld::RenderParticulates();

        if (useFog) {
            GxRsSet(GxRs_Fog, 0);
        }
    }

    GxRsPop();

    // The sun's and the moon's glare over everything (FUN_007f0870, at 0x004f9213, just after the
    // draw lists are flushed).
    DayNightGlareRender();

    GxXformSetViewport(savedMinX, savedMaxX, savedMinY, savedMaxY, savedMinZ, savedMaxZ);
}

// ref: FUN_004f6970
// The per-object update the world update walks the visible objects with. The reference hands each
// to its type's own update -- units FUN_00734390, game objects FUN_0070d040, the rest
// FUN_007051b0 -- which place the model, keep the map entity with it and ease its light. Those
// are phase 4; until they land, frozen does their placement part here.
static int32_t UpdateVisibleObject(WOWGUID guid, void* param) {
    auto object = ClntObjMgrObjectPtr(guid, TYPE_OBJECT, __FILE__, __LINE__);

    if (!object) {
        return 1;
    }

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

        // The map's entity for the object follows it (the reference does this from the
        // object's own movement update; this loop is where frozen places objects).
        object->UpdateWorldObject(0);

        // The entity's light eases toward what its placement found. The reference makes
        // this call from the unit and game object per-frame updates (FUN_00734390 at
        // 0x0073452f, FUN_0070d040 at 0x0070d065), both reached from this same visible-
        // object walk (FUN_004f6970) and neither ported yet, so frozen makes it here.
        if (object->m_worldObject && (object->IsA(TYPE_UNIT) || object->IsA(TYPE_GAMEOBJECT))) {
            CWorld::UpdateObjectLighting(object->m_worldObject);
        }

    }

    return 1;
}

// ref: FUN_004fa5f0
// The world's update for one frame, in the reference's order. What the reference does here that
// frozen does not yet have is marked in place, so filling it in is a matter of calling it.
void CGWorldFrame::OnWorldUpdate() {
    float dt = CWorld::GetTickTimeSec();

    auto player = ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__);

    // The load barriers measure from the unit this client moves (0x004fa65c).
    if (CGUnit_C::s_activeMover) {
        auto mover = ClntObjMgrObjectPtr(CGUnit_C::s_activeMover, TYPE_UNIT, __FILE__, __LINE__);

        if (mover) {
            CWorld::SetBarrierPoint(mover->GetPosition());
        }
    }

    auto target = ClntObjMgrObjectPtr(this->m_camera->GetTarget(), TYPE_OBJECT, __FILE__, __LINE__);

    // With no target the camera falls back to the player. TODO the reference tests the player for
    // commentator mode first (FUN_006de980: descriptor flags 0x80000 with 0x400000 or a PvP state
    // of 4) and hands the camera to the commentator view (FUN_005689a0) instead; it also checks a
    // second camera target (+0x90) and, when the player is in a vehicle, re-seats the camera
    // (FUN_006e2880, FUN_0074ce40).
    if (!target && player) {
        this->m_camera->SetTarget(ClntObjMgrGetActivePlayer());
        target = player;
    }

    // What the camera follows, as the world knows it (FUN_00780500 at 0x004fa7a0).
    CWorld::s_focusEntity = target && target->m_worldObject
        ? reinterpret_cast<CMapStaticEntity*>(target->m_worldObject)
        : nullptr;

    // TODO FUN_0074b130(time): the vehicle passenger update.

    CGCamera::UpdateCallback(nullptr, this->m_camera);

    // FROZEN-ONLY: the camera latches near/far at construction, so without this the projection
    // never follows a farclip change or a map load -- and a world frame built before the first
    // LoadMap would keep far = 0. The horizon distance, not farclip: fog still ends at farclip,
    // but the geometry behind it has to be drawn or it is clipped away in a hard ring.
    this->m_camera->SetNearZ(CWorld::GetNearClip());
    this->m_camera->SetFarZ(CWorld::GetHorizonFarClip());

    this->m_camera->SetupWorldProjection(this->m_screenRect);

    this->UpdateDayNight(dt);

    // TODO the sound listener (0x004fa8d9 .. 0x004faa3a): at the camera, or behind and above the
    // unit it follows by Sound_ListenerBackDist / Sound_ListenerUpDist (FUN_004c5b20).

    // TODO FUN_00744140 (the objects' own pre-update walk), FUN_00616e80 (portraits), and the two
    // deferred model-release lists (FUN_004f9310 on +0x29c and +0x2a8), which nothing in frozen
    // fills.

    ClntObjMgrEnumVisibleObjects(&UpdateVisibleObject, nullptr);

    // TODO FUN_0077f2b0(FUN_004f6560), FUN_006fa450 (effects), FUN_00703b00 (missiles),
    // FUN_00804d20 / FUN_00804c10 (spells).

    auto targetPos = target && !this->m_camera->HasModel()
        ? target->GetPosition()
        : this->m_camera->Position();

    CWorld::Update(this->m_camera->Position(), this->m_camera->Target(), targetPos);

    // TODO the view-projection change test (FUN_004c1830 against +0x340) that marks the frame
    // (+0xb10 bit 2) for OnWorldRender's FUN_00715380, then FUN_00405130, the sound updates
    // (FUN_004d0110, FUN_004cdc80), the active mover's FUN_006fe7e0, FUN_00739630 and the world
    // map's FUN_005488f0.

    // FROZEN-ONLY: the sky builds its geometry around this.
    CWorldScene::s_worldCameraPos = this->m_camera->Position();

    // FROZEN-ONLY: poll the server for questgiver status; nothing populates the overhead markers
    // otherwise.
    QuestStatusUpdate(OsGetAsyncTimeMs());
}

// ref: FUN_004f8410
void CGWorldFrame::UpdateDayNight(float elapsedSec) {
    auto block = DayNightGetBlock();

    block->farClip = this->m_camera->FarZ();
    block->frameDelta = elapsedSec;
    block->timeSec = static_cast<float>(static_cast<uint32_t>(OsGetAsyncTimeMs())) * 0.001f;
    block->cameraPos = this->m_camera->Position();

    C3Vector forward = this->m_camera->Forward();
    float inv = 1.0f / sqrtf(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    block->cameraDir = { forward.x * inv, forward.y * inv, forward.z * inv };

    auto target = ClntObjMgrObjectPtr(this->m_camera->GetTarget(), TYPE_OBJECT, __FILE__, __LINE__);
    block->queryPos = target ? target->GetPosition() : block->cameraPos;

    bool noon = false;
    auto playerGuid = ClntObjMgrGetActivePlayer();

    if (playerGuid) {
        auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(playerGuid, TYPE_PLAYER, __FILE__, __LINE__));

        if (player) {
            block->playerPos = player->GetPosition();

            if ((player->Unit()->flags & 0x100000) && (player->Player()->flags & 0x20000)) {
                block->minutes = 0;
                block->timeOfDay = 0.5f;
                block->dayCount = 0.0f;
                noon = true;
            }
        }
    }

    if (!noon) {
        block->minutes = g_clientGameTime.GetHourAndMinutes();
        block->timeOfDay = g_clientGameTime.GetDayFraction();
        block->dayCount = static_cast<float>(g_clientGameTime.GetDaysSinceEpoch());
    }

    auto map = g_mapDB.GetRecord(CMap::s_mapID);

    if (map && map->m_timeOfDayOverride != -1) {
        block->minutes = 0;
        block->timeOfDay = static_cast<float>(map->m_timeOfDayOverride) * 0.00069444446f;
        block->dayCount = 0.0f;

        if (block->timeOfDay < 0.0f || 1.0f < block->timeOfDay) {
            block->timeOfDay = 0.5f;
        }
    }

    auto weather = CWorld::s_weather;
    block->stormInput = weather ? weather->m_density * weather->m_fog : 0.0f;
}
