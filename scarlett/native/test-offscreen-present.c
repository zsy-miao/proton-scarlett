#define COBJMACROS
#define INITGUID
#include "queue/present.h"
#include <stdio.h>

HRESULT WINAPI D3D12XboxCreateDevice(void *, const void *, REFIID, void **);
typedef HRESULT (WINAPI *Queue)(IUnknown *,const D3D12_COMMAND_QUEUE_DESC *,REFIID,void **);
typedef HRESULT (WINAPI *Interval)(IUnknown *,IDXGIOutput *,UINT,UINT,UINT);
typedef HRESULT (WINAPI *Schedule)(IUnknown *,UINT,UINT,const void *,UINT);
typedef HRESULT (WINAPI *Wait)(IUnknown *,UINT,DWORD,const void *,UINT,UINT64 *);
typedef HRESULT (WINAPI *Present)(IUnknown *,UINT,const void *,const void *);
#define SLOT(i,n,t) ((t)((void *const *)(i)->lpVtbl)[(n)/8])
#define HR(e,want) do { HRESULT h=(e); if(h!=(want)) { fprintf(stderr,"FAIL %d: %s = %#lx\n",__LINE__,#e,(unsigned long)h); return 1; } } while(0)
#define CHECK(e) do { if(!(e)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#e); return 1; } } while(0)
#define W 3840
#define H 2160

struct key { IUnknown i; LONG refs; };
static ULONG WINAPI add(IUnknown *i) { return InterlockedIncrement(&((struct key *)i)->refs); }
static ULONG WINAPI drop(IUnknown *i) { return InterlockedDecrement(&((struct key *)i)->refs); }
static HRESULT WINAPI qi(IUnknown *i,REFIID id,void **out) {
    *out=NULL; if(!IsEqualIID(id,&IID_IUnknown))return E_NOINTERFACE;
    add(i); *out=i; return S_OK;
}
static IUnknownVtbl key_vt={qi,add,drop};

