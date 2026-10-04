#include "ui/game/QuestTextParser.hpp"
#include "db/Db.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/Spell_C.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/CGPetInfo.hpp"
#include "ui/game/CharacterInfoScript.hpp"
#include <storm/String.hpp>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include "ui/game/CGTooltip.hpp"
#include "ui/Types.hpp"
#include <cstring>

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
int32_t AppendSpellVariable(char* dest, uint32_t destSize, const char** cursor, int32_t* intValue, const char* end) {
    int32_t length = static_cast<int32_t>(end - (*cursor + 1)) + 1;

    if (length > 15) {
        length = 15;
    }

    char name[16];
    SStrCopy(name, *cursor + 1, static_cast<uint32_t>(length));

    for (uint32_t i = 0; i < s_spellVariables.Count(); i++) {
        if (!SStrCmp(s_spellVariables[i].name, name, STORM_MAX_STR)) {
            SStrPack(dest, s_spellVariables[i].text, destSize);
            *intValue = s_spellVariables[i].intValue;
            *cursor = end + 1;

            return 1;
        }
    }

    return 0;
}

// The ${...} expression compiler and evaluator. An expression compiles to a postfix program over
// four pools -- constants, spell references, named variables, and the values of $<name> variables
// read at compile time -- and runs against a 32-deep float stack that grows downward.

static const char* s_expressionOperators = "()[]^*/%+-#,> ";       // ref: PTR_DAT_00aceb2c

// The functions an expression may call and how many arguments each takes, in opcode order from
// SPELLEXPR_FUNCTION_FIRST.
static const struct {
    const char* name;
    int32_t argCount;
} s_expressionFunctions[] = {                                     // ref: PTR_DAT_00ace898
    { "abs", 1 },
    { "ceil", 1 },
    { "floor", 1 },
    { "min", 2 },
    { "max", 2 },
    { "gt", 2 },
    { "lt", 2 },
    { "gte", 2 },
    { "lte", 2 },
    { "eq", 2 },
    { "cond", 3 },
    { "clamp", 3 },
};

// The variables, in opcode order from SPELLEXPR_VARIABLE_FIRST. A lower-case name is the base value
// where an upper-case one is the modified one, or the low end of a range where it is the high end.
static const char* s_expressionVariables[] = {                    // ref: PTR_DAT_00ace8f8
    "STR", "AGI", "STA", "INT", "SPI", "str", "agi", "sta", "int", "spi",
    "m1", "m2", "m3", "M1", "M2", "M3", "a1", "a2", "a3", "A1", "A2", "A3",
    "d", "D", "c", "C", "p", "P", "x1", "x2", "x3", "X1", "X2", "X3",
    "t1", "t2", "t3", "T1", "T2", "T3", "h", "H", "n", "N", "b1", "b2", "b3", "B1", "B2", "B3",
    "u", "U", "v", "V", "e1", "e2", "e3", "E1", "E2", "E3", "i", "I",
    "f1", "f2", "f3", "F1", "F2", "F3", "q1", "q2", "q3", "Q1", "Q2", "Q3",
    "ap", "AP", "rap", "RAP", "mwb", "MWB", "owb", "OWB", "rwb", "RWB",
    "mw", "MW", "ow", "OW", "rw", "RW", "ar", "AR", "mws", "MWS", "ows", "OWS", "rws", "RWS",
    "pl", "PL", "hnd", "HND", "sp", "SP",
    "sph", "spfi", "spn", "spfr", "sps", "spa", "SPH", "SPFI", "SPN", "SPFR", "SPS", "SPA",
    "bh", "BH", "ph", "pfi", "pn", "pfr", "ps", "pa", "PH", "PFI", "PN", "PFR", "PS", "PA",
    "pbh", "pBH", "pbhd", "pBHD", "bc1", "bc2", "bc3", "BC1", "BC2", "BC3",
};

SPELLEXPRESSION s_spellExpression;                                // ref: DAT_00beb678

// ref: FUN_00576b10
// The function opcode for a name (any case) and its argument count, or 0. The reference passes name
// in EDI and argCount in EBX.
uint8_t SpellExpressionFindFunction(const char* name, int32_t* argCount) {
    for (uint32_t i = 0; i < 12; i++) {
        if (!SStrCmpI(name, s_expressionFunctions[i].name, STORM_MAX_STR)) {
            *argCount = s_expressionFunctions[i].argCount;

            return static_cast<uint8_t>(i + SPELLEXPR_FUNCTION_FIRST);
        }
    }

    *argCount = 0;

    return 0;
}

// ref: FUN_00576b50
// The variable opcode for a name (exact case), or 0. The reference passes name in EDI.
uint8_t SpellExpressionFindVariable(const char* name) {
    if (name && *name) {
        for (uint32_t i = 0; i < 0x8C; i++) {
            if (!SStrCmp(name, s_expressionVariables[i], STORM_MAX_STR)) {
                return static_cast<uint8_t>(i + SPELLEXPR_VARIABLE_FIRST);
            }
        }
    }

    return 0;
}

// ref: FUN_00576b90
void SPELLEXPRESSION::AddSpell(int32_t spellID) {
    this->spells[this->spellCount] = spellID;
    this->spellCount++;
}

// ref: FUN_00576bb0
// The token at *pos -- one operator character, or a run of anything else -- after any spaces.
// *consumed counts the spaces and the token, and *pos moves past both. False at the end.
int32_t SPELLEXPRESSION::NextToken(char* token, int32_t* pos, int32_t* consumed) {
    *consumed = 0;

    auto cursor = this->text + *pos;
    auto c = *cursor;

    if (c != '\0') {
        while (c == ' ') {
            (*consumed)++;
            c = cursor[1];
            cursor++;

            if (c == '\0') {
                *pos += *consumed;

                return 0;
            }
        }

        if (*cursor != '\0') {
            auto op = SStrChr(s_expressionOperators, *cursor);

            if (!op) {
                c = *cursor;

                while (c != '\0' && !SStrChr(s_expressionOperators, c)) {
                    *token = *cursor;
                    (*consumed)++;
                    c = cursor[1];
                    cursor++;
                    token++;
                }

                *token = '\0';
                *pos += *consumed;

                return 1;
            }

            token[0] = *op;
            token[1] = '\0';
            (*consumed)++;
            *pos += *consumed;

            return 1;
        }
    }

    *pos += *consumed;

    return 0;
}

// ref: FUN_00576c80
// Reads the next token and fails the expression unless it is the one expected (any case).
void SPELLEXPRESSION::Expect(const char* expected, int32_t* pos) {
    char token[32];
    int32_t consumed;

    if (this->NextToken(token, pos, &consumed) && !SStrCmpI(token, expected, STORM_MAX_STR)) {
        return;
    }

    this->status = SPELLEXPR_FAILED;
}

