#ifndef UI_GAME_QUEST_TEXT_PARSER_HPP
#define UI_GAME_QUEST_TEXT_PARSER_HPP

#include <storm/Array.hpp>
#include <cstdint>

// One named value a description can substitute, 0x38 bytes in the reference.
struct SpellVariables {
    char name[16];
    char text[32];
    float value;
    int32_t unk34;
};

extern TSGrowableArray<SpellVariables> s_spellVariables;

int32_t RoundToInt(float value);

bool AppendPluralForm(char* dest, uint32_t destSize, int32_t count, const char** cursor);

void SkipToClosingParen(const char** cursor, const char* end);

int32_t FindBracketPairs(const char* text, const char** open1, const char** close1, const char** question, const char** open2, const char** close2);

float SpellVariableValue(const char* name);

int32_t AppendSpellVariable(char* dest, uint32_t destSize, const char** cursor, int32_t* unk34, const char* end);

#endif
