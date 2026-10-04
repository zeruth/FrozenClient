#include "ui/game/QuestTextParser.hpp"
#include "catch.hpp"
#include <cstring>

namespace {

// Compiles and runs an expression that reads no game state, as SpellDescriptionEvaluate does.
float Run(const char* text, int32_t* status) {
    s_spellExpression.text = text;
    s_spellExpression.opCount = 0;
    s_spellExpression.constantCount = 0;
    s_spellExpression.spellCount = 0;
    s_spellExpression.variableCount = 0;
    s_spellExpression.status = SPELLEXPR_COMPILING;

    s_spellExpression.ParseSum(0);

    if (s_spellExpression.status == SPELLEXPR_FAILED) {
        *status = SPELLEXPR_FAILED;

        return 0.0f;
    }

    s_spellExpression.status = SPELLEXPR_COMPILED;

    auto value = s_spellExpression.Evaluate(nullptr, 0, 1, 0, 0, 0);
    *status = s_spellExpression.status;

    return value;
}

}

TEST_CASE("Spell description expressions", "[ui][spellexpr]") {
    int32_t status;

    SECTION("precedence and grouping") {
        CHECK(Run("(3+4)*2", &status) == 14.0f);
        CHECK(status == SPELLEXPR_COMPILED);
        CHECK(Run("3+4*2", &status) == 11.0f);
        CHECK(Run("2^3*2", &status) == 16.0f);
        CHECK(Run("-4+1", &status) == -3.0f);
        CHECK(Run("10-2-3", &status) == 5.0f);
    }

    SECTION("an operator must follow its operand directly") {
        // The reference skips no spaces before an operator, so the expression ends at "2"
        CHECK(Run("2 *3", &status) == 2.0f);
    }

    SECTION("modulo as the reference computes it") {
        // left - frac(left / right) * right
        CHECK(Run("10%4", &status) == 8.0f);
    }

    SECTION("functions") {
        CHECK(Run("$max(2,5)", &status) == 5.0f);
        CHECK(Run("$min(2,5)", &status) == 2.0f);
        CHECK(Run("$floor(2.7)", &status) == 2.0f);
        CHECK(Run("$ceil(2.2)", &status) == 3.0f);
        CHECK(Run("$abs(-3)", &status) == 3.0f);
        CHECK(Run("$cond(0,1,2)", &status) == 2.0f);
        CHECK(Run("$cond(1,1,2)", &status) == 1.0f);
        CHECK(Run("$gt(3,2)", &status) == 1.0f);
        CHECK(Run("$lte(3,2)", &status) == 0.0f);
        CHECK(Run("$eq(1,1.00001)", &status) == 1.0f);
        CHECK(Run("$clamp(5,7,6)", &status) == 6.0f);
        CHECK(Run("$MAX(1,4)", &status) == 4.0f);
    }

    SECTION("an unknown name fails") {
        Run("$nosuch(1)", &status);
        CHECK(status == SPELLEXPR_FAILED);
    }

    SECTION("a $<name> variable is read when compiled") {
        s_spellVariables.SetCount(0);
        auto variable = s_spellVariables.New();
        memset(variable, 0, sizeof(*variable));
        strcpy(variable->name, "mult");
        variable->value = 3.0f;

        CHECK(Run("$<mult>*2", &status) == 6.0f);
        CHECK(status == SPELLEXPR_COMPILED);

        s_spellVariables.SetCount(0);
    }
}
