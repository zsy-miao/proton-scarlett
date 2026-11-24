#include "native_pool.h"
struct NativePool {
    NativePool *next;void *base;UINT64 id;HANDLE retired;HMODULE module;
    ID3D12Heap *heap;ID3D12Resource *buffer;ID3D12Device *owner;
};
static SRWLOCK native_lock=SRWLOCK_INIT;
static NativePool *native_pools;
static void drop_native(NativePool *p){if(p->buffer)p->buffer->Release();if(p->heap)p->heap->Release();if(p->owner)p->owner->Release();if(p->retired)CloseHandle(p->retired);delete p;}
static DWORD WINAPI retire_native(void *ctx){
    NativePool *p=(NativePool*)ctx;HMODULE module=p->module;
    if(WaitForSingleObject(p->retired,INFINITE)!=WAIT_OBJECT_0)return 1;
    AcquireSRWLockExclusive(&native_lock);NativePool **q=&native_pools;while(*q!=p)q=&(*q)->next;*q=p->next;ReleaseSRWLockExclusive(&native_lock);
    drop_native(p);FreeLibraryAndExitThread(module,0);
}
static HRESULT create_native_pool(ID3D12Device3 *device,void **base){
    using Fn=HRESULT(WINAPI*)(ID3D12Device3*,ID3D12Heap**,void**);
    auto fn=(Fn)(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettNativePoolV1");if(!fn)return S_FALSE;
    NativePool *p=new(std::nothrow) NativePool{};if(!p)return E_OUTOFMEMORY;
    D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=0x89700000u;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    native_pool_ctl(0x53430102,nullptr,nullptr,0);HRESULT hr=fn(device,&p->heap,&p->base);native_pool_ctl(0x53430100,nullptr,nullptr,0);
    if(hr==S_OK && native_pool_ctl(0x53430103,p->base,&p->id,sizeof p->id)<0)hr=DXGI_ERROR_UNSUPPORTED;
    if(hr==S_OK)hr=device->CreatePlacedResource(p->heap,0,&rd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&p->buffer));
    if(hr==S_OK)hr=device->QueryInterface(IID_PPV_ARGS(&p->owner));
    if(hr==S_OK && (!(p->retired=CreateEventW(nullptr,TRUE,FALSE,nullptr)) || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,(LPCSTR)retire_native,&p->module)))hr=HRESULT_FROM_WIN32(GetLastError());
    if(hr==S_OK && native_pool_ctl(0x53430105,p->base,&p->retired,sizeof p->retired)<0)hr=E_FAIL;
    if(hr==S_OK){
        AcquireSRWLockExclusive(&native_lock);HANDLE thread=CreateThread(nullptr,0,retire_native,p,0,nullptr);
        if(thread){p->next=native_pools;native_pools=p;*base=p->base;CloseHandle(thread);}else hr=HRESULT_FROM_WIN32(GetLastError());ReleaseSRWLockExclusive(&native_lock);
    }
    if(hr!=S_OK){HMODULE m=p->module;drop_native(p);if(m)FreeLibrary(m);}return hr;
}
extern "C" __declspec(dllexport) HRESULT WINAPI ScarlettAcquireNativePoolV1(ID3D12Device *d,const void *va,SIZE_T bytes,ID3D12Heap **heap,ID3D12Resource **buffer,UINT *offset){
    if(!offset || (!buffer && !heap))return E_POINTER;
    if(buffer)*buffer=nullptr;
    if(heap)*heap=nullptr;
    *offset=0;
    if(!d || !va || !bytes)return E_INVALIDARG;
    HRESULT hr=S_FALSE;
    AcquireSRWLockShared(&native_lock);for(NativePool *p=native_pools;p;p=p->next){
        SIZE_T off=(UINT_PTR)va-(UINT_PTR)p->base;if(off>=0x89700000u)continue;
        UINT64 id=0;if(native_pool_ctl(0x53430103,va,&id,sizeof id)<0 || id!=p->id)continue;
        if(bytes>0x89700000u-off || d!=p->owner)hr=E_INVALIDARG;
        else if(SUCCEEDED(hr=d->GetDeviceRemovedReason())){if(buffer){p->buffer->AddRef();*buffer=p->buffer;}if(heap){p->heap->AddRef();*heap=p->heap;}*offset=(UINT)off;}
        break;
    }ReleaseSRWLockShared(&native_lock);return hr;
}
