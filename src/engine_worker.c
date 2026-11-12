#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "engine_worker.h"
#include "engine_process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define OUT_CAP (1024 * 1024)
#define ERR_CAP 8192
#define CMD_CAP ENGINE_COMMAND_CAP
#define REQUEST_CAP 65536
#ifndef ENGINE_WORKER_REUSE_MS
#define ENGINE_WORKER_REUSE_MS 12000
#endif
#define REUSE_MS ENGINE_WORKER_REUSE_MS
#define CLEANUP_MS 3000

typedef struct {
    Engine engine;
    HANDLE cancel, done;
    DWORD timeout;
    BOOL resolve;
    char *json, *output;
    EngineFailure failure;
    EngineWorkerResult result;
    WCHAR error[768];
} Request;
typedef struct {
    PROCESS_INFORMATION process;
    HANDLE job, input, output, errors;
    WCHAR exe[MAX_PATH], base[MAX_PATH];
    char *buffer, errors_text[ERR_CAP + 1];
    DWORD used, errors_used;
    BOOL ready;
} Session;
typedef struct { HANDLE pipe; char *data; DWORD length; } WriteJob;

/* Only these few handle/pointer publications use the spin gate. No I/O or waits
 * happen under it. Events and interlocked operations publish request storage. */
static LONG gate, claimed;
static HANDLE actor, work_event, stop_event;
static BOOL accepting;
static Request *pending;
static WCHAR unsupported_exe[MAX_PATH];
static void lock(void) { while(InterlockedCompareExchange(&gate,1,0)) Sleep(0); }
static void unlock(void) { InterlockedExchange(&gate,0); }
static void msg(WCHAR *out, size_t cap, const WCHAR *text) {
    if(out && cap) { wcsncpy(out,text,cap-1); out[cap-1]=0; }
}
static void fail(Request *r, EngineFailure failure, const WCHAR *text) {
    r->failure=failure; msg(r->error,sizeof(r->error)/sizeof(r->error[0]),text);
}
static BOOL cancelled(const Request *r) {
    return WaitForSingleObject(stop_event,0)==WAIT_OBJECT_0 ||
        (r && r->cancel && WaitForSingleObject(r->cancel,0)==WAIT_OBJECT_0);
}
static BOOL deadline(Request *r, DWORD start) {
    if(cancelled(r)) { fail(r,ENGINE_FAILURE_CANCELLED,L"Cancelado."); return TRUE; }
    if((DWORD)(GetTickCount()-start)>=r->timeout) {
        fail(r,ENGINE_FAILURE_TIMEOUT,L"El motor excedio el tiempo de espera. Reintente o cambie de calidad; tambien puede usar el puente.");
        return TRUE;
    }
    return FALSE;
}
static void kill_session(Session *s) {
    if(s->job) TerminateJobObject(s->job,1);
    if(s->process.hProcess && WaitForSingleObject(s->process.hProcess,0)!=WAIT_OBJECT_0)
        TerminateProcess(s->process.hProcess,1);
}
static void release_session(Session *s, BOOL force) {
    if(force) kill_session(s);
    if(s->process.hProcess) { WaitForSingleObject(s->process.hProcess,1000); CloseHandle(s->process.hProcess); }
    if(s->process.hThread) CloseHandle(s->process.hThread);
    if(s->job) { TerminateJobObject(s->job,0); CloseHandle(s->job); }
    if(s->input) CloseHandle(s->input);
    if(s->output) CloseHandle(s->output);
    if(s->errors) CloseHandle(s->errors);
    if(s->buffer) HeapFree(GetProcessHeap(),0,s->buffer);
    ZeroMemory(s,sizeof(*s));
}
static BOOL drain(Session *s) {
    DWORD available, got, want, budget=65536;
    char discard[1024];
    while(budget && PeekNamedPipe(s->output,NULL,0,NULL,&available,NULL) && available) {
        if(s->used>=OUT_CAP) return FALSE;
        want=available<budget ? available : budget;
        if(want>OUT_CAP-s->used) want=OUT_CAP-s->used;
        if(!ReadFile(s->output,s->buffer+s->used,want,&got,NULL) || !got) break;
        s->used+=got; s->buffer[s->used]=0; budget-=got;
    }
    budget=65536;
    while(budget && PeekNamedPipe(s->errors,NULL,0,NULL,&available,NULL) && available) {
        want=available<budget ? available : budget;
        if(s->errors_used==ERR_CAP) {
            if(want>sizeof(discard)) want=sizeof(discard);
            if(!ReadFile(s->errors,discard,want,&got,NULL) || !got) break;
        } else {
            if(want>ERR_CAP-s->errors_used) want=ERR_CAP-s->errors_used;
            if(!ReadFile(s->errors,s->errors_text+s->errors_used,want,&got,NULL) || !got) break;
            s->errors_used+=got; s->errors_text[s->errors_used]=0;
        }
        budget-=got;
    }
    return !memchr(s->buffer,0,s->used);
}
static BOOL start_session(Session *s, Request *r) {
    SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};
    STARTUPINFOW startup;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    HANDLE child_input=NULL, child_output=NULL, child_errors=NULL;
    WCHAR *command=NULL,*env=NULL,plugins[MAX_PATH],js[MAX_PATH+32];
    size_t used=0,i;
    BOOL ok=FALSE;
    const WCHAR *args[]={L"--ignore-config",L"--quiet",L"--no-warnings",L"--no-progress",
        L"--no-cache-dir",L"--no-playlist",L"--skip-download",L"--encoding",L"utf-8",
        L"--socket-timeout",L"12",L"--retries",L"0",L"--extractor-retries",L"0",
        L"--js-runtimes",js,L"--no-plugin-dirs",L"--plugin-dirs",plugins,L"--",L"tubeliviano:worker"};
    ZeroMemory(s,sizeof(*s));
    fail(r,ENGINE_FAILURE_IO,L"No se pudo iniciar el canal del motor.");
    command=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,CMD_CAP*sizeof(WCHAR));
    s->buffer=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,OUT_CAP+1);
    env=engine_child_environment();
    if(!command || !s->buffer || !env) goto done;
    if(_snwprintf(plugins,MAX_PATH,L"%ls\\plugins",r->engine.base)<0 ||
        _snwprintf(js,MAX_PATH+32,L"quickjs:%ls\\runtimes\\qjs.exe",r->engine.base)<0) goto done;
    if(!engine_command_arg(command,&used,r->engine.exe)) goto done;
    for(i=0;i<sizeof(args)/sizeof(args[0]);++i) if(!engine_command_arg(command,&used,args[i])) goto done;
    if(!CreatePipe(&child_input,&s->input,&sa,65536) ||
        !CreatePipe(&s->output,&child_output,&sa,0) ||
        !CreatePipe(&s->errors,&child_errors,&sa,0) ||
        !SetHandleInformation(s->input,HANDLE_FLAG_INHERIT,0) ||
        !SetHandleInformation(s->output,HANDLE_FLAG_INHERIT,0) ||
        !SetHandleInformation(s->errors,HANDLE_FLAG_INHERIT,0)) goto done;
    s->job=CreateJobObjectW(NULL,NULL);
    ZeroMemory(&limits,sizeof(limits));
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!s->job) goto done;
    if(!SetInformationJobObject(s->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) {
        if(GetLastError()!=ERROR_INVALID_PARAMETER) goto done;
        limits.BasicLimitInformation.LimitFlags=0;
        if(!SetInformationJobObject(s->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) goto done;
    }
    ZeroMemory(&startup,sizeof(startup)); startup.cb=sizeof(startup);
    startup.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW; startup.wShowWindow=SW_HIDE;
    startup.hStdInput=child_input; startup.hStdOutput=child_output; startup.hStdError=child_errors;
    if(!CreateProcessW(r->engine.exe,command,NULL,NULL,TRUE,
        CREATE_NO_WINDOW|CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT,env,r->engine.base,&startup,&s->process)) {
        if(GetLastError()==ERROR_BAD_EXE_FORMAT)
            fail(r,ENGINE_FAILURE_LOADER,L"Motor incompatible: compruebe que ambos motores sean x86 y el motor XP tenga su framework.");
        goto done;
    }
    if(!AssignProcessToJobObject(s->job,s->process.hProcess) || ResumeThread(s->process.hThread)==(DWORD)-1) {
        fail(r,ENGINE_FAILURE_IO,L"Windows no permite controlar el proceso del motor. No se iniciara sin cancelacion segura."); goto done;
    }
    wcscpy(s->exe,r->engine.exe); wcscpy(s->base,r->engine.base); ok=TRUE;
done:
    if(child_input) CloseHandle(child_input);
    if(child_output) CloseHandle(child_output);
    if(child_errors) CloseHandle(child_errors);
    if(command) HeapFree(GetProcessHeap(),0,command);
    if(env) HeapFree(GetProcessHeap(),0,env);
    if(!ok) release_session(s,TRUE);
    return ok;
}
static void exited(Session *s, Request *r, BOOL before_ready) {
    DWORD code=1; WCHAR detail[1200];
    GetExitCodeProcess(s->process.hProcess,&code);
    if(before_ready && (code==0 || code==2)) {
        r->result=ENGINE_WORKER_UNAVAILABLE;
        lock(); wcscpy(unsupported_exe,r->engine.exe); unlock(); return;
    }
    if(code==0xC0000139UL || code==0xC0000135UL || code==0xC000007BUL) {
        _snwprintf(detail,1200,L"Motor %ls: fallo DLL 0x%08lX en Windows %lu.%lu. Extraiga el ZIP completo en una carpeta nueva, sin mezclar motores; tambien puede usar Puente.",
            r->engine.xp ? L"XP" : L"oficial",(unsigned long)code,
            (unsigned long)r->engine.os_major,(unsigned long)r->engine.os_minor);
        fail(r,ENGINE_FAILURE_LOADER,detail); return;
    }
    if(s->errors_used) {
        int n=MultiByteToWideChar(CP_UTF8,0,s->errors_text,(int)(s->errors_used>900 ? 900 : s->errors_used),detail,1199);
        detail[n>0 ? n : 0]=0;
        fail(r,code==1 ? ENGINE_FAILURE_EXTRACT : ENGINE_FAILURE_PROCESS,detail);
    } else fail(r,code==1 ? ENGINE_FAILURE_EXTRACT : ENGINE_FAILURE_PROCESS,
        L"El motor termino sin completar su respuesta. Reintente o use el puente.");
}
/* -1 invalid framing, 0 incomplete, 1 ready/OK, 2 extractor error. */
static int frame(Session *s, Request *r, BOOL hello) {
    char *end=memchr(s->buffer,'\n',s->used);
    size_t header,length=0,i,prefix;
    BOOL error_reply;
    if(!end) return s->used>128 ? -1 : 0;
    header=(size_t)(end-s->buffer)+1;
    if(hello) {
        if(header!=11 || memcmp(s->buffer,"TLW1 READY\n",11)) return -1;
        memmove(s->buffer,s->buffer+header,s->used-header); s->used-=(DWORD)header;
        s->buffer[s->used]=0; s->ready=TRUE; return 1;
    }
    error_reply=header>11 && !memcmp(s->buffer,"TLW1 ERROR ",11);
    prefix=error_reply ? 11 : 8;
    if(!error_reply && (header<10 || memcmp(s->buffer,"TLW1 OK ",8))) return -1;
    if(header<=prefix+1) return -1;
    for(i=prefix;i<header-1;++i) {
        if(s->buffer[i]<'0' || s->buffer[i]>'9' || length>OUT_CAP/10) return -1;
        length=length*10+(unsigned)(s->buffer[i]-'0');
    }
    if(length>OUT_CAP-130) return -1;
    if(s->used<header+length+1) return 0;
    if(s->buffer[header+length]!='\n' || s->used!=header+length+1) return -1;
    if(error_reply) {
        WCHAR detail[768];
        int n=MultiByteToWideChar(CP_UTF8,0,s->buffer+header,(int)(length>750 ? 750 : length),detail,767);
        detail[n>0 ? n : 0]=0;
        fail(r,ENGINE_FAILURE_EXTRACT,n ? detail : L"El motor no pudo resolver la peticion.");
    } else {
        r->output=HeapAlloc(GetProcessHeap(),0,length+1);
        if(!r->output) { fail(r,ENGINE_FAILURE_IO,L"Memoria insuficiente para la respuesta del motor."); return 2; }
        memcpy(r->output,s->buffer+header,length); r->output[length]=0;
        r->failure=ENGINE_FAILURE_NONE; r->error[0]=0;
    }
    s->used=0; s->buffer[0]=0; return error_reply ? 2 : 1;
}
static BOOL receive(Session *s, Request *r, DWORD start, BOOL hello) {
    BOOL invalid=FALSE;
    DWORD invalid_start=0;
    for(;;) {
        int status;
        if(deadline(r,start)) return FALSE;
        if(!drain(s)) { fail(r,ENGINE_FAILURE_OUTPUT,L"La respuesta del motor excede el limite de memoria o contiene bytes NUL invalidos."); return FALSE; }
        status=invalid ? 0 : frame(s,r,hello);
        if(status==1) return TRUE;
        if(status==2) return FALSE;
        if(status<0) {
            if(!hello) { fail(r,ENGINE_FAILURE_OUTPUT,L"El motor devolvio un protocolo invalido. Reintente o extraiga el ZIP completo."); return FALSE; }
            /* An older replacement engine may not implement plugin loading.
             * Only its clean/CLI-usage exit permits a one-shot fallback. */
            invalid=TRUE; invalid_start=GetTickCount();
        }
        if(invalid && GetTickCount()-invalid_start>1000) {
            fail(r,ENGINE_FAILURE_OUTPUT,L"El motor devolvio un protocolo de inicio invalido."); return FALSE;
        }
        if(WaitForSingleObject(s->process.hProcess,15)==WAIT_OBJECT_0) {
            if(!drain(s)) { fail(r,ENGINE_FAILURE_OUTPUT,L"Respuesta del motor demasiado grande."); return FALSE; }
            if(!invalid) {
                status=frame(s,r,hello);
                if(status==1) return TRUE;
                if(status==2) return FALSE;
            }
            exited(s,r,hello); return FALSE;
        }
    }
}
static DWORD WINAPI writer(void *data) {
    WriteJob *job=data;
    DWORD used=0,got; BOOL ok=TRUE;
    while(used<job->length) {
        if(!WriteFile(job->pipe,job->data+used,job->length-used,&got,NULL) || !got) { ok=FALSE; break; }
        used+=got;
    }
    CloseHandle(job->pipe);
    HeapFree(GetProcessHeap(),0,job->data); HeapFree(GetProcessHeap(),0,job);
    return ok ? 0 : 1;
}
static BOOL send_request(Session *s, Request *r, DWORD start) {
    WriteJob *job=HeapAlloc(GetProcessHeap(),0,sizeof(*job));
    HANDLE thread; DWORD code;
    if(!job) { fail(r,ENGINE_FAILURE_IO,L"Memoria insuficiente para enviar la consulta."); return FALSE; }
    if(!DuplicateHandle(GetCurrentProcess(),s->input,GetCurrentProcess(),&job->pipe,0,FALSE,DUPLICATE_SAME_ACCESS)) {
        HeapFree(GetProcessHeap(),0,job);
        fail(r,ENGINE_FAILURE_IO,L"Windows no pudo preparar el canal de consulta."); return FALSE;
    }
    job->data=r->json; job->length=(DWORD)strlen(r->json); r->json=NULL;
    thread=CreateThread(NULL,0,writer,job,0,NULL);
    if(!thread) {
        CloseHandle(job->pipe);
        HeapFree(GetProcessHeap(),0,job->data); HeapFree(GetProcessHeap(),0,job);
        fail(r,ENGINE_FAILURE_IO,L"Windows no pudo crear el canal de consulta."); return FALSE;
    }
    while(WaitForSingleObject(thread,15)!=WAIT_OBJECT_0) {
        BOOL aborted=deadline(r,start);
        if(!aborted && !drain(s)) {
            fail(r,ENGINE_FAILURE_OUTPUT,L"La respuesta del motor excede el limite de memoria."); aborted=TRUE;
        }
        if(aborted) {
            kill_session(s);
            /* Killing all read ends makes a blocked synchronous writer fail.
             * Its heap-owned storage is still safe even if Windows delays it. */
            WaitForSingleObject(thread,1000); CloseHandle(thread); return FALSE;
        }
    }
    GetExitCodeThread(thread,&code); CloseHandle(thread);
    if(code) { fail(r,ENGINE_FAILURE_IO,L"No se pudo enviar la consulta al motor."); return FALSE; }
    return TRUE;
}
static void finish(Request *r) {
    HANDLE done=r->done;
    SetEvent(done); /* Last access to caller-owned request storage. */
}
static Request *take(void) {
    Request *r;
    lock(); r=pending; pending=NULL; unlock(); return r;
}
static void cleanup_after_reply(Session *s) {
    DWORD start=GetTickCount();
    if(s->input) { CloseHandle(s->input); s->input=NULL; }
    while(WaitForSingleObject(s->process.hProcess,15)!=WAIT_OBJECT_0) {
        if(cancelled(NULL) || GetTickCount()-start>=CLEANUP_MS) { kill_session(s); break; }
        if(!drain(s)) { kill_session(s); break; }
    }
    release_session(s,TRUE);
}
static DWORD WINAPI actor_main(void *unused) {
    Session s; Request *r=NULL;
    DWORD idle=0;
    (void)unused; ZeroMemory(&s,sizeof(s));
    for(;;) {
        r=take();
        if(r) {
            DWORD start=GetTickCount();
            BOOL success=FALSE,keep=FALSE;
            if(deadline(r,start)) { finish(r); r=NULL; break; }
            if(s.process.hProcess && (_wcsicmp(s.exe,r->engine.exe) || _wcsicmp(s.base,r->engine.base)))
                release_session(&s,TRUE);
            if(s.process.hProcess && (GetTickCount()-idle>=REUSE_MS || WaitForSingleObject(s.process.hProcess,0)==WAIT_OBJECT_0))
                release_session(&s,TRUE);
            if(s.process.hProcess || start_session(&s,r)) {
                if(s.ready || receive(&s,r,start,TRUE)) {
                    s.errors_used=0; s.errors_text[0]=0;
                    if(send_request(&s,r,start)) success=receive(&s,r,start,FALSE);
                }
            }
            keep=success && !r->resolve && !cancelled(r);
            if(!success && r->failure!=ENGINE_FAILURE_EXTRACT) kill_session(&s);
            idle=GetTickCount();
            finish(r); r=NULL;
            if(!keep && s.process.hProcess) cleanup_after_reply(&s);
            /* Resolve and failures end the actor after child cleanup unless a
             * new request arrived in the meantime. Publication is gate-safe. */
            if(!keep) {
                lock();
                if(!pending) { accepting=FALSE; unlock(); break; }
                unlock();
            }
            continue;
        }
        if(cancelled(NULL)) break;
        if(s.process.hProcess) {
            HANDLE events[3]={stop_event,work_event,s.process.hProcess};
            DWORD elapsed=GetTickCount()-idle;
            DWORD wait=elapsed>=REUSE_MS ? WAIT_TIMEOUT : WaitForMultipleObjects(3,events,FALSE,REUSE_MS-elapsed);
            if(wait==WAIT_OBJECT_0+1) continue;
            lock();
            if(pending && wait!=WAIT_OBJECT_0) { unlock(); continue; }
            accepting=FALSE; unlock();
            if(wait==WAIT_TIMEOUT && s.input) {
                /* Graceful EOF lets one-file parent clean its temp files. */
                CloseHandle(s.input); s.input=NULL;
                cleanup_after_reply(&s);
            }
            break;
        }
        break;
    }
    release_session(&s,TRUE);
    lock(); accepting=FALSE; r=pending; pending=NULL; unlock();
    if(r) { fail(r,ENGINE_FAILURE_CANCELLED,L"Cancelado."); finish(r); }
    return 0;
}
static void reap_locked(void) {
    if(actor && WaitForSingleObject(actor,0)==WAIT_OBJECT_0) {
        CloseHandle(actor); actor=NULL;
        CloseHandle(work_event); work_event=NULL;
        CloseHandle(stop_event); stop_event=NULL;
    }
}
void engine_worker_stop(void) {
    lock(); if(stop_event) SetEvent(stop_event); unlock();
}
BOOL engine_worker_closed(void) {
    BOOL closed;
    lock(); if(!claimed) reap_locked(); closed=actor==NULL; unlock(); return closed;
}
static char *request_json(const WCHAR *source, BOOL resolve, int value, BOOL fallback) {
    int n=WideCharToMultiByte(CP_UTF8,0,source,-1,NULL,0,NULL,NULL);
    char *utf8,*json,*p; size_t i;
    if(n<=0 || n>REQUEST_CAP/2) return NULL;
    utf8=HeapAlloc(GetProcessHeap(),0,(size_t)n);
    json=HeapAlloc(GetProcessHeap(),0,REQUEST_CAP);
    if(!utf8 || !json) { if(utf8) HeapFree(GetProcessHeap(),0,utf8); if(json) HeapFree(GetProcessHeap(),0,json); return NULL; }
    WideCharToMultiByte(CP_UTF8,0,source,-1,utf8,n,NULL,NULL);
    p=json+sprintf(json,"{\"op\":\"%s\",\"%s\":%d,\"fallback\":%s,\"input\":\"",
        resolve ? "resolve" : "search",resolve ? "height" : "limit",value,fallback ? "true" : "false");
    for(i=0;i<(size_t)n-1;++i) {
        unsigned char c=(unsigned char)utf8[i];
        if((size_t)(p-json)>REQUEST_CAP-10) { HeapFree(GetProcessHeap(),0,utf8); HeapFree(GetProcessHeap(),0,json); return NULL; }
        if(c=='"' || c=='\\') { *p++='\\'; *p++=(char)c; }
        else if(c<32) p+=sprintf(p,"\\u%04x",c);
        else *p++=(char)c;
    }
    strcpy(p,"\"}\n"); HeapFree(GetProcessHeap(),0,utf8); return json;
}
static EngineWorkerResult request(Engine *engine, const WCHAR *source, BOOL resolve,
    int value, BOOL fallback, HANDLE cancel, DWORD timeout, char **output, WCHAR *error, size_t cap) {
    WCHAR plugin[MAX_PATH]; DWORD attributes, overall=GetTickCount();
    int written;
    Request *r=NULL; EngineWorkerResult result=ENGINE_WORKER_HANDLED;
    *output=NULL;
    if(!engine || !source || !*source || wcslen(source)>(resolve ? 8192u : 2048u) ||
        (resolve ? (value!=144 && value!=240 && value!=360) : (value!=6 && value!=12))) {
        if(engine) engine->failure=ENGINE_FAILURE_ARGUMENT;
        msg(error,cap,L"Consulta, enlace o perfil no valido."); return result;
    }
    if(wcslen(engine->base)>=MAX_PATH || wcslen(engine->exe)>=MAX_PATH) return ENGINE_WORKER_UNAVAILABLE;
    written=_snwprintf(plugin,MAX_PATH,L"%ls\\plugins\\tubeliviano\\yt_dlp_plugins\\extractor\\tubeliviano.py",engine->base);
    if(written<0 || written>=MAX_PATH) return ENGINE_WORKER_UNAVAILABLE;
    attributes=GetFileAttributesW(plugin);
    if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_DIRECTORY)) return ENGINE_WORKER_UNAVAILABLE;
    lock();
    if(!_wcsicmp(unsupported_exe,engine->exe)) { unlock(); return ENGINE_WORKER_UNAVAILABLE; }
    unlock();
    if(InterlockedCompareExchange(&claimed,1,0)) {
        engine->failure=ENGINE_FAILURE_IO; msg(error,cap,L"El motor ya tiene una peticion en curso."); return result;
    }
    r=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*r));
    if(!r) goto allocation;
    r->engine=*engine; r->cancel=cancel; r->timeout=timeout; r->resolve=resolve;
