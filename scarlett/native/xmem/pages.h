/* Private physical-page implementation included by xmem.c. */
#define XMEM_PAGE ((SIZE_T)65536)
#include "tile_sections.h"
#include "marked_section.h"
HANDLE xmem_page_backing(SIZE_T size, BOOL wb);
HANDLE xmem_tile_backing(SIZE_T size);
BOOL xmem_wc_slot(PVOID addr, SIZE_T size, HANDLE backing, SIZE_T offset, DWORD policy);
static PVOID (WINAPI *reserve2)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);
static PVOID (WINAPI *map3)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);
static BOOL (WINAPI *unmap2)(HANDLE, PVOID, ULONG);

static void bind_page_apis(void)
{
    HMODULE kernel = GetModuleHandleW(L"kernelbase.dll");
    if (kernel) {
        reserve2 = (void *)GetProcAddress(kernel, "VirtualAlloc2");
        map3 = (void *)GetProcAddress(kernel, "MapViewOfFile3");
        unmap2 = (void *)GetProcAddress(kernel, "UnmapViewOfFile2");
    }
}

typedef struct PageRun {
    struct PageRun *next;
    ULONG_PTR first, count, views;
    HANDLE section;
    BOOL wc;
    HANDLE parts[5]; /* Only for the five-extent descriptor allocation. */
    HANDLE tile[6]; /* 5 x 1024 MiB + 1116 MiB; each fits the existing RADV WB import. */
} PageRun;

typedef struct PageView {
    struct PageView *next;
    PageRun *run;
    PVOID address;
    SIZE_T size;
    struct Arena *arena;
} PageView;

typedef struct { BYTE *at; SIZE_T bytes,offset; } ScatterExtent;
typedef struct Arena {
    struct Arena *next;
    PVOID base;
    SIZE_T size;
    ULONG_PTR views;
    BOOL split, wc, descriptor;
    DWORD policy;
    BOOL tile;
    PageRun *tile_run;
    unsigned tile_mapped;
    BOOL scatter; /* marked 24 GiB CPU reservation */
    ScatterExtent extent[2]; /* sorted by VA, same run and section */
    unsigned scatter_part;
} Arena;

static SRWLOCK page_lock = SRWLOCK_INIT;
static PageRun *runs;
static PageView *views;
static Arena *arenas;
static ULONG_PTR next_page = 1;

