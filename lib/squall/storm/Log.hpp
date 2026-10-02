#ifndef STORM_LOG_HPP
#define STORM_LOG_HPP

#include <cstdarg>
#include <cstdint>

typedef uint32_t HSLOG;

// SLogCreate flags
#define SLOG_FLAG_OPEN_NOW  0x1  // open the file at once rather than on the first write
#define SLOG_FLAG_NO_FILE   0x2  // no file at all
#define SLOG_FLAG_APPEND    0x4  // keep what the file already holds

void SLogInitialize();

int32_t SLogCreate(const char* filename, uint32_t flags, HSLOG* log);

void SLogClose(HSLOG log);

void SLogFlush(HSLOG log);

void SLogFlushAll();

void SLogVWrite(HSLOG log, const char* format, va_list args);

void SLogWrite(HSLOG log, const char* format, ...);

#endif
