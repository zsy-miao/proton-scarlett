#include <windows.h>
#include <d3d12.h>
#include <stddef.h>
#include <stdint.h>
#include "../xmem/marked_section.h"
#include "../queue/bootstrap_guard.h"
#include "r8_layout.h"

_Static_assert(sizeof(void *) == 8 && sizeof(D3D12_RESOURCE_DESC) == 0x38,
               "requires the x64 descriptor ABI");
_Static_assert(offsetof(D3D12_RESOURCE_DESC, Layout) == 0x2c, "layout field");

__declspec(dllexport) HRESULT WINAPI XGSuggestSwizzleMode(
    const D3D12_RESOURCE_DESC *d, uintptr_t option, D3D12_TEXTURE_LAYOUT *out)
{
    if (!d || !out) return E_POINTER;
    if (!option && !d->Alignment && !d->Layout && is_srgb_row(d)) {
        *out=(D3D12_TEXTURE_LAYOUT)SRGB_ROW_LAYOUT;
        return S_OK;
    }
    /* Project recommendation for the runtime-created texture copied by copy_texture. */
    if (!option && !d->Alignment && d->Layout==D3D12_TEXTURE_LAYOUT_UNKNOWN && (is_r8(d) || is_texel32(d) || is_layered_texel(d))) {
        *out=(D3D12_TEXTURE_LAYOUT)R8_LAYOUT;
        return S_OK;
    }
    /* shortcut: restrict suggestions to the published sample descriptors until more are verified. */
    if (option || d->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        (d->Alignment && d->Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) ||
        d->Width != 4096 || d->Height != 4096 || d->DepthOrArraySize != 1 ||
        d->MipLevels != 1 || d->SampleDesc.Count != 1 || d->SampleDesc.Quality ||
        d->Layout != D3D12_TEXTURE_LAYOUT_UNKNOWN || d->Flags)
        return E_NOTIMPL;

    switch (d->Format) {
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC5_UNORM:
        *out = (D3D12_TEXTURE_LAYOUT)0x109; /* encoded 64KB_S */
        return S_OK;
    default:
        return E_NOTIMPL;
    }
}

struct computer { const void *vt; LONG refs; UINT stack_bytes, layers; };
static ULONG WINAPI add_ref(struct computer *c) { return InterlockedIncrement(&c->refs); }
static ULONG WINAPI release(struct computer *c)
{
    LONG refs=InterlockedDecrement(&c->refs);
    if (!refs) HeapFree(GetProcessHeap(),0,c);
    return refs;
}

/* Acquire the original section, then map by HANDLE; the title VA can be released
 * or reused without redirecting the copy. Never use either alias past this call. */
static HRESULT texture_alias(const void *at,SIZE_T bytes,BOOL wc,void **mapping,BYTE **slot,BOOL *system)
{
    struct scarlett_section_info held,view;
    HRESULT hr=scarlett_query_section(wc?0x53430005:0x5343000c,at,bytes,&held);
    if (FAILED(hr) && !wc) {
        hr=scarlett_acquire_section(at,bytes,&held);
        if (SUCCEEDED(hr)) *system=TRUE;
    }
    if (FAILED(hr)) return hr;
    if (held.view_size!=(wc?0x89700000u:0x49400000u) || held.offset>held.view_size-bytes ||
        (UINT_PTR)at-(UINT_PTR)held.base!=held.offset || (wc && held.offset%4096)) hr=E_INVALIDARG;
    else if (!wc && *system) {
        SIZE_T start=held.offset & ~(SIZE_T)65535, delta=held.offset-start;
        *mapping=MapViewOfFile(held.section,FILE_MAP_READ,0,(DWORD)start,delta+bytes);
        if (*mapping) *slot=(BYTE *)*mapping+delta;
        else hr=HRESULT_FROM_WIN32(GetLastError());
    } else {
        hr=scarlett_query_section_handle(wc?0x53430004:0x5343000b,NULL,held.view_size,&view,held.section);
        if (SUCCEEDED(hr)) {
            *mapping=view.base;
            *slot=(BYTE *)view.base+held.offset;
        }
    }
    CloseHandle(held.section);
    return hr;
}

