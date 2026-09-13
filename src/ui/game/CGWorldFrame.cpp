#include "model/CM2Model.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/ClntObjMgr.hpp"
#include "world/CWorld.hpp"
#include "world/Terrain.hpp"
#include "model/CM2Scene.hpp"
#include "gx/shader/CShaderEffect.hpp"
#include "gx/Draw.hpp"
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

void CGWorldFrame::RenderWorld(void* param) {
    auto frame = reinterpret_cast<CGWorldFrame*>(param);

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

void CGWorldFrame::OnWorldRender() {
    // TODO terrain, map objects, sky, and lighting; for now the scene is cleared and the models
    // in the world drawn

    CImVector clearColor = { 0x00, 0x00, 0x00, 0xFF };
    GxSceneClear(0x3, clearColor);

    CShaderEffect::UpdateProjMatrix();

    TerrainRender();

    auto scene = CWorld::GetM2Scene();

    if (scene) {
        scene->AdvanceTime(CWorld::GetTickTimeMs());
        scene->Animate(this->m_camera->Position());
        scene->Draw(M2PASS_0);
        scene->Draw(M2PASS_1);
    }

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

    this->m_camera->SetupWorldProjection(this->m_screenRect);

    // TODO

    auto targetPos = target && !this->m_camera->HasModel()
        ? target->GetPosition()
        : this->m_camera->Position();

    CWorld::Update(this->m_camera->Position(), this->m_camera->Target(), targetPos);

    TerrainUpdate(this->m_camera->Position());

    // TODO the map entities carry this in the original; until CMap is ported every visible object
    // places its model itself
    auto objMgr = ClntObjMgrGetCurrent();

    if (objMgr) {
        for (auto object = objMgr->m_visibleObjects.Head(); object; object = objMgr->m_visibleObjects.Next(object)) {
            if (object->m_model) {
                object->m_model->SetWorldTransform(object->GetPosition(), object->GetFacing(), 1.0f);

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
