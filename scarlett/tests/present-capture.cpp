/* Synthetic red and green frames for checking the private display recorder. */
#include <windows.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
using Microsoft::WRL::ComPtr;
#define HR(x) do { HRESULT h=(x); if (h!=S_OK) { fprintf(stderr,"%s: %#lx\n",#x,(unsigned long)h); return 1; } } while (0)

int main()
{
    WNDCLASSW cls{};
    cls.lpfnWndProc=DefWindowProcW;
    cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"ScarlettPresentProbe";
    if (!RegisterClassW(&cls)) return 1;
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"Presentation fixture", WS_POPUP | WS_VISIBLE,
                                   40, 40, 320, 240, nullptr, nullptr, cls.hInstance, nullptr);
    if (!window) return 1;
    ComPtr<ID3D12Device> device;
    HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
    D3D12_COMMAND_QUEUE_DESC q{};
    ComPtr<ID3D12CommandQueue> queue;
    HR(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<IDXGIFactory4> factory;
    HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width=320; desc.Height=240; desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount=2; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> chain;
    HR(factory->CreateSwapChainForHwnd(queue.Get(),window,&desc,nullptr,nullptr,&chain));
    ComPtr<IDXGISwapChain3> swap;
    HR(chain.As(&swap));
    ComPtr<ID3D12DescriptorHeap> heap;
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};
    HR(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    ComPtr<ID3D12Fence> done;
    HR(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if (!event) return 1;
    for (unsigned frame=0;frame<4;++frame) {
        ComPtr<ID3D12Resource> image;
        HR(swap->GetBuffer(swap->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&image)));
        auto rtv=heap->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(image.Get(),nullptr,rtv);
        ComPtr<ID3D12CommandAllocator> alloc;
        ComPtr<ID3D12GraphicsCommandList> list;
        HR(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&alloc)));
        HR(device->CreateCommandList(0,q.Type,alloc.Get(),nullptr,IID_PPV_ARGS(&list)));
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Transition={image.Get(),0,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET};
        list->ResourceBarrier(1,&barrier);
        float color[4]={float(frame<2),float(frame>=2),0,1};
        list->ClearRenderTargetView(rtv,color,0,nullptr);
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;
        list->ResourceBarrier(1,&barrier);
        HR(list->Close());
        ID3D12CommandList *batch[]={list.Get()};
        queue->ExecuteCommandLists(1,batch);
        HR(swap->Present(1,0));
        HR(queue->Signal(done.Get(),frame+1));
        HR(done->SetEventOnCompletion(frame+1,event));
        if (WaitForSingleObject(event,5000)!=WAIT_OBJECT_0) return 1;
        const ULONGLONG until=GetTickCount64()+1200;
        do {
            MSG m;
            while (PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) DispatchMessageW(&m);
            Sleep(10);
        } while (GetTickCount64()<until);
    }
    CloseHandle(event);
    puts("PASS synthetic DXGI: presented red then green");
}
