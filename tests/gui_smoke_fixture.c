/* Offline GUI extractor fixture: deterministic results, intentional stream error. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int has(int argc, WCHAR **argv, const WCHAR *s)
{
    int i;
    for (i=1;i<argc;++i) if (!wcscmp(argv[i],s)) return i;
    return 0;
}
int wmain(int argc,WCHAR **argv)
{
    const WCHAR *target=argc>1?argv[argc-1]:L"";
    char text[8192];
    FILE *f;
    int fmt=has(argc,argv,L"--format"),height=0;
    WideCharToMultiByte(CP_UTF8,0,target,-1,text,sizeof(text),NULL,NULL);
    if(fmt && fmt+1<argc) {
        if(wcsstr(argv[fmt+1],L"height<=144"))height=144;
        else if(wcsstr(argv[fmt+1],L"height<=240"))height=240;
        else if(wcsstr(argv[fmt+1],L"height<=360"))height=360;
    }
    f=fopen("fixture.log","ab");
    if(!f)return 10;
    fprintf(f,"%s %d %s\n",has(argc,argv,L"--dump-json")?"SEARCH":"RESOLVE",height,text);
    fclose(f);
    if(has(argc,argv,L"--dump-json")) {
        if(!wcscmp(target,L"ytsearch12:hang")) {Sleep(30000);return 0;}
        puts("{\"id\":\"abcdefghijk\",\"title\":\"Prueba de video 96 segundos\",\"channel\":\"Canal de prueba\",\"duration\":96}");
        puts("{\"id\":\"0123456789_\",\"title\":\"Segundo resultado de prueba\",\"channel\":\"Otro canal\",\"duration\":144}");
        return 0;
    }
    fputs("ERROR: fixture: formato no disponible; cambie calidad o reintente",stderr);
    return 1;
}
