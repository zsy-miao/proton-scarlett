/* Synthetic native x64 CPU acceptance test for the Scarlett section v1 patch.
 * x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror test-section.c -o test.exe */
#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

#define CREATE_V1  0x53430001
#define ACQUIRE_V1 0x53430002
#define NOT_MAPPED ((LONG)0xc0000019)
enum { SPAN = 2 << 20, ROUNDS = 256 };

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "line %d: %s (last error %lu)\n", __LINE__, #x, GetLastError()); \
    ExitProcess(1); } } while (0)

typedef struct {
    SIZE_T size;
    BYTE *base;
    SIZE_T view_size, offset;
    HANDLE section;
} Info;
_Static_assert(sizeof(Info) == 40, "requires x64 ABI");
static LONG (NTAPI *query)(HANDLE, const void *, ULONG, void *, SIZE_T, SIZE_T *);

static LONG call(ULONG op, const void *addr, SIZE_T n, Info *info)
{
    SIZE_T returned = 0;
    *info = (Info){.size = n};
    LONG st = query(GetCurrentProcess(), addr, op, info, sizeof *info, &returned);
    if (!st) CHECK(returned == sizeof *info && info->size == n);
    return st;
}

static BYTE *create(BYTE *at)
{
    Info i;
    LONG st = call(CREATE_V1, at, SPAN, &i);
    if (st) fprintf(stderr, "create(%p): NTSTATUS %#lx\n", (void *)at, (unsigned long)st);
    CHECK(!st && i.base && (!at || at == i.base) && !((UINT_PTR)i.base % SPAN));
    CHECK(i.view_size == SPAN && !i.offset && !i.section);
    return i.base;
}

static HANDLE acquire_last(BYTE *base)
{
    Info i;
    CHECK(!call(ACQUIRE_V1, base + SPAN - 1, 1, &i));
    CHECK(i.base == base && i.view_size == SPAN && i.offset == SPAN - 1 && i.section);
    return i.section;
}

static BYTE *map(HANDLE section)
{
    BYTE *p = MapViewOfFile(section, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, SPAN);
    CHECK(p);
    return p;
}

static void test_lifetime(void)
{
    Info i;
    BYTE *base = create(NULL);
    HANDLE old = acquire_last(base);
    BYTE *alias = map(old);
    CHECK(alias != base);
    base[SPAN - 1] = 0x52;
    CHECK(alias[SPAN - 1] == 0x52);
    alias[SPAN - 1] = 0xa6;
    CHECK(base[SPAN - 1] == 0xa6);
    CHECK(UnmapViewOfFile(alias));

    CHECK(call(ACQUIRE_V1, base, 0, &i) < 0);
    CHECK(call(ACQUIRE_V1, base + SPAN - 1, 2, &i) < 0);
    CHECK(call(ACQUIRE_V1, base + 1, SIZE_MAX, &i) < 0);
    CHECK(!VirtualFree(base + 1, 0, MEM_RELEASE));
    CHECK(!VirtualFree(base + 65536, 0, MEM_RELEASE));
    CHECK(!VirtualFree(base, SPAN, MEM_DECOMMIT));
    CHECK(!VirtualFree(base, SPAN, MEM_RELEASE));
    CHECK(CloseHandle(acquire_last(base)) && base[SPAN - 1] == 0xa6);
    CHECK(VirtualFree(base, 0, MEM_RELEASE));
    MEMORY_BASIC_INFORMATION m;
    CHECK(VirtualQuery(base, &m, sizeof m) == sizeof m && m.State == MEM_FREE);
    CHECK(call(ACQUIRE_V1, base, 1, &i) == NOT_MAPPED);

    CHECK(VirtualAlloc(base, SPAN, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) == base);
    CHECK(call(ACQUIRE_V1, base, 1, &i) == NOT_MAPPED);
    CHECK(VirtualFree(base, 0, MEM_RELEASE));
    HANDLE ordinary = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, SPAN, NULL);
    CHECK(ordinary && MapViewOfFileEx(ordinary, FILE_MAP_WRITE, 0, 0, SPAN, base) == base);
    CHECK(call(ACQUIRE_V1, base, 1, &i) == NOT_MAPPED);
    CHECK(!VirtualFree(base, 0, MEM_RELEASE) && GetLastError() == ERROR_INVALID_PARAMETER);
    CHECK(UnmapViewOfFile(base) && CloseHandle(ordinary));

    CHECK(create(base) == base);
    HANDLE fresh = acquire_last(base);
    BYTE *prior = map(old), *current = map(fresh);
    CHECK(prior[SPAN - 1] == 0xa6 && current[SPAN - 1] == 0);
    base[SPAN - 1] = 0x7b;
    CHECK(current[SPAN - 1] == 0x7b && prior[SPAN - 1] == 0xa6);
    CHECK(VirtualFree(base, 0, MEM_RELEASE));
    CHECK(UnmapViewOfFile(prior) && UnmapViewOfFile(current));
    CHECK(CloseHandle(old) && CloseHandle(fresh));
}

typedef struct {
    SYNCHRONIZATION_BARRIER barrier;
    BYTE *base;
    Info result;
    LONG status;
} Race;

static void compete(Race *r, BOOL acquire)
{
    EnterSynchronizationBarrier(&r->barrier, 0);
    if (acquire)
        r->status = call(ACQUIRE_V1, r->base + SPAN - sizeof(DWORD), sizeof(DWORD), &r->result);
    else
        CHECK(VirtualFree(r->base, 0, MEM_RELEASE));
    EnterSynchronizationBarrier(&r->barrier, 0);
}

static DWORD WINAPI worker(void *arg)
{
    for (unsigned n = 0; n < ROUNDS; ++n) compete(arg, !(n & 1));
    return 0;
}

static void test_race(void)
{
    Race r = {0};
    unsigned wins = 0;
    CHECK(InitializeSynchronizationBarrier(&r.barrier, 2, -1));
    HANDLE thread = CreateThread(NULL, 0, worker, &r, 0, NULL);
    CHECK(thread);
    for (unsigned n = 0; n < ROUNDS; ++n) {
        DWORD marker = 0xface0000 + n;
        r.base = create(NULL);
        *(DWORD *)(r.base + SPAN - sizeof marker) = marker;
        compete(&r, n & 1);
        /* Both calls have returned; only now can an alias or new generation use the VA. */
        if (!r.status) {
            CHECK(r.result.base == r.base && r.result.view_size == SPAN &&
                  r.result.offset == SPAN - sizeof marker && r.result.section);
            BYTE *alias = map(r.result.section);
            CHECK(*(DWORD *)(alias + r.result.offset) == marker);
            CHECK(UnmapViewOfFile(alias) && CloseHandle(r.result.section));
            ++wins;
        } else CHECK(r.status == NOT_MAPPED);
    }
    CHECK(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0 && CloseHandle(thread));
    CHECK(DeleteSynchronizationBarrier(&r.barrier));
    printf("PASS section CPU: bounds, release, exact VA reuse; %u races (%u acquired, %u released first)\n",
           ROUNDS, wins, ROUNDS - wins);
}

int main(void)
{
    query = (void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryVirtualMemory");
    CHECK(query);
    test_lifetime();
    test_race();
    return 0;
}
