#ifndef GX_DRAW_HPP
#define GX_DRAW_HPP

#include "gx/CGxBatch.hpp"
#include "gx/Types.hpp"
#include <cstdint>

class C3Vector;
class CImVector;

void GxDraw(CGxBatch* batch, int32_t indexed);

void GxDrawLockedElements(EGxPrim primType, uint32_t indexCount, const uint16_t* indices);

void GxSceneClear(uint32_t, CImVector);

void GxScenePresent(uint32_t a2);

void GxSub682A00();

// The three deferred draw lists (DAT_00c5dfa4, 0x10 bytes each): objects queued to draw after the
// world, sorted by depth or by a state key so a run of them shares its render state. NOTHING in
// the 3.3.5 client ever adds to them -- only the static initialiser at 0x009cc840 touches the
// arrays besides the three functions below -- so every flush walks an empty list. They are here
// so that frame order is the reference's and a future queue has its drain.
class CGxuDrawListState {
    public:
        virtual ~CGxuDrawListState() = default;
        virtual void Begin() = 0;
        virtual void End() = 0;
};

class CGxuDrawListObject {
    public:
        virtual ~CGxuDrawListObject() = default;
        virtual float Depth(const C3Vector& cameraPos) = 0;
        virtual CGxuDrawListState* State() = 0;
        virtual uint32_t SortKey() = 0;
        virtual void Flushed() = 0;
        virtual void Draw(const C3Vector& cameraPos) = 0;
};

// Key every entry of a list for one ordering, then sort it: 0 back to front by depth, 1 by state
// key. ref: FUN_00681ba0 (keys: FUN_00681b50)
void GxuSortDrawList(EGxuDrawListCategory category, uint32_t order, const C3Vector& cameraPos);

// Draw a list in order, beginning each run of shared state once, then empty it.
// ref: FUN_00682960
void GxuFlushDrawList(EGxuDrawListCategory, const C3Vector&);

#endif
