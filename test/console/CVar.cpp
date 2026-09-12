#include "console/Command.hpp"
#include "console/CVar.hpp"
#include "catch.hpp"
#include <cstdio>
#include <cstring>

namespace {

void WriteFile(const char* path, const char* contents, size_t size) {
    FILE* file = fopen(path, "wb");
    REQUIRE(file != nullptr);
    fwrite(contents, 1, size, file);
    fclose(file);
}

void EnsureConsole() {
    static bool initialized = false;

    if (!initialized) {
        ConsoleCommandInitialize();
        CVar::Initialize();
        initialized = true;
    }
}

} // namespace

TEST_CASE("CVar::Load", "[console]") {
    EnsureConsole();

    const char* path = "cvar-test-config.wtf";

    SECTION("returns 0 for a missing file") {
        remove(path);
        CHECK(CVar::Load(path) == 0);
    }

    SECTION("returns 1 for an empty file") {
        WriteFile(path, "", 0);
        CHECK(CVar::Load(path) == 1);
    }

    SECTION("registers unknown cvars and sets known ones") {
        CVar* known = CVar::Register("cvarTestKnown", "", 0x0, "before", nullptr, DEFAULT, false, nullptr, false);

        const char contents[] =
            "SET cvarTestKnown \"after\"\r\n"
            "SET cvarTestNew \"1280x720\"\r\n"
            "SET cvarTestNumber \"42\"\r\n"
            "not a set line\r\n";

        WriteFile(path, contents, sizeof(contents) - 1);

        REQUIRE(CVar::Load(path) == 1);

        CHECK(strcmp(known->GetString(), "after") == 0);

        CVar* created = CVar::Lookup("cvarTestNew");
        REQUIRE(created != nullptr);
        CHECK(strcmp(created->GetString(), "1280x720") == 0);

        CVar* number = CVar::Lookup("cvarTestNumber");
        REQUIRE(number != nullptr);
        CHECK(number->GetInt() == 42);
    }

    SECTION("skips a UTF-8 byte order mark") {
        const char contents[] = "\xEF\xBB\xBFSET cvarTestBom \"yes\"\n";
        WriteFile(path, contents, sizeof(contents) - 1);

        REQUIRE(CVar::Load(path) == 1);

        CVar* var = CVar::Lookup("cvarTestBom");
        REQUIRE(var != nullptr);
        CHECK(strcmp(var->GetString(), "yes") == 0);
    }

    SECTION("is case-insensitive on the SET keyword") {
        const char contents[] = "set cvarTestLower \"ok\"\n";
        WriteFile(path, contents, sizeof(contents) - 1);

        REQUIRE(CVar::Load(path) == 1);

        CVar* var = CVar::Lookup("cvarTestLower");
        REQUIRE(var != nullptr);
        CHECK(strcmp(var->GetString(), "ok") == 0);
    }

    remove(path);
}

TEST_CASE("ConsoleCommandExecute", "[console]") {
    EnsureConsole();

    SECTION("runs a registered command with trimmed arguments") {
        ConsoleCommandExecute("   set   cvarTestExec   \"spaced\"   ", 0);

        CVar* var = CVar::Lookup("cvarTestExec");
        REQUIRE(var != nullptr);
        CHECK(strcmp(var->GetString(), "spaced") == 0);
    }

    SECTION("ignores unknown commands") {
        ConsoleCommandExecute("definitelyNotACommand 1 2 3", 0);
        CHECK(CVar::Lookup("definitelyNotACommand") == nullptr);
    }

    SECTION("records history without repeating the last entry") {
        ConsoleCommandExecute("set cvarTestHistory \"1\"", 1);
        ConsoleCommandExecute("set cvarTestHistory \"1\"", 1);
        ConsoleCommandExecute("set cvarTestHistory \"2\"", 1);

        CHECK(strcmp(ConsoleCommandHistory(0), "set cvarTestHistory \"2\"") == 0);
        CHECK(strcmp(ConsoleCommandHistory(1), "set cvarTestHistory \"1\"") == 0);
        CHECK(strcmp(ConsoleCommandHistory(2), "set cvarTestHistory \"1\"") != 0);
    }
}

TEST_CASE("CVar::Save", "[console]") {
    EnsureConsole();

    CVar::s_filename = "cvar-test-save.wtf";
    remove("WTF/cvar-test-save.wtf");

    SECTION("does nothing when no saved cvar changed") {
        CVar::m_needsSave = false;
        CHECK(CVar::Save() == 1);
        CHECK(!fopen("WTF/cvar-test-save.wtf", "rb"));
    }

    SECTION("writes saved cvars that differ from their default") {
        CVar* saved = CVar::Register("cvarTestSaved", "", 0x1, "default", nullptr, DEFAULT, false, nullptr, false);
        CVar* untouched = CVar::Register("cvarTestUntouched", "", 0x1, "default", nullptr, DEFAULT, false, nullptr, false);
        // 0x80 marks a cvar that is never written out
        CVar* notSaved = CVar::Register("cvarTestNotSaved", "", 0x80, "default", nullptr, DEFAULT, false, nullptr, false);

        saved->Set("changed", true, false, false, true);
        notSaved->Set("changed", true, false, false, true);
        (void)untouched;

        REQUIRE(CVar::Save() == 1);

        FILE* file = fopen("WTF/cvar-test-save.wtf", "rb");
        REQUIRE(file != nullptr);

        char contents[4096] = {};
        fread(contents, 1, sizeof(contents) - 1, file);
        fclose(file);

        CHECK(strstr(contents, "SET cvarTestSaved \"changed\"\n") != nullptr);
        CHECK(strstr(contents, "cvarTestUntouched") == nullptr);
        CHECK(strstr(contents, "cvarTestNotSaved") == nullptr);
    }

    remove("WTF/cvar-test-save.wtf");
}
