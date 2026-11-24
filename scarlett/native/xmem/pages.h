/* Private physical-page implementation included by xmem.c. */
#define XMEM_PAGE ((SIZE_T)65536)
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
} PageRun;

typedef struct PageView {
    struct PageView *next;
    PageRun *run;
    PVOID address;
    SIZE_T size;
    struct Arena *arena;
} PageView;

typedef struct Arena {
    struct Arena *next;
    PVOID base;
    SIZE_T size;
    ULONG_PTR views;
    BOOL split;
} Arena;

static SRWLOCK page_lock = SRWLOCK_INIT;
static PageRun *runs;
static PageView *views;
static Arena *arenas;
static ULONG_PTR next_page = 1;

static PVOID reserve_cpu_arena(PVOID base, SIZE_T size)
{
    Arena *a = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *a);
    if (!a) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    AcquireSRWLockExclusive(&page_lock);
    /* VirtualFree releases the VA directly; discard its bookkeeping on next reservation. */
    for (Arena **p = &arenas; *p;) {
        Arena *old = *p;
        MEMORY_BASIC_INFORMATION m;
        if (!old->views && VirtualQuery(old->base, &m, sizeof m) &&
            (m.State != MEM_RESERVE || m.AllocationBase != old->base || m.RegionSize != old->size)) {
            *p = old->next;
            HeapFree(GetProcessHeap(), 0, old);
        } else p = &old->next;
    }
    a->base = reserve2(NULL, base, size, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
                       PAGE_NOACCESS, NULL, 0);
    PVOID result = a->base;
    DWORD err = GetLastError();
    if (result) {
        a->size = size;
        a->next = arenas;
        arenas = a;
    } else HeapFree(GetProcessHeap(), 0, a);
    ReleaseSRWLockExclusive(&page_lock);
    if (!result) SetLastError(err);
    return result;
}

__declspec(dllexport) BOOL WINAPI XMemAllocatePhysicalPages(ULONGLONG flags, PULONG_PTR count, PULONG_PTR pages)
{
    ULONG_PTR n = count ? *count : 0;
    if (count) *count = 0;
    if (!n || !pages || n > SIZE_MAX / XMEM_PAGE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (flags || !reserve2 || !map3 || !unmap2) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    SIZE_T size = n * XMEM_PAGE;
    PageRun *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *r);
    if (!r) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    r->section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                     (DWORD)(size >> 32), (DWORD)size, NULL);
    if (!r->section) {
        DWORD err = GetLastError();
        HeapFree(GetProcessHeap(), 0, r);
        SetLastError(err);
        return FALSE;
    }
    AcquireSRWLockExclusive(&page_lock);
    if (n > UINTPTR_MAX - next_page) {
        ReleaseSRWLockExclusive(&page_lock);
        CloseHandle(r->section);
        HeapFree(GetProcessHeap(), 0, r);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    r->first = next_page;
    next_page += n;
    r->count = n;
    r->next = runs;
    runs = r;
    for (ULONG_PTR i = 0; i < n; ++i) pages[i] = r->first + i;
    *count = n;
    ReleaseSRWLockExclusive(&page_lock);
    return TRUE;
}

static BOOL consecutive(PULONG_PTR pages, ULONG_PTR n)
{
    for (ULONG_PTR i = 1; i < n; ++i)
        if (pages[i] != pages[0] + i) return FALSE;
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
            if (r->count != n || !consecutive(pages, n)) break;
            if (r->views) { err = ERROR_BUSY; break; }
            *p = r->next;
            CloseHandle(r->section);
            HeapFree(GetProcessHeap(), 0, r);
            err = ERROR_SUCCESS;
            break;
        }
        ReleaseSRWLockExclusive(&page_lock);
    }
    if (err) SetLastError(err);
    return !err;
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
    PageView **slot = &views;
    while (*slot && (*slot)->address != addr) slot = &(*slot)->next;
    if (*slot) {
        PageView *v = *slot;
        if (pages || size != v->size) err = ERROR_NOT_SUPPORTED;
        else if (!unmap2(GetCurrentProcess(), addr, MEM_PRESERVE_PLACEHOLDER)) err = GetLastError();
        else {
            *slot = v->next;
            --v->run->views;
            err = ERROR_SUCCESS;
            Arena *a = v->arena;
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
                ULONG_PTR offset = pages[0] - r->first;
                if (offset >= r->count || n > r->count - offset) continue;
                if (!consecutive(pages, n)) { err = ERROR_NOT_SUPPORTED; break; }
                PageView *v = HeapAlloc(GetProcessHeap(), 0, sizeof *v);
                if (!v) { err = ERROR_NOT_ENOUGH_MEMORY; break; }
                if (split && !VirtualFree(addr, size, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) {
                    err = GetLastError();
                    HeapFree(GetProcessHeap(), 0, v);
                    break;
                }
                if (!map3(r->section, GetCurrentProcess(), addr, offset * XMEM_PAGE, size,
                          MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, NULL, 0)) {
                    err = GetLastError();
                    if (split)
                        VirtualFree(start.AllocationBase, start.RegionSize,
                                    MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS);
                    HeapFree(GetProcessHeap(), 0, v);
                    break;
                }
                *v = (PageView){views, r, addr, size, arena};
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
