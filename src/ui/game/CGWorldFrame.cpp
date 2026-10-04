#include "world/map/MapFootprints.hpp"
#include "object/client/CGDynamicObject_C.hpp"
#include "model/CM2Model.hpp"
#include <vector>
#include "model/CM2Shared.hpp"
#include "model/M2Data.hpp"
#include "object/client/CGGameObject_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/ClntObjMgr.hpp"
#include "world/CWorld.hpp"
#include "ffx/FFX.hpp"
#include "console/CVar.hpp"
#include "object/client/AuraCache.hpp"
#include "ffx/EffectGlow.hpp"
#include "ffx/FFXEffects.hpp"
#include "world/CWorldScene.hpp"
#include "world/map/CMap.hpp"
#include "world/OverheadIcons.hpp"
#include "gx/Texture.hpp"
#include "component/CCharacterComponent.hpp"
#include "object/client/CGPlayer_C.hpp"
#include <common/Time.hpp>
#include <cstdio>
#include "object/client/QuestStatusCache.hpp"
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
#include "object/client/UnitVehicle_C.hpp"
#include "object/client/CVehiclePassenger_C.hpp"
#include "object/client/GameObjectTypes.hpp"
#include "ui/simple/CSimpleTop.hpp"
#include "object/client/CGCorpse_C.hpp"
#include "world/CWFrustum.hpp"
#include "ui/InputControl.hpp"
#include "ui/game/CGGameUI.hpp"
#include "ui/game/UIBindings.hpp"
#include "event/CEvent.hpp"
#include "object/client/SpellVisuals.hpp"
#include "object/client/CEffect.hpp"
#include <cmath>
#include "object/client/CGObject_C.hpp"
#include "gx/Coordinate.hpp"
#include "gx/Shader.hpp"
#include "gx/Transform.hpp"
#include "object/Client.hpp"
#include "ui/game/CGCamera.hpp"
#include "event/CEvent.hpp"
#include "ui/game/ScreenLayout.hpp"
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

    CGWorldFrame::s_deathEffect = STORM_NEW(EffectDeath);
    CGWorldFrame::s_netherEffect = STORM_NEW(EffectNether);

    CGWorldFrame::s_specialEffect = STORM_NEW(EffectSpecial);

    CGWorldFrame::UpdateScreenEffect();

    // TODO FUN_0047c500.
}

FFX::Effect* CGWorldFrame::s_glowEffect;
FFX::Effect* CGWorldFrame::s_deathEffect;
FFX::Effect* CGWorldFrame::s_netherEffect;
FFX::Effect* CGWorldFrame::s_specialEffect;

// ref: FUN_004f7020
void CGWorldFrame::SetScreenEffect(int32_t id) {
    // FROZEN-ONLY: frozen's aura handlers can ask before the world frame has made its effects.
    if (!CGWorldFrame::s_glowEffect) {
        return;
    }

    auto record = g_screenEffectDB.GetRecord(id);

    if (!record) {
        DayNightEndFogOverride();
        FFX::SetEffect(CGWorldFrame::s_glowEffect);
        DayNightClearForcedParams();

        // TODO FUN_004c8fa0(0, 0): clear the screen effect's ambience and music.
        return;
    }

    switch (record->m_effect) {
    case 0:
        DayNightEndFogOverride();
        FFX::SetEffect(CGWorldFrame::s_glowEffect);
        break;

    case 1:
        DayNightEndFogOverride();
        FFX::SetEffect(CGWorldFrame::s_deathEffect);
        break;

    case 2: {
        // The nether closes the fog in to 150 yards, white when the full-screen effects can tint
        // it and a dark blue-grey when they cannot.
        CImVector color;
        color.value = FFX::s_ffxCvar->m_intValue ? 0xffffffff : 0xff4c4c63;

        DayNightBeginFogOverride(0.7f, 150.0f, color, 0);
        FFX::SetEffect(CGWorldFrame::s_netherEffect);
        break;
    }

    case 3:
        // The special effect has no GL path; GL takes the glow.
        if (GxDevApi() == GxApi_OpenGl) {
            DayNightEndFogOverride();
            FFX::SetEffect(CGWorldFrame::s_glowEffect);
            break;
        }

        DayNightEndFogOverride();
        FFXFieldRestored();
        CGWorldFrame::s_specialEffect->SetParams(3, record->m_params);
        FFX::SetEffect(CGWorldFrame::s_specialEffect);
        break;

    default:
        break;
    }

    DayNightSetForcedParams(record->m_lightParamsID);

    // TODO FUN_004c8fa0(record->m_soundAmbienceID, record->m_zoneMusicID): the effect's ambience
    // and music.
}

// ref: FUN_004f88b0
// The last aura whose spell carries a screen-effect aura (260) names the effect. Without one, a
// ghost outside an arena's closing state sees the death effect and a player glowing from
// invisibility the nether; anyone else the glow.
void CGWorldFrame::UpdateScreenEffect() {
    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

    if (!player) {
        CGWorldFrame::SetScreenEffect(0);
        return;
    }

    // DIVERGED: the reference walks the player's own aura table (+0xc38, or the overflow block at
    // +0xc58 past its inline slots); frozen keeps the auras the server sends in the aura cache
    // instead, in the same slot order.
    WOWGUID guid = player->GetGUID();

    for (int32_t i = AuraCacheCount(guid, 0, 0) - 1; i >= 0; i--) {
        auto aura = AuraCacheGet(guid, i, 0, 0);

        if (!aura || !aura->spellID) {
            continue;
        }

        auto spell = g_spellDB.GetRecord(aura->spellID);

        if (!spell) {
            continue;
        }

        for (uint32_t effect = 0; effect < 3; effect++) {
            if (spell->m_effectAura[effect] == 0x104) {
                CGWorldFrame::SetScreenEffect(spell->m_effectMiscValue[effect]);
                return;
            }
        }
    }

    auto data = player->Player();

    // TODO the battlefield status (DAT_00bea570, BattlefieldInfo.cpp): a ghost sees no death
    // effect while it is 4. Frozen keeps no battlefield state, so the status is never 4.
    int32_t battlefieldStatus = 0;

    if ((data->flags & 0x10) && battlefieldStatus != 4) {
        CGWorldFrame::SetScreenEffect(1);
        return;
    }

    if (data->field_bytes_2_4 & 0x40) {
        CGWorldFrame::SetScreenEffect(0x51);
        return;
    }

    CGWorldFrame::SetScreenEffect(0);
}

