#ifndef APP_WIN_CRASH_REPORT_HPP
#define APP_WIN_CRASH_REPORT_HPP

// Install a last-chance exception handler that writes Logs\crash.log with the faulting address,
// the registers and a heuristic stack walk. See CrashReport.cpp for why this exists rather than a
// debugger or a Windows Error Reporting dump.
void CrashReportInstall();

#endif
