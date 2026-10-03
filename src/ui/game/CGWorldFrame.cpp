#include "model/CM2Model.hpp"
#include <vector>
#include "model/CM2Shared.hpp"
#include "model/M2Data.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/ClntObjMgr.hpp"
#include "world/CWorld.hpp"
#include "ffx/FFX.hpp"
#include "ffx/EffectGlow.hpp"
#include "world/DayNightLight.hpp"
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
#include "gx/RenderTarget.hpp"
#include "gx/Device.hpp"
#include "gx/CGxDevice.hpp"
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

    // TODO the spell shadow textures (DAT_00b74350) and the event handler for event 6.

    // The full-screen effects (0x004fae99 .. 0x004faeec).
    FFX::Init();

    CGWorldFrame::s_glowEffect = STORM_NEW(EffectGlow);

    // TODO EffectDeath (FUN_007ea260), the nether effect (FUN_007ea470) and EffectSpecial
    // (FUN_007ea5f0) are not FFX effects in frozen yet.
    CGWorldFrame::s_deathEffect = nullptr;
    CGWorldFrame::s_netherEffect = nullptr;
    CGWorldFrame::s_specialEffect = nullptr;

    CGWorldFrame::UpdateScreenEffect();

    // TODO FUN_0047c500.
}

FFX::Effect* CGWorldFrame::s_glowEffect;
FFX::Effect* CGWorldFrame::s_deathEffect;
FFX::Effect* CGWorldFrame::s_netherEffect;
FFX::Effect* CGWorldFrame::s_specialEffect;

// ref: FUN_004f7020
void CGWorldFrame::SetScreenEffect(int32_t id) {
    // TODO the ScreenEffect.dbc record (DAT_00ad4510): its type picks the glow (0), death (1),
    // nether with a fog override (2) or the special effect with the record's parameters (3),
    // then forces its light parameters and its ambience and music. Frozen has no ScreenEffect
    // table, so every id takes the reference's no-record arm.
    (void)id;

    DayNightEndFogOverride();
    FFX::SetEffect(CGWorldFrame::s_glowEffect);
    DayNightClearForcedParams();

    // TODO FUN_004c8fa0(0, 0): clear the screen effect's ambience and music.
}

// ref: FUN_004f88b0
void CGWorldFrame::UpdateScreenEffect() {
    auto player = ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__);

    // TODO with a player, the reference walks its auras from the last back for one whose spell
    // carries a screen-effect aura and uses that effect's id; frozen keeps no client aura list,
    // so it takes the no-aura arm.
    (void)player;

    CGWorldFrame::SetScreenEffect(0);
}

// ref: FUN_004f8770
void CGWorldFrame::UpdateGlowParams() {
    if (FFX::s_activeEffect != CGWorldFrame::s_glowEffect) {
        return;
    }

    // The day's glow as a byte (the reference's add-512 float trick truncates).
    uint32_t dayGlow = static_cast<uint8_t>(static_cast<int32_t>(DayNightGetBlock()->info.glow * 255.0f));

    uint32_t underwater = 0;
    uint8_t grey = 0;

    if (ClntObjMgrGetCurrent()) {
        auto player = ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__);

        if (player) {
            // TODO FUN_004f7290 on the player's info block (+0x1008): its drunkenness, 0 to 1,
            // from the larger of the descriptor's drunk byte and the local one. Frozen keeps
            // neither yet, so the player is sober.
            uint8_t drunk = static_cast<uint8_t>(static_cast<int32_t>(0.0f * 255.0f));

            if (CWorldScene::s_cameraLiquidType) {
                grey = 0x54;
                underwater = 1;
            }

            if (grey < drunk) {
                grey = drunk;
            }
        }
    }

    uint32_t params[3] = { underwater, dayGlow, grey };
    CGWorldFrame::s_glowEffect->SetParams(3, params);
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

