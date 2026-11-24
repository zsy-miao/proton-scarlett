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
#define W 160
#define H 120

/* Copy a solid RGBA image onto the very buffer registered by the display factory. */
static int paint(ID3D12CommandQueue *q, ID3D12Resource *buffer, DWORD rgba)
{
    ID3D12Device *d;
    HR(ID3D12CommandQueue_GetDevice(q,&IID_ID3D12Device,(void **)&d),S_OK);
    ID3D12Resource *upload;
    const UINT pitch=(W*4+255)&~255u;
    D3D12_HEAP_PROPERTIES heap={.Type=D3D12_HEAP_TYPE_UPLOAD,.CreationNodeMask=1,.VisibleNodeMask=1};
    D3D12_RESOURCE_DESC desc={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=pitch*H,.Height=1,
        .DepthOrArraySize=1,.MipLevels=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    HR(ID3D12Device_CreateCommittedResource(d,&heap,0,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,NULL,
            &IID_ID3D12Resource,(void **)&upload),S_OK);
    DWORD *pixels;
    HR(ID3D12Resource_Map(upload,0,NULL,(void **)&pixels),S_OK);
    for (UINT i=0;i<pitch*H/4;++i) pixels[i]=rgba;
    ID3D12Resource_Unmap(upload,0,NULL);
    ID3D12CommandAllocator *alloc;
    ID3D12GraphicsCommandList *list;
    HR(ID3D12Device_CreateCommandAllocator(d,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void **)&alloc),S_OK);
    HR(ID3D12Device_CreateCommandList(d,0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc,NULL,&IID_ID3D12GraphicsCommandList,(void **)&list),S_OK);
    D3D12_RESOURCE_BARRIER bar={.Transition={buffer,0,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_DEST}};
    ID3D12GraphicsCommandList_ResourceBarrier(list,1,&bar);
    D3D12_TEXTURE_COPY_LOCATION dst={.pResource=buffer,.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION src={.pResource=upload,.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
        .PlacedFootprint={0,{DXGI_FORMAT_R8G8B8A8_UNORM,W,H,1,pitch}}};
    ID3D12GraphicsCommandList_CopyTextureRegion(list,&dst,0,0,0,&src,NULL);
    bar.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
    bar.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;
    ID3D12GraphicsCommandList_ResourceBarrier(list,1,&bar);
    HR(ID3D12GraphicsCommandList_Close(list),S_OK);
    ID3D12CommandList *batch[]={(ID3D12CommandList *)list};
    ID3D12CommandQueue_ExecuteCommandLists(q,1,batch);
    ID3D12Fence *done;
    HR(ID3D12Device_CreateFence(d,0,0,&IID_ID3D12Fence,(void **)&done),S_OK);
    HANDLE event=CreateEventW(NULL,FALSE,FALSE,NULL); CHECK(event);
    HR(ID3D12CommandQueue_Signal(q,done,1),S_OK);
    HR(ID3D12Fence_SetEventOnCompletion(done,1,event),S_OK);
    CHECK(WaitForSingleObject(event,5000)==WAIT_OBJECT_0);
    CloseHandle(event); ID3D12Fence_Release(done); ID3D12GraphicsCommandList_Release(list);
    ID3D12CommandAllocator_Release(alloc); ID3D12Resource_Release(upload); ID3D12Device_Release(d);
    return 0;
}

int main(void)
{
    const IID id={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    DWORD device_params[20]={[0]=0x20004,[2]=0x100000,[10]=1,[15]=0x20};
    IUnknown *d,*q;
    HR(D3D12XboxCreateDevice(NULL,device_params,&id,(void **)&d),S_OK);
    typedef HRESULT (WINAPI *Placed)(IUnknown *,UINT64,const void *,UINT,const void *,REFIID,void **);
    typedef HRESULT (WINAPI *Committed)(IUnknown *,const void *,UINT,const void *,UINT,const void *,
            const void *,UINT,const void *,REFIID,void **);
    D3D12_RESOURCE_DESC desc={.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D,.Width=W,.Height=H,
        .DepthOrArraySize=1,.MipLevels=1,.Format=DXGI_FORMAT_R8G8B8A8_UNORM,.SampleDesc={1,0},
        .Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET};
    void *result=d;
    HR(SLOT(d,0x428,Placed)(d,0x12340000,&desc,6,NULL,&IID_ID3D12Resource,&result),E_NOTIMPL);
    CHECK(!result); result=d;
    D3D12_HEAP_PROPERTIES props={.Type=D3D12_HEAP_TYPE_DEFAULT,.CreationNodeMask=1,.VisibleNodeMask=1};
    HR(SLOT(d,0x3f8,Committed)(d,&props,8,&desc,0,NULL,NULL,0,NULL,&IID_ID3D12Resource,&result),E_NOTIMPL);
    CHECK(!result);
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
    WNDCLASSW cls={.lpfnWndProc=DefWindowProcW,.hInstance=GetModuleHandleW(NULL),.lpszClassName=L"PresentXTest"};
    CHECK(RegisterClassW(&cls));
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"PresentX",WS_VISIBLE|WS_POPUP,0,0,800,600,NULL,NULL,cls.hInstance,NULL);
    CHECK(window);
    UpdateWindow(window);
    IDXGIFactory2 *factory;
    IDXGISwapChain3 *chain;
    HR(CreateDXGIFactory1(&IID_IDXGIFactory2,(void **)&factory),S_OK);
    HR(xs_display_open(q,factory,window,W,H,&chain),S_OK);
    HR(present(q,1,wire,opts),DXGI_ERROR_INVALID_CALL);
    UINT first=IDXGISwapChain3_GetCurrentBackBufferIndex(chain),before,after; CHECK(first<2);
    HR(IDXGISwapChain3_GetLastPresentCount(chain,&before),S_OK);
    ID3D12Resource *buf[2];
    for (UINT i=0;i<2;++i)
        HR(IDXGISwapChain3_GetBuffer(chain,i,&IID_ID3D12Resource,(void **)&buf[i]),S_OK);
    IUnknown *keys[2];
    ID3D12CommandQueue *hostq=xs_display_queue(q);
    for (UINT i=0;i<2;++i) {
        HR(xs_display_wrap(q,i,buf[1-i],&keys[i]),E_INVALIDARG); CHECK(!keys[i]);
        HR(xs_display_wrap(q,i,buf[i],&keys[i]),S_OK);
        CHECK(xs_surface_host(keys[i])==buf[i]);
        IUnknown *again;
        HR(xs_display_wrap(q,i,buf[i],&again),DXGI_ERROR_INVALID_CALL); CHECK(!again);
        HR(IUnknown_QueryInterface(keys[i],&IID_ID3D12Resource,(void **)&again),E_NOINTERFACE);
    }
    for (UINT i=0;i<2;++i) ID3D12Resource_Release(buf[i]);
    HR(present(q,1,wire,opts),DXGI_ERROR_INVALID_CALL);
    key=(IUnknown *)keys[1-first];
    HR(present(q,1,wire,opts),DXGI_ERROR_INVALID_CALL);
    key=(IUnknown *)keys[first];
    HR(present(q,0,NULL,NULL),E_NOTIMPL);
    HR(present(q,2,wire,opts),E_NOTIMPL);
    wire[1]=2; HR(present(q,1,wire,opts),E_NOTIMPL); wire[1]=1;
    wire[2]=1; HR(present(q,1,wire,opts),E_POINTER); wire[2]=(UINT_PTR)&key;
    rect.right--; HR(present(q,1,wire,opts),E_NOTIMPL); rect.right++;
    dst[2]=0.5f; HR(present(q,1,wire,opts),E_NOTIMPL); dst[2]=1;
    wire[5]=12; HR(present(q,1,wire,opts),E_NOTIMPL); wire[5]=0;
    opts[3]=1; HR(present(q,1,wire,opts),E_NOTIMPL); opts[3]=0;
    HR(IDXGISwapChain3_GetLastPresentCount(chain,&after),S_OK); CHECK(before==after);
    HR(wait(d,2,0,NULL,0,&token[0]),DXGI_ERROR_WAIT_TIMEOUT);
    for (UINT i=0;i<2;++i) {
        UINT index=IDXGISwapChain3_GetCurrentBackBufferIndex(chain);
        CHECK(index==(first^i));
        key=(IUnknown *)keys[index];
        if (i) HR(present(q,1,wire,opts),DXGI_ERROR_INVALID_CALL); /* spent token, correct key */
        wire[0]=token[i];
        CHECK(!paint(hostq,xs_surface_host(keys[index]),i ? 0xff00ff00 : 0xff0000ff));
        HR(present(q,1,wire,opts),S_OK);
        HR(wait(d,2,5000,NULL,0,&token[i]),S_OK);
        HR(IDXGISwapChain3_GetLastPresentCount(chain,&after),S_OK); CHECK(after==before+i+1);
        if (!i) HR(wait(d,2,0,NULL,0,&token[1]),DXGI_ERROR_WAIT_TIMEOUT);
        /* Keep each color visible to the independent recorder. */
        ULONGLONG end=GetTickCount64()+1200;
        do { MSG m; while (PeekMessageW(&m,NULL,0,0,PM_REMOVE)) DispatchMessageW(&m); Sleep(10); }
        while (GetTickCount64()<end);
    }
    for (UINT i=0;i<2;++i) IUnknown_Release(keys[i]);
    IDXGISwapChain3_Release(chain); IDXGIFactory2_Release(factory);
    IUnknown_Release(q); IUnknown_Release(d); DestroyWindow(window);
    puts("PASS PresentX: rejected inputs preserve count/token; mapped red then green, two exact completions");
    return 0;
}