// ref: FUN_00577530
// A primary: a parenthesised sum, a negated primary, $<name>, $func(args), $<spell id>variable,
// a variable, or a number. Returns the position after it.
int32_t SPELLEXPRESSION::ParsePrimary(int32_t pos) {
    char token[32];
    int32_t consumed;

    if (!this->NextToken(token, &pos, &consumed)) {
        return pos;
    }

    auto first = token[0];

    if (token[1] == '\0') {
        if (first == '(') {
            pos = this->ParseSum(pos);
            this->Expect(")", &pos);

            return pos;
        }

        if (first == '-') {
            auto end = this->ParsePrimary(pos);
            this->ops[this->opCount++] = SPELLEXPR_NEGATE;

            return end;
        }
    }

    if (first == '$') {
        auto name = &token[1];

        // $<name>: a description variable, read now
        if (token[1] == '<') {
            this->variables[this->variableCount] = SpellVariableValue(&token[2]);
            this->variableCount++;
            this->ops[this->opCount++] = SPELLEXPR_VARIABLE;
            this->Expect(">", &pos);

            return pos;
        }

        int32_t argCount;
        auto function = SpellExpressionFindFunction(&token[1], &argCount);

        if (function) {
            this->Expect("(", &pos);
            pos = this->ParseSum(pos);

            for (auto remaining = argCount - 1; remaining != 0; remaining--) {
                char separator[32];

                if (!this->NextToken(separator, &pos, &consumed) || SStrCmpI(separator, ",", STORM_MAX_STR)) {
                    this->status = SPELLEXPR_FAILED;
                }

                pos = this->ParseSum(pos);
            }

            this->ops[this->opCount++] = function;
            this->Expect(")", &pos);

            return pos;
        }

        // A leading spell id reads the variable after it from that spell instead. The reference's
        // digit buffer is not cleared first, so its terminator is whatever the stack held; this
        // one is.
        int32_t digitCount = 0;

        if (isdigit(static_cast<uint8_t>(token[1])) && token[1] != '\0') {
            char digits[32] = {};
            auto c = token[1];

            do {
                if (c < '0' || c > '9') {
                    break;
                }

                digits[digitCount] = c;
                c = token[2 + digitCount];
                digitCount++;
            } while (c != '\0');

            if (digitCount != 0) {
                this->AddSpell(SStrToInt(digits));
                this->ops[this->opCount++] = SPELLEXPR_SPELL;
                name = &token[1 + digitCount];
            }
        }

        auto variable = SpellExpressionFindVariable(name);

        if (variable) {
            this->ops[this->opCount++] = variable;

            return pos;
        }
    }

    if (!isdigit(static_cast<uint8_t>(first)) && first != '.') {
        this->status = SPELLEXPR_FAILED;

        return pos;
    }

    this->constants[this->constantCount] = static_cast<float>(atof(token));
    this->constantCount++;
    this->ops[this->opCount++] = SPELLEXPR_CONSTANT;

    return pos;
}

// ref: FUN_005777a0
// Primaries joined by '^'. The operator must follow its operand directly: no spaces are skipped.
int32_t SPELLEXPRESSION::ParsePower(int32_t pos) {
    pos = this->ParsePrimary(pos);

    while (this->text[pos] == '^') {
        pos = this->ParsePrimary(pos + 1);
        this->ops[this->opCount++] = SPELLEXPR_POWER;
    }

    return pos;
}

// ref: FUN_005777f0
// Powers joined by '*', '/' and '%'.
int32_t SPELLEXPRESSION::ParseProduct(int32_t pos) {
    pos = this->ParsePower(pos);

    char op;

    while ((op = this->text[pos]) == '*' || op == '/' || op == '%') {
        pos = this->ParsePrimary(pos + 1);

        while (this->text[pos] == '^') {
            pos = this->ParsePrimary(pos + 1);
            this->ops[this->opCount++] = SPELLEXPR_POWER;
        }

        if (op == '%') {
            this->ops[this->opCount++] = SPELLEXPR_MODULO;
        } else if (op == '*') {
            this->ops[this->opCount++] = SPELLEXPR_MULTIPLY;
        } else if (op == '/') {
            this->ops[this->opCount++] = SPELLEXPR_DIVIDE;
        }
    }

    return pos;
}

// ref: FUN_005778b0
// Products joined by '+' and '-'.
int32_t SPELLEXPRESSION::ParseSum(int32_t pos) {
    pos = this->ParseProduct(pos);

    char op;

    while ((op = this->text[pos]) == '+' || op == '-') {
        pos = this->ParseProduct(pos + 1);
        this->ops[this->opCount++] = op != '+' ? SPELLEXPR_SUBTRACT : SPELLEXPR_ADD;
    }

    return pos;
}

// ref: FUN_00577900
// Pops a function's arguments, first argument deepest, and pushes its value.
void SPELLEXPRESSION::EvaluateFunction(uint8_t op, SPELLEXPRSTACK* stack) {
    float args[3];

    for (auto i = s_expressionFunctions[op - SPELLEXPR_FUNCTION_FIRST].argCount; i != 0; ) {
        i--;
        args[i] = stack->values[stack->top];
        stack->top++;
    }

    float result;

    switch (op) {
        case SPELLEXPR_FUNCTION_FIRST + 0:      // abs
            result = fabsf(args[0]);
            break;

        case SPELLEXPR_FUNCTION_FIRST + 1:      // ceil
            result = static_cast<float>(ceil(static_cast<double>(args[0])));
            break;

        case SPELLEXPR_FUNCTION_FIRST + 2:      // floor
            result = static_cast<float>(floor(static_cast<double>(args[0])));
            break;

        case SPELLEXPR_FUNCTION_FIRST + 3:      // min
            result = args[0] < args[1] ? args[0] : args[1];
            break;

        case SPELLEXPR_FUNCTION_FIRST + 4:      // max
            result = args[0] > args[1] ? args[0] : args[1];
            break;

        case SPELLEXPR_FUNCTION_FIRST + 5:      // gt
            result = args[0] > args[1] ? 1.0f : 0.0f;
            break;

        case SPELLEXPR_FUNCTION_FIRST + 6:      // lt
            result = args[0] < args[1] ? 1.0f : 0.0f;
            break;

        case SPELLEXPR_FUNCTION_FIRST + 7:      // gte
            result = args[0] >= args[1] ? 1.0f : 0.0f;
            break;

        case SPELLEXPR_FUNCTION_FIRST + 8:      // lte
            result = args[0] <= args[1] ? 1.0f : 0.0f;
            break;

        case SPELLEXPR_FUNCTION_FIRST + 9:      // eq
            result = fabsf(args[0] - args[1]) < 0.0001f ? 1.0f : 0.0f;
            break;

        case SPELLEXPR_FUNCTION_FIRST + 10:     // cond
            result = args[0] == 0.0f ? args[2] : args[1];
            break;

        case SPELLEXPR_FUNCTION_FIRST + 11: {   // clamp: the bound, or the larger of the two values below it
            auto larger = args[1] <= args[0] ? args[0] : args[1];
            result = args[2] <= larger ? args[2] : larger;
            break;
        }

        default:
            this->status = SPELLEXPR_FAILED;
            result = 0.0f;
            break;
    }

    stack->top--;
    stack->values[stack->top] = result;
}

// ref: FUN_00577ad0
// Pops the operands, the right one first, and pushes the result.
void SPELLEXPRESSION::EvaluateOperator(uint8_t op, SPELLEXPRSTACK* stack) {
    float result;

    switch (op) {
        case SPELLEXPR_POWER: {
            auto exponent = stack->values[stack->top++];
            auto base = stack->values[stack->top++];
            result = static_cast<float>(pow(static_cast<double>(base), static_cast<double>(exponent)));
            break;
        }

        case SPELLEXPR_NEGATE:
            result = -stack->values[stack->top++];
            break;

        case SPELLEXPR_MULTIPLY: {
            auto right = stack->values[stack->top++];
            auto left = stack->values[stack->top++];
            result = left * right;
            break;
        }

        case SPELLEXPR_DIVIDE: {
            auto right = stack->values[stack->top++];
            auto left = stack->values[stack->top++];
            result = left / right;
            break;
        }

        case SPELLEXPR_MODULO: {
            // As the reference computes it: left - frac(left / right) * right, which is the
            // quotient's whole part times right rather than the remainder.
            auto right = stack->values[stack->top++];
            auto left = stack->values[stack->top++];
            auto quotient = left / right;
            result = left - (quotient - static_cast<float>(floor(static_cast<double>(quotient)))) * right;
            break;
        }

        case SPELLEXPR_ADD: {
            auto right = stack->values[stack->top++];
            auto left = stack->values[stack->top++];
            result = left + right;
            break;
        }

        case SPELLEXPR_SUBTRACT: {
            auto right = stack->values[stack->top++];
            auto left = stack->values[stack->top++];
            result = left - right;
            break;
        }

        default:
            result = 0.0f;
            this->status = SPELLEXPR_FAILED;
            break;
    }

    stack->top--;
    stack->values[stack->top] = result;
}

// The player, the pet, or the player being inspected: whose power the c and p variables cost.
static CGUnit_C* SpellExpressionCaster(CGPlayer_C* player, int32_t inspect, int32_t pet, int32_t inspectLine, int32_t petLine) {
    if (inspect) {
        return static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(s_inspectGUID, TYPE_UNIT, ".\\QuestTextParser.cpp", inspectLine));
    }

    if (pet) {
        return static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(CGPetInfo::GetPet(0), TYPE_UNIT, ".\\QuestTextParser.cpp", petLine));
    }

    return player;
}

