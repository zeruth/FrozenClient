#include "glue/Character.hpp"
#include "console/CVar.hpp"
#include "db/Db.hpp"
#include <cstdio>
#include <regex>
#include <string>
#include <vector>

namespace {

enum NAME_CHARSET {
    CHARSET_LATIN       = 0,
    CHARSET_ASCII       = 1,
    CHARSET_CYRILLIC    = 2,
    CHARSET_KOREAN      = 3,
    CHARSET_CHINESE     = 4,
    NUM_NAME_CHARSETS
};

// The original compiles the filter expressions with its own regex engine at initialization; here
// the patterns are kept as text and compiled with std::regex the first time a locale is checked
struct NameFilter {
    std::vector<std::wstring> patterns;
    std::vector<std::wregex> regexes;
    bool compiled = false;
};

struct LocaleNameFilters {
    NameFilter namesProfanity;
    NameFilter chatProfanity;
    NameFilter namesReserved;
};

bool s_initialized = false;
int32_t s_localeMask = 0;
int32_t s_charsetMask = 0;
CVar* s_forceEnglishNamesCvar = nullptr;
LocaleNameFilters s_filters[NUM_LOCALES];

std::wstring ToWide(const char* text) {
    std::wstring result;

    auto p = reinterpret_cast<const unsigned char*>(text);

    while (*p) {
        uint32_t codepoint;
        int32_t extra;

        if (*p < 0x80) {
            codepoint = *p;
            extra = 0;
        } else if ((*p & 0xE0) == 0xC0) {
            codepoint = *p & 0x1F;
            extra = 1;
        } else if ((*p & 0xF0) == 0xE0) {
            codepoint = *p & 0x0F;
            extra = 2;
        } else if ((*p & 0xF8) == 0xF0) {
            codepoint = *p & 0x07;
            extra = 3;
        } else {
            codepoint = 0xFFFD;
            extra = 0;
        }

        p++;

        for (int32_t i = 0; i < extra; i++) {
            if ((*p & 0xC0) != 0x80) {
                codepoint = 0xFFFD;
                break;
            }

            codepoint = (codepoint << 6) | (*p & 0x3F);
            p++;
        }

        if (codepoint >= 0x10000) {
            codepoint -= 0x10000;
            result.push_back(static_cast<wchar_t>(0xD800 | (codepoint >> 10)));
            result.push_back(static_cast<wchar_t>(0xDC00 | (codepoint & 0x3FF)));
        } else {
            result.push_back(static_cast<wchar_t>(codepoint));
        }
    }

    return result;
}

// The filter expressions use \< and \> for word boundaries
std::wstring TranslatePattern(const std::wstring& pattern) {
    std::wstring result;

    for (size_t i = 0; i < pattern.size(); i++) {
        if (pattern[i] == L'\\' && i + 1 < pattern.size() && (pattern[i + 1] == L'<' || pattern[i + 1] == L'>')) {
            result += L"\\b";
            i++;
        } else {
            result.push_back(pattern[i]);
        }
    }

    return result;
}

void CompileFilter(NameFilter& filter, const char* description) {
    if (filter.compiled) {
        return;
    }

    for (auto& pattern : filter.patterns) {
        try {
            filter.regexes.emplace_back(TranslatePattern(pattern), std::regex::ECMAScript | std::regex::icase);
        } catch (const std::regex_error&) {
            fprintf(stderr, "Warning: invalid %s filter expression\n", description);
        }
    }

    filter.compiled = true;
}

template <class T>
void CollectPatterns(WowClientDB<T>& db, int32_t language, NameFilter& filter) {
    filter.patterns.clear();
    filter.regexes.clear();
    filter.compiled = false;

    for (int32_t i = 0; i < db.GetNumRecords(); i++) {
        auto rec = db.GetRecordByIndex(i);

        if (rec->m_language == language || rec->m_language == -1) {
            filter.patterns.push_back(ToWide(rec->m_name));
        }
    }
}

bool MatchesFilter(NameFilter& filter, const char* description, const std::wstring& name) {
    CompileFilter(filter, description);

    for (auto& regex : filter.regexes) {
        if (std::regex_search(name, regex)) {
            return true;
        }
    }

    return false;
}

bool IsCharInCharset(wchar_t c, int32_t charset) {
    auto u = static_cast<uint16_t>(c);

    switch (charset) {
        case CHARSET_LATIN:
            return (u >= 'A' && u <= 'Z')
                || (u >= 'a' && u <= 'z')
                || (u >= 0xC0 && u <= 0xDD && u != 0xD7)
                || u == 0xDF
                || (u >= 0xE0 && u <= 0xFF && u != 0xF7 && u != 0xFE);

        case CHARSET_ASCII:
            return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z');

        case CHARSET_CYRILLIC:
            return (u >= 0x410 && u <= 0x44F) || u == 0x401 || u == 0x451;

        case CHARSET_KOREAN:
            // The original also checks the syllable against its table of allowed hangul
            return u >= 0xAC00 && u <= 0xD7A3;

        case CHARSET_CHINESE:
            return (u >= 0x4E00 && u <= 0x9FFF)
                || (u >= 0x3400 && u <= 0x4DFF)
                || (u >= 0xF900 && u <= 0xFAFF);

        default:
            return false;
    }
}

wchar_t ToLowerName(wchar_t c) {
    auto u = static_cast<uint16_t>(c);

    if ((u >= 'A' && u <= 'Z') || (u >= 0xC0 && u <= 0xDE) || (u >= 0x410 && u <= 0x42F)) {
        return static_cast<wchar_t>(u + 0x20);
    }

    if (u == 0x152) {
        return 0x153;
    }

    if (u == 0x401) {
        return 0x451;
    }

    return c;
}

// Port of the original validator. allowedChars lists characters accepted outside the detected
// charset (apostrophes and spaces for names that permit them); charsetMask -1 selects the mask
// given to ValidateNameInitialize, or ASCII only when forceEnglishNames is set.
int32_t ValidateNameInternal(int32_t locale, const wchar_t* allowedChars, const char* name, uint32_t& length, int32_t& language, bool checkNamesProfanity, bool checkChatProfanity, bool checkNamesReserved, bool allowSpaces, bool useForceEnglish, int32_t charsetMask) {
    if (!name) {
        return NAME_NO_NAME;
    }

    auto wide = ToWide(name);

    if (wide.size() >= 1024 || locale < 0 || locale > 8) {
        return NAME_FAILURE;
    }

    if (charsetMask == -1) {
        if (useForceEnglish && s_forceEnglishNamesCvar && s_forceEnglishNamesCvar->GetInt()) {
            charsetMask = 1 << CHARSET_ASCII;
        } else {
            charsetMask = s_charsetMask;
        }
    }

    language = -1;
    length = 0;

    int32_t lastApostrophe = -1;
    int32_t lastSpace = -1;

    for (size_t i = 0; ; i++) {
        wchar_t c = i < wide.size() ? wide[i] : 0;

        if (!c) {
            if (!allowSpaces) {
                if (length < 2) {
                    return length ? NAME_TOO_SHORT : NAME_NO_NAME;
                }

                if (lastApostrophe == static_cast<int32_t>(length) - 1) {
                    return NAME_INVALID_APOSTROPHE;
                }

                if (lastSpace == static_cast<int32_t>(length) - 1) {
                    return NAME_INVALID_SPACE;
                }
            }

            auto& filters = s_filters[locale];

            if (checkNamesProfanity && MatchesFilter(filters.namesProfanity, "profane names", wide)) {
                return NAME_PROFANE;
            }

            if (checkChatProfanity && MatchesFilter(filters.chatProfanity, "chat profanity", wide)) {
                return NAME_PROFANE;
            }

            if (checkNamesReserved && MatchesFilter(filters.namesReserved, "reserved names", wide)) {
                return NAME_RESERVED;
            }

            return NAME_SUCCESS;
        }

        length++;

        bool inCharset = false;

        if (language == -1) {
            for (int32_t charset = 0; charset < NUM_NAME_CHARSETS; charset++) {
                if ((charsetMask == 0 || (charsetMask & (1 << charset))) && IsCharInCharset(c, charset)) {
                    language = charset;
                    inCharset = true;
                    break;
                }
            }
        }

        if (!inCharset && !IsCharInCharset(c, language)) {
            if (!allowedChars) {
                return NAME_INVALID_CHARACTER;
            }

            bool allowed = false;

            for (auto p = allowedChars; *p; p++) {
                if (*p == c) {
                    allowed = true;
                    break;
                }
            }

            if (!allowed) {
                return NAME_INVALID_CHARACTER;
            }

            if (!allowSpaces) {
                if (c == L'\'') {
                    if (length == 1) {
                        return NAME_INVALID_APOSTROPHE;
                    }

                    if (lastApostrophe >= 0) {
                        return NAME_MULTIPLE_APOSTROPHES;
                    }

                    lastApostrophe = length - 1;
                } else if (c == L' ') {
                    if (length == 1) {
                        return NAME_INVALID_SPACE;
                    }

                    if (length > 1 && lastSpace == static_cast<int32_t>(length) - 2) {
                        return NAME_CONSECUTIVE_SPACES;
                    }

                    lastSpace = length - 1;
                }
            }

            continue;
        }

        // Three of the same letter in a row
        if (!allowSpaces && length > 2) {
            auto a = ToLowerName(wide[length - 3]);
            auto b = ToLowerName(wide[length - 2]);
            auto d = ToLowerName(wide[length - 1]);

            if (a == b && a == d) {
                return NAME_THREE_CONSECUTIVE;
            }
        }

        // Russian hard and soft signs may not start, end, or double up
        if (language == CHARSET_CYRILLIC) {
            auto current = ToLowerName(wide[length - 1]);
            wchar_t next = i + 1 < wide.size() ? wide[i + 1] : 0;

            if ((length == 1 && (current == 0x44A || current == 0x44C)) || ((next == 0 || next == L' ') && current == 0x44A)) {
                return NAME_RUSSIAN_SILENT_CHARACTER_AT_ENDS;
            }

            if (length > 1 && (current == 0x44A || current == 0x44C)) {
                auto previous = ToLowerName(wide[length - 2]);

                if (previous == 0x44A || previous == 0x44C) {
                    return NAME_RUSSIAN_CONSECUTIVE_SILENT_CHARACTERS;
                }
            }
        }
    }
}

} // namespace

