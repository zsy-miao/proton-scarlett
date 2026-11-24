#include "display_backing.h"
#include <stdio.h>
#include <stdlib.h>

__declspec(dllimport) PVOID WINAPI XMemAlloc(SIZE_T,ULONGLONG);
__declspec(dllimport) PVOID WINAPI XMemAllocDefault(SIZE_T,ULONGLONG);
__declspec(dllimport) VOID WINAPI XMemFree(PVOID,ULONGLONG);
__declspec(dllimport) VOID WINAPI XMemFreeDefault(PVOID,ULONGLONG);
#define CK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s (error %lu)\n",__LINE__,#x,GetLastError()); exit(1); } } while(0)
#define CAP ((SIZE_T)0x4000000)
#define ATTR 0x30808000ULL
#define REQUEST ((SIZE_T)0x1fa4000)
static BYTE *base;
static SIZE_T used;
static unsigned allocs,defaults,frees,default_frees;
static PVOID WINAPI take(SIZE_T n,ULONGLONG attr)
{
    CK(n==REQUEST && attr==ATTR && ++allocs<=2);
    if (!base) { base=XMemAllocDefault(CAP,attr); CK(base); ++defaults; }
    SIZE_T step=(n+65535)&~(SIZE_T)65535;
    CK(step<=CAP-used);
    BYTE *old=base+used; used+=step; return old;
}
static VOID WINAPI give(PVOID p,ULONGLONG attr)
{
    CK(attr==ATTR);
    if (++frees==1) CK(p==base+0x1fb0000);
    else { CK(frees==2 && p==base); XMemFreeDefault(p,attr); ++default_frees; }
}
__declspec(dllexport) struct {
    PVOID (WINAPI *alloc)(SIZE_T,ULONGLONG);
    VOID (WINAPI *free)(PVOID,ULONGLONG);
} __XMemAllocationHooks__={take,give};
static void deny(const void *addr,SIZE_T n)
{
    struct XsDisplayBacking out={(void *)1,1,1};
    CK(FAILED(ScarlettRetainDisplayBackingV1(addr,n,&out)) && !out.base && !out.size && !out.offset);
}
int main(void)
{
    CK(!XMemAllocDefault(CAP-1,ATTR) && GetLastError()==ERROR_NOT_SUPPORTED);
    CK(!XMemAllocDefault(CAP,ATTR^1) && GetLastError()==ERROR_NOT_SUPPORTED);
    BYTE *first=XMemAlloc(REQUEST,ATTR),*second=XMemAlloc(REQUEST,ATTR);
    CK(allocs==2 && defaults==1 && first==base && !((UINT_PTR)base%0x400000) && CAP-used==0xa0000);
    MEMORY_BASIC_INFORMATION m;
    CK(VirtualQuery(base,&m,sizeof m) && m.State==MEM_COMMIT && m.Type==MEM_PRIVATE &&
       m.AllocationBase==base && m.RegionSize==CAP && m.Protect==PAGE_READWRITE);
    struct XsDisplayBacking a,b;
    deny(base-1,1); deny(base+CAP-1,2); deny(base,0); deny(second,SIZE_MAX);
    CK(SUCCEEDED(ScarlettRetainDisplayBackingV1(first,REQUEST,&a)) && a.base==base && a.size==CAP && !a.offset);
    CK(SUCCEEDED(ScarlettRetainDisplayBackingV1(second,REQUEST,&b)) && b.base==base && b.size==CAP && b.offset==0x1fb0000);
    second[-1]=0x27; second[0]=0x84; base[CAP-1]=0xde;
    XMemFreeDefault(second,ATTR); CK(GetLastError()==ERROR_INVALID_PARAMETER);
    // Zero attributes would match a zero-filled fake allocation header on the old generic path.
    XMemFreeDefault(base+65536,0); CK(GetLastError()==ERROR_INVALID_PARAMETER);
    XMemFreeDefault(base,0); CK(GetLastError()==ERROR_INVALID_PARAMETER);
    struct XsDisplayBacking probe;
    CK(SUCCEEDED(ScarlettRetainDisplayBackingV1(base+CAP-1,1,&probe)));
    ScarlettReleaseDisplayBackingV1(probe.base);
    XMemFree(second,ATTR); CK(!default_frees && second[0]==0x84);
    XMemFree(first,ATTR); CK(default_frees==1); deny(second,REQUEST);
    XMemFreeDefault(base,ATTR); CK(GetLastError()==ERROR_INVALID_PARAMETER);
    BYTE *newbase=XMemAllocDefault(CAP,ATTR);
    CK(newbase && newbase!=base && !newbase[CAP-1]); XMemFreeDefault(newbase,ATTR);
    ScarlettReleaseDisplayBackingV1(a.base);
    BYTE *retained=b.base;
    CK(retained[b.offset-1]==0x27 && retained[b.offset]==0x84 && retained[CAP-1]==0xde);
    ScarlettReleaseDisplayBackingV1(b.base);
    CK(VirtualQuery(base,&m,sizeof m) && m.State==MEM_FREE);
    puts("PASS 30808000: two public hooked slices, one cached 4 MiB-aligned 64 MiB default arena, reverse frees, invalid frees rejected, two retained ranges outlived title owner");
}