// ref: FUN_004f7290
// How drunk the player is, 0 to 1: the larger of the server's drunk state and the faked one,
// capped at a hundred.
static float PlayerDrunkenness(CGPlayer_C* player) {
    auto data = player->Player();

    uint32_t drunk = data->bytes_3_2;
    uint32_t fake = static_cast<uint32_t>(data->fakeInebriation);
    uint32_t larger = static_cast<int32_t>(drunk) <= static_cast<int32_t>(fake) ? fake : drunk;

    if (static_cast<int32_t>(larger) < 100) {
        if (static_cast<int32_t>(fake) < static_cast<int32_t>(drunk)) {
            return static_cast<float>(drunk) * 0.01f;
        }
    } else {
        fake = 100;
    }

    return static_cast<float>(fake & 0xff) * 0.01f;
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
        auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

        if (player) {
            uint8_t drunk = static_cast<uint8_t>(static_cast<int32_t>(PlayerDrunkenness(player) * 255.0f));

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
namespace {

// ref: FUN_0055e2c0
// The binding string for a key event: the modifier prefix (less the key's own modifier bit, for
// a modifier key) and the key's name. Only for a key press without the repeat count (< 2).
bool WorldFrameBindingString(const CKeyEvent& evt, char* buffer, size_t bufferBytes) {
    uint32_t modifiers = evt.metaKeyState;

    if (evt.repeat >= 2) {
        return false;
    }

    if (static_cast<int32_t>(evt.key) < 6) {
        modifiers &= ~(1u << static_cast<int32_t>(evt.key));
    }

    char prefix[64] = "";
    UIBindingsModifierPrefix(modifiers, prefix, sizeof(prefix));

    char keyBuffer[32];
    auto name = UIBindingsKeyName(static_cast<int32_t>(evt.key), keyBuffer, sizeof(keyBuffer));

    if (!name || !*name) {
        return false;
    }

    SStrCopy(buffer, prefix, bufferBytes);
    SStrPack(buffer, name, bufferBytes);

    return true;
}

// ref: FUN_0055df30
// A mouse button's slot in the world frame's binding table: its bit's position, except that the
// right button is 2 and the middle one 3, as their binding names go.
int32_t WorldFrameMouseButtonIndex(uint32_t button) {
    switch (button) {
        case 0x1:
            return 1;
        case 0x2:
            return 3;
        case 0x4:
            return 2;
        default:
            break;
    }

    for (int32_t bit = 3; bit < 31; bit++) {
        if (button == (1u << bit)) {
            return bit + 1;
        }
    }

    return 0;
}

// ref: FUN_0055e340
// The binding string for a mouse event: the modifier prefix, then BUTTON1 (left), BUTTON2
// (right), BUTTON3 (middle), BUTTON4 and up, or MOUSEWHEELUP / MOUSEWHEELDOWN.
bool WorldFrameMouseBindingString(const CMouseEvent& evt, char* buffer, size_t bufferBytes) {
    char prefix[64] = "";
    UIBindingsModifierPrefix(evt.metaKeyState, prefix, sizeof(prefix));

    char name[32] = "";

    if (evt.id == 0x400500C8 || evt.id == 0x400500C9) {
        switch (evt.button) {
            case 0x1:
                SStrCopy(name, "BUTTON1", sizeof(name));
                break;
            case 0x2:
                SStrCopy(name, "BUTTON3", sizeof(name));
                break;
            case 0x4:
                SStrCopy(name, "BUTTON2", sizeof(name));
                break;
            default:
                for (int32_t i = 4; i < 32; i++) {
                    if (evt.button == (1u << (i - 1))) {
                        SStrPrintf(name, sizeof(name), "BUTTON%d", i);
                        break;
                    }
                }
                break;
        }
    } else if (evt.id == 0x400500CD) {
        SStrCopy(name, evt.wheelDistance < 0 ? "MOUSEWHEELDOWN" : "MOUSEWHEELUP", sizeof(name));
    } else {
        return false;
    }

    SStrCopy(buffer, prefix, bufferBytes);
    SStrPack(buffer, name, bufferBytes);

    return true;
}

} // namespace

// ref: FUN_004f6ae0
// PARTIAL: a modifier key's own press signals MODIFIER_STATE_CHANGED with its name (FUN_004f5fb0)
// in the reference; frozen consumes it without the signal.
int32_t CGWorldFrame::OnLayerKeyDown(const CKeyEvent& evt) {
    if (this->CSimpleFrame::OnLayerKeyDown(evt)) {
        return 1;
    }

    int32_t key = static_cast<int32_t>(evt.key);

    if (key < 0 || 0x313 <= key) {
        return 0;
    }

    if (key < 6) {
        return 1;
    }

    auto& binding = this->m_keyBindings[key];

    if (!WorldFrameBindingString(evt, binding.name, sizeof(binding.name))) {
        return 0;
    }

    binding.modifiers = evt.metaKeyState;

    return UIBindingsDispatchKey(binding.name, 1);
}

// ref: FUN_004f6b70
int32_t CGWorldFrame::OnLayerKeyUp(const CKeyEvent& evt) {
    if (this->CSimpleFrame::OnLayerKeyUp(evt)) {
        return 1;
    }

    int32_t key = static_cast<int32_t>(evt.key);

    if (key < 0 || 0x313 <= key) {
        return 0;
    }

    if (key < 6) {
        return 1;
    }

    auto& binding = this->m_keyBindings[key];

    if (!binding.name[0]) {
        binding.modifiers = 0;
        WorldFrameBindingString(evt, binding.name, sizeof(binding.name));

        if (!binding.name[0]) {
            return 0;
        }
    }

    binding.modifiers |= evt.metaKeyState;

    int32_t result = UIBindingsDispatchKey(binding.name, 0);
    binding.name[0] = '\0';

    return result;
}

// ref: FUN_004f6c10
// A mouse button over the world runs its binding (BUTTON1 CAMERAORSELECTORMOVE, BUTTON2
// TURNORACTION by default), as a key does; the binding string is kept per button so the release
// runs the same one. A right press first settles the cursor (FUN_0051fb00).
int32_t CGWorldFrame::OnLayerMouseDown(const CMouseEvent& evt, const char* btn) {
    if (this->CSimpleFrame::OnLayerMouseDown(evt, btn)) {
        return 1;
    }

    GameUIWorldRightPress(evt);

    auto& binding = this->m_mouseBindings[WorldFrameMouseButtonIndex(evt.button)];

    if (!WorldFrameMouseBindingString(evt, binding.name, sizeof(binding.name))) {
        return 0;
    }

    binding.modifiers = evt.metaKeyState;

    return UIBindingsDispatchKey(binding.name, 1);
}

// ref: FUN_004f6c90
int32_t CGWorldFrame::OnLayerMouseUp(const CMouseEvent& evt, const char* btn) {
    if (this->CSimpleFrame::OnLayerMouseUp(evt, btn)) {
        return 1;
    }

    auto& binding = this->m_mouseBindings[WorldFrameMouseButtonIndex(evt.button)];

    if (!binding.name[0]) {
        binding.modifiers = 0;
        WorldFrameMouseBindingString(evt, binding.name, sizeof(binding.name));

        if (!binding.name[0]) {
            return 0;
        }
    }

    binding.modifiers |= evt.metaKeyState;

    int32_t result = UIBindingsDispatchKey(binding.name, 0);
    binding.name[0] = '\0';

    return result;
}

// ref: FUN_004f5c80
// The wheel over the world is a binding (MOUSEWHEELUP / MOUSEWHEELDOWN, CAMERAZOOMIN / OUT by
// default), pressed and released at once.
int32_t CGWorldFrame::OnLayerMouseWheel(const CMouseEvent& evt) {
    if (this->CSimpleFrame::OnLayerMouseWheel(evt) || evt.wheelDistance == 0) {
        return 1;
    }

    char binding[32];

    if (!WorldFrameMouseBindingString(evt, binding, sizeof(binding))) {
        return 0;
    }

    return UIBindingsDispatchKey(binding, 1) + UIBindingsDispatchKey(binding, 0);
}

CGWorldFrame* CGWorldFrame::s_currentWorldFrame = nullptr;

// ref: FUN_004f9f70
int32_t CGWorldFrame::ObjectWorldHandler(void* param, int32_t flags, uint32_t guidLow, uint32_t guidHigh,
                                         uint32_t param32) {
    auto frame = CGWorldFrame::s_currentWorldFrame;

    WOWGUID guid = (static_cast<WOWGUID>(guidHigh) << 32) | guidLow;
    auto object = ClntObjMgrObjectPtr(guid, TYPE_OBJECT, ".\\WorldFrame.cpp", 0x41f);

    if (!object) {
        return 1;
    }

    object->m_unreached = (flags & 0x4) ? 1 : 0;

    if (!object->m_disabled) {
        auto model = object->GetObjectModel();

        // FUN_00743450
        if (model && model->IsDrawable(0, 0)) {
            frame->AddVisibleObject(object, static_cast<uint32_t>(flags));

            return 1;
        }
    }

    auto model = object->GetObjectModel();

    if (model) {
        if (model->m_attachParent) {
            model->m_flag80 = 0;
            model->m_flag20000 = 0;
        } else {
            model->m_flag8 = 0;
            model->m_flag10000 = 0;
        }
    }

    return 1;
}

// ref: FUN_004f8d10
void CGWorldFrame::AddVisibleObject(CGObject_C* object, uint32_t flags) {
    auto model = object->GetObjectModel();

    if (!model) {
        return;
    }

    int32_t hidden = 0;
    int32_t hiddenOther = 0;
    object->GetHidden(flags, &hidden, &hiddenOther);

    uint32_t draw = (hidden == 0 && hiddenOther == 0) ? 1 : 0;

    object->UpdateForFrame(this);

    if (!object->PlaceModel(this->m_elapsed)) {
        return;
    }

    // Units, game objects and corpses (type bits 0x08, 0x20, 0x80) the frame draws can be picked.
    if ((object->IsA(TYPE_UNIT) || object->IsA(TYPE_GAMEOBJECT) || object->IsA(TYPE_CORPSE)) && draw
        && !object->m_disablePending) {
        auto record = this->m_freeModelRecords.Head();

        if (record) {
            record->m_link.Unlink();
        } else {
            record = this->NewModelRecord();
        }

        this->m_modelRecords.LinkToTail(record);

        auto recordModel = object->GetObjectModel();
        record->m_model = recordModel;
        recordModel->m_refCount++;
        record->m_guid = object->GetGUID();
    }

    if (model->m_attachParent) {
        model->m_flag80 = draw;
        model->m_flag20000 = draw;
    } else {
        model->m_flag8 = draw;
        model->m_flag10000 = draw;
    }
}

// ref: FUN_004f89e0
CModelRecord* CGWorldFrame::NewModelRecord() {
    auto record = STORM_NEW(CModelRecord);
    record->m_distance = INFINITY;

    return record;
}

// ref: FUN_004f9310
void CGWorldFrame::ReleaseModelRecords(STORM_EXPLICIT_LIST(CModelRecord, m_link)& list) {
    for (auto record = list.Head(); record; record = list.Next(record)) {
        if (record->m_model) {
            record->m_model->Release();
            record->m_model = nullptr;
        }
    }

    while (auto record = list.Head()) {
        record->m_link.Unlink();
        this->m_freeModelRecords.LinkToTail(record);
    }
}

// ref: FUN_004fa5d0
void CGWorldFrame::ReleaseAllModelRecords() {
    this->ReleaseModelRecords(this->m_modelRecords);
    this->ReleaseModelRecords(this->m_modelRecords2);
}

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

    // TODO FUN_004f2db0 (clears a character-component counter, DAT_00b6ba50) and FUN_00715380(1)
    // when the frame's +0xb10 bit 2 is set.

    // The name plates' frame stamp (FUN_007e5120, 0x004f9067).
    PlayerNameNewFrame();

    CShaderEffect::UpdateProjMatrix();

    CWorld::RenderMap(this->m_camera->Position(), CWorld::GetTickTimeSec());

    // FROZEN-ONLY: the frame's view-projection and fog flag, which the sky, the weather, the
    // overhead icons and the particle passes all read. Built after the map so it sees this
    // frame's transforms.
    if (!CWorldScene::s_viewUpdated) {
        CWorldScene::UpdateWorldView();
    }

    CWorldScene::s_viewUpdated = false;

    // The footprints (FUN_0077f070 -> FUN_0079fcc0).
    FootprintsRender();

    // TODO the per-unit visitor ClntObjMgrEnumVisibleObjects(FUN_004f6a40) and the unit flag resets FUN_00715380 /
    // FUN_007153a0 / FUN_007153c0(0), and when the frame's +0xb10 bit 1 is set FUN_00615890(0)
    // and FUN_00725890 -- phase 4.

    // DIVERGED: the reference wraps everything from here to the viewport restore, GxRsPop
    // included, in one test of the world scene, and so would leave the push unbalanced without
    // one. Frozen guards each scene draw instead; the world scene exists for the whole session.
    auto scene = CWorld::GetM2Scene();

    // No fog switch here. The reference turns GxRs_Fog on in exactly one place,
    // CShaderEffect::SetFogEnabled (0x008733ed): every pass that fogs asks for it -- the M2 scene
    // per batch from its lighting -- and nothing turns it on for the frame.

    if (scene) {
        // FUN_0081ca10(-DAT_00cd7758) stores a float at the scene's +0x18 here; DAT_00cd7758 has
        // no writer anywhere in the binary, so it is always -0.0, and frozen's +0x18 is not that
        // float. Not ported.

        // Which passes the scene may run, set one instruction before the draw (0x004f9117).
        scene->m_passMask = CWorld::s_m2PassMask;

        scene->Draw(M2PASS_0);
    }

    // TODO FUN_004f8a40(0x200122) while the cursor mode (DAT_00ac79a4) is below 2: the spell
    // target's ground decal.

    // The first deferred device list, by state key, then drawn.
    GxuSortDrawList(GxuCat_0, 1, CWorldScene::s_cameraPos);
    GxuFlushDrawList(GxuCat_0, CWorldScene::s_cameraPos);

    CWorldScene::RenderDetailDoodads();

    // The transparent block, ordered by the camera's side of the water. The two arms share their
    // last draw (0x004f91b7), which is why they read as mirror images.
    uint32_t lastPass;

    if (!CWorld::IsCameraUnderLiquid()) {
        if (scene) { scene->Draw(M2PASS_2); }

        // FROZEN-ONLY: the overhead icons. (The particle stand-in drew here too; the reference's
        // particles draw inside the scene passes, and the stand-in is retired from the world.)
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
        OverheadIconsRender();
    }

    // FROZEN-ONLY: ages the stand-in's entries, which only the UI model previews
    // (CSimpleModel) still feed.
    ParticleFxEndFrame();

    // TODO FUN_004f8a40(0x20000) under the same cursor test.

    // The spell visuals: chains, shards and mount transitions advance, then draw (0x004f91d4,
    // 0x004f91d9).
    SpellVisualsUpdate();
    SpellVisualsDraw();

    // TODO FUN_006fdfb0 (missile trajectories) and FUN_004f6f90 (the frame's two held objects).

    // The underwater motes last in the world (FUN_0077f9d0 -> FUN_0079ca70).
    CWorld::RenderParticulates();

    // The second deferred device list, back to front, then drawn.
    GxuSortDrawList(GxuCat_1, 0, CWorldScene::s_cameraPos);
    GxuFlushDrawList(GxuCat_1, CWorldScene::s_cameraPos);

    // The sun's and the moon's glare over everything (FUN_007f0870, 0x004f9213), inside the push.
    DayNightGlareRender();

    // The texts of objects nothing updated this frame go (FUN_007e5580, 0x004f9218).
    PlayerNameExpireWorldText();

    if (CWorld::s_enables & CWorld::Enable_20000000) {
        g_theGxDevicePtr->DeviceOverride(8, 1);
    }

    GxRsPop();

    // This frame's world text places are let go (FUN_00615890(1), 0x004f9242).
    ScreenLayoutClear(1);

    // TODO FUN_0056c7a0 (the UI's pending layout).

    GxXformSetViewport(savedMinX, savedMaxX, savedMinY, savedMaxY, savedMinZ, savedMaxZ);

    // The world copied out and the active full-screen effect run over it.
    FFX::EndScene();

    // TODO FUN_00747ae0 (clear the units' 0x1000 flag, phase 4).
}

// ref: FUN_004f6970
// The per-object update the world update walks the visible objects with: each type's own -- units
// FUN_00734390, game objects FUN_0070d040, dynamic objects FUN_007051b0.
static int32_t UpdateVisibleObject(WOWGUID guid, void* param) {
    auto object = ClntObjMgrObjectPtr(guid, TYPE_OBJECT, __FILE__, __LINE__);

    if (!object) {
        return 1;
    }

    if (object->IsA(TYPE_GAMEOBJECT)) {
        static_cast<CGGameObject_C*>(object)->UpdateForFrame(CWorld::GetCurTimeMs());

        return 1;
    }

    if (object->IsA(TYPE_UNIT)) {
        static_cast<CGUnit_C*>(object)->UpdateVisible(CWorld::GetCurTimeMs());

        return 1;
    }

    if (object->IsA(TYPE_DYNAMICOBJECT)) {
        static_cast<CGDynamicObject_C*>(object)->UpdateForFrame(CWorld::GetCurTimeMs());
    }

    return 1;
}

// ref: FUN_004fa5f0
// The world's update for one frame, in the reference's order. What the reference does here that
// frozen does not yet have is marked in place, so filling it in is a matter of calling it.
void CGWorldFrame::OnWorldUpdate() {
    float dt = CWorld::GetTickTimeSec();

    // FUN_004fa040 stores its elapsed time here (0x004fa344) before the update runs.
    this->m_elapsed = dt;

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
    // (FUN_006e2880).
    if (!target && player) {
        this->m_camera->SetTarget(ClntObjMgrGetActivePlayer());
        target = player;
    }

    // FUN_0074ce40: a riding player the camera follows gets its seat's camera.
    if (target && target->IsA(TYPE_PLAYER)) {
        UnitUpdateVehicleCamera(static_cast<CGUnit_C*>(target));
    }

    // What the camera follows, as the world knows it (FUN_00780500 at 0x004fa7a0).
    CWorld::s_focusEntity = target && target->m_worldObject
        ? reinterpret_cast<CMapStaticEntity*>(target->m_worldObject)
        : nullptr;

    CVehiclePassenger_C::UpdateAll(CWorld::GetCurTimeMs());

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

    // The objects' ObjectEffect managers (FUN_00744140), then the frame's pick lists from the last
    // frame let go (FUN_004f9310 on +0x29c and +0x2a8, 0x004faa51).
    // TODO FUN_00616e80 (portraits).
    ObjectsUpdateObjectEffects();

    this->ReleaseAllModelRecords();

    ClntObjMgrEnumVisibleObjects(&UpdateVisibleObject, nullptr);

    // TODO FUN_0077f2b0(FUN_004f6560).

    // Every effect advances (0x004faa83).
    CEffect::UpdateAll();

    // TODO FUN_00703b00 (missiles), FUN_00804d20 / FUN_00804c10 (spells).

    auto targetPos = target && !this->m_camera->HasModel()
        ? target->GetPosition()
        : this->m_camera->Position();

    CWorld::Update(this->m_camera->Position(), this->m_camera->Target(), targetPos);

    // The view-projection kept for the screen projection, and the frame marked when it changed
    // (0x004fab37).
    C44Matrix viewProjection;
    GxXformViewProj(viewProjection);

    if (!(viewProjection == this->m_viewProjection)) {
        this->m_renderFlags |= 0x2;
        this->m_viewProjection = viewProjection;
    }

    // TODO FUN_00405130, the sound updates
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

// ------------------------------------------------------------------------------------------------
// The cursor's pick: what the world frame finds under the mouse when a button goes down, and what
// a short click on it does (WorldFrame.cpp 0x004f6270 .. 0x004fa570).
// ------------------------------------------------------------------------------------------------

namespace {

// ref: FUN_007fd620
// Whether a spell is waiting for the player to pick its target (Spell_C's DAT_00d3f4e4). The spell
// cast port keeps that pending spell; until it does none ever waits, so the pick never takes the
// spell-targeting branches below.
bool SpellIsTargeting() {
    return false;
}

// ref: FUN_00721f50
// Click-to-move applies: the unit is the alive active mover and autoInteract is on.
bool ClickToMoveEnabled(CGUnit_C* unit) {
    static CVar* autoInteract = CVar::Lookup("autoInteract");

    return 0 < unit->Unit()->health && unit->GetGUID() == CGUnit_C::s_activeMover && autoInteract
        && autoInteract->GetInt() != 0;
}

// ref: FUN_00717b60
// A dead unit that may be looted once its death animation has played out.
bool UnitIsLootable(CGUnit_C* unit, uint32_t time) {
    return unit->Unit()->health < 1 && (unit->m_deathTime == 0 || static_cast<int32_t>(time - unit->m_deathTime) >= 0)
        && (unit->Unit()->dynamicFlags & 0x1);
}

// ref: FUN_004f7650
// What the cursor may pick: terrain for click-to-move (1, or 3 while flying above water), and,
// with a player in the world, units (0x8), players (0x10), game objects (0x4) and corpses
// (0x40).
//
// PARTIAL: the spell-targeting masks (FUN_007fd650 .. FUN_00801960, the friendly, hostile, dead,
// party and creature-type filters) are the spell cast port's, along with the item- and
// trade-target ones (FUN_007fd710 / FUN_007fd720).
uint32_t CursorPickMask() {
    if (SpellIsTargeting()) {
        return 0;
    }

    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));
    uint32_t mask = 0;
    auto mover = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(CGUnit_C::s_activeMover, TYPE_UNIT, ".\\WorldFrame.cpp", 0x375));

    if (mover && ClickToMoveEnabled(mover) && CGGameUI::GetCursorKind() == 0) {
        uint32_t moveFlags = mover->m_localMove.m_moveFlags;
        mask = 1;

        if ((moveFlags & 0x10000000) && !(moveFlags & 0x200000)) {
            mask = 3;
        }
    }

    if (player) {
        mask |= 0x5c;
    }

    return mask;
}

