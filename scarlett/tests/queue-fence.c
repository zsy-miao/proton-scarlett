/* A synthetic Scarlett PE: exercise queue/fence interoperability without any game assets. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>

#define CHECK(expr) do { HRESULT hr = (expr); if (FAILED(hr)) { \
    fprintf(stderr, "%s: %#lx\n", #expr, (unsigned long)hr); return 1; } } while (0)

int main(void)
{
    ID3D12Device *device = NULL;
    ID3D12CommandQueue *queue = NULL;
    ID3D12Fence *fence = NULL;
    D3D12_COMMAND_QUEUE_DESC desc = {0};
    HANDLE done = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!done) return 1;

    CHECK(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&device));
    CHECK(ID3D12Device_CreateCommandQueue(device, &desc, &IID_ID3D12CommandQueue, (void **)&queue));
    CHECK(ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence));
    CHECK(ID3D12Fence_Signal(fence, 1));
    CHECK(ID3D12CommandQueue_Wait(queue, fence, 1));
    CHECK(ID3D12Fence_SetEventOnCompletion(fence, 2, done));
    CHECK(ID3D12CommandQueue_Signal(queue, fence, 2));
    if (WaitForSingleObject(done, 5000) != WAIT_OBJECT_0 || ID3D12Fence_GetCompletedValue(fence) != 2)
        return 2;

    ID3D12Fence_Release(fence);
    ID3D12CommandQueue_Release(queue);
    ID3D12Device_Release(device);
    CloseHandle(done);
    puts("PASS Scarlett GPU fence: CPU signal, queue wait, queue signal, completion event");
    return 0;
}
