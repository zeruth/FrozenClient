#include "ui/game/Cursor.hpp"
#include "gx/Device.hpp"
#include "gx/Texture.hpp"
#include "ui/FrameScript.hpp"
#include <storm/String.hpp>
#include <cstring>

namespace {

// ref: DAT_00c25de4
int32_t s_cursorMode;

// The cursor showing (reference 0x00c26de8). 53 is the custom image CursorSetCustom loaded.
int32_t s_cursorIndex;

// What is drawn under the Item cursor: 0 nothing, 1 an item, 2 money, 3 a texture
// (reference 0x00c25de0); the image just loaded for it and its size (0x00c25ddc, 0x00c25dd8,
// 0x00c25dd4); and the 32x32 picture made from it (0x00c25de8).
int32_t s_itemCursorType;
MipBits* s_itemImage;
uint32_t s_itemImageWidth;
uint32_t s_itemImageHeight;
uint32_t s_itemCursorBits[0x400];

// The custom cursor's file (reference 0x00c25cd0).
char s_customCursorPath[STORM_MAX_PATH];

// Every cursor's 32x32 image (reference 0x00c26df0), the custom one last.
uint32_t s_cursorImages[54][0x400];

// ref: DAT_00ad2808
const char* s_cursorNames[54] = {
    nullptr,
    "Point",
    "Cast",
    "Buy",
    "Attack",
    "Interact",
    "Speak",
    "Inspect",
    "Pickup",
    "Taxi",
    "Trainer",
    "Mine",
    "Skin",
    "GatherHerbs",
    "PickLock",
    "Mail",
    "LootAll",
    "Repair",
    "RepairNPC",
    "Item",
    "SkinHorde",
    "SkinAlliance",
    "Innkeeper",
    "Quest",
    "QuestRepeatable",
    "QuestTurnIn",
    "vehichleCursor",
    "UnablePoint",
    "UnableCast",
    "UnableBuy",
    "UnableAttack",
    "UnableInteract",
    "UnableSpeak",
    "UnableInspect",
    "UnablePickup",
    "UnableTaxi",
    "UnableTrainer",
    "UnableMine",
    "UnableSkin",
    "UnableGatherHerbs",
    "UnablePickLock",
    "UnableMail",
    "UnableLootAll",
    "UnableRepair",
    "UnableRepairNPC",
    "UnableItem",
    "UnableSkinHorde",
    "UnableSkinAlliance",
    "UnableInnkeeper",
    "UnableQuest",
    "UnableQuestRepeatable",
    "UnableQuestTurnIn",
    "UnablevehichleCursor",
    nullptr,
};

// The Item cursor's own image (index 19), drawn over the item.
const int32_t CURSOR_ITEM = 19;

// The slot CursorSetCustom loads into.
const int32_t CURSOR_CUSTOM = 53;

}

// ref: FUN_006160b0
// Fills a 32x32 ARGB cursor image. A 32x32 source is copied as is; a 64x64 one is halved by
// averaging each 2x2 block, rounding to nearest, and comes out fully opaque. Any other size, or
// no image, leaves the cursor blank and fails.
int32_t CopyCursorImage(uint32_t* dest, const uint32_t* const* image, int32_t width, int32_t height) {
    if (image) {
        if (width == 32) {
            if (height == 32) {
                memcpy(dest, *image, 0x1000);
                return 1;
            }
        } else if (width == 64 && height == 64) {
            auto row0 = *image;
            auto row1 = row0;
            auto end = dest + 0x400;
            auto out = dest;

            while (out < end) {
                row1 += 64;
                auto rowEnd = out + 32;

                while (out < rowEnd) {
                    auto green = ((row0[0] >> 2) & 0x3fc0) + ((row1[0] >> 2) & 0x3fc0) + ((row0[1] >> 2) & 0x3fc0) + 0x80 + ((row1[1] >> 2) & 0x3fc0);
                    auto redBlue = (row0[0] & 0xff00ff) + (row1[0] & 0xff00ff) + (row0[1] & 0xff00ff) + 0x20002 + (row1[1] & 0xff00ff);

                    *out = (green & 0xff00) | ((redBlue >> 2) & 0xffff00ff) | 0xff000000;

                    row0 += 2;
                    row1 += 2;
                    out++;
                }

                row0 += 64;
            }

            return 1;
        }
    }

    memset(dest, 0, 0x1000);

    return 0;
}

// ref: FUN_00616260
int32_t GetCursorMode() {
    return s_cursorMode;
}

// ref: FUN_00616270
void SetCursorMode(int32_t mode) {
    s_cursorMode = mode;
}

// ref: FUN_00616220
void CursorDestroyItemImage() {
    if (s_itemImage) {
        TextureFreeMem(s_itemImage);
        s_itemImage = nullptr;
        s_itemImageWidth = 0;
        s_itemImageHeight = 0;
    }
}

// ref: FUN_00616280
int32_t CursorGetIndex(const char* name) {
    for (int32_t i = 1; i < CURSOR_CUSTOM; i++) {
        if (SStrCmpI(name, s_cursorNames[i], STORM_MAX_STR) == 0) {
            return i;
        }
    }

    return 0;
}

