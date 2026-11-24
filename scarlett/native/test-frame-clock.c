#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <stdio.h>
#include "frame.h"
HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
typedef HRESULT (WINAPI *Interval)(IUnknown *,IDXGIOutput *,UINT,UINT,UINT);
typedef HRESULT (WINAPI *Schedule)(IUnknown *,UINT,UINT,const void *,UINT);
typedef HRESULT (WINAPI *Wait)(IUnknown *,UINT,DWORD,const void *,UINT,UINT64 *);
#define SLOT(i,n,t) ((t)((void *const *)(i)->lpVtbl)[(n)/8])
#define OK(e) do { HRESULT h=(e); if (h!=S_OK) { fprintf(stderr,"FAIL %d: %s = %08lx\n",__LINE__,#e,(unsigned long)h); return 1; } } while (0)
#define CHECK(e) do { if (!(e)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#e); return 1; } } while (0)
static const IID id={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
struct task { IUnknown *d; UINT64 token; HRESULT result; };
static DWORD WINAPI waiter(void *p)
{
    struct task *t=p;
    t->result=SLOT(t->d,0x280,Wait)(t->d,2,5000,NULL,0,&t->token);
    return 0;
}
int main(void)
{
    DWORD params[20] = {[0]=0x20004,[2]=0x100000,[10]=1,[15]=0x20};
    ID3D12Device *host; IUnknown *d;
    OK(D3D12CreateDevice(NULL,D3D_FEATURE_LEVEL_12_0,&IID_ID3D12Device,(void **)&host));
    OK(D3D12XboxCreateDevice(NULL,params,&id,(void **)&d));
    ID3D12CommandQueue *q;
    D3D12_COMMAND_QUEUE_DESC qd={0};
    OK(ID3D12Device_CreateCommandQueue(host,&qd,&IID_ID3D12CommandQueue,(void **)&q));
    HWND window=CreateWindowExW(0,L"STATIC",L"Frame scheduler",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
            CW_USEDEFAULT,CW_USEDEFAULT,160,120,NULL,NULL,NULL,NULL);
    CHECK(window);
    IDXGIFactory2 *factory; IDXGISwapChain1 *chain;
    OK(CreateDXGIFactory1(&IID_IDXGIFactory2,(void **)&factory));
    DXGI_SWAP_CHAIN_DESC1 sd={.Width=160,.Height=120,.Format=DXGI_FORMAT_R8G8B8A8_UNORM,
        .SampleDesc={1,0},.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT,.BufferCount=2,
        .SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD};
    OK(IDXGIFactory2_CreateSwapChainForHwnd(factory,(IUnknown *)q,window,&sd,NULL,NULL,&chain));
    Wait wait=SLOT(d,0x280,Wait);
    Schedule schedule=SLOT(d,0x278,Schedule);
    Interval set=SLOT(d,0x270,Interval);
    UINT64 a=0,b=0;
    CHECK(FAILED(schedule(d,2,0,NULL,0)));
    OK(set(d,NULL,33333,3,0));
    OK(schedule(d,2,0,NULL,0));
    CHECK(schedule(d,1,0,NULL,0)==E_NOTIMPL);
    CHECK(schedule(d,2,0,&a,0)==E_NOTIMPL);
    CHECK(wait(d,0,0,NULL,2,&a)==DXGI_ERROR_INVALID_CALL && !a);
    OK(schedule(d,0,0,NULL,0));
    OK(wait(d,0,0,NULL,2,&a)); CHECK(a);
    CHECK(wait(d,0,0,NULL,0,&b)==DXGI_ERROR_WAIT_TIMEOUT && !b);
    LARGE_INTEGER t0,t1,freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    OK(wait(d,0,200,NULL,0,&b));
    QueryPerformanceCounter(&t1);
    CHECK((t1.QuadPart-t0.QuadPart)*1000/freq.QuadPart>=20 && a!=b);
    CHECK(wait(d,2,70,NULL,0,&a)==DXGI_ERROR_WAIT_TIMEOUT && a);
    struct task task={d,a,E_PENDING};
    HANDLE thread=CreateThread(NULL,0,waiter,&task,0,NULL); CHECK(thread);
    CHECK(WaitForSingleObject(thread,25)==WAIT_TIMEOUT);
    OK(xs_frame_present(d,a,(IDXGISwapChain *)chain));
    CHECK(WaitForSingleObject(thread,5500)==WAIT_OBJECT_0);
    OK(task.result); CHECK(task.token==a);
    CHECK(xs_frame_present(d,a,(IDXGISwapChain *)chain)==DXGI_ERROR_INVALID_CALL);
    CHECK(wait(d,2,0,NULL,0,&b)==DXGI_ERROR_WAIT_TIMEOUT);
    OK(wait(d,2,0,NULL,0,&a));
    OK(set(d,NULL,16667,1,0));
    OK(schedule(d,2,0,NULL,0));
    CHECK(wait(d,2,0,NULL,0,&a)==DXGI_ERROR_INVALID_CALL);
    CloseHandle(thread);
    IDXGISwapChain1_Release(chain); IDXGIFactory2_Release(factory); DestroyWindow(window);
    ID3D12CommandQueue_Release(q); IUnknown_Release(d); ID3D12Device_Release(host);
    puts("PASS frame clock: paced origins, timeouts, exact presented token, real DXGI swapchain, history reset");
    return 0;
}