__declspec(dllexport) HRESULT WINAPI ScarlettCaptureTileSectionsV1(const void *base,UINT count,struct tile_sections *out)
{
    if (!out) return E_POINTER;
    ZeroMemory(out,sizeof *out);
    if (!base || count!=TILE_PAGES || ((UINT_PTR)base%XMEM_PAGE)) return E_INVALIDARG;
    BOOL (WINAPI *same)(HANDLE,HANDLE)=(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles");
    if (!same) return E_NOTIMPL;
    HRESULT hr=E_INVALIDARG;
    AcquireSRWLockShared(&page_lock);
    for (Arena *a=arenas;a;a=a->next) {
        if (a->base!=base || !a->tile || !a->tile_run || a->size!=count*XMEM_PAGE ||
                a->tile_mapped!=ARRAYSIZE(out->part)) continue;
        for (unsigned i=0;i<ARRAYSIZE(out->part);++i) {
            const BYTE *at=(const BYTE *)base+((SIZE_T)i<<30);
            struct scarlett_section_info info;
            hr=scarlett_query_section(0x5343000c,at,tile_bytes(i),&info);
            if (FAILED(hr)) break;
            out->part[i]=info.section;
            if (info.base!=at || info.offset || info.view_size!=tile_bytes(i) ||
                    !same(info.section,a->tile_run->tile[i])) { hr=E_INVALIDARG; break; }
        }
        break;
    }
    ReleaseSRWLockShared(&page_lock);
    if (FAILED(hr)) tile_sections_close(out);
    return hr;
}

/* Preserve the distinction from a later ordinary reservation at the same VA. */
static BOOL descriptor_marker(PVOID base)
{
    PVOID hit;
    ULONG_PTR count = 1;
    ULONG grain;
    return !GetWriteWatch(0, base, 1, &hit, &count, &grain);
}

static PVOID occupy_descriptor(PVOID base, SIZE_T size)
{
    return reserve2(NULL, base, size, MEM_RESERVE | MEM_REPLACE_PLACEHOLDER | MEM_WRITE_WATCH,
                    PAGE_NOACCESS, NULL, 0);
}

static void discard_released_arenas(void)
{
    /* VirtualFree releases the VA directly; discard its bookkeeping on next reservation. */
    for (Arena **p = &arenas; *p;) {
        Arena *old = *p;
        MEMORY_BASIC_INFORMATION m;
        if (!old->views && VirtualQuery(old->base, &m, sizeof m) &&
            (m.State != MEM_RESERVE || m.AllocationBase != old->base || m.RegionSize != old->size ||
               ((old->descriptor || old->tile || old->scatter) && !descriptor_marker(old->base)))) {
            *p = old->next;
            HeapFree(GetProcessHeap(), 0, old);
        } else p = &old->next;
    }
}

static PVOID reserve_arena(PVOID base, SIZE_T size, BOOL wc, BOOL tile)
{
    Arena *a = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *a);
    if (!a) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    AcquireSRWLockExclusive(&page_lock);
    discard_released_arenas();
    a->base = reserve2(NULL, base, size, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
                       PAGE_NOACCESS, NULL, 0);
    PVOID result = a->base;
    DWORD err = GetLastError();
    /* A raw VA release may have punched a hole in a still-live scatter arena.
     * Do not put another XMem generation (and its write-watch marker) there. */
    for (Arena *old=arenas; result && old;old=old->next) {
        if (old->scatter && old->views &&
            (UINT_PTR)result<(UINT_PTR)old->base+old->size && (UINT_PTR)old->base<(UINT_PTR)result+size) {
            VirtualFree(result,0,MEM_RELEASE); result=NULL; err=ERROR_INVALID_ADDRESS;
        }
    }
    a->scatter = !wc && !tile && size==(24ull<<30);
    if ((tile || a->scatter) && result && !occupy_descriptor(result,size)) {
        err = GetLastError();
        VirtualFree(result,0,MEM_RELEASE);
        result = NULL;
    }
    if (result) {
        a->tile = tile;
        a->size = size;
        a->wc = wc;
        a->next = arenas;
        arenas = a;
    } else HeapFree(GetProcessHeap(), 0, a);
    ReleaseSRWLockExclusive(&page_lock);
    if (!result) SetLastError(err);
    return result;
}

static PVOID descriptor_va(PVOID base, SIZE_T size, BOOL replace, DWORD policy)
{
    if (!!base != replace || !size || ((size | (UINT_PTR)base) % XMEM_PAGE) ||
        size > UINTPTR_MAX - (UINT_PTR)base) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if (!reserve2) { SetLastError(ERROR_NOT_SUPPORTED); return NULL; }
    AcquireSRWLockExclusive(&page_lock);
    PVOID result = NULL;
    DWORD err = ERROR_INVALID_ADDRESS;
    discard_released_arenas();
    if (!replace) {
        base = reserve2(NULL, NULL, size, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, NULL, 0);
        if (!base) { err = GetLastError(); goto done; }
    }
    /* A live XMem page arena also looks like a vacant Win32 placeholder. */
    for (Arena *a = arenas; a; a = a->next) {
        if ((UINT_PTR)base < (UINT_PTR)a->base + a->size && (UINT_PTR)a->base < (UINT_PTR)base + size) {
            if (!replace) VirtualFree(base, 0, MEM_RELEASE);
            goto done;
        }
    }
    if (replace) {
        Arena *a = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *a);
        if (!a) { err = ERROR_NOT_ENOUGH_MEMORY; goto done; }
        if (!(result = occupy_descriptor(base, size))) {
            err = GetLastError();
            HeapFree(GetProcessHeap(), 0, a);
            goto done;
        }
        *a = (Arena){.next = arenas, .base = base, .size = size, .wc = !!(policy & PAGE_WRITECOMBINE), .descriptor = TRUE, .policy = policy};
        arenas = a;
    } else result = base;
    if (!result) err = GetLastError();