// ref: FUN_004f7350
// A unit the cursor may pick: not the player itself or a unit it controls (unless the mask
// allows it, 0x20).
//
// PARTIAL: the spell-target filters (mask bits 0xf0000, 0x300000, 0x400000 and the cast's own
// attribute checks) come only from a spell waiting for its target; see SpellIsTargeting.
bool UnitIsPickable(CGUnit_C* unit, uint32_t mask) {
    if (!(mask & 0x20)) {
        if (unit->GetGUID() == ClntObjMgrGetActivePlayer()) {
            return false;
        }

        if (unit->IsControlledBy(ClntObjMgrGetActivePlayer())) {
            return false;
        }
    }

    if ((mask & 0x300000) != 0) {
        uint32_t allowed = unit->Unit()->health < 1 ? (mask & 0x200000) : (mask & 0x100000);

        if (!allowed) {
            return false;
        }
    }

    if ((mask & 0x400000) && unit->GetCreatureType() == 0xC) {
        return true;
    }

    return true;
}

// ref: FUN_004f7530
bool ObjectIsPickable(CGObject_C* object, uint32_t mask) {
    switch (object->GetType()) {
        case HIER_TYPE_UNIT:
            return (mask & 0x8) && UnitIsPickable(static_cast<CGUnit_C*>(object), mask);

        case HIER_TYPE_PLAYER:
            return (mask & 0x10) && UnitIsPickable(static_cast<CGUnit_C*>(object), mask);

        case HIER_TYPE_GAMEOBJECT:
            // A game object only for a spell that targets one (FUN_007fff20) while one waits.
            return (mask & 0x4) && !SpellIsTargeting();

        case HIER_TYPE_CORPSE:
            // FUN_004f58e0: any corpse, or one the waiting spell accepts (FUN_007ffea0).
            return (mask & 0x40) != 0;

        default:
            return false;
    }
}

