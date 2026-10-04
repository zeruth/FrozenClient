#ifndef UI_GAME_QUEST_TEXT_PARSER_HPP
#define UI_GAME_QUEST_TEXT_PARSER_HPP

#include <storm/Array.hpp>
#include <cstdint>

// One named value a description can substitute, 0x38 bytes in the reference.
struct SpellVariables {
    char name[16];
    char text[32];
    float value;            // +0x30
    int32_t intValue;       // +0x34, the value rounded to the nearest whole number
};

extern TSGrowableArray<SpellVariables> s_spellVariables;

int32_t RoundToInt(float value);

bool AppendPluralForm(char* dest, uint32_t destSize, int32_t count, const char** cursor);

void SkipToClosingParen(const char** cursor, const char* end);

int32_t FindBracketPairs(const char* text, const char** open1, const char** close1, const char** question, const char** open2, const char** close2);

float SpellVariableValue(const char* name);

int32_t AppendSpellVariable(char* dest, uint32_t destSize, const char** cursor, int32_t* intValue, const char* end);

class SpellRec;

// The opcodes of a compiled expression.
enum : uint8_t {
    SPELLEXPR_CONSTANT = 0,
    SPELLEXPR_SPELL = 1,
    SPELLEXPR_VARIABLE = 2,
    SPELLEXPR_POWER = 3,
    SPELLEXPR_NEGATE = 4,
    SPELLEXPR_MULTIPLY = 5,
    SPELLEXPR_DIVIDE = 6,
    SPELLEXPR_MODULO = 7,
    SPELLEXPR_ADD = 8,
    SPELLEXPR_SUBTRACT = 9,
    SPELLEXPR_FUNCTION_FIRST = 10,      // abs .. clamp, 12 of them
    SPELLEXPR_VARIABLE_FIRST = 22,      // STR .. BC3, 140 of them
};

enum {
    SPELLEXPR_COMPILING = 0,
    SPELLEXPR_FAILED = 1,
    SPELLEXPR_COMPILED = 2,
};

// The evaluation stack: 32 floats filled from the top down.
struct SPELLEXPRSTACK {
    float values[32];
    int32_t top;            // +0x80
};

// One ${...} expression, compiled to postfix (0x31c bytes, a single instance at DAT_00beb678).
struct SPELLEXPRESSION {
    int32_t status;             // +0x000
    uint32_t opCount;           // +0x004
    uint8_t ops[0x80];          // +0x008
    uint32_t constantCount;     // +0x088
    float constants[64];        // +0x08c
    uint32_t spellCount;        // +0x18c
    int32_t spells[64];         // +0x190
    uint32_t variableCount;     // +0x290
    float variables[32];        // +0x294, the $<name> values, read when compiled
    const char* tag;            // +0x314, "TomT"
    const char* text;           // +0x318

    void AddSpell(int32_t spellID);
    int32_t NextToken(char* token, int32_t* pos, int32_t* consumed);
    void Expect(const char* expected, int32_t* pos);
    int32_t ParsePrimary(int32_t pos);
    int32_t ParsePower(int32_t pos);
    int32_t ParseProduct(int32_t pos);
    int32_t ParseSum(int32_t pos);
    void EvaluateFunction(uint8_t op, SPELLEXPRSTACK* stack);
    void EvaluateOperator(uint8_t op, SPELLEXPRSTACK* stack);
    void EvaluateVariable(uint32_t op, SPELLEXPRSTACK* stack, const SpellRec* spell, int32_t level, int32_t multiplier, int32_t inspect, int32_t pet, int32_t noModifiers);
    float Evaluate(const SpellRec* spell, int32_t level, int32_t multiplier, int32_t inspect, int32_t pet, int32_t noModifiers);
};

extern SPELLEXPRESSION s_spellExpression;

uint8_t SpellExpressionFindFunction(const char* name, int32_t* argCount);

uint8_t SpellExpressionFindVariable(const char* name);

class CGUnit_C;
class NameCacheRec;

extern int32_t s_spellDescriptionNumber;

int32_t FloatToIntTruncate(float value);

void AppendGenderForm(char* dest, uint32_t destSize, int32_t sex, const CGUnit_C* unit, const NameCacheRec* entry, const char** cursor);

int32_t SpellDescriptionEvaluate(const SpellRec* spell, char* dest, uint32_t destSize, int32_t pet, int32_t inspect, int32_t level, int32_t multiplier, int32_t noModifiers, int32_t* intValue, float* value, const char** cursor, const char* end);

void SpellDescriptionDefineVariable(const SpellRec* spell, const char* name, uint32_t nameSize, int32_t pet, int32_t inspect, int32_t level, int32_t multiplier, int32_t noModifiers, const char* end, const char** cursor);

bool SpellDescriptionCheckCondition(const char** cursor);

bool SpellDescriptionEvaluateCondition(const char** cursor, const char* end);

int32_t SpellDescriptionConditional(const SpellRec* spell, char* dest, uint32_t destSize, int32_t pet, int32_t inspect, int32_t level, int32_t multiplier, int32_t noModifiers, const char** cursor, bool define);

void SpellDescriptionDefineVariables(const SpellRec* spell, int32_t pet, int32_t inspect, int32_t level, int32_t multiplier, int32_t noModifiers, const char** cursor);

int32_t SpellDescriptionExpandToken(const SpellRec* spell, char* dest, uint32_t destSize, int32_t pet, int32_t inspect, int32_t level, int32_t multiplier, int32_t noModifiers, const char** cursor);

int32_t SpellDescriptionAppendText(const SpellRec* spell, char* dest, uint32_t destSize, int32_t pet, int32_t inspect, int32_t level, int32_t multiplier, int32_t noModifiers, const char* text, const char** dollar);

void SpellParseDescription(const SpellRec* spell, char* dest, uint32_t destSize, int32_t pet, int32_t inspect, int32_t level, int32_t aura, int32_t multiplier, int32_t noModifiers);

#endif
