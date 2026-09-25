#ifndef UI_GAME_CHAT_BUBBLE_FRAME_HPP
#define UI_GAME_CHAT_BUBBLE_FRAME_HPP

#include <cstdint>

// ref: FUN_0056c1c0
// How long a chat bubble holds its text: a base plus a step for every further word (a run of
// spaces or tabs counts once). 2750 plus 750 per word, or 1500 plus 500 with `quick`; 0 for no text.
int32_t ChatBubbleDuration(const char* text, bool quick);

#endif