// ref: FUN_004f6270
// The highlight a picked object's model takes: a living unit 3, a lootable one 2, a usable game
// object 1, a lootable corpse 2.
uint32_t ObjectHighlightKind(CGObject_C* object) {
    if (!object) {
        return 0;
    }

    switch (object->GetType()) {
        case HIER_TYPE_UNIT:
        case HIER_TYPE_PLAYER: {
            auto unit = static_cast<CGUnit_C*>(object);

            if (0 < unit->Unit()->health) {
                return 3;
            }

            return UnitIsLootable(unit, static_cast<uint32_t>(OsGetAsyncTimeMs())) ? 2 : 0;
        }

        case HIER_TYPE_GAMEOBJECT: {
            // FUN_0070ba00: the type behaviour's own say (slot 0x18).
            auto type = static_cast<CGGameObject_C*>(object)->m_type;
            return type && type->CanUse() ? 1 : 0;
        }

        case HIER_TYPE_CORPSE:
            // FUN_007058f0
            return (static_cast<CGCorpse_C*>(object)->Corpse()->dynamicFlags & 0x1) ? 2 : 0;

        default:
            return 0;
    }
}

// ref: FUN_007207e0
// A dead unit the cursor tries only after everything else: one with nothing left to give (not
// lootable, not skinnable).
//
// PARTIAL: whether the player may loot it (FUN_006d5a60, Player_C's loot rights) and whether it
// can skin it (FUN_0053bce0, the skinning professions) are those ports'; a lootable corpse
// counts as lootable, and skinning is not checked.
bool UnitIsPickedLast(CGUnit_C* unit, uint32_t time) {
    if (unit->Unit()->flags & 0x2000000) {
        return true;
    }

    if (!unit->IsA(TYPE_UNIT) || unit->IsA(TYPE_PLAYER) || 0 < unit->Unit()->health) {
        return false;
    }

    if (UnitIsLootable(unit, time)) {
        return false;
    }

    return true;
}

