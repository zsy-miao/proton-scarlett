#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

__declspec(dllimport) PVOID WINAPI XMemAlloc(SIZE_T, ULONGLONG);
__declspec(dllimport) PVOID WINAPI XMemAllocDefault(SIZE_T, ULONGLONG);
__declspec(dllimport) VOID WINAPI XMemFree(PVOID, ULONGLONG);
__declspec(dllimport) VOID WINAPI XMemFreeDefault(PVOID, ULONGLONG);

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s failed\n", __LINE__, #x); exit(1); } } while (0)

#if HOOK_MODE
static unsigned allocs, frees;
static BYTE private_block;
static const ULONGLONG outer = 0xcafef00d12344321ULL;
static const ULONGLONG inner = 0x0c610123;

static PVOID WINAPI allocate(SIZE_T size, ULONGLONG attr)
{
    CHECK(++allocs <= 2);
    if (attr == inner) {
        CHECK(size == 73);
        return XMemAllocDefault(size, attr);
    }
    CHECK(size == 0x100000007ULL && attr == outer);
    BYTE *nested = XMemAlloc(73, inner);
    CHECK(nested && !((UINT_PTR)nested % 4096));
    nested[72] = 42;
    XMemFree(nested, inner);
    return &private_block;
}

#if HOOK_MODE == 1
static VOID WINAPI release(PVOID p, ULONGLONG attr)
{
    CHECK(++frees <= 2);
    if (attr == inner) {
        CHECK(((BYTE *)p)[72] == 42);
        XMemFreeDefault(p, attr);
    } else {
        CHECK(p == &private_block && attr == outer);
    }
}
#endif

__declspec(dllexport) struct {
    PVOID (WINAPI *alloc)(SIZE_T, ULONGLONG);
    VOID (WINAPI *free)(PVOID, ULONGLONG);
} __XMemAllocationHooks__ = {
    allocate,
#if HOOK_MODE == 1
    release
#else
    NULL
#endif
};
#endif

int main(void)
{
    const ULONGLONG labels = 0x005a3abc; /* AllocatorId and ObjectType */
    for (unsigned e = 0; e <= 17; ++e) {
        if (e == 1 || e == 3) continue;
        ULONGLONG attr = labels | ((ULONGLONG)e << 24);
        BYTE *p = XMemAllocDefault(123, attr);
        CHECK(p && !((UINT_PTR)p % (1u << (e < 4 ? 4 : e))));
        p[122] = 93;
        SetLastError(0);
        XMemFreeDefault(p, attr ^ 1);
        CHECK(GetLastError() == ERROR_INVALID_PARAMETER && p[122] == 93);
        XMemFreeDefault(p, attr);
    }
    const ULONGLONG unsupported[] = {1ULL<<14, 1ULL<<29, 1ULL<<33, 1ULL<<63, 31ULL<<24};
    for (unsigned i = 0; i < sizeof unsupported / sizeof *unsupported; ++i) {
        CHECK(!XMemAllocDefault(32, unsupported[i]));
        CHECK(GetLastError() == ERROR_NOT_SUPPORTED);
    }
    CHECK(!XMemAllocDefault((SIZE_T)-1, labels));
    CHECK(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
#if HOOK_MODE == 1
    /* Mutating the export after load must not replace either bound callback. */
    __XMemAllocationHooks__.alloc = NULL;
    __XMemAllocationHooks__.free = NULL;
    CHECK(allocs == 0 && frees == 0);
    PVOID p = XMemAlloc(0x100000007ULL, outer);
    CHECK(p == &private_block && allocs == 2 && frees == 1);
    XMemFree(p, outer);
    CHECK(allocs == 2 && frees == 2);
    puts("EXE hooks: load-time binding, 64-bit forwarding, nested alloc/free passed");
#elif HOOK_MODE == 2
    CHECK(!XMemAlloc(123, labels) && GetLastError() == ERROR_INVALID_DATA);
    SetLastError(0);
    XMemFree(&private_block, labels);
    CHECK(GetLastError() == ERROR_INVALID_DATA && !allocs && !frees);
    puts("incomplete EXE hook pair rejected");
#else
    BYTE *p = XMemAlloc(123, labels | (12ULL << 24));
    CHECK(p && !((UINT_PTR)p % 4096));
    p[122] = 11;
    XMemFree(p, labels | (12ULL << 24));
    puts("no hooks: default fallback, alignment, labels and pairing passed");
#endif
    return 0;
}