// The minimum or maximum damage of the weapon in a visible slot, from the item cache; 0 when there
// is none, it has not arrived, or its first band is not physical. Inline in the reference, once
// for each of the six variables.
static bool SpellExpressionWeaponDamage(CGPlayer_C* player, uint32_t slot, int32_t max, float* damage) {
    auto item = player->GetVisibleItem(slot);

    if (!item) {
        return false;
    }

    WOWGUID guid = 0;
    auto entry = item->entryID < 0 ? -item->entryID : item->entryID;
    auto stats = g_itemCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(entry)), &guid, nullptr, nullptr, false);

    if (!stats || stats->damageType[0] != 0) {
        return false;
    }

    *damage = max ? stats->damageMax[0] : stats->damageMin[0];

    return true;
}

// The whole minimum damage, at least 1. Inline in the reference.
static float SpellExpressionFloorDamage(float damage) {
    auto whole = static_cast<float>(floor(static_cast<double>(damage)));

    return 1.0f < whole ? whole : 1.0f;
}

// The whole maximum damage, at least 1. Inline in the reference, which rounds it up twice.
static float SpellExpressionCeilDamage(float damage) {
    auto whole = static_cast<float>(ceil(static_cast<double>(damage)));

    if (!(1.0f < whole)) {
        return 1.0f;
    }

    return static_cast<float>(ceil(static_cast<double>(damage)));
}

