#include "os_detect.h"
#include <string.h>

OsDecision os_classify(const OsEvidence *e, OsInfo *out)
{
    if (!out) return OS_DECISION_UNKNOWN;
    memset(out, 0, sizeof(*out));
    if (!e) return OS_DECISION_UNKNOWN;
    /* A real Windows 10 export is stronger evidence than a compatibility
     * shim reporting Windows XP, Vista, 7, or 8 to RtlGetVersion. */
    if (e->modern_api) {
        if (e->file_version_ok && e->file_major >= 10) {
            out->major=e->file_major; out->minor=e->file_minor; out->build=e->file_build;
        } else if (e->rtl_version_ok && e->rtl_major >= 10) {
            out->major=e->rtl_major; out->minor=e->rtl_minor; out->build=e->rtl_build;
        } else out->major=10;
        return OS_DECISION_MODERN;
    }
    if (e->file_version_ok) {
        out->major=e->file_major; out->minor=e->file_minor; out->build=e->file_build;
        if (out->major >= 10) return OS_DECISION_MODERN;
        if (out->major == 5 && (out->minor == 1 || out->minor == 2)) {
            /* XP needs affirmative native-file evidence and no Vista+ exports.
             * Contradictory evidence is never allowed to load its framework. */
            if (e->vista_api || (e->rtl_version_ok && e->rtl_major >= 6))
                return OS_DECISION_UNKNOWN;
            out->xp=TRUE;
            return OS_DECISION_XP;
        }
        return OS_DECISION_UNSUPPORTED;
    }
    /* RtlGetVersion >=10 is a safe modern fallback; a lower reported version
     * alone is insufficient to permit the XP-only DLL framework. */
    if (e->rtl_version_ok && e->rtl_major >= 10) {
        out->major=e->rtl_major; out->minor=e->rtl_minor; out->build=e->rtl_build;
        return OS_DECISION_MODERN;
    }
    return OS_DECISION_UNKNOWN;
}

#ifdef _WIN32
#include <winver.h>

static BOOL kernel_file_version(HMODULE kernel, OsEvidence *e)
{
    typedef DWORD (WINAPI *SizeFn)(LPCWSTR, LPDWORD);
    typedef BOOL (WINAPI *InfoFn)(LPCWSTR, DWORD, DWORD, LPVOID);
    typedef BOOL (WINAPI *QueryFn)(LPCVOID, LPCWSTR, LPVOID *, PUINT);
    WCHAR kernel_path[MAX_PATH], version_path[MAX_PATH];
    HMODULE version_dll=NULL;
    SizeFn size_fn=NULL;
    InfoFn info_fn=NULL;
    QueryFn query_fn=NULL;
    VS_FIXEDFILEINFO *fixed=NULL;
    DWORD len, bytes, ignored=0;
    UINT fixed_size=0;
    void *data=NULL;
    BOOL ok=FALSE;
    if (!kernel) return FALSE;
    len=GetModuleFileNameW(kernel,kernel_path,MAX_PATH);
    if (!len || len>=MAX_PATH) return FALSE;
    len=GetSystemDirectoryW(version_path,MAX_PATH);
    if (!len || len>=MAX_PATH || len+13>=MAX_PATH) return FALSE;
    lstrcatW(version_path,L"\\version.dll");
    /* Absolute system path: never pick up a portable-directory version.dll. */
    version_dll=LoadLibraryW(version_path);
    if (!version_dll) return FALSE;
#define LOAD_FN(target,name) do { FARPROC p=GetProcAddress(version_dll,name); memcpy(&target,&p,sizeof(target)); } while (0)
    LOAD_FN(size_fn,"GetFileVersionInfoSizeW");
    LOAD_FN(info_fn,"GetFileVersionInfoW");
    LOAD_FN(query_fn,"VerQueryValueW");
#undef LOAD_FN
    if (!size_fn || !info_fn || !query_fn) goto done;
    bytes=size_fn(kernel_path,&ignored);
    if (!bytes || bytes>2*1024*1024) goto done;
    data=HeapAlloc(GetProcessHeap(),0,bytes);
    if (!data) goto done;
    if (!info_fn(kernel_path,0,bytes,data) ||
        !query_fn(data,L"\\",(LPVOID *)&fixed,&fixed_size) ||
        fixed_size<sizeof(*fixed) || !fixed || fixed->dwSignature!=0xFEEF04BD)
        goto done;
    e->file_major=HIWORD(fixed->dwFileVersionMS);
    e->file_minor=LOWORD(fixed->dwFileVersionMS);
    e->file_build=HIWORD(fixed->dwFileVersionLS);
    ok=TRUE;
done:
    if (data) HeapFree(GetProcessHeap(),0,data);
    FreeLibrary(version_dll);
    return ok;
}

BOOL os_detect(OsInfo *info, WCHAR *error, size_t cap)
{
    typedef LONG (WINAPI *RtlVersionFn)(OSVERSIONINFOW *);
    HMODULE kernel=GetModuleHandleW(L"kernel32.dll");
    HMODULE ntdll=GetModuleHandleW(L"ntdll.dll");
    OSVERSIONINFOW version;
    OsEvidence e;
    OsDecision decision;
    RtlVersionFn rtl=NULL;
    if (!info) return FALSE;
    memset(&e,0,sizeof(e));
    memset(&version,0,sizeof(version));
    version.dwOSVersionInfoSize=sizeof(version);
    if (kernel) {
        e.modern_api=GetProcAddress(kernel,"GetSystemCpuSetInformation")!=NULL;
        e.vista_api=GetProcAddress(kernel,"InitOnceExecuteOnce")!=NULL ||
                    GetProcAddress(kernel,"GetTickCount64")!=NULL;
        e.file_version_ok=kernel_file_version(kernel,&e);
    }
    if (ntdll) {
        FARPROC p=GetProcAddress(ntdll,"RtlGetVersion");
        memcpy(&rtl,&p,sizeof(rtl));
    }
    if (rtl && rtl(&version)==0) {
        e.rtl_version_ok=TRUE;
        e.rtl_major=version.dwMajorVersion;
        e.rtl_minor=version.dwMinorVersion;
        e.rtl_build=version.dwBuildNumber;
    }
    decision=os_classify(&e,info);
    if (error && cap) {
        const WCHAR *message=L"";
        if (decision==OS_DECISION_UNKNOWN)
            message=L"No se pudo identificar Windows de forma segura. No se iniciara el motor XP. Quite el modo de compatibilidad y reintente; tambien puede usar Puente.";
        else if (decision==OS_DECISION_UNSUPPORTED)
            message=L"Este Windows no es XP nativo ni Windows 10/11. No se iniciara el framework XP en Windows Vista/7/8; use Puente para reproducir enlaces.";
        lstrcpynW(error,message,(int)cap);
    }
    return decision==OS_DECISION_XP || decision==OS_DECISION_MODERN;
}
#endif
