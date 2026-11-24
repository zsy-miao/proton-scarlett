/* xmem.c, after pages.h has bound VirtualAlloc2. */
#include "display_backing.h"
#define DISPLAY_ATTR 0x30808000ULL
#define DISPLAY_SIZE ((SIZE_T)64 << 20)
#define DISPLAY_ALIGNMENT ((SIZE_T)4 << 20)

struct display_arena {
    struct display_arena *next;
    void *base;
    SIZE_T pins;
    BOOL owned;
};
static SRWLOCK display_lock=SRWLOCK_INIT;
static struct display_arena *display_arenas;

static void *allocate_display(SIZE_T size)
{
    if (size!=DISPLAY_SIZE || !reserve2) { SetLastError(ERROR_NOT_SUPPORTED); return NULL; }
    struct display_arena *a=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *a);
    if (!a) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    MEM_ADDRESS_REQUIREMENTS req={.Alignment=DISPLAY_ALIGNMENT};
    MEM_EXTENDED_PARAMETER param={.Type=MemExtendedParameterAddressRequirements,.Pointer=&req};
    a->base=reserve2(GetCurrentProcess(),NULL,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE,&param,1);
    if (!a->base) {
        DWORD err=GetLastError();
        HeapFree(GetProcessHeap(),0,a); SetLastError(err); return NULL;
    }
    a->owned=TRUE;
    AcquireSRWLockExclusive(&display_lock);
    a->next=display_arenas; display_arenas=a;
    ReleaseSRWLockExclusive(&display_lock);
    return a->base;
}

/* Caller holds display_lock. Metadata never lives inside the importable bytes. */
static struct display_arena **find_display(const void *addr)
{
    struct display_arena **p=&display_arenas;
    while (*p && (UINT_PTR)addr-(UINT_PTR)(*p)->base>=DISPLAY_SIZE) p=&(*p)->next;
    return p;
}
static void retire_display(struct display_arena **p)
{
    struct display_arena *a=*p;
    if (a->owned || a->pins) return;
    *p=a->next;
    VirtualFree(a->base,0,MEM_RELEASE);
    HeapFree(GetProcessHeap(),0,a);
}
/* Intercept interior/wrong-word frees before the ordinary heap-header path. */
static BOOL free_display(void *addr,ULONGLONG attrs)
{
    AcquireSRWLockExclusive(&display_lock);
    struct display_arena **p=find_display(addr);
    BOOL handled=*p || attrs==DISPLAY_ATTR;
    BOOL valid=*p && (*p)->base==addr && (*p)->owned && attrs==DISPLAY_ATTR;
    if (valid) { (*p)->owned=FALSE; retire_display(p); }
    ReleaseSRWLockExclusive(&display_lock);
    if (handled && !valid) SetLastError(ERROR_INVALID_PARAMETER);
    return handled;
}

__declspec(dllexport) HRESULT WINAPI ScarlettRetainDisplayBackingV1(const void *slice,SIZE_T bytes,struct XsDisplayBacking *out)
{
    if (!out) return E_POINTER;
    *out=(struct XsDisplayBacking){0};
    if (!slice || !bytes) return E_INVALIDARG;
    HRESULT hr=E_INVALIDARG;
    AcquireSRWLockExclusive(&display_lock);
    struct display_arena *a=*find_display(slice);
    if (a && a->owned && a->pins!=SIZE_MAX) {
        SIZE_T off=(UINT_PTR)slice-(UINT_PTR)a->base;
        if (bytes<=DISPLAY_SIZE-off) {
            ++a->pins;
            *out=(struct XsDisplayBacking){a->base,DISPLAY_SIZE,off};
            hr=S_OK;
        }
    }
    ReleaseSRWLockExclusive(&display_lock);
    return hr;
}
__declspec(dllexport) void WINAPI ScarlettReleaseDisplayBackingV1(void *base)
{
    AcquireSRWLockExclusive(&display_lock);
    struct display_arena **p=find_display(base);
    if (*p && (*p)->base==base && (*p)->pins) { --(*p)->pins; retire_display(p); }
    ReleaseSRWLockExclusive(&display_lock);
}
