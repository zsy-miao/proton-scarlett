#pragma once
#include <windows.h>
#include <stdio.h>

/* There is no safe error return convention for an unidentified method. */
typedef void (WINAPI *xs_stop_fn)(void);
static __attribute__((noreturn)) void WINAPI xs_stop(void)
{
    OutputDebugStringA("scarlett: unsupported graphics bootstrap call; stopping\n");
    void *frames[16];
    USHORT n=CaptureStackBackTrace(0,ARRAYSIZE(frames),frames,NULL);
    HMODULE exe=GetModuleHandleW(NULL);
    HMODULE builder=GetModuleHandleW(L"XboxBvhBuilder.dll");
    for (USHORT i=0;i<n;i++) {
        HMODULE module=NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (const char *)frames[i],&module) && (module==exe || module==builder)) {
            char text[96];
            snprintf(text,sizeof text,"scarlett: unsupported graphics call; %s return RVA +%llx\n",
                    module==exe ? "title" : "XboxBvhBuilder.dll",
                    (unsigned long long)((UINT_PTR)frames[i]-(UINT_PTR)module));
            OutputDebugStringA(text);
            if (module==exe) break;
            builder=NULL; /* Only the innermost builder call identifies the missing slot. */
        }
    }
    RaiseFailFastException(NULL, NULL, 0);
    __builtin_trap();
}