// ref: FUN_005782d0
// Pushes a variable's value for the spell: a player stat, a value from the spell's record with the
// spell modifiers applied, or a combat figure of the player's. 0 when there is no player.
void SPELLEXPRESSION::EvaluateVariable(uint32_t op, SPELLEXPRSTACK* stack, const SpellRec* spell, int32_t level, int32_t multiplier, int32_t inspect, int32_t pet, int32_t noModifiers) {
    float result = 0.0f;
    float missing = 0.0f;

    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, ".\\QuestTextParser.cpp", 0x38b));

    if (!player) {
        result = missing;
        goto push;
    }

    switch (op) {
        // STR..SPI: the stat; str..spi: the stat without its buffs
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
        case 0x1A:
            result = static_cast<float>(player->GetStatNonNegative(op - 0x16));
            break;

        case 0x1B:
        case 0x1C:
        case 0x1D:
        case 0x1E:
        case 0x1F: {
            auto data = player->Unit();
            auto index = op - 0x1B;
            result = static_cast<float>(static_cast<double>(data->stats[index]) - data->posStats[index] - data->negStats[index]);
            break;
        }

        // m1..m3, M1..M3: the effect's points, low and high
        case 0x20:
        case 0x21:
        case 0x22: {
            float low;
            float high;
            SpellGetEffectPoints(spell, op - 0x20, &low, &high, level, pet, inspect, noModifiers);
            result = static_cast<float>(static_cast<double>(multiplier) * low);
            break;
        }

        case 0x23:
        case 0x24:
        case 0x25: {
            float low;
            float high;
            SpellGetEffectPoints(spell, op - 0x23, &low, &high, level, pet, inspect, noModifiers);
            result = static_cast<float>(static_cast<double>(multiplier) * high);
            break;
        }

        // a1..A3: the effect's radius
        case 0x26:
        case 0x27:
        case 0x28:
        case 0x29:
        case 0x2A:
        case 0x2B: {
            float radius = 0.0f;
            auto index = op < 0x29 ? op - 0x26 : op - 0x29;

            if (index < 3) {
                auto row = g_spellRadiusDB.GetRecord(spell->m_effectRadiusIndex[index]);

                if (row) {
                    radius = row->m_radius;
                    SpellApplyModifier(spell, &radius, 6);
                }
            }

            result = radius;
            break;
        }

        // d, D: the duration in seconds
        case 0x2C:
        case 0x2D:
            result = static_cast<float>(static_cast<double>(SpellGetDuration(spell, pet, inspect, noModifiers, 1)) * 0.001f);
            break;

        // c, C: the power cost
        case 0x2E:
        case 0x2F: {
            auto caster = SpellExpressionCaster(player, inspect, pet, 0x3c8, 0x3ca);
            result = static_cast<float>(SpellGetPowerCost(spell, caster));
            break;
        }

        // p, P: the power cost a second
        case 0x30:
        case 0x31: {
            auto caster = SpellExpressionCaster(player, inspect, pet, 0x3d7, 0x3d9);
            result = static_cast<float>(SpellGetPowerCostPerTime(spell, caster));
            break;
        }

        // x1..X3: the effect's chain targets
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37: {
            int32_t targets = 0;
            auto index = op < 0x35 ? op - 0x32 : op - 0x35;

            if (index < 3) {
                targets = spell->m_effectChainTargets[index];
                SpellApplyModifier(spell, &targets, 17);
            }

            result = static_cast<float>(targets);
            break;
        }

        // t1..T3: the effect's period in milliseconds, 5000 for a spell that ticks on regeneration
        case 0x38:
        case 0x39:
        case 0x3A:
        case 0x3B:
        case 0x3C:
        case 0x3D: {
            int32_t period = 0;

            if (spell->m_procTypeMask & 0x1) {
                result = 5000.0f;
                break;
            }

            auto index = op < 0x3B ? op - 0x38 : op - 0x3B;

            if (index < 3) {
                period = spell->m_effectAuraPeriod[index];
                SpellApplyModifier(spell, &period, 19);
            }

            result = static_cast<float>(period);
            break;
        }

        // h, H: the proc chance
        case 0x3E:
        case 0x3F: {
            auto chance = spell->m_procChance;
            SpellApplyModifier(spell, &chance, 18);
            result = static_cast<float>(chance);
            break;
        }

        // n, N: the proc charges
        case 0x40:
        case 0x41: {
            auto charges = spell->m_procCharges;
            SpellApplyModifier(spell, &charges, 4);
            result = static_cast<float>(charges);
            break;
        }

        // b1..B3: the effect's points per combo point
        case 0x42:
        case 0x43:
        case 0x44:
        case 0x45:
        case 0x46:
        case 0x47: {
            auto index = op < 0x45 ? op - 0x42 : op - 0x45;

            if (index >= 3) {
                result = missing;
                break;
            }

            result = spell->m_effectPointsPerCombo[index];
            break;
        }

        // u, U: the stack size
        case 0x48:
        case 0x49:
            result = static_cast<float>(spell->m_cumulativeAura);
            break;

        // v, V: the highest target level
        case 0x4A:
        case 0x4B:
            result = static_cast<float>(spell->m_maxTargetLevel);
            break;

        // e1..E3: the effect's amplitude
        case 0x4C:
        case 0x4D:
        case 0x4E:
        case 0x4F:
        case 0x50:
        case 0x51: {
            result = 0.0f;
            auto index = op < 0x4F ? op - 0x4C : op - 0x4F;

            if (index >= 3) {
                break;
            }

            auto amplitude = spell->m_effectAmplitude[index];
            SpellApplyModifier(spell, &amplitude, 27);
            result = amplitude;
            break;
        }

        // i, I: the target cap
        case 0x52:
        case 0x53:
            result = static_cast<float>(spell->m_maxTargets);
            break;

        // f1..F3: the effect's chain amplitude
        case 0x54:
        case 0x55:
        case 0x56:
        case 0x57:
        case 0x58:
        case 0x59: {
            result = 0.0f;
            auto index = op < 0x57 ? op - 0x54 : op - 0x57;

            if (index < 3) {
                result = spell->m_effectChainAmplitude[index];
            }

            break;
        }

        // q1..Q3: the effect's misc value -- but the reference bases the index on f1 and F1, so it
        // is never below 3 and these always read 0.
        case 0x5A:
        case 0x5B:
        case 0x5C:
        case 0x5D:
        case 0x5E:
        case 0x5F: {
            result = 0.0f;
            auto index = op < 0x57 ? op - 0x54 : op - 0x57;

            if (index < 3) {
                result = static_cast<float>(spell->m_effectMiscValue[index]);
            }

            break;
        }

        // ap: the base attack power; AP: with its mods and multiplier. Neither below 0.
        case 0x60:
            result = static_cast<float>(player->Unit()->attackPower);
            result = 0.0f < result ? result : 0.0f;
            break;

        case 0x61: {
            auto data = player->Unit();
            auto mods = static_cast<int32_t>(static_cast<int16_t>(data->attackPowerMods >> 16)) + static_cast<int16_t>(data->attackPowerMods & 0xFFFF);
            result = static_cast<float>(static_cast<double>(mods + data->attackPower) * (static_cast<double>(data->attackPowerMultiplier) + 1.0));
            result = 0.0f < result ? result : 0.0f;
            break;
        }

        // rap, RAP: the same, ranged
        case 0x62:
            result = static_cast<float>(player->Unit()->rangedAttackPower);
            result = 0.0f < result ? result : 0.0f;
            break;

        case 0x63: {
            auto data = player->Unit();
            auto mods = static_cast<int32_t>(static_cast<int16_t>(data->rangedAttackPowerMods >> 16)) + static_cast<int16_t>(data->rangedAttackPowerMods & 0xFFFF);
            result = static_cast<float>(static_cast<double>(mods + data->rangedAttackPower) * (static_cast<double>(data->rangedAttackPowerMultiplier) + 1.0));
            result = 0.0f < result ? result : 0.0f;
            break;
        }

        // mwb..RWB: the equipped weapons' own damage, main hand, off hand and ranged
        case 0x64:
            if (!SpellExpressionWeaponDamage(player, 15, 0, &result)) {
                result = missing;
            }

            break;

        case 0x65:
            if (!SpellExpressionWeaponDamage(player, 15, 1, &result)) {
                result = missing;
            }

            break;

        case 0x66:
            if (!SpellExpressionWeaponDamage(player, 16, 0, &result)) {
                result = missing;
            }

            break;

        case 0x67:
            if (!SpellExpressionWeaponDamage(player, 16, 1, &result)) {
                result = missing;
            }

            break;

        case 0x68:
            if (!SpellExpressionWeaponDamage(player, 17, 0, &result)) {
                result = missing;
            }

            break;

        case 0x69:
            if (!SpellExpressionWeaponDamage(player, 17, 1, &result)) {
                result = missing;
            }

            break;

        // mw..RW: the player's damage with each, whole numbers and at least 1
        case 0x6A:
            result = SpellExpressionFloorDamage(player->Unit()->minDamage);
            break;

        case 0x6B:
            result = SpellExpressionCeilDamage(player->Unit()->maxDamage);
            break;

        case 0x6C:
            result = SpellExpressionFloorDamage(player->Unit()->minOffhandDamage);
            break;

        case 0x6D:
            result = SpellExpressionCeilDamage(player->Unit()->maxOffhandDamage);
            break;

        case 0x6E:
            result = SpellExpressionFloorDamage(player->Unit()->minRangedDamage);
            break;

        case 0x6F:
            result = SpellExpressionCeilDamage(player->Unit()->maxRangedDamage);
            break;

        // ar: the base armor; AR: the armor
        case 0x70: {
            int32_t base = 0;
            int32_t total = 0;
            int32_t effective = 0;
            int32_t positive = 0;
            int32_t negative = 0;
            player->GetResistanceBreakdown(ResistancesGetPhysicalIndex(), &base, &total, &effective, &positive, &negative);
            result = static_cast<float>(base);
            break;
        }

        case 0x71: {
            int32_t base = 0;
            int32_t total = 0;
            int32_t effective = 0;
            int32_t positive = 0;
            int32_t negative = 0;
            player->GetResistanceBreakdown(ResistancesGetPhysicalIndex(), &base, &total, &effective, &positive, &negative);
            result = static_cast<float>(effective);
            break;
        }

        // mws..RWS: the weapon speeds in seconds
        case 0x72:
        case 0x73:
            result = static_cast<float>(static_cast<double>(player->Unit()->attackRoundBaseTime[0]) * 0.001f);
            break;

        case 0x74:
        case 0x75:
            result = static_cast<float>(static_cast<double>(player->Unit()->attackRoundBaseTime[1]) * 0.001f);
            break;

        case 0x76:
        case 0x77:
            result = static_cast<float>(static_cast<double>(player->Unit()->rangedAttackTime) * 0.001f);
            break;

        // pl, PL: the player's level
        case 0x78:
        case 0x79:
            result = static_cast<float>(player->Unit()->level);
            break;

        // hnd, HND: 2 for a two-handed main hand weapon, otherwise 1
        case 0x7A:
        case 0x7B: {
            auto weapon = player->GetWeaponInfo(0, 0);
            result = IsTwoHandedWeapon(reinterpret_cast<const uint8_t*>(weapon)) ? 2.0f : 1.0f;
            break;
        }

        // sp, SP: the lowest spell damage bonus across the magic schools
        case 0x7C:
        case 0x7D: {
            auto lowest = player->GetModDamageDonePos(1) + player->GetModDamageDoneNeg(1);

            for (uint32_t school = 2; school <= 6; school++) {
                if (player->GetModDamageDonePos(school) + player->GetModDamageDoneNeg(school) <= lowest) {
                    lowest = player->GetModDamageDonePos(school) + player->GetModDamageDoneNeg(school);
                }
            }

            result = static_cast<float>(lowest);
            break;
        }

        // sph..SPA: one school's spell damage bonus
        case 0x7E:
        case 0x7F:
        case 0x80:
        case 0x81:
        case 0x82:
        case 0x83:
        case 0x84:
        case 0x85:
        case 0x86:
        case 0x87:
        case 0x88:
        case 0x89: {
            auto school = (op < 0x84 ? op - 0x7E : op - 0x84) + 1;
            auto positive = static_cast<float>(player->GetModDamageDonePos(school));
            result = static_cast<float>(static_cast<double>(player->GetModDamageDoneNeg(school)) + positive);
            break;
        }

        // bh, BH: the healing bonus
        case 0x8A:
        case 0x8B:
            result = static_cast<float>(player->Player()->modHealingDonePos);
            break;

        // ph..PA: one school's damage multiplier
        case 0x8C:
        case 0x8D:
        case 0x8E:
        case 0x8F:
        case 0x90:
        case 0x91:
        case 0x92:
        case 0x93:
        case 0x94:
        case 0x95:
        case 0x96:
        case 0x97:
            if (op < 0x92) {
                result = player->GetModDamageDonePct(op - 0x8C + 1);
            } else {
                result = player->GetModDamageDonePct(op - 0x92 + 1);
            }

            break;

        // pbh, pBH: the healing taken multiplier; pbhd, pBHD: the healing done one
        case 0x98:
        case 0x99:
            result = player->Player()->modHealingPct;
            break;

        case 0x9A:
        case 0x9B:
            result = player->Player()->modHealingDonePct;
            break;

        // bc1..BC3: the effect's bonus coefficient
        case 0x9C:
        case 0x9D:
        case 0x9E:
        case 0x9F:
        case 0xA0:
        case 0xA1: {
            auto index = op < 0x9F ? op - 0x9C : op - 0x9F;
            result = 0.0f;

            if (index > 2) {
                break;
            }

            auto coefficient = spell->m_effectBonusCoefficient[index];
            SpellApplyModifier(spell, &coefficient, 24);
            result = coefficient;
            break;
        }

        default:
            this->status = SPELLEXPR_FAILED;
            result = 0.0f;
            break;
    }

push:
    stack->top--;
    stack->values[stack->top] = result;
}

