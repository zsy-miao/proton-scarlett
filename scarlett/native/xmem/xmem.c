#include <windows.h>
#include <stdint.h>
#include "pages.h"

#if !defined(__x86_64__) || !defined(_WIN64)
#error This DLL implements only the Windows x64 ABI.
#endif

#define XMEM_CPU 0x00000001ULL
void *xmem_graphics_alloc(void *base, SIZE_T size, BOOL wc);
void *xmem_control64_alloc(void);

typedef struct {
    PVOID (WINAPI *alloc)(SIZE_T, ULONGLONG);
    VOID (WINAPI *free)(PVOID, ULONGLONG);
} AllocationHooks;

static AllocationHooks hooks;
static BOOL invalid_hooks;

BOOL WINAPI DllMain(HINSTANCE dll, DWORD reason, LPVOID reserved)
{
    (void)dll;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        const AllocationHooks *exported = (const void *)
            GetProcAddress(GetModuleHandleW(NULL), "__XMemAllocationHooks__");
        if (exported) {
            hooks = *exported;
            invalid_hooks = !hooks.alloc || !hooks.free;
        }
        bind_page_apis();
    }
    return TRUE;
}

typedef struct {
    PVOID base;
    ULONGLONG attributes;
} Block;

__declspec(dllexport) PVOID WINAPI XMemAllocDefault(SIZE_T size, ULONGLONG attributes)
{
    /* XALLOC_ATTRIBUTES: ObjectType[13:0], AllocatorId[23:16], Alignment[28:24]. */
    unsigned exponent = (unsigned)((attributes >> 24) & 31);
    if ((attributes & ~0x1fff3fffULL) || exponent > 17 || exponent == 1 || exponent == 3) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return NULL;
    }
    SIZE_T align = (SIZE_T)1 << (exponent < 4 ? 4 : exponent);
    SIZE_T extra = sizeof(Block) + align - 1;
    if (size > SIZE_MAX - extra) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    PVOID base = HeapAlloc(GetProcessHeap(), 0, size + extra);
    if (!base) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    Block *user = (Block *)(((UINT_PTR)base + extra) & ~(align - 1));
    user[-1] = (Block){base, attributes};
    return user;
}

__declspec(dllexport) VOID WINAPI XMemFreeDefault(PVOID address, ULONGLONG attributes)
{
    if (!address)
        return;
    Block *block = (Block *)address - 1;
    if (block->attributes != attributes) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return;
    }
    HeapFree(GetProcessHeap(), 0, block->base);
}

__declspec(dllexport) PVOID WINAPI XMemAlloc(SIZE_T size, ULONGLONG attributes)
{
    if (invalid_hooks) {
        SetLastError(ERROR_INVALID_DATA);
        return NULL;
    }
    return hooks.alloc ? hooks.alloc(size, attributes) : XMemAllocDefault(size, attributes);
}

__declspec(dllexport) VOID WINAPI XMemFree(PVOID address, ULONGLONG attributes)
{
    if (invalid_hooks) {
        SetLastError(ERROR_INVALID_DATA);
        return;
    }
    if (hooks.free)
        hooks.free(address, attributes);
    else
        XMemFreeDefault(address, attributes);
}

__declspec(dllexport) BOOL WINAPI XMemIsToolsMemAvailable(void)
{
    return FALSE;
}

__declspec(dllexport) PVOID WINAPI XMemVirtualAlloc(PVOID base, SIZE_T size,
        DWORD type, ULONGLONG flags, DWORD protect)
{
    if (!base && size == 65536 && type == 0x20401000 && flags == 0x283 && protect == 0x42404)
        return xmem_control64_alloc();
    if (type == 0x20003000 && flags == 0x183 && protect == 0x22004)
        return xmem_graphics_alloc(base, size, FALSE);
    if (!base && size == 0x89700000 && type == 0x20003000 && flags == 0x283 && protect == 0x2404)
        return xmem_graphics_alloc(NULL, size, TRUE);
    BOOL cpu_commit64 = type == 0x20403000 && flags == 0x181 && protect == PAGE_READWRITE;
    BOOL wc_arena = type == 0x20402000 && flags == 0x23 && protect == 0x2404;
    if (cpu_commit64 || wc_arena || (type == (0x20400000u | MEM_RESERVE) &&
        ((flags == (XMEM_CPU | 0x20) && protect == PAGE_READWRITE) ||
         (flags == 3 && protect == 0x801)))) {
        if (!size || ((size | (UINT_PTR)base) & (XMEM_PAGE - 1))) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return NULL;
        }
        if (cpu_commit64)
            return VirtualAlloc(base, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        /* The GPU form is VA only. It must never accept a section view. */
        if (flags == 3)
            return VirtualAlloc(base, size, MEM_RESERVE, PAGE_NOACCESS);
        if (!reserve2 || !map3 || !unmap2) {
            SetLastError(ERROR_NOT_SUPPORTED);
            return NULL;
        }
        return reserve_arena(base, size, wc_arena);
    }
    if (flags != XMEM_CPU ||
        (type != MEM_RESERVE && type != MEM_COMMIT && type != (MEM_RESERVE | MEM_COMMIT)) ||
        (protect != PAGE_READONLY && protect != PAGE_READWRITE)) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return NULL;
    }
    if (base && type == MEM_COMMIT) {
        MEMORY_BASIC_INFORMATION m;
        if (VirtualQuery(base, &m, sizeof m) && m.AllocationProtect == PAGE_NOACCESS) {
            SetLastError(ERROR_NOT_SUPPORTED);
            return NULL;
        }
    }
    return VirtualAlloc(base, size, type, protect);
}
