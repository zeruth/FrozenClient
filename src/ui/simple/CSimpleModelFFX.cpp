#include "ui/simple/CSimpleModelFFX.hpp"
#include "gx/Draw.hpp"
#include "ui/CRenderBatch.hpp"
#include "ui/simple/CSimpleModelFFXScript.hpp"
#include <storm/Memory.hpp>

int32_t CSimpleModelFFX::s_metatable;
int32_t CSimpleModelFFX::s_objectType;

CSimpleFrame* CSimpleModelFFX::Create(CSimpleFrame* parent) {
    // TODO
    // auto m = CDataAllocator::GetData(CSimpleModelFFX::s_simpleModelFFXHeap, 0, __FILE__, __LINE__);
    auto m = SMemAlloc(sizeof(CSimpleModelFFX), __FILE__, __LINE__, 0x0);
    return new (m) CSimpleModelFFX(parent);
}

int32_t CSimpleModelFFX::GetObjectType() {
    if (!CSimpleModelFFX::s_objectType) {
        CSimpleModelFFX::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CSimpleModelFFX::s_objectType;
}

void CSimpleModelFFX::Render(void* arg) {
    CSimpleModelFFX* simpleModel = static_cast<CSimpleModelFFX*>(arg);

    CImVector clearColor = { 0x00, 0x00, 0x00, 0xFF };
    GxSceneClear(0x1 | 0x2, clearColor);

    // TODO

    GxSceneClear(0x1 | 0x2, clearColor);

    if (simpleModel->m_model) {
        CSimpleModel::RenderModel(simpleModel);
    }

    // TODO
}

void CSimpleModelFFX::CreateScriptMetaTable() {
    lua_State* L = FrameScript_GetContext();
    int32_t ref = FrameScript_Object::CreateScriptMetaTable(L, &CSimpleModelFFX::RegisterScriptMethods);
    CSimpleModelFFX::s_metatable = ref;
}

void CSimpleModelFFX::RegisterScriptMethods(lua_State* L) {
    CSimpleModel::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, SimpleModelFFXMethods, NUM_SIMPLE_MODEL_FFX_SCRIPT_METHODS);
}

CSimpleModelFFX::CSimpleModelFFX(CSimpleFrame* parent) : CSimpleModel(parent) {
    // TODO
}

bool CSimpleModelFFX::IsA(int32_t type) {
    return type == CSimpleModelFFX::s_objectType
        || type == CSimpleModel::s_objectType
        || type == CSimpleFrame::s_objectType
        || type == CScriptRegion::s_objectType
        || type == CScriptObject::s_objectType;
}

int32_t CSimpleModelFFX::GetScriptMetaTable() {
    return CSimpleModelFFX::s_metatable;
}

void CSimpleModelFFX::OnFrameRender(CRenderBatch* batch, uint32_t layer) {
    CSimpleFrame::OnFrameRender(batch, layer);

    if (layer == DRAWLAYER_ARTWORK) {
        batch->QueueCallback(CSimpleModelFFX::Render, this);
    }
}