// ref: FUN_00578d10
// Runs the compiled program. A spell reference makes the next variable read from that spell (its
// difficulty variant) instead of the one described; a missing spell fails the expression. Returns
// the value on top of the stack, or 0 unless the expression is still good.
float SPELLEXPRESSION::Evaluate(const SpellRec* spell, int32_t level, int32_t multiplier, int32_t inspect, int32_t pet, int32_t noModifiers) {
    SPELLEXPRSTACK stack;
    stack.top = 32;

    const SpellRec* referenced = nullptr;
    bool useReferenced = false;

    auto constants = this->constants;
    auto variables = this->variables;
    auto spells = this->spells;

    for (uint32_t i = 0; i < this->opCount; i++) {
        auto op = this->ops[i];

        if (op == SPELLEXPR_SPELL) {
            auto id = SpellGetDifficultySpellID(*spells);
            spells++;

            referenced = g_spellDB.GetRecord(id);

            if (!referenced) {
                this->status = SPELLEXPR_FAILED;

                return 0.0f;
            }

            useReferenced = true;
            i++;
            op = this->ops[i];
        }

        switch (op) {
            case SPELLEXPR_CONSTANT:
                stack.top--;
                stack.values[stack.top] = *constants++;
                break;

            case SPELLEXPR_VARIABLE:
                stack.top--;
                stack.values[stack.top] = *variables++;
                break;

            case SPELLEXPR_POWER:
            case SPELLEXPR_NEGATE:
            case SPELLEXPR_MULTIPLY:
            case SPELLEXPR_DIVIDE:
            case SPELLEXPR_MODULO:
            case SPELLEXPR_ADD:
            case SPELLEXPR_SUBTRACT:
                this->EvaluateOperator(op, &stack);
                break;

            case SPELLEXPR_FUNCTION_FIRST + 0:
            case SPELLEXPR_FUNCTION_FIRST + 1:
            case SPELLEXPR_FUNCTION_FIRST + 2:
            case SPELLEXPR_FUNCTION_FIRST + 3:
            case SPELLEXPR_FUNCTION_FIRST + 4:
            case SPELLEXPR_FUNCTION_FIRST + 5:
            case SPELLEXPR_FUNCTION_FIRST + 6:
            case SPELLEXPR_FUNCTION_FIRST + 7:
            case SPELLEXPR_FUNCTION_FIRST + 8:
            case SPELLEXPR_FUNCTION_FIRST + 9:
            case SPELLEXPR_FUNCTION_FIRST + 10:
            case SPELLEXPR_FUNCTION_FIRST + 11:
                this->EvaluateFunction(op, &stack);
                break;

            default:
                this->EvaluateVariable(op, &stack, useReferenced ? referenced : spell, level, multiplier, inspect, pet, noModifiers);
                useReferenced = false;
                break;
        }
    }

    if (this->status != SPELLEXPR_COMPILED) {
        return 0.0f;
    }

    return stack.values[stack.top];
}

// The number the last token printed, which $l picks its plural form for.
int32_t s_spellDescriptionNumber;                                 // ref: DAT_00beb660

// ref: FUN_00577370
// A float to an integer toward zero, by rounding 2x -/+ 0.5 to nearest and halving.
int32_t FloatToIntTruncate(float value) {
    if (value >= 0.0f) {
        return static_cast<int32_t>(std::nearbyint(static_cast<double>(value + value) - 0.5)) >> 1;
    }

    return static_cast<int32_t>(std::nearbyint(static_cast<double>(value + value) + 0.5)) >> 1;
}

// ref: FUN_00576cd0
// Reads a "male:female;" group at *cursor and appends the form for sex, trimmed of spaces. A third
// field starting C or R ("male:female:C;") takes the sex from the unit's class or race name
// instead. Leaves *cursor on the ';'. The reference passes cursor in EDI.
void AppendGenderForm(char* dest, uint32_t destSize, int32_t sex, const CGUnit_C* unit, const NameCacheRec* entry, const char** cursor) {
    while (**cursor != '\0' && **cursor == ' ') {
        (*cursor)++;
    }

    if (**cursor == '\0') {
        return;
    }

    auto colon = SStrChr(*cursor, ':');

    if (!colon) {
        return;
    }

    auto female = colon + 1;
    auto selector = SStrChr(female, ':');
    auto terminator = SStrChr(colon, ';');

    if (!terminator) {
        return;
    }

    if (selector) {
        if (selector + 1 < terminator) {
            switch (selector[1]) {
                case 'C':
                case 'c':
                    sex = CGUnit_C::GetClassDisplaySex(unit, entry);
                    break;

                case 'R':
                case 'r':
                    sex = CGUnit_C::GetRaceDisplaySex(unit, entry);
                    break;
            }
        } else {
            selector = nullptr;
        }
    }

    auto end = colon;
    int32_t length;

    if (sex) {
        *cursor = female;

        while (**cursor == ' ') {
            (*cursor)++;
        }

        end = terminator;

        if (selector) {
            length = static_cast<int32_t>(selector - *cursor);
            goto append;
        }
    }

    length = static_cast<int32_t>(end - *cursor);

append:
    if (length != 0) {
        uint32_t start = SStrLen(dest);
        SStrPack(dest, *cursor, destSize);

        if (start + length < destSize) {
            dest[start + length] = '\0';

            auto last = length - 1;

            if (dest[start + last] == ' ') {
                do {
                    dest[start + last] = '\0';

                    if (last == 0) {
                        break;
                    }

                    last--;
                } while (dest[start + last] == ' ');
            }
        }
    }

    *cursor = terminator;
}

// Appends text[0, length) to dest. Inline in the reference wherever text is copied up to a token.
static void AppendPrefix(char* dest, uint32_t destSize, const char* text, int32_t length) {
    if (length != 0) {
        uint32_t start = SStrLen(dest);
        SStrPack(dest, text, destSize);

        if (start + length < destSize) {
            dest[start + length] = '\0';
        }
    }
}

// ref: FUN_00579980
// Evaluates the expression between the '{' at *cursor and end, printing it to dest with "%.0f" or,
// after "}.N", N decimals. The value and its nearest whole number are written out. The reference
// passes cursor in ESI and end in EAX.
int32_t SpellDescriptionEvaluate(const SpellRec* spell, char* dest, uint32_t destSize, int32_t pet, int32_t inspect, int32_t level, int32_t multiplier, int32_t noModifiers, int32_t* intValue, float* value, const char** cursor, const char* end) {
    char text[256];
    SStrCopy(text, *cursor + 1, sizeof(text));
    auto start = *cursor;
    *cursor = end + 1;
    text[end - start - 1] = '\0';

    char format[5];
    memcpy(format, "%.0f", sizeof(format));

    if (end[1] == '.' && static_cast<uint8_t>(end[2] - '1') <= 8) {
        SStrPrintf(format, sizeof(format), "%%.%df", end[2] - '0');
        *cursor += 2;
    }

    s_spellExpression.text = text;
    s_spellExpression.opCount = 0;
    s_spellExpression.constantCount = 0;
    s_spellExpression.spellCount = 0;
    s_spellExpression.variableCount = 0;
    s_spellExpression.tag = "TomT";
    s_spellExpression.status = SPELLEXPR_COMPILING;

    s_spellExpression.ParseSum(0);

    if (s_spellExpression.status == SPELLEXPR_FAILED) {
        return 0;
    }

    s_spellExpression.status = SPELLEXPR_COMPILED;

    auto result = s_spellExpression.Evaluate(spell, level, multiplier, inspect, pet, noModifiers);
    *value = result;

    if (s_spellExpression.status != SPELLEXPR_COMPILED) {
        return 0;
    }

    *intValue = static_cast<int32_t>(std::nearbyint(result));

    char number[32];
    SStrPrintf(number, sizeof(number), format, static_cast<double>(*value));
    SStrPack(dest, number, destSize);

    return 1;
}

// ref: FUN_00579b30
// Defines a $<name> variable as the value of the expression ending at end. The reference passes
// cursor in ESI.
void SpellDescriptionDefineVariable(const SpellRec* spell, const char* name, uint32_t nameSize, int32_t pet, int32_t inspect, int32_t level, int32_t multiplier, int32_t noModifiers, const char* end, const char** cursor) {
    auto variable = s_spellVariables.New();

    if (!variable) {
        return;
    }

    memset(variable, 0, sizeof(*variable));

    if (SpellDescriptionEvaluate(spell, variable->text, sizeof(variable->text), pet, inspect, level, multiplier, noModifiers, &variable->intValue, &variable->value, cursor, end)) {
        SStrCopy(variable->name, name, nameSize);
    }
}

