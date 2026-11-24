/* Synthetic Proton release/alias test. Never submit GPU work against the public VA. */
#include "gpu_buffer.hpp"
#include <cstdio>
#ifdef TEST_RADV_WB_RANGE
#define TEST_WB563_SECTION
#endif
#if defined(TEST_WC_SECTION) || defined(TEST_WB563_SECTION)
#define TEST_MARKED_SECTION
#endif
#ifdef TEST_MARKED_SECTION
extern "C" __declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *, SIZE_T, DWORD, ULONGLONG, DWORD);
#endif

// On failure after submission, tear down the process rather than unwind a busy heap.
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "line %d: %s, last error %lu\n", \
    __LINE__, #expr, GetLastError()); ExitProcess(1); } } while (0)
#define HR(call) do { HRESULT h = (call); if (FAILED(h)) { \
    std::fprintf(stderr, "%s failed: %#lx\n", #call, (unsigned long)h); ExitProcess(1); } } while (0)

static MEMORY_BASIC_INFORMATION query(void *p)
{
    MEMORY_BASIC_INFORMATION m;
    CHECK(VirtualQuery(p, &m, sizeof m) == sizeof m);
    return m;
}

static void expect_mapped_release_failure(void *p)
{
    CHECK(query(p).Type == MEM_MAPPED);
    SetLastError(0);
    CHECK(!VirtualFree(p, 0, MEM_RELEASE));
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    CHECK(query(p).State == MEM_COMMIT);
}

#ifdef TEST_WC_SECTION
struct Race {
    SYNCHRONIZATION_BARRIER gate;
    void *base;
    scarlett_section_info acquired;
    HRESULT hr;
};
static void compete(Race *race, bool acquire)
{
    EnterSynchronizationBarrier(&race->gate,0);
    if (acquire) race->hr = scarlett_query_section(0x53430005,race->base,4,&race->acquired);
    else CHECK(VirtualFree(race->base,0,MEM_RELEASE));
    EnterSynchronizationBarrier(&race->gate,0);
}
static DWORD WINAPI racer(void *arg)
{
    for (unsigned i=0;i<64;i++) compete((Race *)arg, i%2);
    return 0;
}
static void test_wc_race(HANDLE backing, SIZE_T size)
{
    Race race = {};
    CHECK(InitializeSynchronizationBarrier(&race.gate,2,-1));
    HANDLE t=CreateThread(NULL,0,racer,&race,0,NULL);
    CHECK(t);
    unsigned wins=0;
    for (unsigned i=0;i<64;i++) {
        scarlett_section_info info;
        HR(scarlett_query_section_handle(0x53430004,race.base,size,&info,backing));
        race.base=info.base;
        *(volatile DWORD*)race.base=0x11133000+i;
        MemoryBarrier();
        compete(&race,!(i%2));
        CHECK(query(race.base).State==MEM_FREE);
        if (SUCCEEDED(race.hr)) {
            HR(scarlett_query_section_handle(0x53430004,race.base,size,&info,race.acquired.section));
            CHECK(*(volatile DWORD*)info.base==0x11133000+i);
            CHECK(VirtualFree(info.base,0,MEM_RELEASE) && CloseHandle(race.acquired.section));
            ++wins;
        } else CHECK(race.hr==HRESULT_FROM_NT((LONG)0xc0000019));
    }
    CHECK(WaitForSingleObject(t,10000)==WAIT_OBJECT_0 && CloseHandle(t));
    CHECK(DeleteSynchronizationBarrier(&race.gate));
    printf("PASS WC acquire/release: 64 races (%u acquired)\n",wins);
}
#endif

static int exercise(SIZE_T size)
{
#ifdef TEST_WC_UNSUPPORTED
    CHECK(!XMemVirtualAlloc(nullptr,0x89700000,0x20003000,0x283,0x2404));
    CHECK(GetLastError()!=ERROR_SUCCESS);
    puts("PASS WC: unavailable backing rejected");
    return 0;
#endif
    const SIZE_T align = 2u << 20;
    HMODULE kernel = GetModuleHandleW(L"kernelbase.dll");
    auto alloc2 = (Alloc2)(void *)GetProcAddress(kernel, "VirtualAlloc2");
    CHECK(alloc2);
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, nullptr);
    CHECK(section);
    auto title = (DWORD *)map_aligned_section(section, size);
    CHECK(title && !((UINT_PTR)title % align));
    expect_mapped_release_failure(title);
    CHECK(UnmapViewOfFile(title));

    // VirtualAlloc2 replacement is releasable, but it has no section backing.
    CHECK(alloc2(nullptr, title, size, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
                 PAGE_NOACCESS, nullptr, 0) == title);
    CHECK(alloc2(nullptr, title, size, MEM_RESERVE | MEM_COMMIT | MEM_REPLACE_PLACEHOLDER,
                 PAGE_READWRITE, nullptr, 0) == title);
    CHECK(query(title).Type == MEM_PRIVATE && VirtualFree(title, 0, MEM_RELEASE));
    CHECK(query(title).State == MEM_FREE);
    CHECK(MapViewOfFileEx(section, FILE_MAP_ALL_ACCESS, 0, 0, size, title) == title);
    expect_mapped_release_failure(title);
    // Opt in explicitly; keep the stock-Wine baseline runnable.
#ifdef TEST_MARKED_SECTION
    CHECK(UnmapViewOfFile(title));
    CHECK(CloseHandle(section));
#ifdef TEST_WC_SECTION
    title = (DWORD *)XMemVirtualAlloc(nullptr, size, 0x20003000, 0x283, 0x2404);
#else
    title = (DWORD *)XMemVirtualAlloc(nullptr, size, 0x20003000, 0x183, 0x22004);
#endif
    CHECK(title && !((UINT_PTR)title % align));
#endif
    auto original = query(title);
#ifdef TEST_WB563_SECTION
    scarlett_section_info info;
    CHECK(original.RegionSize == size && original.Protect == PAGE_READWRITE &&
          original.Type == MEM_MAPPED && original.State == MEM_COMMIT);
#ifdef TEST_RADV_WB_RANGE
    HR(scarlett_query_section(0x5343000c,title+size/4-1,4,&info));
    CHECK(info.base == title && info.view_size == size && info.offset == size-4 && info.section);
    CHECK(CloseHandle(info.section));
    CHECK(FAILED(scarlett_query_section(0x5343000c,title+size/4-1,5,&info)));
#else
    CHECK(FAILED(scarlett_query_section(0x53430001,nullptr,size,&info)));
#endif
    CHECK(FAILED(scarlett_query_section(0x53430002,title+size/4-1,5,&info)));
    CHECK(FAILED(scarlett_query_section(0x53430005,title,4,&info)));
#endif
#ifdef TEST_WC_SECTION
    CHECK(original.RegionSize == size && original.State == MEM_COMMIT && original.Type == MEM_MAPPED &&
          original.Protect == (PAGE_READWRITE|PAGE_WRITECOMBINE));
    scarlett_section_info held,bad;
    HR(scarlett_query_section(0x53430005,title+size/4-1,4,&held));
    CHECK(held.view_size == size && held.offset == size-4 && held.section);
    CHECK(FAILED(scarlett_query_section_handle(0x53430004,nullptr,size-65536,&bad,held.section)));
    HANDLE other = CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,65536,nullptr);
    CHECK(other && FAILED(scarlett_query_section_handle(0x53430004,nullptr,65536,&bad,other)) && CloseHandle(other));
    CHECK(FAILED(scarlett_query_section(0x53430005,title+size/4-1,5,&bad)));
    CHECK(FAILED(scarlett_query_section(0x53430002,title,4,&bad)));
    CHECK(!VirtualFree((BYTE*)title+1,0,MEM_RELEASE) && !UnmapViewOfFile(title));
