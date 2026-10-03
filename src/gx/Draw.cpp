#include "gx/Buffer.hpp"
#include "gx/Draw.hpp"
#include "gx/Device.hpp"
#include <bc/Debug.hpp>
#include <cstdlib>
#include <tempest/Vector.hpp>
#include <storm/Array.hpp>

// ref: FUN_00482a40
void GxDraw(CGxBatch* batch, int32_t indexed) {
    g_theGxDevicePtr->Draw(batch, indexed);
}

// ref: FUN_00682340
void GxDrawLockedElements(EGxPrim primType, uint32_t indexCount, const uint16_t* indices) {
    if (Buffer::s_lockVertexCount == 0) {
        return;
    }

    GxPrimIndexPtr(indexCount, indices);

    CGxBatch batch;
    batch.m_primType = primType;
    batch.m_minIndex = 0;
    batch.m_maxIndex = Buffer::s_lockVertexCount - 1;
    batch.m_start = 0;
    batch.m_count = indexCount;

    g_theGxDevicePtr->Draw(&batch, 1);
}

void GxSceneClear(uint32_t mask, CImVector color) {
    g_theGxDevicePtr->SceneClear(mask, color);
}

void GxScenePresent(uint32_t a2) {
    g_theGxDevicePtr->ScenePresent();
}

void GxSub682A00() {
    C3Vector v2 = { 0.0f, 0.0f, 0.0f };
    GxuFlushDrawList(GxuCat_2, v2);

    GxScenePresent(0);
}

namespace {

struct GxuDrawListEntry {
    CGxuDrawListObject* object;     // +0x00
    float depth;                    // +0x04
    uint32_t key;                   // +0x08
};

TSGrowableArray<GxuDrawListEntry> s_drawLists[3];   // DAT_00c5dfa4

// Set while a list's entries are told they were flushed (DAT_00c5df8c).
int32_t s_drawListFlushing;

// ref: FUN_00681310
void KeyByDepth(GxuDrawListEntry* entry, const C3Vector& cameraPos) {
    entry->depth = entry->object->Depth(cameraPos);
}

// ref: FUN_00681370
void KeyByState(GxuDrawListEntry* entry, const C3Vector& cameraPos) {
    entry->key = entry->object->SortKey();
}

// ref: FUN_00681330
int CompareDepth(const void* a, const void* b) {
    auto entryA = static_cast<const GxuDrawListEntry*>(a);
    auto entryB = static_cast<const GxuDrawListEntry*>(b);

    if (entryB->depth < entryA->depth) {
        return -1;
    }

    return entryA->depth < entryB->depth ? 1 : 0;
}

// ref: FUN_00681390
int CompareState(const void* a, const void* b) {
    auto entryA = static_cast<const GxuDrawListEntry*>(a);
    auto entryB = static_cast<const GxuDrawListEntry*>(b);

    if (entryA->key < entryB->key) {
        return -1;
    }

    return entryB->key < entryA->key ? 1 : 0;
}

// The two orderings' key and compare functions (DAT_00ad88a8, DAT_00ad88b0).
void (* const s_keyFunctions[2])(GxuDrawListEntry*, const C3Vector&) = { &KeyByDepth, &KeyByState };
int (* const s_compareFunctions[2])(const void*, const void*) = { &CompareDepth, &CompareState };

// ref: FUN_00681b50
void KeyDrawList(EGxuDrawListCategory category, uint32_t order, const C3Vector& cameraPos) {
    auto& list = s_drawLists[category];

    for (uint32_t i = 0; i < list.Count(); i++) {
        s_keyFunctions[order](&list[i], cameraPos);
    }
}

// ref: FUN_00682900
void ClearDrawList(EGxuDrawListCategory category) {
    auto& list = s_drawLists[category];

    s_drawListFlushing = 1;

    for (uint32_t i = 0; i < list.Count(); i++) {
        list[i].object->Flushed();
    }

    list.SetCount(0);

    s_drawListFlushing = 0;
}

}

// ref: FUN_00681ba0
void GxuSortDrawList(EGxuDrawListCategory category, uint32_t order, const C3Vector& cameraPos) {
    KeyDrawList(category, order, cameraPos);

    auto& list = s_drawLists[category];

    if (list.Count()) {
        qsort(list.Ptr(), list.Count(), sizeof(GxuDrawListEntry), s_compareFunctions[order]);
    }
}

// ref: FUN_00682960
void GxuFlushDrawList(EGxuDrawListCategory category, const C3Vector& cameraPos) {
    auto& list = s_drawLists[category];
    CGxuDrawListState* current = nullptr;

    for (uint32_t i = 0; i < list.Count(); i++) {
        auto object = list[i].object;
        auto state = object->State();

        if (state && state != current) {
            if (current) {
                current->End();
            }

            state->Begin();
            current = state;
        }

        object->Draw(cameraPos);
    }

    if (current) {
        current->End();
    }

    ClearDrawList(category);
}