// ref: FUN_005796c0
// One condition: A<id> for an aura from the spell on the player, S<id> for a spell the player
// knows. The reference passes cursor in ESI.
bool SpellDescriptionCheckCondition(const char** cursor) {
    auto kind = **cursor;
    (*cursor)++;

    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, "d:\\BuildServer\\WoW\\1\\work\\WoW-code\\branches\\wow-patch-3_3_5_A-BNet\\WoW\\Source\\Object/ObjectClient/Player_C.h", 0xa0));

    int32_t length = 0;

    if (**cursor != '\0') {
        while ((*cursor)[length] >= '0' && (*cursor)[length] <= '9') {
            length++;

            if ((*cursor)[length] == '\0') {
                break;
            }
        }
    }

    if (length == 0) {
        return false;
    }

    char digits[8];
    SStrCopy(digits, *cursor, length + 1);
    *cursor += length;
    auto id = SStrToInt(digits);

    switch (kind) {
        case 'S':
        case 's':
            if (player) {
                return player->KnowsSpell(static_cast<uint32_t>(id));
            }

            break;

        case 'A':
        case 'a':
            if (player) {
                return player->HasAuraFromSpell(id);
            }

            break;
    }

    return false;
}

// ref: FUN_005797f0
// A condition expression up to end: conditions joined by '&' and '|', negated by '!', grouped by
// parentheses, evaluated left to right with short-circuiting and no precedence.
bool SpellDescriptionEvaluateCondition(const char** cursor, const char* end) {
    bool result = false;

    while (**cursor != '\0' && *cursor < end) {
        auto p = *cursor;

        switch (*p) {
            case '!':
                p++;
                *cursor = p;

                while (*p != '\0' && p < end) {
                    if (SStrChr("sSaA", *p)) {
                        result = !SpellDescriptionCheckCondition(cursor);
                        break;
                    }

                    if (**cursor == '(') {
                        result = !SpellDescriptionEvaluateCondition(cursor, end);
                        SkipToClosingParen(cursor, end);
                        break;
                    }

                    p = *cursor + 1;
                    *cursor = p;
                }

                break;

            case '&':
                if (!result) {
                    return false;
                }

                *cursor = p + 1;
                result = SpellDescriptionEvaluateCondition(cursor, end);
                break;

            case '(':
                *cursor = p + 1;
                result = SpellDescriptionEvaluateCondition(cursor, end);
                SkipToClosingParen(cursor, end);
                break;

            case ')':
                return result;

            case 'A':
            case 'S':
            case 'a':
            case 's':
                result = SpellDescriptionCheckCondition(cursor);
                break;

            case '|':
                if (result) {
                    return true;
                }

                *cursor = p + 1;
                result = SpellDescriptionEvaluateCondition(cursor, end);
                break;

            default:
                *cursor = p + 1;
                break;
        }
    }

    return result;
}

// ref: FUN_00579ba0
// "$?cond[then][else]", with "[then]?cond2[...]" chaining further conditions. The chosen branch is
// expanded into dest -- or, when defining, its ${...} becomes the variable dest names. Leaves
// *cursor past the last branch.
int32_t SpellDescriptionConditional(const SpellRec* spell, char* dest, uint32_t destSize, int32_t pet, int32_t inspect, int32_t level, int32_t multiplier, int32_t noModifiers, const char** cursor, bool define) {
    bool failed = false;

    while (**cursor != '\0' && **cursor == ' ') {
        (*cursor)++;
    }

    if (**cursor == '\0') {
        return 1;
    }

    const char* open1;
    const char* close1;
    const char* question = nullptr;
    const char* open2;
    const char* close2;

    if (!FindBracketPairs(*cursor, &open1, &close1, &question, &open2, &close2)) {
        return 1;
    }

    const char* start;
    const char* finish;

    if (SpellDescriptionEvaluateCondition(cursor, open1)) {
        start = open1 + 1;
        finish = close1;
    } else {
        if (question) {
            auto result = SpellDescriptionConditional(spell, dest, destSize, pet, inspect, level, multiplier, noModifiers, &question, define);
            *cursor = question;

            return result;
        }

        start = open2 + 1;
        finish = close2;
    }

    auto length = static_cast<int32_t>(finish - start) + 1;

    char branch[1024];
    SStrCopy(branch, start, length);
    branch[length] = '\0';

    const char* dollar = SStrChr(branch, '$');

    if (define) {
        const char* brace;
        const char* braceEnd;

        if (dollar && (brace = SStrChr(dollar, '{')) && (braceEnd = SStrChr(brace, '}'))) {
            SpellDescriptionDefineVariable(spell, dest, destSize, pet, inspect, level, multiplier, noModifiers, braceEnd, &brace);
        }
    } else if (!SpellDescriptionAppendText(spell, dest, destSize, pet, inspect, level, multiplier, noModifiers, branch, &dollar)) {
        SStrPack(dest, "$", destSize);
        failed = true;
    }

    *cursor = close2 + 1;

    return !failed;
}

// ref: FUN_00579d60
// Reads the "$name=..." definitions of a SpellDescriptionVariables row: a "$?" conditional or a
// "${...}" expression each. The reference passes cursor in EAX.
void SpellDescriptionDefineVariables(const SpellRec* spell, int32_t pet, int32_t inspect, int32_t level, int32_t multiplier, int32_t noModifiers, const char** cursor) {
    if (!*cursor) {
        return;
    }

    while (**cursor != '\0') {
        auto name = *cursor + 1;
        *cursor = name;

        auto equals = SStrChr(name, '=');

        if (equals) {
            auto nameSize = static_cast<int32_t>(equals - name) + 1;

            if (nameSize > 15) {
                nameSize = 15;
            }

            *cursor = SStrChr(*cursor, '$');

            if (!*cursor) {
                return;
            }

            (*cursor)++;

            if (**cursor == '?') {
                (*cursor)++;
                SpellDescriptionConditional(spell, const_cast<char*>(name), nameSize, pet, inspect, level, multiplier, noModifiers, cursor, true);
            } else if (**cursor == '{') {
                auto end = SStrChr(*cursor, '}');

                if (end) {
                    SpellDescriptionDefineVariable(spell, name, nameSize, pet, inspect, level, multiplier, noModifiers, end, cursor);
                }
            }
        }

        if (*cursor) {
            *cursor = SStrChr(*cursor, '$');
        }

        if (!*cursor) {
            return;
        }
    }
}

// The caster whose power the c and p tokens cost. Inline in the reference.
static CGUnit_C* SpellDescriptionCaster(int32_t pet, int32_t inspect, int32_t inspectLine, int32_t petLine) {
    if (inspect) {
        return static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(s_inspectGUID, TYPE_UNIT, ".\\QuestTextParser.cpp", inspectLine));
    }

    if (pet) {
        return static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(CGPetInfo::GetPet(0), TYPE_UNIT, ".\\QuestTextParser.cpp", petLine));
    }

    return CGPlayer_C::GetActivePtr();
}

// Whether a whole-number end of a point range rounds to a whole number: within 0.001 of its floor
// or of its ceiling. A value just under a whole number nudges its truncated integer up. Inline in
// the reference, once for each end.
static bool SpellDescriptionPointsWhole(float scaled, int32_t* whole) {
    if (!(static_cast<double>(scaled) - floor(static_cast<double>(scaled)) < static_cast<double>(0.001f))
        && !(ceil(static_cast<double>(scaled)) - static_cast<double>(scaled) < static_cast<double>(0.001f))) {
        return false;
    }

    auto up = ceil(static_cast<double>(scaled));

    if (*whole != static_cast<int32_t>(up) && ceil(static_cast<double>(scaled)) - static_cast<double>(scaled) < static_cast<double>(0.001f)) {
        *whole += 1;
    }

    return true;
}

