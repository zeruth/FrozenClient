#ifndef UTIL_C_STATUS_HPP
#define UTIL_C_STATUS_HPP

#include "util/Log.hpp"
#include <storm/List.hpp>
#include <cstdarg>

enum STATUS_TYPE {
    STATUS_INFO = 0x0,
    STATUS_WARNING = 0x1,
    STATUS_ERROR = 0x2,
    STATUS_FATAL = 0x3,
    STATUS_NUMTYPES = 0x4,
};

struct STATUSENTRY : TSLinkedNode<STATUSENTRY> {
    char* text = nullptr;
    STATUS_TYPE severity = STATUS_INFO;
};

// Collects status messages. Messages are formatted into at most 255 characters when added.
class CStatus {
    public:
        // Static variables
        static CStatus s_errorList;

        // Member variables
        TSList<STATUSENTRY, TSGetLink<STATUSENTRY>> m_entries;
        STATUS_TYPE m_maxSeverity = STATUS_INFO;

        // Virtual member functions
        virtual ~CStatus();
        virtual void Add(const CStatus& status);
        virtual void Add(STATUS_TYPE severity, const char* format, ...);

        // Member functions
        void Clear();
        void Prepend(STATUS_TYPE severity, const char* format, ...);

    protected:
        STATUSENTRY* NewEntry(STATUS_TYPE severity, const char* format, va_list args);
};

// A status collector that writes everything it collected to a log file when destroyed
class CWOWClientStatus : public CStatus {
    public:
        // Member variables
        HSLOG m_logFile = nullptr;

        // Virtual member functions
        virtual ~CWOWClientStatus();
};

CStatus& GetGlobalStatusObj(void);

#endif
