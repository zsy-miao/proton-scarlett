/* Synthetic red and green frames for checking the private display recorder. */
#include <windows.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#ifdef TEST_GAMEINPUT
#include <GameInput.h>
#endif
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
    UINT64 serial=0;
    auto present=[&](const float (&color)[4]) -> int {
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
        list->ClearRenderTargetView(rtv,color,0,nullptr);
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;
        list->ResourceBarrier(1,&barrier);
        HR(list->Close());
        ID3D12CommandList *batch[]={list.Get()};
        queue->ExecuteCommandLists(1,batch);
        HR(swap->Present(1,0));
        HR(queue->Signal(done.Get(),++serial));
        HR(done->SetEventOnCompletion(serial,event));
        if (WaitForSingleObject(event,5000)!=WAIT_OBJECT_0) return 1;
        return 0;
    };
#ifdef TEST_GAMEINPUT
    ComPtr<IGameInput> pad;
    HR(GameInputCreate(&pad));
    SetForegroundWindow(window);
    SetFocus(window);
    int previous=-1, transitions=0;
    ULONGLONG changed=0, deadline=GetTickCount64()+12000;
    while (GetTickCount64()<deadline) {
        MSG m;
        while (PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) DispatchMessageW(&m);
        ComPtr<IGameInputReading> reading;
        HR(pad->GetCurrentReading(GameInputKindGamepad,nullptr,&reading));
        GameInputGamepadState state{};
        if (!reading->GetGamepadState(&state) || GetForegroundWindow()!=window) return 1;
        if ((state.buttons && state.buttons!=GameInputGamepadA) ||
            (state.leftThumbstickY!=0 && state.leftThumbstickY!=1) ||
            (state.rightThumbstickX!=0 && state.rightThumbstickX!=-1) ||
            state.leftThumbstickX || state.rightThumbstickY) return 1;
        int action=(state.buttons?1:0) | (state.leftThumbstickY?2:0) | (state.rightThumbstickX?4:0);
        const int sequence[]={0,1,0,2,0,4,0};
        if (action!=previous) {
            if (transitions>=7 || action!=sequence[transitions]) return 1;
            previous=action;
            ++transitions;
            changed=GetTickCount64();
            printf("GameInput step=%d buttons=%#x movementY=%.0f cameraX=%.0f\n",transitions,
                unsigned(state.buttons),state.leftThumbstickY,state.rightThumbstickX); fflush(stdout);
        }
        if (present({float(action==2 || action==4),float(action==1 || action==4),float(!action),1})) return 1;
        if (transitions==7 && GetTickCount64()-changed>=500) break;
        Sleep(10);
    }
    if (transitions!=7) return 1;
    puts("PASS synthetic GameInput DXGI: A, forward, camera left; all returned to neutral");
#else
    for (unsigned frame=0;frame<4;++frame) {
        if (present({float(frame<2),float(frame>=2),0,1})) return 1;
        const ULONGLONG until=GetTickCount64()+1200;
        do {
            MSG m;
            while (PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) DispatchMessageW(&m);
            Sleep(10);
        } while (GetTickCount64()<until);
    }
    puts("PASS synthetic DXGI: presented red then green");
#endif
    CloseHandle(event);
}
