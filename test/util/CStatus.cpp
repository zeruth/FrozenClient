#include "util/CStatus.hpp"
#include "catch.hpp"
#include <cstdio>
#include <cstring>

TEST_CASE("CStatus", "[util]") {
    SECTION("collects formatted entries in order and tracks the worst severity") {
        CStatus status;

        status.Add(STATUS_INFO, "first %d", 1);
        status.Add(STATUS_ERROR, "second %s", "two");
        status.Add(STATUS_WARNING, "third");

        auto entry = status.m_entries.Head();
        REQUIRE(entry != nullptr);
        CHECK(strcmp(entry->text, "first 1") == 0);
        CHECK(entry->severity == STATUS_INFO);

        entry = status.m_entries.Next(entry);
        REQUIRE(entry != nullptr);
        CHECK(strcmp(entry->text, "second two") == 0);

        entry = status.m_entries.Next(entry);
        REQUIRE(entry != nullptr);
        CHECK(strcmp(entry->text, "third") == 0);

        CHECK(status.m_entries.Next(entry) == nullptr);
        CHECK(status.m_maxSeverity == STATUS_ERROR);
    }

    SECTION("ignores empty messages") {
        CStatus status;
        status.Add(STATUS_INFO, "%s", "");
        CHECK(status.m_entries.Head() == nullptr);
    }

    SECTION("prepends ahead of existing entries") {
        CStatus status;
        status.Add(STATUS_INFO, "body");
        status.Prepend(STATUS_INFO, "header");

        CHECK(strcmp(status.m_entries.Head()->text, "header") == 0);
    }

    SECTION("merges another status") {
        CStatus inner;
        inner.Add(STATUS_WARNING, "inner");

        CStatus outer;
        outer.Add(inner);

        REQUIRE(outer.m_entries.Head() != nullptr);
        CHECK(strcmp(outer.m_entries.Head()->text, "inner") == 0);
        CHECK(outer.m_maxSeverity == STATUS_WARNING);
    }
}

TEST_CASE("CWOWClientStatus", "[util]") {
    const char* path = "cstatus-test.log";
    remove(path);

    {
        CWOWClientStatus status;
        REQUIRE(SLogCreate(path, 0, &status.m_logFile) == 1);
        status.Add(STATUS_ERROR, "Couldn't open %s", "Interface\\Missing.xml");
        status.Add(STATUS_WARNING, "second line");
    }

    // Text mode, so the platform's line ending reads back as a newline
    FILE* file = fopen(path, "r");
    REQUIRE(file != nullptr);

    char contents[1024] = {};
    fread(contents, 1, sizeof(contents) - 1, file);
    fclose(file);

    CHECK(strcmp(contents, "Couldn't open Interface\\Missing.xml\nsecond line\n") == 0);

    remove(path);
}