#endif
    ID3D12Device3 *device;
    HR(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));
    ImportedBuffer allocation;
#ifdef TEST_MARKED_SECTION
    SIZE_T offset;
    HR(allocation.acquire(device, title + size / 4 - 1, 4, &offset));
    CHECK(offset == size - 4);
#else
    HR(allocation.create(device, section, size));
    CHECK(CloseHandle(section));
#endif
    auto cpu = (volatile DWORD *)allocation.cpu();
    auto shared = allocation.resource();
#ifdef TEST_WB563_SECTION
    D3D12_HEAP_PROPERTIES wb_props;
    D3D12_HEAP_FLAGS wb_flags;
    HR(shared->GetHeapProperties(&wb_props,&wb_flags));
    CHECK(wb_props.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_BACK && shared->GetDesc().Width == size);
#endif
#ifndef TEST_WC_SECTION
    CHECK(!((UINT_PTR)cpu % align));
#endif
    CHECK(cpu && cpu != title && shared->GetGPUVirtualAddress() &&
          allocation.translate((void *)(cpu + 1), 4) == shared->GetGPUVirtualAddress() + 4 &&
          !allocation.translate((void *)(cpu + size / 4 - 1), 5) && !allocation.translate(title, 4));
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 65536;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource *scratch;
    D3D12_HEAP_PROPERTIES props = {};
    props.Type = D3D12_HEAP_TYPE_DEFAULT;
    props.CreationNodeMask = props.VisibleNodeMask = 1;
    HR(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
                                       nullptr, IID_PPV_ARGS(&scratch)));
    D3D12_COMMAND_QUEUE_DESC q = {};
    q.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *alloc;
    ID3D12GraphicsCommandList *list;
    ID3D12Fence *gate, *retire;
    HR(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
    HR(device->CreateCommandAllocator(q.Type, IID_PPV_ARGS(&alloc)));
    HR(device->CreateCommandList(0, q.Type, alloc, nullptr, IID_PPV_ARGS(&list)));
    HR(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
    HR(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&retire)));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    CHECK(event);
    title[0] = 0xa103bd17;
    MemoryBarrier();
    CHECK(cpu[0] == title[0] && !cpu[size / 4 - 1]);
    list->CopyBufferRegion(scratch, 0, shared, 0, 4);
    HR(list->Close());
    ID3D12CommandList *batch[] = {list};
    queue->ExecuteCommandLists(1, batch);
    HR(queue->Signal(retire, 1));
    HR(retire->SetEventOnCompletion(1, event));
    CHECK(WaitForSingleObject(event, 15000) == WAIT_OBJECT_0);
    title[0] = 0;
    MemoryBarrier();