done:
    ReleaseSRWLockExclusive(&page_lock);
    if (!result) SetLastError(err);
    return result;
}

/* Physical page order is the title's map order, not the VA order. */
static const struct { ULONG_PTR pages; DWORD policy; } layout[] = {
    {752,0x2404},{1600,0x2004},{160,0x1004},{16,0x1404},{432,0x8404}
};
static void restore_gap(BYTE *at,SIZE_T bytes,BOOL split);

/* Internal only: pin the live code view's dedicated backing before unmap/free can race. */
HRESULT xmem_pin_code_source(const void *addr, SIZE_T bytes, HANDLE *section, SIZE_T *size, SIZE_T *offset)
{
    HRESULT hr = E_INVALIDARG;
    AcquireSRWLockShared(&page_lock);
    for (PageView *v = views; v; v = v->next) {
        SIZE_T off = (UINT_PTR)addr - (UINT_PTR)v->address;
        if (off >= v->size) continue;
        if (v->arena->policy == 0x8404 && v->run->parts[4] && bytes && bytes <= v->size-off) {
            if (DuplicateHandle(GetCurrentProcess(),v->run->parts[4],GetCurrentProcess(),section,0,FALSE,DUPLICATE_SAME_ACCESS)) {
                *offset = off;
                *size = v->size;
                hr = S_OK;
            } else hr = HRESULT_FROM_WIN32(GetLastError());
        }
        break;
    }
    ReleaseSRWLockShared(&page_lock);
    return hr;
}

static BOOL has_descriptor_layout(void)
{
    static const unsigned order[] = {2,3,4,0,1};
    for (Arena *a = arenas; a; a = a->next) {
        if (a->policy != layout[2].policy) continue;
        UINT_PTR addr = (UINT_PTR)a->base;
        unsigned i = 0;
        for (; i < ARRAYSIZE(order); ++i) {
            unsigned j = order[i];
            Arena *b = arenas;
            while (b && (UINT_PTR)b->base != addr) b = b->next;
            if (!b || b->policy != layout[j].policy || b->size != layout[j].pages * XMEM_PAGE ||
                !descriptor_marker(b->base)) break;
            addr += b->size;
        }
        if (i == ARRAYSIZE(order)) return TRUE;
    }
    return FALSE;
}

/* DLL-internal. XMemMapPhysicalPages and XMemFreePhysicalPages both take the
 * exclusive side. Duplicate the run's three original handles before unlocking;
 * importing by title VA after this point could silently select a different run. */
HRESULT xmem_pin_placed(const void *base, UINT64 width, HANDLE parts[3], ULONG_PTR *generation)
{
    static const unsigned order[] = {4,0,1}; /* code, WC data, WB data */
    UINT_PTR at = (UINT_PTR)base;
    if (!at || at % XMEM_PAGE || width != 0xae00000 || width > UINTPTR_MAX - at)
        return E_INVALIDARG;
    PageRun *run = NULL;
    HRESULT hr = HRESULT_FROM_WIN32(ERROR_INVALID_ADDRESS);
    AcquireSRWLockShared(&page_lock);
    for (unsigned i = 0; i < ARRAYSIZE(order); ++i) {
        unsigned j = order[i];
        PageView *v = views;
        while (v && (UINT_PTR)v->address != at) v = v->next;
        if (!v || !v->arena->descriptor || v->arena->base != v->address ||
            v->arena->policy != layout[j].policy || v->size != layout[j].pages * XMEM_PAGE ||
            v->run->count != 2960 || !v->run->parts[j] || (run && run != v->run)) goto done;
        run = v->run;
        at += v->size;
    }
    for (unsigned i = 0; parts && i < ARRAYSIZE(order); ++i) {
        if (!DuplicateHandle(GetCurrentProcess(),run->parts[order[i]],GetCurrentProcess(),
                             &parts[i],0,FALSE,DUPLICATE_SAME_ACCESS)) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            while (i) CloseHandle(parts[--i]);
            goto done;
        }
    }
    *generation = run->first; /* page IDs increase monotonically and never wrap */
    hr = S_OK;