int main(void)
{
    const IID id={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    DWORD device_params[20]={[0]=0x20004,[2]=0x100000,[10]=1,[15]=0x20};
    IUnknown *d,*q;
    HR(D3D12XboxCreateDevice(NULL,device_params,&id,(void **)&d),S_OK);
    D3D12_COMMAND_QUEUE_DESC qd={.NodeMask=1};
    HR(SLOT(d,0x40,Queue)(d,&qd,&IID_ID3D12CommandQueue,(void **)&q),S_OK);
    HR(SLOT(d,0x270,Interval)(d,NULL,33333,3,0),S_OK);
    HR(SLOT(d,0x278,Schedule)(d,0,0,NULL,0),S_OK);
    HR(SLOT(d,0x278,Schedule)(d,2,0,NULL,0),S_OK);
    Wait wait=SLOT(d,0x280,Wait);
    UINT64 token[2];
    HR(wait(d,0,1000,NULL,0,&token[0]),S_OK);
    HR(wait(d,0,1000,NULL,0,&token[1]),S_OK);
    RECT rect={0,0,W,H};
    float dst[]={0,0,1,1};
    IUnknown *key=(IUnknown *)(UINT_PTR)1;
    /* Independent producer fixture; only the pointed-to array contains the resource. */
    UINT64 wire[9]={token[0],1,(UINT_PTR)&key,(UINT_PTR)&rect,(UINT_PTR)dst};
    UINT64 opts[4]={0};
    Present present=SLOT(q,0x100,Present);
    HR(present(q,1,wire,opts),E_NOTIMPL);
#ifdef TEST_CAPTURE_HWND
    WNDCLASSA cls={.lpfnWndProc=DefWindowProcA,.hInstance=GetModuleHandleW(NULL),.lpszClassName="sgaWindowClass"};
    CHECK(RegisterClassA(&cls));
    HWND window=CreateWindowExA(0,cls.lpszClassName,"Window capture",WS_OVERLAPPEDWINDOW,0,0,800,600,NULL,NULL,cls.hInstance,NULL);
    CHECK(window); ShowWindow(window,SW_SHOW);
#else
    WNDCLASSW cls={.lpfnWndProc=DefWindowProcW,.hInstance=GetModuleHandleW(NULL),.lpszClassName=L"PresentXTest"};
    CHECK(RegisterClassW(&cls));
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"PresentX",WS_VISIBLE|WS_POPUP,0,0,800,600,NULL,NULL,cls.hInstance,NULL);
#endif
    CHECK(window);
    UpdateWindow(window);
    IDXGIFactory2 *factory;
    IDXGISwapChain3 *chain;
    HR(CreateDXGIFactory1(&IID_IDXGIFactory2,(void **)&factory),S_OK);
#ifdef TEST_CAPTURE_HWND
    HR(xs_display_open_title(q,factory,W,H,&chain),S_OK);
    HWND actual=NULL; HR(IDXGISwapChain3_GetHwnd(chain,&actual),S_OK); CHECK(actual==window);
#else
    HR(xs_display_open(q,factory,window,W,H,&chain),S_OK);
#endif
    HR(present(q,1,wire,opts),DXGI_ERROR_INVALID_CALL);
    UINT first=IDXGISwapChain3_GetCurrentBackBufferIndex(chain),before,after; CHECK(first<2);
    HR(IDXGISwapChain3_GetLastPresentCount(chain,&before),S_OK);
    ID3D12CommandQueue *producer=xs_display_queue(q), *foreign;
    ID3D12Device *host; HR(ID3D12CommandQueue_GetDevice(producer,&IID_ID3D12Device,(void **)&host),S_OK);
    HR(ID3D12Device_CreateCommandQueue(host,&qd,&IID_ID3D12CommandQueue,(void **)&foreign),S_OK);
    struct key keys[2]={{{&key_vt},1},{{&key_vt},1}};
    for(UINT i=0;i<2;i++) {
        D3D12_HEAP_PROPERTIES hp={.Type=D3D12_HEAP_TYPE_DEFAULT,.CreationNodeMask=1,.VisibleNodeMask=1};
        D3D12_RESOURCE_DESC rd={.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D,.Width=W,.Height=H,.DepthOrArraySize=1,
            .MipLevels=1,.Format=DXGI_FORMAT_R8G8B8A8_UNORM,.SampleDesc={1,0},.Flags=5};
        ID3D12Resource *tex;
        HR(ID3D12Device_CreateCommittedResource(host,&hp,0,&rd,D3D12_RESOURCE_STATE_RENDER_TARGET,NULL,&IID_ID3D12Resource,(void **)&tex),S_OK);
        D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,0,1}; ID3D12DescriptorHeap *rt;
        HR(ID3D12Device_CreateDescriptorHeap(host,&hd,&IID_ID3D12DescriptorHeap,(void **)&rt),S_OK);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv; rt->lpVtbl->GetCPUDescriptorHandleForHeapStart(rt,&rtv);
        ID3D12Device_CreateRenderTargetView(host,tex,NULL,rtv);
        ID3D12CommandAllocator *alloc; ID3D12GraphicsCommandList *list;
        HR(ID3D12Device_CreateCommandAllocator(host,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void **)&alloc),S_OK);
        HR(ID3D12Device_CreateCommandList(host,0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc,NULL,&IID_ID3D12GraphicsCommandList,(void **)&list),S_OK);
        const float left[][4]={{1,0,0,1},{0,1,0,1}},right[][4]={{0,0,1,1},{1,0,1,1}};
        RECT half={W/2,0,W,H};
        ID3D12GraphicsCommandList_ClearRenderTargetView(list,rtv,left[i],0,NULL);
        ID3D12GraphicsCommandList_ClearRenderTargetView(list,rtv,right[i],1,&half);
        D3D12_RESOURCE_STATES handoff=D3D12_RESOURCE_STATE_RENDER_TARGET;
