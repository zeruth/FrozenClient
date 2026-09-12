#include "util/CStatus.hpp"
#include <cstdarg>
#include <cstdio>
#include <storm/Memory.hpp>
#include <storm/String.hpp>

#define STATUS_MAX_TEXT 256

CStatus CStatus::s_errorList;

CStatus::~CStatus() {
    this->Clear();
}

void CStatus::Add(const CStatus& status) {
    // TSList iteration is not const-correct
    auto& entries = const_cast<CStatus&>(status).m_entries;

    for (auto entry = entries.Head(); entry; entry = entries.Next(entry)) {
        this->Add(entry->severity, "%s", entry->text);
    }
}

void CStatus::Add(STATUS_TYPE severity, const char* format, ...) {
    va_list args;
    va_start(args, format);
    auto entry = this->NewEntry(severity, format, args);
    va_end(args);

    if (entry) {
        this->m_entries.LinkToTail(entry);
    }
}

void CStatus::Clear() {
    while (auto entry = this->m_entries.Head()) {
        this->m_entries.UnlinkNode(entry);

        if (entry->text) {
            SMemFree(entry->text, __FILE__, __LINE__, 0);
        }

        delete entry;
    }

    this->m_maxSeverity = STATUS_INFO;
}

STATUSENTRY* CStatus::NewEntry(STATUS_TYPE severity, const char* format, va_list args) {
    if (!format) {
        return nullptr;
    }

    char text[STATUS_MAX_TEXT];
    vsnprintf(text, sizeof(text), format, args);
    text[sizeof(text) - 1] = '\0';

    if (!*text) {
        return nullptr;
    }

    auto entry = new STATUSENTRY;
    entry->text = SStrDupA(text, __FILE__, __LINE__);
    entry->severity = severity;

    if (severity > this->m_maxSeverity) {
        this->m_maxSeverity = severity;
    }

    return entry;
}

void CStatus::Prepend(STATUS_TYPE severity, const char* format, ...) {
    va_list args;
    va_start(args, format);
    auto entry = this->NewEntry(severity, format, args);
    va_end(args);

    if (entry) {
        this->m_entries.LinkToHead(entry);
    }
}

CWOWClientStatus::~CWOWClientStatus() {
    if (this->m_logFile) {
        for (auto entry = this->m_entries.Head(); entry; entry = this->m_entries.Next(entry)) {
            SLogWrite(this->m_logFile, "%s", entry->text);
        }

        SLogClose(this->m_logFile);
        this->m_logFile = nullptr;
    }
}

CStatus& GetGlobalStatusObj() {
    return CStatus::s_errorList;
}