// ref: FUN_004f6370
// A model (and every model attached under it) takes part in the scene's ray query, with the
// highlight it would get, the record it answers for, and the query kind.
void AddModelToRayQuery(CM2Model* model, uint32_t highlight, void* owner, uint32_t kind) {
    if (model->m_loaded) {
        if (!model->m_rayPrev && model->m_scene) {
            auto& head = model->m_scene->m_rayModelList;

            model->m_rayPrev = &head;
            model->m_rayNext = head;
            head = model;

            if (model->m_rayNext) {
                model->m_rayNext->m_rayPrev = &model->m_rayNext;
            }
        }

        model->m_rayQueryType = kind;
        model->m_rayKey = highlight;
        model->m_rayOwner = owner;
    }

    for (auto child = model->m_attachList; child; child = child->m_attachNext) {
        if (!child->m_rayPrev) {
            AddModelToRayQuery(child, highlight, reinterpret_cast<void*>(static_cast<uintptr_t>(0xFFFFFFFF)), kind);
        }
    }
}

// ref: FUN_004bf0f0
// The ray through a point of the view (u, v in 0..1 across and up the frustum) from the near
// face to the far one, in the camera's space.
bool FrustumRay(float u, float v, C3Vector* start, C3Vector* end) {
    if (!start || !end || u < 0.0f || 1.0f < u || v < 0.0f || 1.0f < v) {
        return false;
    }

    C44Matrix view;
    C44Matrix proj;
    GxXformView(view);
    GxXformProjection(proj);

    C3Vector c[8];
    FrustumCorners(view, proj, c);

    C3Vector nearA = c[0] + (c[1] - c[0]) * v;
    C3Vector nearB = c[3] + (c[2] - c[3]) * v;
    C3Vector farA = c[4] + (c[5] - c[4]) * v;
    C3Vector farB = c[7] + (c[6] - c[7]) * v;

    *start = nearA + (nearB - nearA) * u;
    *end = farA + (farB - farA) * u;

    return true;
}

} // namespace