// ref: FUN_006162c0
// With an item picked up, the Item cursor is drawn over the item's icon wherever the cursor
// itself is clear.
void CursorUpdate() {
    auto bits = g_theGxDevicePtr->CursorLock();

    if (!s_itemCursorType) {
        memcpy(bits, s_cursorImages[s_cursorIndex], 0x1000);
    } else if (!s_itemImage) {
        for (uint32_t i = 0; i < 0x400; i++) {
            uint32_t pixel = s_cursorImages[CURSOR_ITEM][i];

            if ((pixel & 0xFF000000) == 0) {
                pixel = s_itemCursorBits[i];
            }

            bits[i] = pixel;
        }
    } else {
        memcpy(bits, s_cursorImages[CURSOR_ITEM], 0x1000);
    }

    g_theGxDevicePtr->CursorUnlock(0, 0);
}

// ref: FUN_006163b0
void CursorInitialize() {
    s_cursorIndex = 1;
    s_cursorMode = 1;
    s_itemCursorType = 0;
    s_itemImage = nullptr;
    s_itemImageWidth = 0;
    s_itemImageHeight = 0;

    for (uint32_t i = 0; i < 54; i++) {
        MipBits* image = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        PIXEL_FORMAT format = PIXEL_ARGB8888;

        if (s_cursorNames[i]) {
            char path[STORM_MAX_PATH];
            SStrPrintf(path, sizeof(path), "Interface\\Cursor\\%s.blp", s_cursorNames[i]);
            image = TextureLoadImage(path, &width, &height, &format, nullptr, nullptr, nullptr, 0);
        }

        if (!image) {
            memset(s_cursorImages[i], 0, 0x1000);
            continue;
        }

        CopyCursorImage(s_cursorImages[i], reinterpret_cast<const uint32_t* const*>(image), width, height);
        TextureFreeMippedImg(image, format, width, height);
    }

    s_customCursorPath[0] = 0;

    CursorUpdate();
}

// ref: FUN_006164b0
// Turns the image just loaded for the item cursor into its 32x32 picture, then frees it.
static void CursorApplyItemImage() {
    auto image = s_itemImage;

    if (!image) {
        return;
    }

    if (!s_itemCursorType) {
        TextureFreeMem(image);
    } else {
        CopyCursorImage(s_itemCursorBits, reinterpret_cast<const uint32_t* const*>(image), s_itemImageWidth, s_itemImageHeight);
        TextureFreeMem(image);
    }

    s_itemImageHeight = 0;
    s_itemImageWidth = 0;
    s_itemImage = nullptr;

    CursorUpdate();
}

// ref: FUN_006165b0
void CursorSetItemTexture(const char* path) {
    if (s_itemImage) {
        TextureFreeMem(s_itemImage);
        s_itemImage = nullptr;
        s_itemImageWidth = 0;
        s_itemImageHeight = 0;
    }

    if (path) {
        PIXEL_FORMAT format = PIXEL_ARGB8888;
        s_itemImage = TextureLoadImage(path, &s_itemImageWidth, &s_itemImageHeight, &format, nullptr, nullptr, nullptr, 0);
    }

    s_itemCursorType = 3;
    CursorApplyItemImage();
}

// ref: FUN_006167e0
void CursorClearItem() {
    if (s_itemCursorType) {
        s_itemCursorType = 0;
        CursorUpdate();
    }
}

// ref: FUN_00616800
void CursorSet(int32_t index) {
    if (index != CURSOR_CUSTOM && s_cursorIndex != index) {
        s_cursorIndex = index;
        CursorUpdate();
        FrameScript_SignalEvent(0x113, nullptr);
    }
}

// ref: FUN_00616830
// A new path is loaded into the custom slot (cleared when it will not load); the custom slot
// is then shown unless it already was.
bool CursorSetCustom(const char* path) {
    bool same = SStrCmpI(s_customCursorPath, path, STORM_MAX_STR) == 0;
    bool loaded = same;
    bool show = !same || s_cursorIndex != CURSOR_CUSTOM;

    if (!same) {
        uint32_t width = 0;
        uint32_t height = 0;
        PIXEL_FORMAT format = PIXEL_ARGB8888;

        auto image = TextureLoadImage(path, &width, &height, &format, nullptr, nullptr, nullptr, 0);

        if (!image) {
            memset(s_cursorImages[CURSOR_CUSTOM], 0, 0x1000);
        } else {
            loaded = CopyCursorImage(s_cursorImages[CURSOR_CUSTOM], reinterpret_cast<const uint32_t* const*>(image), width, height) != 0 || same;
            TextureFreeMem(image);
        }

        SStrCopy(s_customCursorPath, path, sizeof(s_customCursorPath));
    }

    if (show) {
        s_cursorIndex = CURSOR_CUSTOM;
        CursorUpdate();
        FrameScript_SignalEvent(0x113, nullptr);
    }

    return loaded;
}

// ref: FUN_00616920
void CursorReset() {
    if (s_cursorMode != CURSOR_CUSTOM && s_cursorIndex != s_cursorMode) {
        s_cursorIndex = s_cursorMode;
        CursorUpdate();
        FrameScript_SignalEvent(0x113, nullptr);
    }
}

// ref: FUN_00493c80
void CursorSetImage(MipBits* image) {
    if (!image) {
        return;
    }

    memcpy(g_theGxDevicePtr->CursorLock(), image->mip[0], 0x1000);
    g_theGxDevicePtr->CursorUnlock(0, 0);
    g_theGxDevicePtr->CursorSetVisible(1);
}
