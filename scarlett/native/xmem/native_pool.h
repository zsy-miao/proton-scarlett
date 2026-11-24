#pragma once
#include <windows.h>
static inline LONG native_pool_ctl(ULONG op,const void *address,void *data,SIZE_T bytes)
{
    typedef LONG (WINAPI *Fn)(HANDLE,const void *,ULONG,void *,SIZE_T,SIZE_T *);
    Fn query=(Fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryVirtualMemory");
    return query?query(GetCurrentProcess(),address,op,data,bytes,NULL):(LONG)0xc0000003;
}
