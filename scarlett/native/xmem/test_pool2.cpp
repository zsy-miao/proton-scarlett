/* Scratch experiment: exact span, release/reuse/retire, and cache policy. */
#include "gpu_buffer.hpp"
#include <cstdio>

extern "C" __declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *, SIZE_T, DWORD, ULONGLONG, DWORD);

#define CHECK(e) do { if (!(e)) { std::fprintf(stderr, "line %d: %s, error %lu\n", \
    __LINE__, #e, GetLastError()); ExitProcess(1); } } while (0)
#define HR(e) do { HRESULT h = (e); if (FAILED(h)) { \
    std::fprintf(stderr, "line %d: %s, hr %#lx\n", __LINE__, #e, (unsigned long)h); ExitProcess(1); } } while (0)

int main()
{
    const SIZE_T size = 0x89700000;
    const DWORD marker = 0x74ac1375;
    CHECK(!XMemVirtualAlloc(nullptr, size, 0x20003000, 0x283, 0x2404) &&
          GetLastError() == ERROR_NOT_SUPPORTED);

    HANDLE wc = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT | SEC_WRITECOMBINE,
                                   0, 65536, nullptr);
    if (wc) {
        BYTE *p = (BYTE *)MapViewOfFile(wc, FILE_MAP_WRITE, 0, 0, 0);
        MEMORY_BASIC_INFORMATION m;
        CHECK(p && VirtualQuery(p, &m, sizeof m) == sizeof m);
        std::printf("SEC_WRITECOMBINE section: protection %#lx, allocation protection %#lx\n",
                    m.Protect, m.AllocationProtect);
        CHECK(!(m.Protect & PAGE_WRITECOMBINE) && !(m.AllocationProtect & PAGE_WRITECOMBINE));
        CHECK(UnmapViewOfFile(p) && CloseHandle(wc));
    } else std::printf("SEC_WRITECOMBINE section rejected: %lu\n", GetLastError());

    scarlett_section_info i;
    CHECK(FAILED(scarlett_query_section(0x53430001, nullptr, size, &i))); // Original ABI unchanged.
    HR(scarlett_query_section(0x53430003, nullptr, size, &i));
    auto base = (BYTE *)i.base;
    CHECK(base && !((UINT_PTR)base % (2u << 20)) && i.view_size == size);
    MEMORY_BASIC_INFORMATION m;
    CHECK(VirtualQuery(base, &m, sizeof m) == sizeof m && m.RegionSize == size);
    CHECK(FAILED(scarlett_acquire_section(base + size - 4, 5, &i)) &&
          FAILED(scarlett_acquire_section(base + size, 1, &i)));
    ID3D12Device3 *device;
    HR(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));
    ImportedBuffer buf;
    SIZE_T off;
    HR(buf.acquire(device, base + size - 4, 4, &off));
    CHECK(off == size - 4 && buf.resource()->GetDesc().Width == size);
    auto alias = (BYTE *)buf.cpu();
    CHECK(!buf.translate(alias + off, 5) && buf.translate(alias + off, 4));
    auto last = (volatile DWORD *)(alias + off);
    *(DWORD *)(base + off) = marker;
    CHECK(*last == marker);
    D3D12_HEAP_PROPERTIES props;
    D3D12_HEAP_FLAGS flags;
    HR(buf.resource()->GetHeapProperties(&props, &flags));
    CHECK(props.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_BACK);

    // GPU copy from a separate upload resource into the retired title range.
    D3D12_RESOURCE_DESC desc = buf.resource()->GetDesc();
    desc.Width = 65536;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    D3D12_HEAP_PROPERTIES upload = {};
    upload.Type = D3D12_HEAP_TYPE_UPLOAD;
    upload.CreationNodeMask = upload.VisibleNodeMask = 1;
    ID3D12Resource *src;
    HR(device->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&src)));
    void *src_cpu;
    HR(src->Map(0, nullptr, &src_cpu));
    *(DWORD *)src_cpu = ~marker;
    src->Unmap(0, nullptr);
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *alloc;
    ID3D12GraphicsCommandList *list;
    ID3D12Fence *gate, *retire;
    D3D12_COMMAND_QUEUE_DESC q = {};
    q.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    HR(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
    HR(device->CreateCommandAllocator(q.Type, IID_PPV_ARGS(&alloc)));
    HR(device->CreateCommandList(0, q.Type, alloc, nullptr, IID_PPV_ARGS(&list)));
    HR(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
    HR(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&retire)));
    HR(queue->Wait(gate, 1));
    list->CopyBufferRegion(buf.resource(), off, src, 0, 4);
    HR(list->Close());
    ID3D12CommandList *batch[] = {list};
    queue->ExecuteCommandLists(1, batch);
    HR(queue->Signal(retire, 1));
    CHECK(VirtualFree(base, 0, MEM_RELEASE));
    CHECK(VirtualAlloc(base, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) == base);
    CHECK(FAILED(scarlett_acquire_section(base + off, 4, &i)));
    *(DWORD *)(base + off) = 42;
    CHECK(!retire->GetCompletedValue() && *last == marker);
    HR(gate->Signal(1));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    CHECK(event);
    HR(retire->SetEventOnCompletion(1, event));
    CHECK(WaitForSingleObject(event, 15000) == WAIT_OBJECT_0);
    HR(device->GetDeviceRemovedReason());
    CHECK(retire->GetCompletedValue() == 1 && *last == ~marker && *(DWORD *)(base + off) == 42);
    CHECK(VirtualFree(base, 0, MEM_RELEASE) && CloseHandle(event));
    src->Release(); retire->Release(); gate->Release(); list->Release();
    alloc->Release(); queue->Release(); device->Release();
    puts("PASS: exact 0x89700000 logical span, GPU last word after reuse, imported heap WRITE_BACK; WC request rejected");
}
