/* Synthetic experiment for CPU-visible GPU progress markers. */
#include <windows.h>
#include <d3d12.h>
#include <cstdio>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); ExitProcess(1); } } while (0)

int main()
{
    ID3D12Device *device;
    CHECK(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))));
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 features{};
    CHECK(SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &features, sizeof features)) &&
          (features.WriteBufferImmediateSupportFlags & D3D12_COMMAND_LIST_SUPPORT_FLAG_DIRECT));
    D3D12_COMMAND_QUEUE_DESC q{};
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *allocator;
    ID3D12Fence *gate, *done;
    CHECK(SUCCEEDED(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue))));
    CHECK(SUCCEEDED(device->CreateCommandAllocator(q.Type, IID_PPV_ARGS(&allocator))));
    CHECK(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))));
    CHECK(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&done))));
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 65536;
    desc.Height = desc.MipLevels = desc.DepthOrArraySize = desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES props{};
    props.Type = D3D12_HEAP_TYPE_READBACK;
    props.CreationNodeMask = props.VisibleNodeMask = 1;
    ID3D12Resource *buffer;
    CHECK(SUCCEEDED(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer))));
    void *mapping;
    CHECK(SUCCEEDED(buffer->Map(0, nullptr, &mapping)));
    volatile UINT32 *words = static_cast<volatile UINT32 *>(mapping);
    for (unsigned i = 0; i < 54; ++i) words[i] = 0;
    const UINT indexes[] = {17, 18};
    ID3D12GraphicsCommandList2 *lists[2];
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(SUCCEEDED(device->CreateCommandList(0, q.Type, allocator, nullptr, IID_PPV_ARGS(&lists[i]))));
        D3D12_WRITEBUFFERIMMEDIATE_PARAMETER marker{buffer->GetGPUVirtualAddress() + 4 * indexes[i], i + 1};
        lists[i]->WriteBufferImmediate(1, &marker, nullptr);
        CHECK(SUCCEEDED(lists[i]->Close()));
        if (i) CHECK(SUCCEEDED(queue->Wait(gate, 1)));
        ID3D12CommandList *batch[] = {lists[i]};
        queue->ExecuteCommandLists(1, batch);
    }
    CHECK(SUCCEEDED(queue->Signal(done, 1)));
    ULONGLONG start = GetTickCount64();
    while (!words[17] && GetTickCount64() - start < 5000) Sleep(1);
    CHECK(words[17] == 1 && !words[18] && !done->GetCompletedValue());
    words[17] = 0;
    CHECK(SUCCEEDED(gate->Signal(1)));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    CHECK(event && SUCCEEDED(done->SetEventOnCompletion(1, event)));
    CHECK(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0 && SUCCEEDED(device->GetDeviceRemovedReason()));
    for (unsigned i = 0; i < 54; ++i) CHECK(words[i] == (i == 18 ? 2u : 0u));
    CloseHandle(event);
    buffer->Unmap(0, nullptr);
    for (auto *list : lists) list->Release();
    allocator->Release(); buffer->Release(); gate->Release(); done->Release(); queue->Release(); device->Release();
    puts("PASS markers: CPU observed first bank before queue retirement; second bank changed only after gate");
}
