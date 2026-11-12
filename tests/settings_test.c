/* Native portable INI regression tests. No application files are touched. */
#include "../src/settings.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int checks, failed;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; printf("FAIL %d: %s\n",__LINE__,#x); } } while (0)

static BOOL join_path(WCHAR *out, const WCHAR *base, const WCHAR *name)
{
    size_t a=wcslen(base), b=wcslen(name);
    if (a+b+2>MAX_PATH) return FALSE;
    memcpy(out,base,a*sizeof(WCHAR)); out[a++]=L'\\';
    memcpy(out+a,name,(b+1)*sizeof(WCHAR));
    return TRUE;
}

static BOOL temporary_directory(WCHAR *out)
{
    WCHAR root[MAX_PATH];
    DWORD count=GetTempPathW(MAX_PATH,root);
    if (!count || count>=MAX_PATH || !GetTempFileNameW(root,L"tls",0,out)) return FALSE;
    if (!DeleteFileW(out)) return FALSE;
    return CreateDirectoryW(out,NULL);
}

static void remove_directory_files(const WCHAR *base)
{
    WCHAR pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    if (!join_path(pattern,base,L"*")) return;
    find=FindFirstFileW(pattern,&data);
    if (find!=INVALID_HANDLE_VALUE) {
        do {
            if (!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) &&
                join_path(path,base,data.cFileName)) {
                SetFileAttributesW(path,FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(path);
            }
        } while (FindNextFileW(find,&data));
        FindClose(find);
    }
    RemoveDirectoryW(base);
}