// ref: FUN_004f6d20
// Where a point of the world lands on the screen, in DDC across the frame, its depth in z. `flags`
// gets which of the frame's edges it is inside: 1 left, 2 bottom, 4 right, 8 top. Nonzero when it is
// inside all four, and 0 for a point behind the near plane.
int32_t CGWorldFrame::GetScreenCoordinates(const C3Vector& world, C3Vector* screen, uint32_t* flags) {
    const C3Vector& camera = this->m_camera->Position();

    // The view carries no translation, so the point is taken relative to the camera with w 0.
    C4Vector relative = { world.x - camera.x, world.y - camera.y, world.z - camera.z, 0.0f };
    C4Vector clip;
    TransformVector4(&clip, relative, this->m_viewProjection);

    if (clip.z < this->m_camera->NearZ()) {
        return 0;
    }

    screen->z = clip.z;

    float inv = 1.0f / clip.w;
    float u = (clip.x * inv + 1.0f) * 0.5f;
    float v = (clip.y * inv + 1.0f) * 0.5f;

    float x;
    float y;
    NDCToDDC((this->m_viewport.maxX - this->m_viewport.minX) * u, (this->m_viewport.maxY - this->m_viewport.minY) * v, &x, &y);

    if (this->m_rect.minX < 0.0f) {
        x -= this->m_rect.minX;
    }

    if (this->m_rect.minY < 0.0f) {
        y -= this->m_rect.minY;
    }

    screen->x = x;
    screen->y = y;

    uint32_t inside = (y <= this->m_rect.maxY - this->m_rect.minY ? 0x8 : 0)
        | (x <= this->m_rect.maxX - this->m_rect.minX ? 0x4 : 0)
        | (0.0f < y ? 0x2 : 0)
        | (0.0f <= x ? 0x1 : 0);

    if (flags) {
        *flags = inside;
        return inside == 0xf;
    }

    return inside == 0xf;
}