// ref: FUN_00579e50
// Expands the token after a '$' at *cursor into dest: ${expr}, $<variable>, $/N; or $*N; scaling,
// an optional spell id, then a letter and an optional effect index (1-3). Returns 0 when the token
// cannot be expanded. The reference passes spell in EAX.
int32_t SpellDescriptionExpandToken(const SpellRec* spell, char* dest, uint32_t destSize, int32_t pet, int32_t inspect, int32_t level, int32_t multiplier, int32_t noModifiers, const char** cursor) {
    if (**cursor == '{') {
        auto end = SStrChr(*cursor, '}');

        if (end) {
            float value;

            return SpellDescriptionEvaluate(spell, dest, destSize, pet, inspect, level, multiplier, noModifiers, &s_spellDescriptionNumber, &value, cursor, end);
        }
    }

    if (**cursor == '<') {
        auto end = SStrChr(*cursor, '>');

        if (end) {
            return AppendSpellVariable(dest, destSize, cursor, &s_spellDescriptionNumber, end);
        }
    }

    // $/N; and $*N; divide or multiply what follows
    float scale = 1.0f;

    if (**cursor == '/' || **cursor == '*') {
        auto semicolon = SStrChr(*cursor, ';');

        if (semicolon) {
            char digits[8];
            SStrCopy(digits, *cursor + 1, sizeof(digits));
            digits[semicolon - *cursor - 1] = '\0';

            auto factor = SStrToInt(digits);

            if (factor != 0) {
                scale = static_cast<float>(factor);

                if (**cursor == '/') {
                    scale = 1.0f / scale;
                }
            }

            *cursor = semicolon + 1;
        }
    }

    // A spell id reads the token from that spell instead
    int32_t length = 0;

    if (**cursor != '\0') {
        while ((*cursor)[length] >= '0' && (*cursor)[length] <= '9') {
            length++;

            if ((*cursor)[length] == '\0') {
                break;
            }
        }
    }

    if (length != 0) {
        char digits[8];
        SStrCopy(digits, *cursor, length + 1);
        *cursor += length;

        auto other = g_spellDB.GetRecord(SpellGetDifficultySpellID(SStrToInt(digits)));

        if (!other) {
            return 0;
        }

        spell = other;

        if (spell->m_attributesEx2 & 0x1000000) {
            auto player = CGPlayer_C::GetActivePtr();

            if (player) {
                level = player->Unit()->level;
            }
        }

        if (spell->m_maxLevel > 0 && level >= spell->m_maxLevel) {
            level = spell->m_maxLevel;
        }
    }

    auto token = *cursor;
    auto next = token + 1;

    uint32_t index = 0;
    int32_t hasIndex = 0;

    if (static_cast<uint8_t>(token[1] - '1') <= 8) {
        index = token[1] - '1';
        hasIndex = 1;

        if (index > 2) {
            index = 0;
        }
    }

    char number[32];
    char text[64];
    const char* output = number;

    switch (*token) {
        case '?':
            *cursor = next;

            return SpellDescriptionConditional(spell, dest, destSize, pet, inspect, level, multiplier, noModifiers, cursor, false);

        // $g: a male:female; group
        case 'G':
        case 'g': {
            *cursor = next;

            auto player = CGPlayer_C::GetActivePtr();

            if (!player) {
                return 0;
            }

            int32_t sex = (player->Unit()->bytes0 >> 16) & 0xFF;

            if (sex == UNITSEX_NONE) {
                sex = player->Player()->bytes_3_1;
            }

            AppendGenderForm(dest, destSize, sex, player, nullptr, cursor);

            if (**cursor != '\0') {
                (*cursor)++;
            }

            return 1;
        }

        // $l: a plural group for the last number
        case 'L':
        case 'l':
            *cursor = next;

            return AppendPluralForm(dest, destSize, s_spellDescriptionNumber, cursor);

        // $a: the radius
        case 'A':
        case 'a': {
            int32_t radius = 0;
            auto row = g_spellRadiusDB.GetRecord(spell->m_effectRadiusIndex[index]);

            if (row) {
                radius = static_cast<int32_t>(row->m_radius);
                SpellApplyModifier(spell, &radius, 6);
            }

            SStrPrintf(number, sizeof(number), "%d", radius);
            break;
        }

        // $b: the points per combo point
        case 'B':
        case 'b':
            s_spellDescriptionNumber = static_cast<int32_t>(spell->m_effectPointsPerCombo[index]);
            SStrPrintf(number, sizeof(number), "%d", static_cast<int32_t>(spell->m_effectPointsPerCombo[index]));
            break;

        // $c: the power cost
        case 'C':
        case 'c':
            SStrPrintf(number, sizeof(number), "%d", SpellGetPowerCost(spell, SpellDescriptionCaster(pet, inspect, 0x9e9, 0x9eb)));
            break;

        // $d: the duration
        case 'D':
        case 'd': {
            auto duration = SpellGetDuration(spell, pet, inspect, noModifiers, 1);
            output = text;

            if (duration > 0) {
                auto milliseconds = static_cast<float>(duration);

                if (TimeIsWholeUnit(milliseconds, 0.01f)) {
                    FormatTimeInterval(text, sizeof(text), static_cast<uint64_t>(static_cast<int64_t>(duration)), "INT_SPELL_DURATION", 0, 0, false);
                } else {
                    FormatTimeIntervalFloat(text, sizeof(text), milliseconds, "SPELL_DURATION", 0);
                }
            } else {
                SStrCopy(text, FrameScript_GetText("SPELL_DURATION_UNTIL_CANCELLED", -1, GENDER_NOT_APPLICABLE), sizeof(text));
            }

            break;
        }

        // $e: the amplitude
        case 'E':
        case 'e': {
            auto amplitude = spell->m_effectAmplitude[index];
            SpellApplyModifier(spell, &amplitude, 27);
            SStrPrintf(number, sizeof(number), "%.1f", static_cast<double>(amplitude) * scale);
            break;
        }

        // $f: the chain amplitude; $F: rounded
        case 'f':
            SStrPrintf(number, sizeof(number), "%.1f", static_cast<double>(spell->m_effectChainAmplitude[index]) * scale);
            break;

        case 'F':
            SStrPrintf(number, sizeof(number), "%d", RoundToInt(spell->m_effectChainAmplitude[index] * scale));
            break;

        // $h: the proc chance; $n: the charges; $x: the chain targets
        case 'H':
        case 'h': {
            auto value = spell->m_procChance;
            SpellApplyModifier(spell, &value, 18);
            s_spellDescriptionNumber = value;
            SStrPrintf(number, sizeof(number), "%d", value);
            break;
        }

        case 'N':
        case 'n': {
            auto value = spell->m_procCharges;
            SpellApplyModifier(spell, &value, 4);
            s_spellDescriptionNumber = value;
            SStrPrintf(number, sizeof(number), "%d", value);
            break;
        }

        case 'X':
        case 'x': {
            auto value = spell->m_effectChainTargets[index];
            SpellApplyModifier(spell, &value, 17);
            s_spellDescriptionNumber = value;
            SStrPrintf(number, sizeof(number), "%d", value);
            break;
        }

        // $i: the target cap
        case 'I':
        case 'i':
            s_spellDescriptionNumber = spell->m_maxTargets;
            SStrPrintf(number, sizeof(number), "%d", spell->m_maxTargets);
            break;

        // $m/$M: the low or high end of the points; $s: the range; $o: the total over the duration
        case 'M':
        case 'O':
        case 'S':
        case 'm':
        case 'o':
        case 's': {
            float low;
            float high;
            SpellGetEffectPoints(spell, index, &low, &high, level, pet, inspect, noModifiers);

            // The products stay in extended precision on the reference's FPU; only the stored
            // copies are rounded
            auto lowProduct = static_cast<double>(multiplier) * low;
            auto highProduct = static_cast<double>(multiplier) * high;
            low = static_cast<float>(lowProduct);
            high = static_cast<float>(highProduct);

            if (**cursor == 'o' || **cursor == 'O') {
                auto period = spell->m_effectAuraPeriod[index];
                bool ticks = false;

                if (period == 0) {
                    period = 5000;
                    ticks = true;
                } else if (period > 0) {
                    ticks = true;
                }

                if (ticks) {
                    auto duration = SpellGetDuration(spell, pet, inspect, noModifiers, 0);

                    if (duration > 0) {
                        auto total = ((spell->m_attributesEx5 & 0x200) ? period : 0) + duration;
                        lowProduct = static_cast<double>(low) * total / period;
                        low = static_cast<float>(lowProduct);
                        highProduct = static_cast<double>(high) * total / period;
                        high = static_cast<float>(highProduct);
                        ticks = false;
                    } else {
                        ticks = true;
                    }

                    if (ticks) {
                        lowProduct = 0.0;
                        highProduct = 0.0;
                        low = 0.0f;
                        high = 0.0f;
                    }
                } else {
                    lowProduct = 0.0;
                    highProduct = 0.0;
                    low = 0.0f;
                    high = 0.0f;
                }
            }

            auto lowScaled = static_cast<float>(fabs(lowProduct) * scale);
            auto highScaled = static_cast<float>(fabs(highProduct) * scale);

            auto lowWhole = FloatToIntTruncate(lowScaled);
            auto highWhole = FloatToIntTruncate(highScaled);

            auto lowIsWhole = SpellDescriptionPointsWhole(lowScaled, &lowWhole);
            auto highIsWhole = SpellDescriptionPointsWhole(highScaled, &highWhole);

            auto letter = **cursor;
            auto isS = letter == 'S';

            if (letter == 'm') {
                s_spellDescriptionNumber = lowIsWhole ? lowWhole : 2;
            } else {
                s_spellDescriptionNumber = highIsWhole ? highWhole : 2;
            }

            if (letter == 'm') {
                if (lowIsWhole) {
                    SStrPrintf(number, sizeof(number), "%d", lowWhole);
                } else {
                    SStrPrintf(number, sizeof(number), "%.1f", static_cast<double>(lowScaled));
                }
            } else if (letter == 'M') {
                if (highIsWhole) {
                    SStrPrintf(number, sizeof(number), "%d", highWhole);
                } else {
                    SStrPrintf(number, sizeof(number), "%.1f", static_cast<double>(highScaled));
                }
            } else if (high == low) {
                if (lowIsWhole || !isS) {
                    SStrPrintf(number, sizeof(number), "%d", lowWhole);
                } else {
                    SStrPrintf(number, sizeof(number), "%.1f", static_cast<double>(lowScaled));
                }
            } else if (lowIsWhole && highIsWhole) {
                SStrPrintf(number, sizeof(number), FrameScript_GetText("INT_SPELL_POINTS_SPREAD_TEMPLATE", -1, GENDER_NOT_APPLICABLE), lowWhole, highWhole);
            } else if (isS) {
                SStrPrintf(number, sizeof(number), FrameScript_GetText("SPELL_POINTS_SPREAD_TEMPLATE", -1, GENDER_NOT_APPLICABLE), static_cast<double>(lowScaled), static_cast<double>(highScaled));
            } else {
                auto highValue = highIsWhole ? highWhole : static_cast<int32_t>(highScaled) + 1;
                SStrPrintf(number, sizeof(number), FrameScript_GetText("INT_SPELL_POINTS_SPREAD_TEMPLATE", -1, GENDER_NOT_APPLICABLE), static_cast<int32_t>(lowScaled), highValue);
            }

            break;
        }

        // $p: the power cost a second
        case 'P':
        case 'p':
            SStrPrintf(number, sizeof(number), "%d", SpellGetPowerCostPerTime(spell, SpellDescriptionCaster(pet, inspect, 0x9fb, 0x9fd)));
            break;

        // $q: the misc value
        case 'Q':
        case 'q':
            s_spellDescriptionNumber = spell->m_effectMiscValue[index];
            SStrPrintf(number, sizeof(number), "%d", spell->m_effectMiscValue[index]);
            break;

        // $r: the range in yards
        case 'R':
        case 'r': {
            float range = 0.0f;
            auto rangeIndex = spell->m_rangeIndex > 1 ? spell->m_rangeIndex : 1;
            auto row = g_spellRangeDB.GetRecord(rangeIndex);

            if (row) {
                range = row->m_rangeMax[0];
                SpellApplyModifier(spell, &range, 5);
            }

            s_spellDescriptionNumber = static_cast<int32_t>(ceil(static_cast<double>(range)));
            SStrPrintf(number, sizeof(number), "%d", static_cast<int32_t>(range));
            break;
        }

        // $t: the period in seconds
        case 'T':
        case 't': {
            int32_t period;

            if (spell->m_procTypeMask & 0x1) {
                period = 5000;
            } else {
                period = spell->m_effectAuraPeriod[index];
                SpellApplyModifier(spell, &period, 19);

                for (uint32_t i = 0; i < 3; i++) {
                    if (spell->m_effectAuraPeriod[i] == 0) {
                        continue;
                    }

                    if ((spell->m_attributesEx5 & 0x2000) && !(spell->m_attributesEx3 & 0x20000000)) {
                        auto player = CGPlayer_C::GetActivePtr();

                        if (player && player->Unit()->modCastingSpeed >= 0.001f) {
                            period = static_cast<int32_t>(std::nearbyint(static_cast<float>(period) * player->Unit()->modCastingSpeed));
                        }
                    }

                    break;
                }
            }

            if (TimeIsWholeUnit(static_cast<float>(period), 0.01f)) {
                SStrPrintf(number, sizeof(number), "%d", period / 1000);
            } else {
                SStrPrintf(number, sizeof(number), "%.2f", static_cast<double>(period) * 0.001f);
            }

            break;
        }

        // $u: the stack size; $v: the highest target level
        case 'U':
        case 'u':
            s_spellDescriptionNumber = spell->m_cumulativeAura;
            SStrPrintf(number, sizeof(number), "%d", spell->m_cumulativeAura);
            break;

        case 'V':
        case 'v':
            s_spellDescriptionNumber = spell->m_maxTargetLevel;
            SStrPrintf(number, sizeof(number), "%d", spell->m_maxTargetLevel);
            break;

        // $z: the inn the player is bound to
        case 'Z':
        case 'z': {
            auto area = g_areaTableDB.GetRecord(PlayerGetBindAreaID());
            output = area ? area->m_areaName : FrameScript_GetText("HOME_INN", -1, GENDER_NOT_APPLICABLE);
            break;
        }

        default:
            return 0;
    }

    SStrPack(dest, output, destSize);

    if (**cursor != '\0') {
        (*cursor)++;

        if (hasIndex) {
            (*cursor)++;
        }
    }

    return 1;
}