#include "../xmem/native_pool.h"
static HRESULT WINAPI copy_texture(struct computer *c,void *dst,UINT plane,UINT sub,const void *src,UINT row,UINT slice)
{
    if (!dst || !src) return E_POINTER;
    SIZE_T bytes=c->stack_bytes?c->stack_bytes:R8_BYTES;
    if (plane || sub>=c->layers || row!=(c->stack_bytes?c->stack_bytes:256u) || slice!=bytes) return E_INVALIDARG;
    BYTE *in=NULL,*out=NULL; void *wb=NULL,*wc=NULL;
    BOOL system=FALSE;
    HRESULT hr;
    BYTE snapshot[SRGB_ROW_BYTES];
    if (c->stack_bytes) {
        ULONG_PTR lo,hi,p=(ULONG_PTR)src; SIZE_T got;
        GetCurrentThreadStackLimits(&lo,&hi);
        if (p<lo || p>=hi || hi-p<bytes ||
            !ReadProcessMemory(GetCurrentProcess(),src,snapshot,bytes,&got) || got!=bytes) return E_POINTER;
    } else {
        hr=texture_alias(src,R8_BYTES,FALSE,&wb,&in,&system);
        if (FAILED(hr)) return hr;
    }
    SIZE_T span=c->stack_bytes?c->layers*R8_ALIGNMENT:R8_BYTES,layer_offset=sub*R8_ALIGNMENT;
    struct {UINT64 id;BYTE data[R8_BYTES];} *stage=NULL;UINT64 id=0;
    if(native_pool_ctl(0x53430103,dst,&id,sizeof id)==0){
        UINT64 last=0;
        if ((UINT_PTR)dst%R8_ALIGNMENT || (UINT_PTR)dst>UINTPTR_MAX-(span-1) ||
            native_pool_ctl(0x53430103,(BYTE*)dst+span-1,&last,sizeof last)<0 || last!=id) hr=E_INVALIDARG;
        else {
            stage=HeapAlloc(GetProcessHeap(),0,sizeof(UINT64)+bytes);hr=stage?S_OK:E_OUTOFMEMORY;
            if(stage){stage->id=id;out=stage->data;}
        }
    }else {
        hr=texture_alias(dst,span,TRUE,&wc,&out,NULL);
        if (SUCCEEDED(hr)) out+=layer_offset;
    }
    if (SUCCEEDED(hr)) {
        if (c->stack_bytes) memcpy(out,snapshot,bytes);
        else for (UINT i=0;i<R8_BYTES;i+=16) {
            UINT x=((i>>12)&3)*64+((i>>5)&16)+((i>>6)&32);
            UINT y=(i>>14)*64+((i>>5)&32)+((i>>4)&31);
            memcpy(out+i,in+y*256+x,16);
        }
        MemoryBarrier();
        if(stage){LONG st=native_pool_ctl(0x53430104,(BYTE*)dst+layer_offset,stage,sizeof(UINT64)+bytes);hr=st<0?HRESULT_FROM_NT(st):S_OK;HeapFree(GetProcessHeap(),0,stage);}
        else VirtualFree(wc,0,MEM_RELEASE);
    }
    if (system) UnmapViewOfFile(wb);
    else if (wb) VirtualFree(wb,0,MEM_RELEASE);
    return hr;
}
static const struct {
    __typeof__(&add_ref) add_ref;
    __typeof__(&release) release;
    xs_stop_fn gap[6];
    __typeof__(&copy_texture) copy_into;
    xs_stop_fn other[3];
} methods={add_ref,release,{[0 ... 5]=xs_stop},copy_texture,{[0 ... 2]=xs_stop}};

__declspec(dllexport) HRESULT WINAPI XGCreateTextureComputer(
    const D3D12_RESOURCE_DESC *d, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    D3D12_RESOURCE_DESC desc={0};
    SIZE_T n=offsetof(D3D12_RESOURCE_DESC,Flags)+sizeof desc.Flags,got;
    if (!d || !ReadProcessMemory(GetCurrentProcess(),d,&desc,n,&got) || got!=n) return E_POINTER;
    BOOL layered=is_layered_texel(&desc),texel32=layered || is_texel32(&desc);
    BOOL row=is_srgb_row(&desc);
    if ((!row && !texel32 && !is_r8(&desc)) || (UINT)desc.Layout!=(row?SRGB_ROW_LAYOUT:R8_LAYOUT) || desc.Alignment!=R8_ALIGNMENT) return E_NOTIMPL;
    struct computer *c=HeapAlloc(GetProcessHeap(),0,sizeof *c);
    if (!c) return E_OUTOFMEMORY;
    *c=(struct computer){&methods,1,row?SRGB_ROW_BYTES:texel32?4:0,layered?desc.DepthOrArraySize:1};
    *out=c;
    return S_OK;
}