// ref: FUN_004f6450
// The cursor's ray in the world: the point's place across the frame's rect, through the frustum,
// moved out to the camera.
int32_t CGWorldFrame::GetCursorRay(float x, float y, C3Vector* start, C3Vector* end) {
    float u = (x - this->m_screenRect.minX) / (this->m_screenRect.maxX - this->m_screenRect.minX);
    float v = (y - this->m_screenRect.minY) / (this->m_screenRect.maxY - this->m_screenRect.minY);

    if (u < 0.0f || v < 0.0f || 1.0f < u || 1.0f < v) {
        return 0;
    }

    if (!FrustumRay(u, v, start, end)) {
        return 0;
    }

    const C3Vector& eye = this->m_camera->Position();
    *start = *start + eye;
    *end = *end + eye;

    return 1;
}

// ref: FUN_004f9550
// The nearest picked model along the ray: every model the frame drew this frame whose object
// may be picked joins the scene's ray query (a corpse with nothing to give only on a second
// pass, when nothing else was hit), and the hit's object and distance come back.
WOWGUID CGWorldFrame::FindClosestModel(const C3Vector& start, const C3Vector& end, uint32_t mask, float* distance) {
    C44Matrix view;
    GxXformView(view);

    const C3Vector& eye = this->m_camera->Position();
    C3Vector viewStart = (start - eye) * view;
    C3Vector viewEnd = (end - eye) * view;

    STORM_EXPLICIT_LIST(CModelRecord, m_link) later;
    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());
    auto scene = CWorld::GetM2Scene();

    scene->BeginRayQuery();

    // FUN_00801b10: a waiting spell that may target anything (attribute 0x1000000) picks
    // un-highlightable objects too; none waits (SpellIsTargeting).
    bool anyObject = false;

    for (auto record = this->m_modelRecords.Head(); record;) {
        auto next = this->m_modelRecords.Next(record);
        auto object = static_cast<CGObject_C*>(ClntObjMgrObjectPtr(record->m_guid, TYPE_OBJECT, ".\\WorldFrame.cpp", 0x276));

        if (object && ObjectIsPickable(object, mask) && (object->CanHighlight() || anyObject)) {
            record->m_object = object;

            if (object->IsA(TYPE_UNIT) && UnitIsPickedLast(static_cast<CGUnit_C*>(object), now)) {
                record->m_link.Unlink();
                later.LinkToTail(record);
            } else if (record->m_model) {
                // FUN_004f6400
                uint32_t highlight = record->m_guid == this->m_cursorModelObject ? 0xFFFFFFFF : ObjectHighlightKind(object);
                record->m_object = nullptr;
                AddModelToRayQuery(record->m_model, highlight, record, (mask >> 25) & 4);
            }
        }

        record = next;
    }

    float fraction = 1.0f;
    auto hit = static_cast<CModelRecord*>(scene->RayQuery(viewStart, viewEnd, &fraction, 1));

    if (!hit) {
        scene->BeginRayQuery();

        for (auto record = later.Head(); record; record = later.Next(record)) {
            uint32_t highlight = record->m_guid == this->m_cursorModelObject ? 0xFFFFFFFF : ObjectHighlightKind(record->m_object);
            record->m_object = nullptr;

            if (record->m_model) {
                AddModelToRayQuery(record->m_model, highlight, record, (mask >> 25) & 4);
            }
        }

        fraction = 1.0f;
        hit = static_cast<CModelRecord*>(scene->RayQuery(viewStart, viewEnd, &fraction, 1));
    }

    while (auto record = later.Head()) {
        record->m_link.Unlink();
        this->m_modelRecords.LinkToTail(record);
    }

    if (!hit || hit == reinterpret_cast<CModelRecord*>(static_cast<uintptr_t>(0xFFFFFFFF))) {
        return 0;
    }

    C3Vector d = viewEnd - viewStart;
    float length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) * fraction;

    hit->m_distance = length;
    *distance = length;

    return hit->m_guid;
}

