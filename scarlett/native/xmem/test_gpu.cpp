/* Synthetic Proton release/alias test. Never submit GPU work against the public VA. */
#include "gpu_buffer.hpp"
#include <cstdio>

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

int main()
{
    const SIZE_T size = 0x49400000;  // First nonzero startup pool, 1,172 MiB.
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
    auto original = query(title);
    ID3D12Device3 *device;
    HR(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));
    ImportedBuffer allocation;
    HR(allocation.create(device, section, size));
    CHECK(CloseHandle(section));
    auto cpu = (volatile DWORD *)allocation.cpu();
    auto shared = allocation.resource();
    CHECK(cpu && cpu != title && !((UINT_PTR)cpu % align) && shared->GetGPUVirtualAddress() &&
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
    CHECK(cpu[0] == title[0] && !cpu[size / 4 - 1]);
    list->CopyBufferRegion(scratch, 0, shared, 0, 4);
    HR(list->Close());
    ID3D12CommandList *batch[] = {list};
    queue->ExecuteCommandLists(1, batch);
    HR(queue->Signal(retire, 1));
    HR(retire->SetEventOnCompletion(1, event));
    CHECK(WaitForSingleObject(event, 15000) == WAIT_OBJECT_0);
    title[0] = 0;

    HR(alloc->Reset());
    HR(list->Reset(alloc, nullptr));
    list->CopyBufferRegion(shared, size - 4, scratch, 0, 4);
    HR(list->Close());
    HR(queue->Wait(gate, 1));
    queue->ExecuteCommandLists(1, batch);
    HR(queue->Signal(retire, 2));

    // Only explicit unmap works today. Reuse the exact VA with identical metadata.
    CHECK(UnmapViewOfFile(title));
    HANDLE fresh = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, nullptr);
    CHECK(fresh && MapViewOfFileEx(fresh, FILE_MAP_ALL_ACCESS, 0, 0, size, title) == title);
    CHECK(CloseHandle(fresh));
    auto reused = query(title);
    CHECK(reused.BaseAddress == original.BaseAddress && reused.AllocationBase == original.AllocationBase &&
          reused.RegionSize == original.RegionSize && reused.State == original.State &&
          reused.Type == original.Type && reused.Protect == original.Protect &&
          reused.AllocationProtect == original.AllocationProtect);
    title[size / 4 - 1] = 0x55aabb33;
    CHECK(retire->GetCompletedValue() == 1 && !cpu[size / 4 - 1]);
    HR(gate->Signal(1));
    HR(retire->SetEventOnCompletion(2, event));
    CHECK(WaitForSingleObject(event, 15000) == WAIT_OBJECT_0);
    HR(device->GetDeviceRemovedReason());
    CHECK(retire->GetCompletedValue() == 2 && cpu[size / 4 - 1] == 0xa103bd17 &&
          title[size / 4 - 1] == 0x55aabb33);
    CHECK(UnmapViewOfFile(title));
    CHECK(CloseHandle(event));
    list->Release(); alloc->Release(); gate->Release(); retire->Release(); queue->Release();
    scratch->Release(); device->Release();
    puts("PASS: VirtualFree rejected both section views (87); private replacement released; "
         "GPU retired on old backing after exact public VA reuse");
    return 0;
}
