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

static BYTE *raw;
static LONG refs=1; static BOOL released;
static ULONG WINAPI memadd(IUnknown *p){(void)p; return InterlockedIncrement(&refs);}
static ULONG WINAPI memdrop(IUnknown *p){(void)p;LONG n=InterlockedDecrement(&refs); if(!n){
    for(UINT i=0;i<2;i++) for(UINT y=0;y<H;y++)for(UINT x=0;x<W;x++) {
        UINT want=i?(x<W/2?0xff00ff00:0xffff00ff):(x<W/2?0xff0000ff:0xffff0000);
        UINT got=((UINT *)(raw+i*0x1fb0000))[y*W+x]; if(got!=want){fprintf(stderr,"image %u (%u,%u) %08x != %08x\n",i,x,y,got,want); exit(1);}
    }
    for(UINT j=0;j<2;j++)for(UINT i=0x1fa4000;i<0x1fb0000;i++) if(raw[j*0x1fb0000+i]!=0xa5) exit(1);
    for(UINT i=0x3f60000;i<0x4000000;i++) if(raw[i]!=0xa5) exit(1);
    if(!VirtualFree(raw,0,MEM_RELEASE)) exit(1);
    released=TRUE;
} return n;}
static HRESULT WINAPI memqi(IUnknown *p,REFIID iid,void **out){*out=NULL;if(!IsEqualIID(iid,&IID_IUnknown))return E_NOINTERFACE;memadd(p);*out=p;return S_OK;}
static IUnknownVtbl memvt={memqi,memadd,memdrop};
typedef HRESULT (WINAPI *Info)(ID3D12Device *,D3D12_RESOURCE_ALLOCATION_INFO *);
typedef HRESULT (WINAPI *Import)(ID3D12Device *,void *,SIZE_T,SIZE_T,IUnknown *,ID3D12Resource **);
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
    HMODULE core=GetModuleHandleW(L"d3d12core.dll");
    Info info=(Info)(void*)GetProcAddress(core,"ScarlettPrimaryImageInfoV1"); Import image=(Import)(void*)GetProcAddress(core,"ScarlettPrimaryImageV1"); CHECK(info && image);
    D3D12_RESOURCE_ALLOCATION_INFO ai; HR(info(host,&ai),S_OK); CHECK(ai.SizeInBytes==0x1fb0000 && ai.Alignment==65536);
    raw=VirtualAlloc(NULL,0x4000000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE); CHECK(raw); memset(raw,0xa5,0x4000000);
    IUnknown owner={&memvt}; ID3D12Resource *texs[2],*bad=(void*)1;
    DWORD old;
    CHECK(VirtualProtect(raw,65536,PAGE_READONLY,&old)); HR(image(host,raw,0x4000000,0,&owner,&bad),E_INVALIDARG); CHECK(!bad);
    CHECK(VirtualProtect(raw,65536,old,&old));
    HR(image(host,raw,0x4000000,0,NULL,&bad),E_INVALIDARG); CHECK(!bad);
    HR(image(host,raw,0x3ff0000,0,&owner,&bad),E_INVALIDARG); CHECK(!bad);
    HR(image(host,raw,0x4000000,0,&owner,&texs[0]),S_OK); HR(image(host,raw,0x4000000,0,&owner,&bad),DXGI_ERROR_INVALID_CALL);CHECK(!bad);
    HR(image(host,raw,0x4000000,65536,&owner,&bad),E_INVALIDARG);CHECK(!bad);
    HR(image(host,raw,0x4000000,ai.SizeInBytes,&owner,&texs[1]),S_OK); CHECK(IUnknown_Release(&owner)==2);
    for(UINT j=0;j<0x4000000;j++) CHECK(raw[j]==0xa5); /* optimized clear must not initialize the imported arena */
    D3D12_DESCRIPTOR_HEAP_DESC sh={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    ID3D12DescriptorHeap *heap; HR(ID3D12Device_CreateDescriptorHeap(host,&sh,&IID_ID3D12DescriptorHeap,(void**)&heap),S_OK);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu; heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(heap,&cpu);
    ID3D12Device_CreateShaderResourceView(host,texs[0],NULL,cpu); cpu.ptr+=ID3D12Device_GetDescriptorHandleIncrementSize(host,sh.Type);
    ID3D12Device_CreateUnorderedAccessView(host,texs[1],NULL,NULL,cpu);
    UINT stride=ID3D12Device_GetDescriptorHandleIncrementSize(host,sh.Type);
    cpu.ptr+=stride; ID3D12Device_CreateUnorderedAccessView(host,texs[0],NULL,NULL,cpu);
    sh.NumDescriptors=1; sh.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ID3D12DescriptorHeap *clearheap; HR(ID3D12Device_CreateDescriptorHeap(host,&sh,&IID_ID3D12DescriptorHeap,(void**)&clearheap),S_OK);
    D3D12_CPU_DESCRIPTOR_HANDLE clearcpu; clearheap->lpVtbl->GetCPUDescriptorHandleForHeapStart(clearheap,&clearcpu);
    ID3D12Device_CreateUnorderedAccessView(host,texs[0],NULL,NULL,clearcpu);
    D3D12_DESCRIPTOR_RANGE range[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1}};
    D3D12_ROOT_PARAMETER rp={.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,.DescriptorTable={2,range}};
    D3D12_ROOT_SIGNATURE_DESC rs={1,&rp,0,NULL,0};ID3DBlob *blob;ID3D12RootSignature *root;ID3D12PipelineState *ps;
    HR(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,NULL),S_OK);
    HR(ID3D12Device_CreateRootSignature(host,0,blob->lpVtbl->GetBufferPointer(blob),blob->lpVtbl->GetBufferSize(blob),&IID_ID3D12RootSignature,(void**)&root),S_OK);blob->lpVtbl->Release(blob);
    FILE *f=fopen("image.dxil","rb"); CHECK(f); BYTE code[8192];size_t len=fread(code,1,sizeof code,f);CHECK(len && feof(f)); fclose(f);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={.pRootSignature=root,.CS={code,len}};
    HR(ID3D12Device_CreateComputePipelineState(host,&pd,&IID_ID3D12PipelineState,(void**)&ps),S_OK);
    for(UINT i=0;i<2;i++) {
        ID3D12Resource *tex=texs[i];
        D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,0,1}; ID3D12DescriptorHeap *rt;
        HR(ID3D12Device_CreateDescriptorHeap(host,&hd,&IID_ID3D12DescriptorHeap,(void **)&rt),S_OK);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv; rt->lpVtbl->GetCPUDescriptorHandleForHeapStart(rt,&rtv);
        ID3D12Device_CreateRenderTargetView(host,tex,NULL,rtv);
        ID3D12CommandAllocator *alloc; ID3D12GraphicsCommandList *list;
        HR(ID3D12Device_CreateCommandAllocator(host,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void **)&alloc),S_OK);
        HR(ID3D12Device_CreateCommandList(host,0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc,NULL,&IID_ID3D12GraphicsCommandList,(void **)&list),S_OK);
        const float left[][4]={{1,0,0,1},{0,0,0,1}},right[][4]={{0,0,1,1},{0,0,0,1}};
        RECT half={W/2,0,W,H};
        if(!i) {
            D3D12_GPU_DESCRIPTOR_HANDLE cleargpu;heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(heap,&cleargpu); cleargpu.ptr+=2*stride;
            ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&heap);
            ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat(list,cleargpu,clearcpu,tex,left[i],0,NULL);
        }
        D3D12_RESOURCE_BARRIER to_rt={.Transition={tex,0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_RENDER_TARGET}};
        ID3D12GraphicsCommandList_ResourceBarrier(list,1,&to_rt);
        if(i) ID3D12GraphicsCommandList_ClearRenderTargetView(list,rtv,left[i],0,NULL);
        ID3D12GraphicsCommandList_ClearRenderTargetView(list,rtv,right[i],1,&half);
        if(i) {
            D3D12_RESOURCE_BARRIER b[2]={{.Transition={texs[0],0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE}},
                {.Transition={tex,0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_UNORDERED_ACCESS}}};
            ID3D12GraphicsCommandList_ResourceBarrier(list,2,b); ID3D12GraphicsCommandList_SetPipelineState(list,ps);ID3D12GraphicsCommandList_SetComputeRootSignature(list,root);
            ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&heap); D3D12_GPU_DESCRIPTOR_HANDLE gpu;heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(heap,&gpu);
            ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list,0,gpu);ID3D12GraphicsCommandList_Dispatch(list,240,135,1);
            for(unsigned j=0;j<2;j++){D3D12_RESOURCE_STATES t=b[j].Transition.StateBefore;b[j].Transition.StateBefore=b[j].Transition.StateAfter;b[j].Transition.StateAfter=t;}ID3D12GraphicsCommandList_ResourceBarrier(list,2,b);
        }
        HR(ID3D12GraphicsCommandList_Close(list),S_OK);
        ID3D12CommandList *batch[]={(ID3D12CommandList *)list}; ID3D12CommandQueue_ExecuteCommandLists(producer,1,batch);
        key=&keys[i].i; wire[0]=token[i];
        #define ARM(queue_,hwnd_,state_,flags_,hf_) xs_display_arm_primary(q,queue_,hwnd_,token[i],&keys[i].i,tex,state_,flags_,hf_)
        HR(ARM(foreign,window,D3D12_RESOURCE_STATE_RENDER_TARGET,0x300005,8),E_INVALIDARG);
        HR(ARM(producer,GetDesktopWindow(),D3D12_RESOURCE_STATE_RENDER_TARGET,0x300005,8),DXGI_ERROR_INVALID_CALL);
        HR(ARM(producer,window,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,0x300005,8),E_NOTIMPL);
        HR(ARM(producer,window,D3D12_RESOURCE_STATE_RENDER_TARGET,0x300005,0),E_NOTIMPL);
        HR(ARM(producer,window,D3D12_RESOURCE_STATE_RENDER_TARGET,0x5,8),E_NOTIMPL);
        HR(ARM(producer,window,D3D12_RESOURCE_STATE_RENDER_TARGET,0x300005,8),S_OK);
        HR(ARM(producer,window,D3D12_RESOURCE_STATE_RENDER_TARGET,0x300005,8),DXGI_ERROR_INVALID_CALL);
        if(i) ID3D12Resource_Release(tex);
        CHECK(IUnknown_Release(key)==1);
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
    CHECK(!released); ID3D12DescriptorHeap_Release(clearheap); ID3D12DescriptorHeap_Release(heap);ID3D12PipelineState_Release(ps);ID3D12RootSignature_Release(root);ID3D12Resource_Release(texs[0]); CHECK(released);
    ID3D12CommandQueue_Release(foreign); ID3D12Device_Release(host);
    IDXGISwapChain3_Release(chain); IDXGIFactory2_Release(factory);
    IUnknown_Release(q); IUnknown_Release(d); DestroyWindow(window);
    puts("PASS imported D3D12: 64 MiB sentinel intact at creation; initial UAV clear, both UAV->RT transitions, RTV/SRV/UAV, owner bytes and two +100 flips checked");
    return 0;
}