// ref: FUN_004f9930
// What the ray meets first: the terrain and buildings (and the water while flying), the nearest
// picked model, or a transport's surface. 2 is a model, 3 a map object with an owner (its point
// in the owner's space when it moves), 1 bare terrain while click-to-move wants it, 0 nothing.
int32_t CGWorldFrame::IntersectWorld(const C3Vector& start, const C3Vector& end, uint32_t mask, CURSORHIT* hit) {
    C3Vector point = {};
    float t = 1.0f;
    float objectDistance = 0.0f;
    WOWGUID entity = 0;

    C3Vector stop = end;
    C3Vector dir = end - start;
    float length = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;

    if (2.38419e-07f <= std::fabs(length)) {
        length = std::sqrt(length);
        dir = dir * (1.0f / length);
    }

    float hitDistance = 0.0f;
    bool hitWorld = WorldQuerySegment(start, stop, &point, &t, (mask & 2) ? 0x1020124 : 0x1000124, nullptr) != 0;

    if (hitWorld) {
        length = length * t;
        entity = CMap::s_segmentHitGUID;
        stop = start + dir * length;
        hitDistance = length;
    }

    WOWGUID model = 0;

    if (mask & 0x7C) {
        model = this->FindClosestModel(start, stop, mask, &objectDistance);

        if (model) {
            length = objectDistance;
            stop = start + dir * objectDistance;
        }
    }

    if (mask & 0x3) {
        float t2 = 1.0f;

        if (WorldQuerySegment(start, stop, &point, &t2, 0x100151, nullptr)) {
            hitDistance = length * t2;
            entity = CMap::s_segmentHitGUID;
            hitWorld = true;
            stop = start + dir * hitDistance;
        }
    }

    // FUN_004f6d20 projects the point back to the screen and, when it is on it, hands it to
    // FUN_00683660 (an object the decompilation does not name); nothing in frozen reads it.

    if (model) {
        hit->distance = objectDistance;
        hit->position = stop;
        hit->guid = model;
        return 2;
    }

    if (!hitWorld) {
        return 0;
    }

    hit->position = stop;
    hit->distance = hitDistance;

    if (entity == 0) {
        hit->guid = 0;
        return (mask & 0x3) != 0 ? 1 : 0;
    }

    auto object = static_cast<CGObject_C*>(ClntObjMgrObjectPtr(entity, TYPE_OBJECT, ".\\WorldFrame.cpp", 800));

    if (!object) {
        hit->guid = 0;
        return 3;
    }

    hit->guid = entity;

    if (object->Virtual0EC()) {
        C44Matrix world;
        object->GetWorldMatrix(world);
        hit->position = stop * world.AffineInverse();
    }

    return 3;
}

// ref: FUN_004f9da0
// The pick at a point of the frame: the world projection set up, the mask asked, the ray built
// and intersected; the model the cursor is over remembered (+0x2d0) for its highlight.
int32_t CGWorldFrame::Intersect(float x, float y, uint32_t unused, CURSORHIT* hit) {
    (void)unused;

    auto input = InputControlGetActive();

    if (!input || !(input->m_unk58 & 0x1) || !this->m_camera) {
        return 0;
    }

    GxXformPush(GxXform_View);
    GxXformPush(GxXform_Projection);

    this->m_camera->SetupWorldProjection(this->m_screenRect);

    int32_t type = 0;
    uint32_t mask = CursorPickMask();

    if (mask) {
        C3Vector start = {};
        C3Vector end = {};

        if (this->GetCursorRay(x, y, &start, &end)) {
            hit->start = start;
            hit->end = end;

            type = this->IntersectWorld(start, end, mask, hit);

            if (type < 2) {
                this->ReleaseModelRecords(this->m_modelRecords2);
                this->m_cursorModelObject = 0;
            } else {
                this->m_cursorModelObject = hit->guid;
            }
        }
    }

    GxXformPop(GxXform_Projection);
    GxXformPop(GxXform_View);

    return type;
}

// ref: FUN_004fa570
// The pick under the mouse, as a button goes down.
void CGWorldFrame::UpdateCursorPick() {
    if (!this->m_top) {
        return;
    }

    NDCToDDC(this->m_top->m_mousePosition.x, this->m_top->m_mousePosition.y, &this->m_cursorX, &this->m_cursorY);

    this->m_cursorHitType = this->Intersect(this->m_cursorX, this->m_cursorY, 0, &this->m_cursorHit);
}

// ref: FUN_004f7880
// A short click (1 the left button, 4 the right) on what was picked when it went down: nothing
// (deselect, click-to-move along the ray), a model (select it, or interact with it), a surface.
// A charmed player only clicks through to nothing.
int32_t CGWorldFrame::OnWorldClick(int32_t button) {
    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

    WORLDCLICK click = {};
    click.button = button;

    if (!player || player->Unit()->charmedBy != 0) {
        return GameUIClickNothing(click);
    }

    // FUN_00715c30: a name plate under the cursor (DAT_00ca1204) takes the click instead, for its
    // unit (the left button selects it, the right one interacts); name plates are PlayerName's
    // port, and none is ever under the cursor yet.
    {
        switch (this->m_cursorHitType) {
            case 0:
                click.start = this->m_cursorHit.start;
                click.end = this->m_cursorHit.end;
                return GameUIClickNothing(click);

            case 1:
            case 3: {
                WOWGUID owner = this->m_cursorHit.guid;
                uint32_t high = static_cast<uint32_t>(owner >> 32);

                // Only a transport's (high 0x1fc...) surface keeps its owner.
                if ((high & 0xF0000000) != 0x10000000 || (high & 0x0FF00000) != 0x0FC00000) {
                    owner = 0;
                }

                WORLDCLICK surface = {};
                surface.guid = owner;
                surface.position = this->m_cursorHit.position;
                surface.button = button;

                return GameUIClickSurface(surface);
            }

            case 2:
                return GameUIClickObject(this->m_cursorHit.guid, button);

            default:
                return 0;
        }
    }

}