done:
    ReleaseSRWLockShared(&page_lock);
    return hr;
}

static void close_run(PageRun *r)
{
    if (r->section) CloseHandle(r->section);
    for (unsigned i=0;i<ARRAYSIZE(r->tile);++i) if (r->tile[i]) CloseHandle(r->tile[i]);
    for (unsigned i=0;i<ARRAYSIZE(r->parts);++i) if (r->parts[i]) CloseHandle(r->parts[i]);
    HeapFree(GetProcessHeap(),0,r);
}

__declspec(dllexport) BOOL WINAPI XMemAllocatePhysicalPages(ULONGLONG flags, PULONG_PTR count, PULONG_PTR pages)
{
    ULONG_PTR n = count ? *count : 0;
    if (count) *count = 0;
    if (!n || !pages || n > SIZE_MAX / XMEM_PAGE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL tile = flags == 0x380 && n == TILE_PAGES;
    if ((flags && flags != 0x280 && !tile) || !reserve2 || !map3 || !unmap2) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    SIZE_T size = n * XMEM_PAGE;
    PageRun *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *r);
    if (!r) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    r->wc = flags == 0x280;
    BOOL mixed = FALSE;
    if (r->wc && n == 2960) {
        AcquireSRWLockShared(&page_lock);
        mixed = has_descriptor_layout();
        ReleaseSRWLockShared(&page_lock);
    }
    if (tile) {
        for (unsigned i=0;i<ARRAYSIZE(r->tile);i++) {
            r->tile[i] = xmem_tile_backing(tile_bytes(i));
            if (!r->tile[i]) { DWORD e=GetLastError(); close_run(r); SetLastError(e); return FALSE; }
        }
    } else if (mixed) {
        for (unsigned i=0;i<ARRAYSIZE(layout);++i) {
            r->parts[i] = xmem_page_backing(layout[i].pages * XMEM_PAGE, !(layout[i].policy & PAGE_WRITECOMBINE));
            if (!r->parts[i]) { DWORD e=GetLastError(); close_run(r); SetLastError(e); return FALSE; }
        }
    } else r->section = r->wc ? xmem_page_backing(size,FALSE) :
        CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, (DWORD)(size >> 32), (DWORD)size, NULL);
    if (!tile && !mixed && !r->section) {
        DWORD err = GetLastError();
        HeapFree(GetProcessHeap(), 0, r);
        SetLastError(err);
        return FALSE;
    }
    AcquireSRWLockExclusive(&page_lock);
    if (n > UINTPTR_MAX - next_page || (tile && next_page > (0x1fffffULL-n)*16)) {
        ReleaseSRWLockExclusive(&page_lock);
        close_run(r);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    r->first = tile ? (next_page+15)&~(ULONG_PTR)15 : next_page;
    next_page = r->first + n*(tile ? 16 : 1);
    r->count = n;
    r->next = runs;
    runs = r;
    for (ULONG_PTR i = 0; i < n; ++i) pages[i] = r->first + i*(tile ? 16 : 1);
    *count = n;
    ReleaseSRWLockExclusive(&page_lock);
    return TRUE;
}

static BOOL consecutive(PULONG_PTR pages, ULONG_PTR n, ULONG_PTR step)
{
    for (ULONG_PTR i = 1; i < n; ++i)
        if (pages[i] != pages[0] + i*step) return FALSE;
    return TRUE;
}

__declspec(dllexport) BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR n, PULONG_PTR pages)
{
    DWORD err = ERROR_INVALID_PARAMETER;
    if (n && pages) {
        AcquireSRWLockExclusive(&page_lock);
        for (PageRun **p = &runs; *p; p = &(*p)->next) {
            PageRun *r = *p;
            if (r->first != pages[0]) continue;
            if (r->count != n || !consecutive(pages,n,r->tile[0] ? 16 : 1)) break;
            if (r->views) { err = ERROR_BUSY; break; }
            *p = r->next;
            close_run(r);
            err = ERROR_SUCCESS;
            break;
        }
        ReleaseSRWLockExclusive(&page_lock);
    }
    if (err) SetLastError(err);
    return !err;
}