static DWORD directory_files(const WCHAR *base)
{
    WCHAR pattern[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    DWORD count=0;
    if (!join_path(pattern,base,L"*")) return (DWORD)-1;
    find=FindFirstFileW(pattern,&data);
    if (find==INVALID_HANDLE_VALUE) return 0;
    do { if (!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) ++count; }
    while (FindNextFileW(find,&data));
    FindClose(find);
    return count;
}

static DWORD read_bytes(const WCHAR *path, unsigned char *bytes, DWORD cap)
{
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
                            NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    DWORD count=0;
    if (file==INVALID_HANDLE_VALUE) return (DWORD)-1;
    if (!ReadFile(file,bytes,cap,&count,NULL)) count=(DWORD)-1;
    CloseHandle(file);
    return count;
}

static BOOL bytes_have_secret(const unsigned char *bytes, DWORD length)
{
    static const char ascii[]="SESSION_SECRET_123";
    static const WCHAR wide[]=L"SESSION_SECRET_123";
    DWORD i;
    for (i=0;i+sizeof(ascii)-1<=length;++i)
        if (!memcmp(bytes+i,ascii,sizeof(ascii)-1)) return TRUE;
    for (i=0;i+sizeof(wide)-sizeof(WCHAR)<=length;++i)
        if (!memcmp(bytes+i,wide,sizeof(wide)-sizeof(WCHAR))) return TRUE;
    return FALSE;
}

static void put(const WCHAR *path, const WCHAR *section, const WCHAR *key,
                const WCHAR *value)
{
    CHECK(WritePrivateProfileStringW(section,key,value,path));
    WritePrivateProfileStringW(NULL,NULL,NULL,path);
}

static void check_defaults(const AppSettings *s)
{
    CHECK(s->quality==240);
    CHECK(s->cache_ms==450);
    CHECK(s->prefetch_kib==128);
    CHECK(s->decoder_threads==1);
    CHECK(s->volume==80);
    CHECK(s->muted==FALSE);
    CHECK(s->dark_theme==TRUE);
    CHECK(s->large_text==FALSE);
    CHECK(s->results_limit==12);
    CHECK(s->search_timeout_s==120);
    CHECK(s->resolve_timeout_s==90);
    CHECK(s->mode==0);
    CHECK(s->remember_size==TRUE);
    CHECK(s->width==840);
    CHECK(s->height==590);
    CHECK(s->width>=640 && s->width<=4096);
    CHECK(s->height>=400 && s->height<=2160);
    CHECK(wcschr(s->bridge_url,L'?')==NULL);
}

static void check_equal(const AppSettings *a, const AppSettings *b)
{
    CHECK(a->quality==b->quality);
    CHECK(a->cache_ms==b->cache_ms);
    CHECK(a->prefetch_kib==b->prefetch_kib);
    CHECK(a->decoder_threads==b->decoder_threads);
    CHECK(a->volume==b->volume);
    CHECK(a->muted==b->muted);
    CHECK(a->dark_theme==b->dark_theme);
    CHECK(a->large_text==b->large_text);
    CHECK(a->results_limit==b->results_limit);
    CHECK(a->search_timeout_s==b->search_timeout_s);
    CHECK(a->resolve_timeout_s==b->resolve_timeout_s);
    CHECK(a->mode==b->mode);
    CHECK(a->remember_size==b->remember_size);
    CHECK(a->width==b->width);
    CHECK(a->height==b->height);
    CHECK(!wcscmp(a->bridge_url,b->bridge_url));
}

int main(void)
{
    WCHAR base[MAX_PATH], path[MAX_PATH], absent[MAX_PATH], notice[512];
    WCHAR value[8192], huge[4097];
    AppSettings defaults, saved, loaded;
    unsigned char before[32768], after[32768];
    DWORD before_size, after_size;
    BOOL have_directory;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    settings_defaults(&defaults);
    check_defaults(&defaults);
    have_directory=temporary_directory(base);
    CHECK(have_directory);
    if (!have_directory) return 2;
    CHECK(join_path(path,base,L"TubeLiviano.ini"));
    CHECK(join_path(absent,base,L"missing-folder"));

    /* A fresh portable folder has usable defaults without creating files. */
    memset(&loaded,0x55,sizeof(loaded));
    CHECK(settings_load(&loaded,base,notice,512));
    check_equal(&defaults,&loaded);
    CHECK(directory_files(base)==0);

    saved=defaults;
    saved.quality=360; saved.cache_ms=3000; saved.prefetch_kib=256;
    saved.decoder_threads=2; saved.volume=0; saved.muted=TRUE;
    saved.dark_theme=FALSE; saved.large_text=TRUE; saved.results_limit=6;
    saved.search_timeout_s=30; saved.resolve_timeout_s=60;
    saved.mode=1; saved.remember_size=FALSE; saved.width=1024; saved.height=600;
    lstrcpyW(saved.bridge_url,L"https://bridge.example.test:8443/api");
    CHECK(settings_save(&saved,base,notice,512));
    CHECK(settings_load(&loaded,base,notice,512));
    check_equal(&saved,&loaded);
    CHECK(directory_files(base)==1);

    saved=defaults;
    saved.quality=144; saved.cache_ms=250; saved.prefetch_kib=64;
    saved.volume=100; saved.search_timeout_s=60; saved.resolve_timeout_s=30;
    saved.width=640; saved.height=400;
    CHECK(settings_save(&saved,base,notice,512));
    CHECK(settings_load(&loaded,base,notice,512));
    check_equal(&saved,&loaded);

    /* Save only the endpoint. Authentication remains in the session object. */
    lstrcpyW(saved.bridge_url,L"https://bridge.example.test/api?token=SESSION_SECRET_123");
    CHECK(settings_save(&saved,base,notice,512));
    CHECK(wcsstr(saved.bridge_url,L"SESSION_SECRET_123")!=NULL);
    CHECK(settings_load(&loaded,base,notice,512));
    CHECK(!wcscmp(loaded.bridge_url,L"https://bridge.example.test/api"));
    before_size=read_bytes(path,before,sizeof(before));
    CHECK(before_size!=(DWORD)-1 && before_size<sizeof(before));
    if (before_size!=(DWORD)-1 && before_size<sizeof(before))
        CHECK(!bytes_have_secret(before,before_size));

    /* A failed replacement must leave the existing portable INI intact. */
    CHECK(SetFileAttributesW(path,FILE_ATTRIBUTE_READONLY));
    saved.volume=99;
    CHECK(!settings_save(&saved,base,notice,512));
    CHECK(notice[0]!=0);
    after_size=read_bytes(path,after,sizeof(after));
    CHECK(before_size==after_size);
    CHECK(before_size!=(DWORD)-1 && before_size==after_size &&
          before_size<=sizeof(before) && !memcmp(before,after,before_size));
    CHECK(directory_files(base)==1);
    CHECK(SetFileAttributesW(path,FILE_ATTRIBUTE_NORMAL));

    /* A sharing violation occurs after the private temp file has been
     * generated; the original still survives and the temp must be removed. */
    {
        HANDLE locked=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,
                                   OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
        CHECK(locked!=INVALID_HANDLE_VALUE);
        if (locked!=INVALID_HANDLE_VALUE) {
            CHECK(!settings_save(&saved,base,notice,512));
            CHECK(notice[0]!=0);
            after_size=read_bytes(path,after,sizeof(after));
            CHECK(before_size!=(DWORD)-1 && before_size==after_size &&
                  before_size<=sizeof(before) && !memcmp(before,after,before_size));
            CHECK(directory_files(base)==1);
            CHECK(CloseHandle(locked));
        }
    }

    CHECK(!settings_save(&saved,absent,notice,512));
    CHECK(notice[0]!=0);
    CHECK(GetFileAttributesW(absent)==INVALID_FILE_ATTRIBUTES);
    CHECK(directory_files(base)==1);

    /* Future settings survive writes by this version of the application. */
    put(path,L"FutureVersion",L"KeepMe",L"value-for-another-version");
    put(path,L"General",L"UnknownOption",L"also-preserved");
    CHECK(settings_save(&saved,base,notice,512));
    CHECK(GetPrivateProfileStringW(L"FutureVersion",L"KeepMe",L"",value,8192,path)>0);
    CHECK(!wcscmp(value,L"value-for-another-version"));
    CHECK(GetPrivateProfileStringW(L"General",L"UnknownOption",L"",value,8192,path)>0);
    CHECK(!wcscmp(value,L"also-preserved"));

    /* Manually edited old files cannot introduce unsupported enum choices,
     * booleans, geometry, or values outside the actual engine limits. */
    CHECK(DeleteFileW(path));
    put(path,L"General",L"DarkTheme",L"2");
    put(path,L"General",L"LargeText",L"-1");
    put(path,L"General",L"ResultsLimit",L"7");
    put(path,L"General",L"RememberSize",L"true");
    put(path,L"General",L"Width",L"4097");
    put(path,L"General",L"Height",L"2161");
    put(path,L"Playback",L"Quality",L"480");
    put(path,L"Playback",L"CacheMs",L"249");
    put(path,L"Playback",L"PrefetchKiB",L"512");
    put(path,L"Playback",L"DecoderThreads",L"0");
    put(path,L"Playback",L"Volume",L"101");
    put(path,L"Playback",L"Muted",L"2");
    put(path,L"Network",L"SearchTimeout",L"15");
    put(path,L"Network",L"ResolveTimeout",L"120");
    put(path,L"Network",L"Mode",L"3");
    CHECK(settings_load(&loaded,base,notice,512));
    check_equal(&defaults,&loaded);

    /* The numeric reader rejects overflow before converting to an int, and
     * rejects numeric prefixes followed by junk or an unsupported sign. */
    memset(huge,0,sizeof(huge));
    {
        size_t i;
        for (i=0;i<4096;++i) huge[i]=L'9';
    }
    put(path,L"Playback",L"Quality",huge);
    put(path,L"Playback",L"CacheMs",L"2147483648");
    put(path,L"Playback",L"PrefetchKiB",L"128garbage");
    put(path,L"Playback",L"DecoderThreads",L"+2");
    put(path,L"Playback",L"Volume",huge);
    put(path,L"General",L"Width",L"4294968136");
    put(path,L"General",L"Height",L"4294967886");
    put(path,L"Network",L"SearchTimeout",L"4294967416");
    put(path,L"Network",L"BridgeURL",huge);
    CHECK(settings_load(&loaded,base,notice,512));
    check_equal(&defaults,&loaded);

    /* An INI written by a previous build may contain a session token; do not
     * revive that token merely by opening the portable application again. */
    put(path,L"Network",L"BridgeURL",L"http://bridge.example.test:8787/?token=SESSION_SECRET_123");
    CHECK(settings_load(&loaded,base,notice,512));
    CHECK(!wcscmp(loaded.bridge_url,L"http://bridge.example.test:8787/"));
    CHECK(settings_save(&loaded,base,notice,512));
    after_size=read_bytes(path,after,sizeof(after));
    CHECK(after_size!=(DWORD)-1 && after_size<sizeof(after));
    if (after_size!=(DWORD)-1 && after_size<sizeof(after))
        CHECK(!bytes_have_secret(after,after_size));

    /* Nonexistent folders and path collisions fail with a useful notice. */
    CHECK(!settings_load(&loaded,absent,notice,512));
    CHECK(notice[0]!=0);
    CHECK(DeleteFileW(path));
    CHECK(CreateDirectoryW(path,NULL));
    CHECK(!settings_save(&saved,base,notice,512));
    CHECK(notice[0]!=0);
    CHECK(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES &&
          (GetFileAttributesW(path)&FILE_ATTRIBUTE_DIRECTORY));
    CHECK(directory_files(base)==0);
    CHECK(RemoveDirectoryW(path));

    remove_directory_files(base);
    printf("%d native settings checks; %d failed\n",checks,failed);
    return failed?1:0;
}