#ifdef TEST_WC_SECTION
    HR(alloc->Reset());
    HR(list->Reset(alloc,nullptr));
    list->CopyBufferRegion(shared,4,scratch,0,4);
    HR(list->Close());
    queue->ExecuteCommandLists(1,batch);
    HR(queue->Signal(gate,1));
    HR(gate->SetEventOnCompletion(1,event));
    CHECK(WaitForSingleObject(event,15000)==WAIT_OBJECT_0 && title[1]==0xa103bd17);
#endif

    HR(alloc->Reset());
    HR(list->Reset(alloc, nullptr));
    list->CopyBufferRegion(shared, size - 4, scratch, 0, 4);
    HR(list->Close());
    HR(queue->Wait(gate, 2));
    queue->ExecuteCommandLists(1, batch);
    HR(queue->Signal(retire, 2));

    // Let the queued GPU write retire after the public VA has been reused.
#ifdef TEST_MARKED_SECTION
    CHECK(VirtualFree(title, 0, MEM_RELEASE));
#ifdef TEST_WC_SECTION
    CHECK(query(title).State == MEM_FREE && FAILED(scarlett_query_section(0x53430005,title,4,&bad)));
#endif
#else
    CHECK(UnmapViewOfFile(title));
#endif
    HANDLE fresh = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, nullptr);
    CHECK(fresh && MapViewOfFileEx(fresh, FILE_MAP_ALL_ACCESS, 0, 0, size, title) == title);
    CHECK(CloseHandle(fresh));
    auto reused = query(title);
    CHECK(reused.BaseAddress == original.BaseAddress && reused.AllocationBase == original.AllocationBase &&
          reused.RegionSize == original.RegionSize && reused.State == original.State &&
           reused.Type == original.Type && reused.Protect == PAGE_READWRITE);
#ifdef TEST_WC_SECTION
    CHECK(FAILED(scarlett_query_section(0x53430005,title,4,&bad)));
    ImportedBuffer retained;
    HR(retained.wc(device,size,held.section,nullptr));
#endif
    title[size / 4 - 1] = 0x55aabb33;
    CHECK(retire->GetCompletedValue() == 1 && !cpu[size / 4 - 1]);
    HR(gate->Signal(2));
    HR(retire->SetEventOnCompletion(2, event));
    CHECK(WaitForSingleObject(event, 15000) == WAIT_OBJECT_0);
    HR(device->GetDeviceRemovedReason());
    CHECK(retire->GetCompletedValue() == 2 && cpu[size / 4 - 1] == 0xa103bd17 &&
          title[size / 4 - 1] == 0x55aabb33);
#ifdef TEST_WC_SECTION
    CHECK(((volatile DWORD*)retained.cpu())[size/4-1] == 0xa103bd17);
    test_wc_race(held.section,size);
    CHECK(CloseHandle(held.section));
    puts("PASS WC XMem: exact size, bounds, acquire before free/import after reuse, GPU retired into old backing");
#endif
    CHECK(UnmapViewOfFile(title));
    CHECK(CloseHandle(event));
    list->Release(); alloc->Release(); gate->Release(); retire->Release(); queue->Release();
    scratch->Release(); device->Release();
#ifdef TEST_MARKED_SECTION
    printf("PASS %#llx: ordinary VirtualFree released marked title view; "
           "GPU retired on old backing after exact public VA reuse\n",(unsigned long long)size);
#else
    puts("PASS: VirtualFree rejected both section views (87); private replacement released; "
         "GPU retired on old backing after exact public VA reuse");
#endif
    return 0;
}

int main()
{
#ifdef TEST_RADV_WB_RANGE
    const SIZE_T sizes[] = {0x25400000,0x23300000,0x23310000,0x253f0000,0x25410000,
                            0x30170000,0x3c000000,0x493f0000,0x49400000};
    const SIZE_T rejected[] = {0x232f0000,0x49410000,0x25400001,0x25401000};
    for (SIZE_T bytes : sizes) CHECK(exercise(bytes) == 0);
    for (SIZE_T bytes : rejected)
        CHECK(!XMemVirtualAlloc(nullptr,bytes,0x20003000,0x183,0x22004));
    puts("PASS RADV write-back range: nine sizes, final-word acquisition and invalid bounds");
    return 0;
#elif defined(TEST_WC_SECTION)
    return exercise(0x89700000);
#elif defined(TEST_WB563_SECTION)
    return exercise(0x23300000);
#else
    return exercise(0x49400000);
#endif
}