/* Called with page_lock exclusive. Keep the run pinned if restoration needs a retry. */
static DWORD unmap_tile(Arena *a)
{
    while (a->tile_mapped) {
        unsigned i = a->tile_mapped-1;
        if (!xmem_wc_slot((BYTE *)a->base+((SIZE_T)i<<30),tile_bytes(i),NULL,0,0))
            return GetLastError();
        a->tile_mapped--;
    }
    if (a->split) {
        if (!VirtualFree(a->base,a->size,MEM_RELEASE|MEM_COALESCE_PLACEHOLDERS))
            return GetLastError();
        a->split = FALSE;
    }
    if (!occupy_descriptor(a->base,a->size)) return GetLastError();
    --a->tile_run->views;
    a->tile_run = NULL;
    --a->views;
    return ERROR_SUCCESS;
}

static DWORD map_tile(Arena *a, PULONG_PTR pages)
{
    if (!pages) return a->tile_run ? unmap_tile(a) : ERROR_INVALID_ADDRESS;
    if (a->tile_run || !descriptor_marker(a->base)) return ERROR_INVALID_ADDRESS;
    PageRun *r = runs;
    while (r && (!r->tile[0] || r->first != pages[0])) r = r->next;
    if (!r || !consecutive(pages,r->count,16)) return ERROR_INVALID_PARAMETER;
    if (!VirtualFree(a->base,a->size,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER))
        return GetLastError();
    a->tile_run = r;
    ++a->views;
    ++r->views;
    while (a->tile_mapped < ARRAYSIZE(r->tile)) {
        unsigned i = a->tile_mapped;
        BYTE *at = (BYTE *)a->base+((SIZE_T)i<<30);
        if (i+1 < ARRAYSIZE(r->tile)) {
            if (!VirtualFree(at,tile_bytes(i),MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER)) break;
            a->split = TRUE;
        }
        if (!xmem_wc_slot(at,tile_bytes(i),r->tile[i],0,0x2004)) break;
        a->tile_mapped++;
    }
    if (a->tile_mapped == ARRAYSIZE(r->tile)) return ERROR_SUCCESS;
    DWORD err = GetLastError(), cleanup = unmap_tile(a);
    return cleanup ? cleanup : err;
}

