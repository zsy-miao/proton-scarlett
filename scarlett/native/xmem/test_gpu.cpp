/* Synthetic experiment: can the host device bind the CPU VA without copying? */
#include "gpu_buffer.hpp"
#include <cstdio>

#define HR(call) do { HRESULT h = (call); if (FAILED(h)) { \
    std::printf("%s failed: %#lx\n", #call, (unsigned long)h); return 1; } } while (0)

int main()
{
    const SIZE_T size = 0x49400000;  // First nonzero startup pool, 1,172 MiB.
    ID3D12Device3 *device;
    HR(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));
    ImportedBuffer allocation;
    HR(allocation.create(device, size));
    auto cpu = (volatile DWORD *)allocation.cpu();
    auto shared = allocation.resource();
    if (!cpu || (UINT_PTR)cpu % (2u << 20) || !shared->GetGPUVirtualAddress() ||
        allocation.translate((void *)(cpu + 1), 4) != shared->GetGPUVirtualAddress() + 4 ||
        allocation.translate((void *)(cpu + size / 4 - 1), 5)) return 2;
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
    ID3D12Fence *fence;
    HR(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
    HR(device->CreateCommandAllocator(q.Type, IID_PPV_ARGS(&alloc)));
    HR(device->CreateCommandList(0, q.Type, alloc, nullptr, IID_PPV_ARGS(&list)));
    HR(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) return 2;
    cpu[0] = 0xa103bd17;
    cpu[size / 4 - 1] = 0;
    for (unsigned i = 1; i <= 2; ++i) {
        if (i == 1) list->CopyBufferRegion(scratch, 0, shared, 0, 4);
        else list->CopyBufferRegion(shared, size - 4, scratch, 0, 4);
        HR(list->Close());
        ID3D12CommandList *batch[] = {list};
        queue->ExecuteCommandLists(1, batch);
        HR(queue->Signal(fence, i));
        HR(fence->SetEventOnCompletion(i, event));
        if (WaitForSingleObject(event, 15000) != WAIT_OBJECT_0) return 3;
        HR(device->GetDeviceRemovedReason());
        if (i == 1) {
            cpu[0] = 0;
            HR(alloc->Reset());
            HR(list->Reset(alloc, nullptr));
        }
    }
    if (cpu[size / 4 - 1] != 0xa103bd17) { puts("GPU copy did not reach CPU VA"); return 4; }
    printf("GPU copied CPU sentinel back into original allocation; CPU %p, GPU %#llx\n",
           (void *)cpu, (unsigned long long)shared->GetGPUVirtualAddress());
    CloseHandle(event);
    list->Release(); alloc->Release(); fence->Release(); queue->Release();
    scratch->Release(); device->Release();
    return 0;
}
