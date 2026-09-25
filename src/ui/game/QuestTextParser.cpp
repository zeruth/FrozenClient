#include "ui/game/QuestTextParser.hpp"
#include "ui/FrameScript.hpp"
#include <storm/String.hpp>
#include <cmath>

// Filled by the description parser before a text is expanded.
// ref: DAT_00beb66c (count), DAT_00beb670 (data)
TSGrowableArray<SpellVariables> s_spellVariables;

// Half away from zero, then the FPU's own rounding (fistp) on the result.
// ref: FUN_005773b0
int32_t RoundToInt(float value) {
    if (value > 0.0f) {
        return static_cast<int32_t>(llrint(static_cast<double>(value) + 0.5));
    }

    return static_cast<int32_t>(llrint(static_cast<double>(value) - 0.5));
}

// Reads a "form0:form1:...;" group at *cursor, appends the form FrameScript_GetPluralIndex picks
// for count to dest with its surrounding spaces trimmed, and leaves *cursor past the ';'. The
// reference passes cursor in EAX.
// ref: FUN_00576ef0
bool AppendPluralForm(char* dest, uint32_t destSize, int32_t count, const char** cursor) {
    while (**cursor != '\0' && **cursor == ' ') {
        (*cursor)++;
    }

    if (**cursor == '\0') {
        return true;
    }

    auto terminator = SStrChr(*cursor, ';');

    if (!terminator) {
        return true;
    }

    auto form = *cursor;
    *cursor = terminator + 1;

    uint32_t pluralIndex = static_cast<uint8_t>(FrameScript_GetPluralIndex(count));

    for (uint32_t i = 0; i < pluralIndex; i++) {
        while (*form != ':') {
            if (*form == ';') {
                goto found;
            }

            form++;
        }

        form++;
    }

found:
    while (*form == ' ') {
        form++;
    }

    auto end = form;

    while (*end != ':' && *end != ';') {
        end++;
    }

    int32_t length = static_cast<int32_t>(end - form);

    if (length == 0) {
        return true;
    }

    uint32_t start = SStrLen(dest);
    SStrPack(dest, form, destSize);

    if (start + length < destSize) {
        dest[start + length] = '\0';

        length--;

        if (dest[start + length] == ' ') {
            do {
                dest[start + length] = '\0';

                if (length == 0) {
                    break;
                }

                length--;
            } while (dest[start + length] == ' ');
        }
    }

    return true;
}

// Advances *cursor past the ')' that closes an already opened '(', stopping at end or at the end of
// the string. The reference passes end in EDI.
// ref: FUN_00576ff0
void SkipToClosingParen(const char** cursor, const char* end) {
    int32_t depth = 1;

    if (**cursor == '\0') {
        return;
    }

    while (*cursor < end) {
        auto p = *cursor;

        if (*p == '(') {
            depth++;
        } else if (*p == ')' && --depth == 0) {
            (*cursor)++;
            return;
        }

        *cursor = p + 1;

        if (p[1] == '\0') {
            return;
        }
    }
}

// Finds "[...]" followed by a second "[...]". A '?' straight after the first ']' records where the
// text after it starts and looks for both pairs again from there.
// ref: FUN_00577030
int32_t FindBracketPairs(const char* text, const char** open1, const char** close1, const char** question, const char** open2, const char** close2) {
    *open1 = SStrChr(text, '[');

    if (!*open1) {
        return 0;
    }

    *close1 = SStrChr(*open1, ']');

    if (!*close1) {
        return 0;
    }

    if ((*close1)[1] == '?') {
        *question = *close1 + 2;

        const char* innerOpen;
        const char* innerClose;
        const char* innerQuestion;

        return FindBracketPairs(*close1 + 2, &innerOpen, &innerClose, &innerQuestion, open2, close2);
    }

    *open2 = SStrChr(*close1, '[');

    if (!*open2) {
        return 0;
    }

    *close2 = SStrChr(*open2, ']');

    return *close2 != nullptr;
}

// The reference passes name in EBX.
// ref: FUN_005774d0
float SpellVariableValue(const char* name) {
    if (name && *name) {
        for (uint32_t i = 0; i < s_spellVariables.Count(); i++) {
            if (!SStrCmp(s_spellVariables[i].name, name, STORM_MAX_STR)) {
                return s_spellVariables[i].value;
            }
        }
    }

    return 0.0f;
}

// Looks up the variable named between *cursor + 1 and end (at most 15 characters), appends its text
// to dest, and leaves *cursor past end. The reference passes end in EAX.
// ref: FUN_00577c40
int32_t AppendSpellVariable(char* dest, uint32_t destSize, const char** cursor, int32_t* unk34, const char* end) {
    int32_t length = static_cast<int32_t>(end - (*cursor + 1)) + 1;

    if (length > 15) {
        length = 15;
    }

    char name[16];
    SStrCopy(name, *cursor + 1, static_cast<uint32_t>(length));

    for (uint32_t i = 0; i < s_spellVariables.Count(); i++) {
        if (!SStrCmp(s_spellVariables[i].name, name, STORM_MAX_STR)) {
            SStrPack(dest, s_spellVariables[i].text, destSize);
            *unk34 = s_spellVariables[i].unk34;
            *cursor = end + 1;

            return 1;
        }
    }

    return 0;
}
