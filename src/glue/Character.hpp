#ifndef GLUE_CHARACTER_HPP
#define GLUE_CHARACTER_HPP

#include "util/Locale.hpp"
#include <cstdint>

// Results of name validation, offset by CHAR_NAME_SUCCESS so they index the error token table
enum NAME_RESULT {
    NAME_SUCCESS                                = 0,
    NAME_FAILURE                                = 1,
    NAME_NO_NAME                                = 2,
    NAME_TOO_SHORT                              = 3,
    NAME_TOO_LONG                               = 4,
    NAME_INVALID_CHARACTER                      = 5,
    NAME_MIXED_LANGUAGES                        = 6,
    NAME_PROFANE                                = 7,
    NAME_RESERVED                               = 8,
    NAME_INVALID_APOSTROPHE                     = 9,
    NAME_MULTIPLE_APOSTROPHES                   = 10,
    NAME_THREE_CONSECUTIVE                      = 11,
    NAME_INVALID_SPACE                          = 12,
    NAME_CONSECUTIVE_SPACES                     = 13,
    NAME_RUSSIAN_CONSECUTIVE_SILENT_CHARACTERS  = 14,
    NAME_RUSSIAN_SILENT_CHARACTER_AT_ENDS       = 15,
};

// Error token index of CHAR_NAME_SUCCESS; ValidateName returns this plus a NAME_RESULT
#define CHAR_NAME_SUCCESS 87

bool IsDigitChar(uint16_t c);

bool IsLatinLetter(uint16_t c);

bool IsLowerCaseChar(uint16_t c);

uint16_t ToUpperName(uint16_t c);

uint16_t ToLowerName(uint16_t c);

bool TruncateAtLineBreak(char* text);

bool StripTextEscapes(const char* src, char* dst, int32_t dstSize);

void StripPipeCharacters(const char* src, char* dst, int32_t dstSize);

bool NameNeedsDeclension(WOW_LOCALE locale, const char* name);

int32_t ValidateCharacterName(int32_t locale, const char* name, bool checkNamesProfanity, bool checkChatProfanity, bool checkNamesReserved, bool useForceEnglish, int32_t extraLength);

int32_t ValidatePetName(int32_t locale, const char* name, bool checkNamesProfanity, bool checkChatProfanity, bool checkNamesReserved, bool useForceEnglish, int32_t extraLength);

int32_t ValidateName(const char* name);

void ValidateNameInitialize(int32_t localeMask, int32_t charsetMask);

#endif
