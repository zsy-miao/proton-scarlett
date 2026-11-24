#pragma once
#include <windows.h>

/* There is no safe error return convention for an unidentified method. */
typedef void (WINAPI *xs_stop_fn)(void);
static __attribute__((noreturn)) void WINAPI xs_stop(void)
{
    OutputDebugStringA("scarlett: unsupported graphics bootstrap call; stopping\n");
    RaiseFailFastException(NULL, NULL, 0);
    __builtin_trap();
}