__declspec(dllexport) PVOID WINAPI XMemMapPhysicalPages(PVOID addr, ULONG_PTR n, PULONG_PTR pages)
{
    if (!addr || ((UINT_PTR)addr % XMEM_PAGE) || !n || n > SIZE_MAX / XMEM_PAGE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    SIZE_T size = n * XMEM_PAGE;
    DWORD err = ERROR_INVALID_ADDRESS;
    AcquireSRWLockExclusive(&page_lock);
    for (Arena *a = arenas; a; a = a->next) {
        if (a->scatter && a->views && (UINT_PTR)addr-(UINT_PTR)a->base<a->size) goto done;
        if (!a->tile || (UINT_PTR)addr-(UINT_PTR)a->base >= a->size) continue;
        if (addr == a->base && size == a->size) err = map_tile(a,pages);
        goto done;
    }
    PageView **slot = &views;
    while (*slot && (*slot)->address != addr) slot = &(*slot)->next;
    if (*slot) {
        PageView *v = *slot;
        if (pages || size != v->size) err = ERROR_NOT_SUPPORTED;
        else if (!(v->run->wc ? xmem_wc_slot(addr, size, NULL, 0, 0) :
                   unmap2(GetCurrentProcess(), addr, MEM_PRESERVE_PLACEHOLDER))) err = GetLastError();
        else {
            *slot = v->next;
            --v->run->views;
            err = ERROR_SUCCESS;
            Arena *a = v->arena;
            if (a->descriptor && !occupy_descriptor(addr, size)) err = GetLastError();
            if (!--a->views && a->split) {
                if (VirtualFree(a->base, a->size, MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS))
                    a->split = FALSE;
                else err = GetLastError();
            }
            HeapFree(GetProcessHeap(), 0, v);
        }
    } else if (pages) {
        Arena *arena = arenas;
        while (arena && (UINT_PTR)addr - (UINT_PTR)arena->base >= arena->size)
            arena = arena->next;
        if (!arena || size > arena->size - ((UINT_PTR)addr - (UINT_PTR)arena->base)) goto done;
        if (arena->descriptor && (addr != arena->base || size != arena->size ||
                                  !descriptor_marker(addr))) goto done;
        MEMORY_BASIC_INFORMATION m;
        if (VirtualQuery(addr, &m, sizeof m) && m.State == MEM_RESERVE &&
            (UINT_PTR)addr - (UINT_PTR)m.AllocationBase <= SIZE_MAX - size &&
            m.RegionSize >= size) {
            MEMORY_BASIC_INFORMATION start;
            if (!VirtualQuery(m.AllocationBase, &start, sizeof start) ||
                (UINT_PTR)addr - (UINT_PTR)m.AllocationBase + size > start.RegionSize)
                goto done;
            BOOL split = addr != m.AllocationBase || size != start.RegionSize;
            err = ERROR_INVALID_PARAMETER;
            for (PageRun *r = runs; r; r = r->next) {
                if (r->tile[0]) continue;
                ULONG_PTR offset = pages[0] - r->first;
                if (offset >= r->count || n > r->count - offset) continue;
                HANDLE section = r->section;
                if (!section) {
                    unsigned j = 0;
                    while (j < ARRAYSIZE(layout) && offset >= layout[j].pages) offset -= layout[j++].pages;
                    if (j == ARRAYSIZE(layout) || n > layout[j].pages-offset ||
                        !!(layout[j].policy & PAGE_WRITECOMBINE) != arena->wc ||
                        (arena->descriptor ? arena->policy != layout[j].policy : (n != 1 || !arena->wc || layout[j].policy != 0x2404))) {
                        err = ERROR_NOT_SUPPORTED; break;
                    }
                    section = r->parts[j];
                } else if (r->wc != arena->wc || (arena->descriptor && arena->policy != 0x2404) || (r->wc && !arena->descriptor && n != 1)) { err = ERROR_NOT_SUPPORTED; break; }
                if (!consecutive(pages, n, 1)) { err = ERROR_NOT_SUPPORTED; break; }
                PageView *v = HeapAlloc(GetProcessHeap(), 0, sizeof *v);
                if (!v) { err = ERROR_NOT_ENOUGH_MEMORY; break; }
                if (arena->scatter && (!descriptor_marker(arena->base) ||
                    !VirtualFree(arena->base,arena->size,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER))) {
                    HeapFree(GetProcessHeap(),0,v); err=ERROR_INVALID_ADDRESS; break;
                }
                if ((split || arena->descriptor) && !VirtualFree(addr, size, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) {
                    err = GetLastError();
                    if (arena->scatter) restore_gap(arena->base,arena->size,FALSE);
                    HeapFree(GetProcessHeap(), 0, v);
                    break;
                }
                BOOL mapped = r->wc ? xmem_wc_slot(addr, size, section, offset * XMEM_PAGE, arena->policy) :
                    map3(r->section, GetCurrentProcess(), addr, offset * XMEM_PAGE, size,
                         MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, NULL, 0) != NULL;
                if (!mapped) {
                    err = GetLastError();
                    if (arena->descriptor && !occupy_descriptor(addr, size)) err = GetLastError();
                    if (arena->scatter) restore_gap(arena->base,arena->size,split);
                    else if (split)
                        VirtualFree(start.AllocationBase, start.RegionSize,
                                    MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS);
                    HeapFree(GetProcessHeap(), 0, v);
                    break;
                }
                *v = (PageView){views, r, addr, size, arena};
                /* Subsequent ordinary page views use the existing unmarked arena lifecycle. */
                arena->scatter=FALSE;
                ++arena->views;
                arena->split |= split;
                views = v;
                ++r->views;
                err = ERROR_SUCCESS;
                break;
            }
        }
    }
done:
    ReleaseSRWLockExclusive(&page_lock);
    if (err) SetLastError(err);
    return err ? NULL : addr;
}

#include "scatter.h"