// ref: FUN_004f8ea0
// The world's draw for one frame, in the reference's order: the frame's rectangle of the screen
// as the viewport (flipped when drawing into a target), the map, the opaque models, the detail
// doodads, the transparent block in the order the camera's side of the water decides, the
// underwater motes, the glare, and the viewport given back. What the reference does here that
// frozen does not have yet is marked in place.
void CGWorldFrame::OnWorldRender() {
    CRect window;
    g_theGxDevicePtr->CapsWindowSize(window);

    if (window.maxY - window.minY == 0.0f || window.maxX - window.minX == 0.0f) {
        return;
    }

    if (!(this->m_viewport.minY < this->m_viewport.maxY && this->m_viewport.minX < this->m_viewport.maxX)) {
        return;
    }

    // The whole world render sits in a render-state push with multisampling on (0x004f8f2a), so
    // the world is drawn antialiased and the UI is not.
    GxRsPush();
    GxRsSet(GxRs_Multisample, 1);

    // The full-screen effect's parameters, and the world kept within what its scene target can
    // take.
    CGWorldFrame::UpdateGlowParams();
    FFX::BeginScene();

    // A world frame that does not cover the screen clears its own rectangle first.
    CRect fullScreen = { 0.0f, 0.0f, 1.0f, 1.0f };

    if (this->m_viewport != fullScreen) {
        GxSceneClear(3, { 0, 0, 0, 0xff });
    }

    if (CWorld::s_enables & CWorld::Enable_20000000) {
        g_theGxDevicePtr->DeviceOverride(8, 0);
    }

    // The frame's rectangle within the current viewport, upside down when drawing into a
    // target. The depth range is the saved minimum to 0.94 (DAT_00adeee4): everything past it is
    // held for the low-detail horizon at [0.998, 0.999] and the sky at [0.999, 1.0]. With the
    // world on [0, 1] its far terrain wrote depth above 0.998 and the horizon's mesh won the
    // test against it, drawing a fog-coloured ring along the ground at the far clip.
    float savedMinX, savedMaxX, savedMinY, savedMaxY, savedMinZ, savedMaxZ;
    GxXformViewport(savedMinX, savedMaxX, savedMinY, savedMaxY, savedMinZ, savedMaxZ);

    CGxTex* target = nullptr;
    GxRenderTargetGet(GxBuffers_Color, target);

    float bottom;
    float top;

    if (!target) {
        bottom = this->m_viewport.maxY;
        top = this->m_viewport.minY;
    } else {
        bottom = 1.0f - this->m_viewport.minY;
        top = 1.0f - this->m_viewport.maxY;
    }

    GxXformSetViewport(
        savedMinX + this->m_viewport.minX * (savedMaxX - savedMinX),
        this->m_viewport.maxX * (savedMaxX - savedMinX) + savedMinX,
        savedMinY + top * (savedMaxY - savedMinY),
        bottom * (savedMaxY - savedMinY) + savedMinY,
        savedMinZ,
        0.94f
    );

    // TODO FUN_004f2db0 (clears a character-component counter, DAT_00b6ba50), FUN_007e5120 (the
    // projected-texture frame stamp), and FUN_00715380(1) when the frame's +0xb10 bit 2 is set.

    CShaderEffect::UpdateProjMatrix();

    CWorld::RenderMap(this->m_camera->Position(), CWorld::GetTickTimeSec());

    // FROZEN-ONLY: the frame's view-projection and fog flag, which the sky, the weather, the
    // overhead icons and the particle passes all read. Built after the map so it sees this
    // frame's transforms.
    if (!CWorldScene::s_viewUpdated) {
        CWorldScene::UpdateWorldView();
    }

    CWorldScene::s_viewUpdated = false;

    // TODO FUN_0079fcc0 (a map pass with no other caller), then the per-unit visitor
    // ClntObjMgrEnumVisibleObjects(FUN_004f6a40) and the unit flag resets FUN_00715380 /
    // FUN_007153a0 / FUN_007153c0(0), and when the frame's +0xb10 bit 1 is set FUN_00615890(0)
    // and FUN_00725890 -- phase 4.

    // FROZEN-ONLY: the blob-shadow caster pass. It drains the frame's entity list, which is
    // what decides who casts; the reference reaches its casters from the M2 scene's projection
    // callback instead.
    if (ClntObjMgrGetCurrent()) {
        CWorldScene::DrawEntityShadows();
    }

    // DIVERGED: the reference wraps everything from here to the viewport restore, GxRsPop
    // included, in one test of the world scene, and so would leave the push unbalanced without
    // one. Frozen guards each scene draw instead; the world scene exists for the whole session.
    auto scene = CWorld::GetM2Scene();

    // FROZEN-ONLY: fog the models with the same data-driven fog the terrain and WMOs use; the
    // guard keeps clear zones unfogged.
    bool useFog = CWorld::GetFogEnd() > 1.0f && CWorld::GetFogStart() < CWorld::GetFarClip();

    if (useFog) {
        GxRsSet(GxRs_Fog, 1);
    }

    if (scene) {
        // FUN_0081ca10(-DAT_00cd7758) stores a float at the scene's +0x18 here; DAT_00cd7758 has
        // no writer anywhere in the binary, so it is always -0.0, and frozen's +0x18 is not that
        // float. Not ported.

        // Which passes the scene may run, set one instruction before the draw (0x004f9117).
        scene->m_passMask = CWorld::s_m2PassMask;

        scene->Draw(M2PASS_0);
    }

    // TODO FUN_004f8a40(0x200122) while the cursor mode (DAT_00ac79a4) is below 2: the spell
    // target's ground decal. Then the two deferred device lists, sorted from the camera and drawn
    // (FUN_00681ba0(0, 1), FUN_00682960(0)).

    CWorldScene::RenderDetailDoodads();

    // The transparent block, ordered by the camera's side of the water. The two arms share their
    // last draw (0x004f91b7), which is why they read as mirror images.
    uint32_t lastPass;

    if (!CWorld::IsCameraUnderLiquid()) {
        if (scene) { scene->Draw(M2PASS_2); }

        // FROZEN-ONLY: the particle emitters' quads, which belong with pass 2 in the reference,
        // and the overhead icons.
        ParticleFxRender();
        OverheadIconsRender();

        CWorldScene::DrawLiquidPass();
        CWorld::RenderWeather();
        CWorld::RenderBarriers(CWorld::GetTickTimeSec());
        lastPass = M2PASS_1;
    } else {
        CWorld::RenderBarriers(CWorld::GetTickTimeSec());
        if (scene) { scene->Draw(M2PASS_1); }
        CWorld::RenderWeather();
        CWorldScene::DrawLiquidPass();
        lastPass = M2PASS_2;
    }

    if (scene) { scene->Draw(static_cast<M2PASS>(lastPass)); }

    if (CWorld::IsCameraUnderLiquid()) {
        // FROZEN-ONLY: as above, after the last pass under water.
        ParticleFxRender();
        OverheadIconsRender();
    }

    // FROZEN-ONLY
    ParticleFxEndFrame();

    // TODO FUN_004f8a40(0x20000) under the same cursor test, FUN_007fca30 (mount transitions),
    // FUN_007f9ec0 (a camera-centred pass), FUN_006fdfb0 (missile trajectories) and FUN_004f6f90
    // (the frame's two held objects) -- phase 3 and 4.

    // The underwater motes last in the world (FUN_0077f9d0 -> FUN_0079ca70).
    CWorld::RenderParticulates();

    if (useFog) {
        GxRsSet(GxRs_Fog, 0);
    }

    // TODO the second pair of deferred device lists (FUN_00681ba0(1, 0), FUN_00682960(1)).

    // The sun's and the moon's glare over everything (FUN_007f0870, 0x004f9213), inside the push.
    DayNightGlareRender();

    // TODO FUN_007e5580: release the projected textures nothing used this frame.

    if (CWorld::s_enables & CWorld::Enable_20000000) {
        g_theGxDevicePtr->DeviceOverride(8, 1);
    }

    GxRsPop();

    // TODO FUN_00615890(1) (empty deferred list 1) and FUN_0056c7a0 (the UI's pending layout).

    GxXformSetViewport(savedMinX, savedMaxX, savedMinY, savedMaxY, savedMinZ, savedMaxZ);

    // The world copied out and the active full-screen effect run over it.
    FFX::EndScene();

    // TODO FUN_00747ae0 (clear the units' 0x1000 flag, phase 4).
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
