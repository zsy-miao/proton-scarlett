/* Private native-x64 win32u contract. Use immediately on the creating GUI
 * thread, and validate again after any operation that may dispatch messages. */
#pragma once
#include <windows.h>
struct xs_title_window { HWND hwnd; DWORD pid,tid; ULONG_PTR generation; };
static inline BOOL xs_title_window_query(struct xs_title_window *out,const struct xs_title_window *expected)
{
    typedef ULONG_PTR (WINAPI *query_fn)(HWND,ULONG_PTR,DWORD);
    query_fn fn=(query_fn)(void *)GetProcAddress(GetModuleHandleW(L"win32u.dll"),"NtUserCallHwndParam");
    struct xs_title_window w={.pid=GetCurrentProcessId(),.tid=GetCurrentThreadId()};
    if (!out) return FALSE;
    if (expected) w=*expected;
    *out=(struct xs_title_window){0};
    if (!fn || w.pid!=GetCurrentProcessId() || w.tid!=GetCurrentThreadId()) return FALSE;
    if (!expected) w.generation=fn(NULL,w.pid,0x53435701);
    else if (!w.hwnd) return FALSE;
    if (!w.generation) return FALSE;
    HWND hwnd=(HWND)fn(w.hwnd,w.generation,0x53435702);
    if (!hwnd || (expected && hwnd!=w.hwnd)) return FALSE;
    w.hwnd=hwnd;
    *out=w;
    return TRUE;
}
