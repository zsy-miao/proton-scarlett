#include "queue/title_window.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %u: %s (%lu)\n",__LINE__,#x,GetLastError()); return 1; } } while(0)
static HINSTANCE instance;
static struct xs_title_window primary;
static HANDLE ready,finish;
static HWND make(const char *cls, DWORD style, HWND parent)
{
    return CreateWindowExA(0,cls,"test",style,4,4,180,100,parent,NULL,instance,NULL);
}
static LRESULT CALLBACK reject(HWND h,UINT m,WPARAM w,LPARAM l)
{ return m==WM_CREATE?-1:DefWindowProcA(h,m,w,l); }
static DWORD WINAPI worker(void *unused)
{
    struct xs_title_window tmp;
    (void)unused;
    CHECK(!xs_title_window_query(&tmp,NULL) && !xs_title_window_query(&tmp,&primary));
    CHECK(make("sgaWindowClass",WS_OVERLAPPEDWINDOW,NULL));
    CHECK(!xs_title_window_query(&tmp,NULL)); /* ambiguous even on creator thread */
    SetEvent(ready); CHECK(WaitForSingleObject(finish,5000)==WAIT_OBJECT_0);
    return 0; /* no DestroyWindow: exercise thread-exit cleanup */
}
int main(int argc,char **argv)
{
    struct xs_title_window t,old,tmp;
    instance=GetModuleHandleW(NULL);
    WNDCLASSA cls={.lpfnWndProc=DefWindowProcA,.hInstance=instance,.lpszClassName="other"};
    CHECK(RegisterClassA(&cls));
    cls.lpszClassName="sgaWindowClass"; cls.lpfnWndProc=reject; CHECK(RegisterClassA(&cls));
    CHECK(!make(cls.lpszClassName,WS_OVERLAPPEDWINDOW,NULL) && !xs_title_window_query(&tmp,NULL));
    CHECK(UnregisterClassA(cls.lpszClassName,instance));
    cls.lpfnWndProc=DefWindowProcA; CHECK(RegisterClassA(&cls));
    HWND owner=make("other",WS_OVERLAPPEDWINDOW,NULL);
    CHECK(owner && make(cls.lpszClassName,WS_CHILD,owner) && make(cls.lpszClassName,WS_OVERLAPPEDWINDOW,owner));
    CHECK(!xs_title_window_query(&tmp,NULL));
    HWND h=make(cls.lpszClassName,WS_OVERLAPPEDWINDOW,NULL);
    CHECK(h);
    if (argc==2 && !strcmp(argv[1],"other-image")) {
        CHECK(!xs_title_window_query(&tmp,NULL)); puts("PASS sibling image cannot capture its own matching HWND"); return 0;
    }
    CHECK(xs_title_window_query(&t,NULL) && t.hwnd==h && t.pid==GetCurrentProcessId() && t.tid==GetCurrentThreadId());
    if(argc==4) {
        struct xs_title_window alien=t;
        alien.pid=strtoul(argv[1],NULL,10); alien.hwnd=(HWND)(UINT_PTR)strtoull(argv[2],NULL,10);
        alien.generation=strtoull(argv[3],NULL,10);
        CHECK(IsWindow(alien.hwnd) && !xs_title_window_query(&tmp,&alien));
        puts("PASS same-name child process saw only its own created window"); return 0;
    }
    char cmd[256];
    for(unsigned i=0;i<2;i++) {
        if (!i) snprintf(cmd,sizeof cmd,"game_scarlett_master.exe %lu %llu %llu",t.pid,(unsigned long long)(UINT_PTR)t.hwnd,(unsigned long long)t.generation);
        else strcpy(cmd,"sibling.exe other-image");
        STARTUPINFOA si={.cb=sizeof si}; PROCESS_INFORMATION pi; DWORD status;
        CHECK(CreateProcessA(NULL,cmd,NULL,NULL,FALSE,0,NULL,NULL,&si,&pi));
        CHECK(WaitForSingleObject(pi.hProcess,10000)==WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess,&status) && !status);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
    primary=t; ready=CreateEventW(NULL,TRUE,FALSE,NULL); finish=CreateEventW(NULL,TRUE,FALSE,NULL);
    HANDLE thread=CreateThread(NULL,0,worker,NULL,0,NULL); DWORD status;
    CHECK(thread && WaitForSingleObject(ready,5000)==WAIT_OBJECT_0);
    CHECK(!xs_title_window_query(&tmp,NULL) && !xs_title_window_query(&tmp,&t));
    SetEvent(finish); CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0 && GetExitCodeThread(thread,&status) && !status);
    CloseHandle(thread); CloseHandle(ready); CloseHandle(finish);
    CHECK(xs_title_window_query(&tmp,&t) && tmp.hwnd==h);
    old=t; CHECK(DestroyWindow(h) && !xs_title_window_query(&tmp,&old));
    h=make(cls.lpszClassName,WS_OVERLAPPEDWINDOW,NULL);
    CHECK(h && xs_title_window_query(&t,NULL) && t.hwnd==h && t.generation!=old.generation);
    CHECK(!xs_title_window_query(&tmp,&old));
    old.hwnd=h; CHECK(!xs_title_window_query(&tmp,&old)); /* identical numeric HWND + stale generation */
    CHECK(xs_title_window_query(&tmp,&t) && DestroyWindow(h) && DestroyWindow(owner));
    printf("PASS creator-thread HWND generation %llu -> %llu; wrong class, child, owner, thread, process, ambiguity, destroy/recreate and stale generation denied\n",
        (unsigned long long)primary.generation,(unsigned long long)t.generation);
}
