/* Private x64 XMem/device bridge for the observed 6236 MiB source pool. */
#pragma once
#include <windows.h>
#define TILE_PAGES ((ULONG_PTR)99776)
struct tile_sections { HANDLE part[6]; };
static inline SIZE_T tile_bytes(unsigned i)
{
    return i == 5 ? (1116ULL<<20) : (1ULL<<30);
}
static inline void tile_sections_close(struct tile_sections *s)
{
    for (unsigned i=0;i<ARRAYSIZE(s->part);++i)
        if (s->part[i]) CloseHandle(s->part[i]);
    ZeroMemory(s,sizeof *s);
}
typedef HRESULT (WINAPI *capture_tiles_fn)(const void *,UINT,struct tile_sections *);
