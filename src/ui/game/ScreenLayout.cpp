// ScriptEvents.cpp, 0x00615050..0x00615cd0: where on the screen the next floating thing may go. Each
// list holds the rects already taken this frame; a new rect is walked breadth-first through the four
// directions until it lands somewhere free (or the search has visited as many places as the screen
// has cells), then taken.
#include "ui/game/ScreenLayout.hpp"
#include "gx/Coordinate.hpp"
#include <storm/Array.hpp>
#include <storm/List.hpp>
#include <storm/Memory.hpp>
#include <tempest/Rect.hpp>
#include <algorithm>
#include <cmath>
#include <new>

namespace {

// The top edge a rect may reach is this far below the screen's, and the bottom this far above it
// (0x00a23bf8, 0x00a23bfc).
const float LAYOUT_TOP_MARGIN = 0.0375f;
const float LAYOUT_BOTTOM = 0.01875f;

// One place the search has reached (BFSNODE, 0x1c bytes). The direction is always -1: nothing in the
// reference ever sets it, so the test that would keep a search from doubling back never fires.
struct BFSNODE {
    TSLink<BFSNODE> m_link;     // +0x00
    CRect m_rect;               // +0x08
    int32_t m_direction = -1;   // +0x18
};

// The three lists (0x00c25cac, 0x10 bytes each).
TSGrowableArray<CRect> s_layoutLists[3];

// The search's queue and the nodes it is done with (0x00ad27b4, 0x00ad27c0).
STORM_EXPLICIT_LIST(BFSNODE, m_link) s_openNodes;
STORM_EXPLICIT_LIST(BFSNODE, m_link) s_freeNodes;

// The edges FitToScreen last measured (0x00c25ca4, 0x00c25ca0). Kept as the reference keeps them;
// nothing reads them back.
float s_layoutWidth;
float s_layoutTop;
uint32_t s_layoutMeasured;

// The four directions each start case tries, in order (0x00a23c08): 0 up, 1 left, 2 right, 3 down.
const int32_t s_layoutDirections[9][4] = {
    { 0, 2, 3, 1 },
    { 1, 0, 2, 0 },
    { 1, 3, 2, 3 },
    { 0, 1, 3, 1 },
    { 0, 2, 3, 2 },
    { 0, 1, 0, 1 },
    { 0, 2, 0, 2 },
    { 3, 1, 3, 1 },
    { 3, 2, 3, 2 },
};

bool Overlaps(const CRect& placed, const CRect& rect) {
    return placed.minX < rect.maxX && placed.maxX > rect.minX && placed.maxY < rect.minY && placed.minY > rect.maxY;
}

// ref: FUN_00615050
// Which edges of the screen `rect` crosses: 1 left, 2 right, 4 top, 8 bottom. Unless `test`, `out`
// gets the rect moved back on.
uint8_t FitToScreen(const CRect& rect, int32_t test, CRect* out) {
    float width = NDCToDDCWidth(1.0f);
    float top = NDCToDDCHeight(1.0f) - LAYOUT_TOP_MARGIN;

    if (!(s_layoutMeasured & 0x1)) {
        s_layoutMeasured |= 0x1;
        s_layoutWidth = width;
    }

    if (!(s_layoutMeasured & 0x2)) {
        s_layoutMeasured |= 0x2;
        s_layoutTop = top - LAYOUT_BOTTOM;
    }

    float rectWidth = rect.maxX - rect.minX;
    float rectHeight = rect.minY - rect.maxY;

    uint8_t flags = rect.minX < 0.0f;

    if (width < rect.maxX) {
        flags |= 0x2;
    }

    if (rect.maxY < LAYOUT_BOTTOM) {
        flags |= 0x8;
    }

    if (top < rect.minY) {
        flags |= 0x4;
    }

    if (test) {
        return flags;
    }

    CRect fitted = rect;

    if (flags & 0xc) {
        if (flags & 0x4) {
            fitted.minY = top;
            fitted.maxY = top - rectHeight;
        } else if (flags & 0x8) {
            fitted.maxY = LAYOUT_BOTTOM;
            fitted.minY = LAYOUT_BOTTOM + rectHeight;
        }
    }

    if (flags & 0x3) {
        if (flags & 0x1) {
            fitted.minX = 0.0f;
            fitted.maxX = rectWidth;
        } else if (flags & 0x2) {
            fitted.minX = width - rectWidth;
            fitted.maxX = width;
        }
    }

    *out = fitted;

    return flags;
}

// ref: FUN_006151c0
// How many places of `rect`'s size the screen holds, which bounds the search.
int32_t CellCount(const CRect& rect) {
    float width = NDCToDDCWidth(1.0f);
    float height = NDCToDDCHeight(1.0f);

    int32_t columns = static_cast<int32_t>(std::lrint(width / (rect.maxX - rect.minX))) + 1;
    int32_t rows = static_cast<int32_t>(std::lrint(height / (rect.minY - rect.maxY)));

    return columns * (rows + 1);
}

// ref: FUN_00615240
// Push `rect` back inside the screen, keeping its size.
void ClampToScreen(CRect& rect) {
    float top = rect.minY;
    float bottom = rect.maxY;
    float right = rect.maxX;
    float left = rect.minX;

    float width = NDCToDDCWidth(1.0f);
    float height = NDCToDDCHeight(1.0f);

    if (height <= rect.minY) {
        rect.minY = height;
        rect.maxY = height - (top - bottom);
    }

    if (rect.maxY < 0.0f) {
        rect.maxY = 0.0f;
        rect.minY = top - bottom;
    }

    if (rect.minX < 0.0f) {
        rect.minX = 0.0f;
        rect.maxX = right - left;
    }

    if (rect.maxX <= width) {
        return;
    }

    rect.maxX = width;
    rect.minX = width - (right - left);
}

// ref: FUN_00615380
// The first rect of the list `rect` overlaps, as the distance `rect` has to move in `direction` to
// clear it.
int32_t FindOverlap(uint32_t list, const CRect& rect, int32_t direction, float* delta) {
    auto& placed = s_layoutLists[list];

    for (uint32_t i = 0; i < placed.Count(); i++) {
        const CRect& other = placed[i];

        if (!Overlaps(other, rect)) {
            continue;
        }

        switch (direction) {
            case 0:
                *delta = other.minY - rect.maxY;
                return 1;

            case 1:
                *delta = rect.maxX - other.minX;
                return 1;

            case 2:
                *delta = other.maxX - rect.minX;
                return 1;

            case 3:
                *delta = rect.minY - other.maxY;
                return 1;
        }
    }

    return 0;
}

// ref: FUN_00615430
CRect MoveUp(uint32_t list, CRect rect) {
    float delta;

    if (FindOverlap(list, rect, 0, &delta)) {
        rect.minY += delta;
        rect.maxY += delta;
    }

    return rect;
}

// ref: FUN_00615480
CRect MoveLeft(uint32_t list, CRect rect) {
    auto& placed = s_layoutLists[list];

    for (uint32_t i = 0; i < placed.Count(); i++) {
        const CRect& other = placed[i];

        if (Overlaps(other, rect)) {
            rect.minX -= rect.maxX - other.minX;
            rect.maxX -= rect.maxX - other.minX;
            break;
        }
    }

    return rect;
}

// ref: FUN_00615520
CRect MoveRight(uint32_t list, CRect rect) {
    auto& placed = s_layoutLists[list];

    for (uint32_t i = 0; i < placed.Count(); i++) {
        const CRect& other = placed[i];

        if (Overlaps(other, rect)) {
            float delta = other.maxX - rect.minX;
            rect.minX += delta;
            rect.maxX += delta;
            break;
        }
    }

    return rect;
}

// ref: FUN_006155c0
CRect MoveDown(uint32_t list, CRect rect) {
    auto& placed = s_layoutLists[list];

    for (uint32_t i = 0; i < placed.Count(); i++) {
        const CRect& other = placed[i];

        if (Overlaps(other, rect)) {
            float delta = rect.minY - other.maxY;
            rect.minY -= delta;
            rect.maxY -= delta;
            break;
        }
    }

    return rect;
}

// The moves by direction (0x00a23c98).
CRect (*const s_layoutMoves[4])(uint32_t list, CRect rect) = {
    &MoveUp,
    &MoveLeft,
    &MoveRight,
    &MoveDown,
};

// ref: FUN_006158c0
// A node at the back of the queue, recycled when one is free.
BFSNODE* NewNode() {
    BFSNODE* node = s_freeNodes.Head();

    if (node) {
        s_freeNodes.UnlinkNode(node);
    } else {
        node = new (SMemAlloc(sizeof(BFSNODE), __FILE__, __LINE__, 0x8)) BFSNODE();
    }

    s_openNodes.LinkToTail(node);
    node->m_direction = -1;

    return node;
}

// ref: FUN_006157c0
// The rest of the queue goes back to the free nodes.
void ReleaseOpenNodes() {
    while (auto node = s_openNodes.Head()) {
        s_openNodes.UnlinkNode(node);
        s_freeNodes.LinkToTail(node);
    }
}

// ref: FUN_00615980
// The nearest place to `rect` that list `list` leaves free, or `rect` itself when the search runs
// out first.
CRect FindPlace(uint32_t list, const CRect& rect) {
    CRect out;
    uint8_t flags = FitToScreen(rect, 0, &out);

    // Which row of directions to try, from the edges the rect crossed.
    int32_t start;

    if (flags & 0x1) {
        start = (flags & 0x4) ? 8 : ((flags & 0x8) | 0x10) >> 2;
    } else if (flags & 0x4) {
        start = (flags & 0x2) ? 7 : 2;
    } else if (flags & 0x2) {
        start = (flags & 0x8) ? 5 : 3;
    } else {
        start = (flags >> 3) & 1;
    }

    auto first = NewNode();
    first->m_rect = out;

    uint32_t limit = static_cast<uint32_t>(CellCount(rect));

    for (uint32_t visited = 0; visited < limit; visited++) {
        auto node = s_openNodes.Head();

        if (!node) {
            break;
        }

        for (int32_t i = 0; i < 4; i++) {
            int32_t direction = s_layoutDirections[start][i];

            if (node->m_direction != -1 && node->m_direction == direction) {
                continue;
            }

            if (FitToScreen(node->m_rect, 1, nullptr)) {
                continue;
            }

            CRect moved = s_layoutMoves[direction](list, node->m_rect);

            if (node->m_rect.minY == moved.minY && node->m_rect.minX == moved.minX
                && node->m_rect.maxY == moved.maxY && node->m_rect.maxX == moved.maxX) {
                ReleaseOpenNodes();

                return moved;
            }

            auto next = NewNode();
            next->m_rect = moved;
        }

        s_openNodes.UnlinkNode(node);
        s_freeNodes.LinkToTail(node);
    }

    ReleaseOpenNodes();

    return rect;
}

} // namespace

// ref: FUN_00615890
void ScreenLayoutClear(uint32_t list) {
    if (list < 3) {
        s_layoutLists[list].SetCount(0);
    }
}

// ref: FUN_00615cd0
void ScreenLayoutPlace(uint32_t list, CRect& rect) {
    float height = std::fabs(rect.maxY - rect.minY);
    float halfWidth = std::fabs(rect.maxX - rect.minX) * 0.5f;
    float halfHeight = 0.5f * height;

    float width = NDCToDDCWidth(1.0f);
    float screenHeight = NDCToDDCHeight(1.0f);

    ClampToScreen(rect);

    // FUN_00615c50: the centre of the place's top edge.
    CRect place = FindPlace(list, rect);
    float x = (place.minX + place.maxX) * 0.5f;
    float y = place.minY;

    x = std::max(x, halfWidth);
    x = width - halfWidth <= x ? width - halfWidth : x;

    y = std::max(y, halfHeight);
    y = screenHeight - halfHeight <= y ? screenHeight - halfHeight : y;

    rect.minY = y;
    rect.minX = x - halfWidth;
    rect.maxY = y - height;
    rect.maxX = x + halfWidth;

    ClampToScreen(rect);

    *s_layoutLists[list].New() = rect;
}
