#include "gx/CGxFormat.hpp"

const char* CGxFormat::formatToBitsString[] = {
    "16", // Fmt_Rgb565
    "24", // Fmt_ArgbX888
    "24", // Fmt_Argb8888
    "30", // Fmt_Argb2101010
    "16", // Fmt_Ds160
    "24", // Fmt_Ds24X
    "24", // Fmt_Ds248
    "32", // Fmt_Ds320
};

int32_t CGxFormat::formatToBitsUint[] = {
    16, // Fmt_Rgb565
    24, // Fmt_ArgbX888
    24, // Fmt_Argb8888
    30, // Fmt_Argb2101010
    16, // Fmt_Ds160
    24, // Fmt_Ds24X
    24, // Fmt_Ds248
    32, // Fmt_Ds320
};

int32_t AdapterFormatSort(const void* a, const void* b) {
    auto formatA = static_cast<const CGxFormat*>(a);
    auto formatB = static_cast<const CGxFormat*>(b);

    auto sizeSort = (formatA->size.x * formatA->size.y) - (formatB->size.x * formatB->size.y);

    if (sizeSort != 0) {
        return sizeSort;
    }

    auto colorSort = formatA->colorFormat - formatB->colorFormat;

    if (colorSort != 0) {
        return colorSort;
    }

    auto multisampleSort = formatA->multisampleCount - formatB->multisampleCount;

    return multisampleSort;
}

// ref: FUN_00681950
// The shader-target clamps (unk38..unk4c) start at -1, "no clamp": ISetCaps lowers a target to
// them only when they are not -1, and the fixedFunction path zeroes them. Without this they were
// zero, which ISetCaps reads as "clamp every shader target to none".
// ref: FUN_00681990
CGxFormat::CGxFormat(bool window, const C2iVector& size, Format colorFormat, Format depthFormat, uint32_t refreshRate, uint32_t vsync, bool hwTnL, int8_t fixLag, int8_t cursor, int8_t aspect, int32_t maximize) {
    this->hwTnL = hwTnL;
    this->cursor = cursor;
    this->fixLag = fixLag;
    this->window = window;
    this->depthFormat = depthFormat;
    this->size = size;
    this->multisampleQuality = 0.0f;
    this->colorFormat = colorFormat;
    this->refreshRate = refreshRate;
    this->vsync = vsync;
    this->aspect = aspect;
    this->stereoEnabled = 0;
    this->maximize = maximize;
    this->backBufferCount = 1;
    this->multisampleCount = 1;
    this->unk38 = -1;
    this->unk3c = -1;
    this->unk40 = -1;
    this->unk44 = -1;
    this->unk48 = -1;
    this->unk4c = -1;
    this->unk50 = 0;
    this->unk54 = 0;
    this->pos.x = 0;
    this->pos.y = 0;
}

CGxFormat::CGxFormat() {
    this->size.x = 0;
    this->size.y = 0;
    this->pos.x = 0;
    this->pos.y = 0;
    this->multisampleQuality = 0.0f;
    this->maximize = 0;
    this->stereoEnabled = 0;
    this->multisampleCount = 1;
    this->aspect = 1;
    this->unk38 = -1;
    this->unk3c = -1;
    this->unk40 = -1;
    this->unk44 = -1;
    this->unk48 = -1;
    this->unk4c = -1;
}