#ifdef SCARLETT_EXPERIMENTAL_PRIMARY_RELEASE
        ID3D12GraphicsCommandList7 *enhanced;
        HR(ID3D12GraphicsCommandList_QueryInterface(list,&IID_ID3D12GraphicsCommandList7,(void**)&enhanced),S_OK);
        D3D12_TEXTURE_BARRIER release={D3D12_BARRIER_SYNC_RENDER_TARGET,D3D12_BARRIER_SYNC_NONE,
            D3D12_BARRIER_ACCESS_RENDER_TARGET,D3D12_BARRIER_ACCESS_NO_ACCESS,
            D3D12_BARRIER_LAYOUT_RENDER_TARGET,D3D12_BARRIER_LAYOUT_COMMON,tex,{UINT_MAX,0,0,0,0,0},D3D12_TEXTURE_BARRIER_FLAG_NONE};
        D3D12_BARRIER_GROUP bg={.Type=D3D12_BARRIER_TYPE_TEXTURE,.NumBarriers=1,.pTextureBarriers=&release};
        ID3D12GraphicsCommandList7_Barrier(enhanced,1,&bg);ID3D12GraphicsCommandList7_Release(enhanced);
        handoff=D3D12_RESOURCE_STATE_COMMON;
#endif
        HR(ID3D12GraphicsCommandList_Close(list),S_OK);
        ID3D12CommandList *batch[]={(ID3D12CommandList *)list}; ID3D12CommandQueue_ExecuteCommandLists(producer,1,batch);
        key=&keys[i].i; wire[0]=token[i];
        #define ARM(queue_,hwnd_,state_,flags_,hf_) xs_display_arm_primary(q,queue_,hwnd_,token[i],&keys[i].i,tex,state_,flags_,hf_)
        HR(ARM(foreign,window,D3D12_RESOURCE_STATE_RENDER_TARGET,0x300005,8),E_INVALIDARG);
        HR(ARM(producer,GetDesktopWindow(),D3D12_RESOURCE_STATE_RENDER_TARGET,0x300005,8),DXGI_ERROR_INVALID_CALL);
        HR(ARM(producer,window,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,0x300005,8),E_NOTIMPL);
        HR(ARM(producer,window,D3D12_RESOURCE_STATE_RENDER_TARGET,0x300005,0),E_NOTIMPL);
        HR(ARM(producer,window,D3D12_RESOURCE_STATE_RENDER_TARGET,0x5,8),E_NOTIMPL);
        HR(ARM(producer,window,handoff,0x300005,8),S_OK);
        HR(ARM(producer,window,handoff,0x300005,8),DXGI_ERROR_INVALID_CALL);
        ID3D12Resource_Release(tex); CHECK(IUnknown_Release(key)==1);
        key=(IUnknown *)(UINT_PTR)1; HR(present(q,1,wire,opts),DXGI_ERROR_INVALID_CALL); key=&keys[i].i;
        wire[0]=~token[i]; HR(present(q,1,wire,opts),DXGI_ERROR_INVALID_CALL); wire[0]=token[i];
        HR(IDXGISwapChain3_GetLastPresentCount(chain,&after),S_OK); CHECK(after==before+i);
        CHECK(IDXGISwapChain3_GetCurrentBackBufferIndex(chain)==(first^i));
        HR(present(q,1,wire,opts),S_OK); CHECK(!keys[i].refs);
        HR(wait(d,2,5000,NULL,0,&token[i]),S_OK);
        HR(IDXGISwapChain3_GetLastPresentCount(chain,&after),S_OK); CHECK(after==before+i+1);
        ID3D12GraphicsCommandList_Release(list); ID3D12CommandAllocator_Release(alloc); ID3D12DescriptorHeap_Release(rt);
        ULONGLONG end=GetTickCount64()+1500;
        do { MSG m; while (PeekMessageW(&m,NULL,0,0,PM_REMOVE)) DispatchMessageW(&m); Sleep(10); } while(GetTickCount64()<end);
    }
    ID3D12CommandQueue_Release(foreign); ID3D12Device_Release(host);
    IDXGISwapChain3_Release(chain); IDXGIFactory2_Release(factory);
    IUnknown_Release(q); IUnknown_Release(d); DestroyWindow(window);
    puts("PASS +100: UHD offscreen SDR RT+UAV rendered halves red/blue then green/magenta; two actual flips and frame tokens after producer release");
    return 0;
}