// ref: FUN_0057aaf0
// Copies text to dest with each '$' token expanded; a token that cannot be expanded shows as "$".
// *dollar is the first '$' in text.
int32_t SpellDescriptionAppendText(const SpellRec* spell, char* dest, uint32_t destSize, int32_t pet, int32_t inspect, int32_t level, int32_t multiplier, int32_t noModifiers, const char* text, const char** dollar) {
    bool failed = false;

    while (*dollar && **dollar != '\0') {
        AppendPrefix(dest, destSize, text, static_cast<int32_t>(*dollar - text));

        (*dollar)++;

        if (!SpellDescriptionExpandToken(spell, dest, destSize, pet, inspect, level, multiplier, noModifiers, dollar)) {
            SStrPack(dest, "$", destSize);
            failed = true;
        }

        text = *dollar;
        *dollar = SStrChr(text, '$');
    }

    SStrPack(dest, text, destSize);

    return !failed;
}

// ref: FUN_0057abc0
// A spell's description (or, with aura set and one present, its aura description) with every token
// expanded, at the given level -- 0 for the caster's. The row of SpellDescriptionVariables the spell
// names is read first, for its $<name> variables.
void SpellParseDescription(const SpellRec* spell, char* dest, uint32_t destSize, int32_t pet, int32_t inspect, int32_t level, int32_t aura, int32_t multiplier, int32_t noModifiers) {
    *dest = '\0';

    if (level == 0) {
        level = SpellGetCasterLevel(spell, pet, inspect);
    }

    auto variables = g_spellDescriptionVariablesDB.GetRecord(spell->m_descriptionVariablesID);

    if (variables) {
        const char* cursor = SStrChr(variables->m_variables, '$');
        SpellDescriptionDefineVariables(spell, pet, inspect, level, multiplier, noModifiers, &cursor);
    }

    const char* text = spell->m_auraDescription;

    if (!aura || *text == '\0') {
        text = spell->m_description;
    }

    const char* cursor = SStrChr(text, '$');
    SpellDescriptionAppendText(spell, dest, destSize, pet, inspect, level, multiplier, noModifiers, text, &cursor);

    s_spellVariables.SetCount(0);
}