bool NameNeedsDeclension(WOW_LOCALE locale, const char* name) {
    // TODO

    return false;
}

int32_t ValidateName(const char* name) {
    uint32_t length = 0;
    int32_t language = -1;

    auto result = ValidateNameInternal(CURRENT_LANGUAGE, nullptr, name, length, language, true, true, true, false, true, -1);

    if (result == NAME_SUCCESS) {
        uint32_t maxLength = 12;

        if (language == CHARSET_KOREAN) {
            maxLength = 8;
        } else if (language == CHARSET_CHINESE) {
            maxLength = 6;
        }

        if (length > maxLength) {
            result = NAME_TOO_LONG;
        }
    }

    return result + CHAR_NAME_SUCCESS;
}

void ValidateNameInitialize(int32_t localeMask, int32_t charsetMask) {
    if (s_initialized && localeMask == s_localeMask && charsetMask == s_charsetMask) {
        return;
    }

    s_initialized = true;

    if (!s_forceEnglishNamesCvar) {
        s_forceEnglishNamesCvar = CVar::Register("forceEnglishNames", "", 0, "0", nullptr, DEFAULT);
    }

    s_localeMask = localeMask;
    s_charsetMask = charsetMask;

    for (int32_t locale = 0; locale < NUM_LOCALES; locale++) {
        if (localeMask && !(localeMask & (1 << locale))) {
            continue;
        }

        auto& filters = s_filters[locale];

        CollectPatterns(g_namesProfanityDB, locale, filters.namesProfanity);
        CollectPatterns(g_chatProfanityDB, locale, filters.chatProfanity);
        CollectPatterns(g_namesReservedDB, locale, filters.namesReserved);
    }
}
