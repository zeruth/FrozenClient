#include "world/Minimap.hpp"
#include "util/SFile.hpp"
#include <storm/String.hpp>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

// "Textures\Minimap" in the reference, which holds it behind a pointer so a tool can point the
// client at another directory. Frozen has no such tool, so it is the literal.
static const char* MINIMAP_DIRECTORY = "Textures\\Minimap";

// Lines beginning with this are directory markers, not entries, and the reference skips them by
// comparing the first four characters.
static const char* TRS_DIRECTIVE = "dir:";

// A stored name is 32 hex characters plus ".blp"; the reference copies 0x28 bytes, which is that
// with room to spare.
#define TRS_NAME_SIZE 0x28

static std::unordered_map<std::string, std::string> s_translate;
static bool s_loaded = false;

static std::string Fold(const char* s) {
    std::string out;

    for (; s && *s; s++) {
        char c = *s;

        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        } else if (c == '/') {
            // The .trs writes backslashes; a caller may not.
            c = '\\';
        }

        out.push_back(c);
    }

    return out;
}

// ref: FUN_007f6540
// Every line is either a "dir:" marker or "<readable path>\t<stored name>". The reference splits on
// the TAB and keys the table by the left side; a line with no TAB is skipped rather than treated as
// an entry.
//
// A repeated key keeps the LAST entry, not the first. The reference looks the key up, creates the
// node only when the lookup misses, and then copies the value in either way -- the copy sits
// outside the not-found branch. Easy to read the other way round, and it decides which name wins
// for a tile the .trs lists twice.
//
// DIVERGENCE on blank lines. The reference's loop ends as soon as a line comes back empty, so a
// stray blank line part way through silently truncates the table. This skips blanks and keeps
// reading. Deliberately not bug-for-bug: what that reproduces is a half-loaded minimap with no
// diagnostic.
//
// CHECKED against the shipped file rather than assumed -- extracted with MpqTool and measured:
//
//   19090 lines, all CRLF, no bare LF
//   445 "dir:" markers, 18644 entries, 0 lines that are neither
//   exactly one blank line, and it is the trailing one -- so the divergence above never fires
//     on real data, and the reference stops there at EOF anyway
//   0 repeated keys, so last-wins versus first-wins does not change this file's result
//   longest stored name 36 bytes against the 40-byte buffer
//   longest line 154 bytes against the 260-byte line buffer, so neither guard below trips
void MinimapLoadTranslate() {
    if (s_loaded) {
        return;
    }

    s_loaded = true;

    char path[260];
    SStrPrintf(path, sizeof(path), "%s\\md5translate.trs", MINIMAP_DIRECTORY);

    void* data = nullptr;
    size_t size = 0;

    if (!SFile::Load(nullptr, path, &data, &size, 0, 0, nullptr) || !data) {
        return;
    }

    auto text = static_cast<const char*>(data);
    size_t i = 0;

    while (i < size) {
        // One line, without its terminator. The reference reads through a helper with "\r\n" as
        // the delimiter set, so a lone CR or LF both end a line and an empty line is skipped.
        size_t start = i;

        while (i < size && text[i] != '\r' && text[i] != '\n') {
            i++;
        }

        size_t length = i - start;

        while (i < size && (text[i] == '\r' || text[i] == '\n')) {
            i++;
        }

        if (!length || length >= 260) {
            continue;
        }

        char line[260];
        memcpy(line, text + start, length);
        line[length] = '\0';

        if (!SStrCmpI(line, TRS_DIRECTIVE, SStrLen(TRS_DIRECTIVE))) {
            continue;
        }

        char* tab = SStrChr(line, '\t');

        if (!tab) {
            continue;
        }

        *tab = '\0';

        char name[TRS_NAME_SIZE];
        SStrCopy(name, tab + 1, sizeof(name));

        s_translate[Fold(line)] = name;
    }

    SFile::Unload(data);
}

void MinimapUnloadTranslate() {
    s_translate.clear();
    s_loaded = false;
}

const char* MinimapTranslate(const char* tilePath) {
    if (!tilePath || !*tilePath) {
        return nullptr;
    }

    auto it = s_translate.find(Fold(tilePath));

    return it == s_translate.end() ? nullptr : it->second.c_str();
}

int32_t MinimapTranslateCount() {
    return static_cast<int32_t>(s_translate.size());
}
