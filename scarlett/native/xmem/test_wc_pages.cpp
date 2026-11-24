#include "gpu_buffer.hpp"
#include <cstdio>

extern "C" {
__declspec(dllimport) BOOL WINAPI XMemAllocatePhysicalPages(ULONGLONG, PULONG_PTR, PULONG_PTR);
__declspec(dllimport) BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR, PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemMapPhysicalPages(void *, ULONG_PTR, PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *, SIZE_T, DWORD, ULONGLONG, DWORD);
}
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s (error %lu)\n", __LINE__, #x, GetLastError()); return 1; } } while (0)

int main()
{
    constexpr SIZE_T page = 65536, count = 0x2c60;
    static ULONG_PTR ids[count];
    ULONG_PTR n = count;
    CHECK(XMemAllocatePhysicalPages(0x280, &n, ids) && n == count && ids[0]);
    for (SIZE_T i = 1; i < count; ++i) CHECK(ids[i] == ids[0] + i);
    auto arena = (BYTE *)XMemVirtualAlloc(nullptr, 3*page, 0x20402000, 0x23, 0x2404);
    auto cpu = XMemVirtualAlloc(nullptr, page, 0x20402000, 0x21, PAGE_READWRITE);
    CHECK(arena && cpu && !XMemMapPhysicalPages(cpu, 1, ids));
    CHECK(!XMemMapPhysicalPages(arena, 2, ids));
    ULONG_PTR cached, one = 1;
    CHECK(XMemAllocatePhysicalPages(0, &one, &cached) && one == 1);
    CHECK(!XMemMapPhysicalPages(arena, 1, &cached) && XMemFreePhysicalPages(1, &cached));
#ifdef TEST_MISSING_SLOT_ABI
    CHECK(!XMemMapPhysicalPages(arena + page, 1, ids + count - 1));
    CHECK(GetLastError() == ERROR_NOT_SUPPORTED);
    MEMORY_BASIC_INFORMATION m;
    CHECK(VirtualQuery(arena, &m, sizeof m) && m.State == MEM_RESERVE && m.RegionSize == 3*page);
    CHECK(XMemFreePhysicalPages(count, ids) && VirtualFree(arena, 0, MEM_RELEASE));
    CHECK(VirtualFree(cpu, 0, MEM_RELEASE));
    puts("PASS: 710 MiB WC run allocated/freed; missing slot ABI rejected, arena intact");
#else
    auto first = (DWORD *)arena, middle = (DWORD *)(arena + page), last = (DWORD *)(arena + 2*page);
    CHECK(XMemMapPhysicalPages(middle, 1, ids + count - 1) == middle);
    CHECK(XMemMapPhysicalPages(first, 1, ids) == first);
    CHECK(XMemMapPhysicalPages(last, 1, ids + count - 1) == last);
    CHECK(!XMemFreePhysicalPages(count, ids) && GetLastError() == ERROR_BUSY);
    MEMORY_BASIC_INFORMATION m;
    CHECK(VirtualQuery(middle, &m, sizeof m) && m.Protect == (PAGE_READWRITE | PAGE_WRITECOMBINE));
    first[1] = 123;
    middle[1] = 456;
    MemoryBarrier();
    CHECK(first[1] == 123 && last[1] == 456);
    ID3D12Device3 *device = nullptr;
    CHECK(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))));
    ImportedBuffer held;
    SIZE_T offset;
    CHECK(SUCCEEDED(held.acquire(device, middle + 1, 4, &offset)) && offset == (count-1)*page+4);
    auto gpu_alias = (volatile DWORD *)((BYTE *)held.cpu() + offset);
    CHECK(*gpu_alias == 456);
    *gpu_alias = 789;
    MemoryBarrier();
    CHECK(last[1] == 789 && middle[1] == 789);
    CHECK(XMemMapPhysicalPages(middle, 1, nullptr) == middle);
    CHECK(XMemMapPhysicalPages(middle, 1, ids) == middle && middle[1] == 123 && *gpu_alias == 789);
    CHECK(XMemMapPhysicalPages(middle, 1, nullptr) == middle);
    CHECK(XMemMapPhysicalPages(first, 1, nullptr) == first);
    CHECK(XMemMapPhysicalPages(last, 1, nullptr) == last);
    CHECK(VirtualQuery(arena, &m, sizeof m) && m.State == MEM_RESERVE && m.RegionSize == 3*page);
    CHECK(XMemFreePhysicalPages(count, ids) && !XMemMapPhysicalPages(first, 1, ids));
    CHECK(VirtualFree(arena, 0, MEM_RELEASE) && VirtualFree(cpu, 0, MEM_RELEASE));
    CHECK(*gpu_alias == 789);
    device->Release();
    puts("PASS: 710 MiB WC run, selected page aliases, retained GPU backing and teardown");
#endif
    return 0;
}