#ifdef ENGINE_TEST_TIMEOUT_MS
    r->timeout=ENGINE_TEST_TIMEOUT_MS;
#endif
    r->failure=ENGINE_FAILURE_IO;
    r->json=request_json(source,resolve,value,fallback);
    r->done=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!r->json || !r->done) goto allocation;
    for(;;) {
        BOOL wait;
        lock(); reap_locked(); wait=actor && (!accepting || WaitForSingleObject(stop_event,0)==WAIT_OBJECT_0);
        if(!wait) {
            if(cancel && WaitForSingleObject(cancel,0)==WAIT_OBJECT_0) {
                unlock(); fail(r,ENGINE_FAILURE_CANCELLED,L"Cancelado."); break;
            }
            if(!actor) {
                work_event=CreateEventW(NULL,FALSE,FALSE,NULL);
                stop_event=CreateEventW(NULL,TRUE,FALSE,NULL);
                accepting=TRUE;
                if(work_event && stop_event) actor=CreateThread(NULL,0,actor_main,NULL,CREATE_SUSPENDED,NULL);
                if(!actor) {
                    if(work_event) CloseHandle(work_event);
                    if(stop_event) CloseHandle(stop_event);
                    work_event=stop_event=NULL; accepting=FALSE;
                    unlock(); fail(r,ENGINE_FAILURE_IO,L"Windows no pudo crear el controlador del motor."); break;
                }
                pending=r;
                if(ResumeThread(actor)==(DWORD)-1) {
                    TerminateThread(actor,1); accepting=FALSE; pending=NULL; reap_locked();
                    unlock(); fail(r,ENGINE_FAILURE_IO,L"Windows no pudo iniciar el controlador del motor."); break;
                }
            } else pending=r;
            SetEvent(work_event); unlock();
            {
                HANDLE events[2]={r->done,actor};
                if(WaitForMultipleObjects(2,events,FALSE,INFINITE)!=WAIT_OBJECT_0)
                    fail(r,ENGINE_FAILURE_PROCESS,L"El controlador del motor termino sin respuesta.");
            }
            break;
        }
        unlock();
        if(cancel && WaitForSingleObject(cancel,0)==WAIT_OBJECT_0) { fail(r,ENGINE_FAILURE_CANCELLED,L"Cancelado."); break; }
        if((DWORD)(GetTickCount()-overall)>=timeout) { fail(r,ENGINE_FAILURE_TIMEOUT,L"El motor excedio el tiempo de espera."); break; }
        Sleep(10);
    }
    result=r->result; engine->failure=r->failure; msg(error,cap,r->error);
    *output=r->output;
    goto done;
allocation:
    engine->failure=ENGINE_FAILURE_IO; msg(error,cap,L"Memoria insuficiente para iniciar el motor.");
done:
    if(r) {
        if(r->json) HeapFree(GetProcessHeap(),0,r->json);
        if(r->done) CloseHandle(r->done);
        HeapFree(GetProcessHeap(),0,r);
    }
    InterlockedExchange(&claimed,0); return result;
}
EngineWorkerResult engine_worker_search(Engine *engine, const WCHAR *query,
    int limit, HANDLE cancel, DWORD timeout, char **output, WCHAR *error, size_t cap) {
    return request(engine,query,FALSE,limit,FALSE,cancel,timeout,output,error,cap);
}
EngineWorkerResult engine_worker_resolve(Engine *engine, const WCHAR *url,
    int height, BOOL fallback, HANDLE cancel, DWORD timeout, char **output, WCHAR *error, size_t cap) {
    return request(engine,url,TRUE,height,fallback,cancel,timeout,output,error,cap);
}
