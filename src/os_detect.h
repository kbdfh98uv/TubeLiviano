#ifndef TUBELIVIANO_OS_DETECT_H
#define TUBELIVIANO_OS_DETECT_H

#include <stddef.h>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#include <windows.h>
#else
/* The routing policy is also exercised directly by the Linux test runner. */
#include <stdint.h>
#include <wchar.h>
typedef uint32_t DWORD;
typedef int BOOL;
typedef wchar_t WCHAR;
#define TRUE 1
#define FALSE 0
#endif

typedef struct {
    DWORD major, minor, build;
    BOOL xp;
} OsInfo;

typedef struct {
    BOOL modern_api; /* GetSystemCpuSetInformation: Windows 10 and later. */
    BOOL vista_api;  /* InitOnceExecuteOnce/GetTickCount64: never native XP. */
    BOOL file_version_ok;
    DWORD file_major, file_minor, file_build;
    BOOL rtl_version_ok;
    DWORD rtl_major, rtl_minor, rtl_build;
} OsEvidence;

typedef enum {
    OS_DECISION_UNKNOWN = 0,
    OS_DECISION_XP,
    OS_DECISION_MODERN,
    OS_DECISION_UNSUPPORTED
} OsDecision;

/* Pure policy: compatibility-mode version lies cannot turn positive modern
 * evidence into an XP decision. Unknown evidence never defaults to XP. */
OsDecision os_classify(const OsEvidence *evidence, OsInfo *info);
BOOL os_detect(OsInfo *info, WCHAR *error, size_t cap);

#endif
