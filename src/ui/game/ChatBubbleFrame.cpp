#include "ui/game/ChatBubbleFrame.hpp"

// ref: FUN_0056c1c0
int32_t ChatBubbleDuration(const char* text, bool quick) {
    int32_t step = quick ? 500 : 750;

    if (!text || !*text) {
        return 0;
    }

    int32_t duration = (quick ? 1000 : 2000) + step;
    bool inSpace = true;

    for (const char* c = text; *c; c++) {
        if (*c == ' ' || *c == '\t') {
            bool wordEnded = !inSpace;
            inSpace = true;

            if (wordEnded) {
                duration += step;
            }
        } else {
            inSpace = false;
        }
    }

    return duration;
}
