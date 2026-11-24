/* Staged +0x428 ABI, included only in the qualified fixture build. */
#include "xmem/placed_capture.h"
#define PLACED_WIDTH (174ull << 20)
struct placed_identity {
    IUnknown iface;
    LONG refs;
    struct placed_backing *private;
};
struct placed_backing {
    struct xbox_device *owner;
    struct placed_identity *title;
    struct placed_backing *next;
    HMODULE module;
    struct XsPlacedCapture *capture;
    UINT64 guest_va;
    __typeof__(&ScarlettPlacedMatchesV1) matches;
    __typeof__(&ScarlettPlacedDataV1) data;
    __typeof__(&ScarlettReleasePlacedV1) drop;
};
static ULONG WINAPI placed_addref(IUnknown *i)
{
    return InterlockedIncrement(&CONTAINING_RECORD(i,struct placed_identity,iface)->refs);
}
static ULONG WINAPI placed_release(IUnknown *i)
{
    struct placed_identity *title=CONTAINING_RECORD(i,struct placed_identity,iface);
    struct placed_backing *p=title->private;
    struct xbox_device *d=p->owner;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    ULONG refs=InterlockedDecrement(&title->refs);
    if (!refs) {
        struct placed_backing **it=&d->placed;
        while (*it!=p) it=&(*it)->next;
        *it=p->next;
    }
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    if (!refs) {
        p->drop(p->capture);
        FreeLibrary(p->module);
        HeapFree(GetProcessHeap(),0,p);
        HeapFree(GetProcessHeap(),0,title);
        IUnknown_Release(&d->iface);
    }
    return refs;
}
static HRESULT WINAPI placed_query(IUnknown *i,REFIID iid,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!iid) return E_INVALIDARG;
    if (!IsEqualIID(iid,&IID_IUnknown) && !IsEqualIID(iid,&IID_ID3D12Resource)) return E_NOINTERFACE;
    placed_addref(i);
    *out=i;
    return S_OK;
}
static const struct {
    IUnknownVtbl unknown;
    xs_stop_fn unsupported[64];
} placed_vtbl={{placed_query,placed_addref,placed_release},{[0 ... 63]=xs_stop}};

static HRESULT placed_create(struct xbox_device *d,UINT64 va,const void *wire,UINT state,
        const void *clear,REFIID iid,void **out)
{
    D3D12_RESOURCE_DESC rd;
    SIZE_T n,bytes=offsetof(D3D12_RESOURCE_DESC,Flags)+sizeof(rd.Flags);
    if (!out) return E_POINTER;
    *out=NULL;
    if (!iid || !va) return E_INVALIDARG;
    if (!IsEqualIID(iid,&IID_ID3D12Resource)) return E_NOINTERFACE;
    if (!ReadProcessMemory(GetCurrentProcess(),wire,&rd,bytes,&n) || n!=bytes) return E_POINTER;
    if (rd.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || rd.Alignment || rd.Width!=PLACED_WIDTH ||
        rd.Height!=1 || rd.DepthOrArraySize!=1 || rd.MipLevels!=1 || rd.Format!=DXGI_FORMAT_UNKNOWN ||
        rd.SampleDesc.Count!=1 || rd.SampleDesc.Quality || rd.Layout!=D3D12_TEXTURE_LAYOUT_ROW_MAJOR ||
        (UINT)rd.Flags!=0x20004 || state!=UINT_MAX || clear) return E_NOTIMPL;
    struct placed_identity *title=HeapAlloc(GetProcessHeap(),0,sizeof *title);
    struct placed_backing *p=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *p);
    HRESULT hr=E_OUTOFMEMORY;
    if (!p || !title) goto fail;
    hr=E_NOTIMPL;
    if (!GetModuleHandleExW(0,L"xmem.dll",&p->module)) goto fail;
    __typeof__(&ScarlettCapturePlacedV1) take=(void *)GetProcAddress(p->module,"ScarlettCapturePlacedV1");
    p->data=(void *)GetProcAddress(p->module,"ScarlettPlacedDataV1");
    p->matches=(void *)GetProcAddress(p->module,"ScarlettPlacedMatchesV1");
    p->drop=(void *)GetProcAddress(p->module,"ScarlettReleasePlacedV1");
    if (!take || !p->data || !p->drop || !p->matches || !GetProcAddress(p->module,"ScarlettCreatePlacedTableV1") ||
        !GetProcAddress(p->module,"ScarlettBindPlacedTableV1") ||
        !GetProcAddress(p->module,"ScarlettReleasePlacedTableV1")) goto fail;
    ID3D12Device3 *host=NULL;
    hr=ID3D12Device_QueryInterface(d->host,&IID_ID3D12Device3,(void **)&host);
    if (FAILED(hr)) goto fail;
    hr=take(host,(const void *)va,rd.Width,&p->capture);
    ID3D12Device3_Release(host);
    if (FAILED(hr)) goto fail;
    *title=(struct placed_identity){{&placed_vtbl.unknown},1,p};
    p->owner=d; p->title=title; p->guest_va=va;
    IUnknown_AddRef(&d->iface);
    AcquireSRWLockExclusive(&d->descriptor_lock);
    p->next=d->placed; d->placed=p;
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    *out=&title->iface;
    return S_OK;
fail:
    if (p && p->module) FreeLibrary(p->module);
    if (p) HeapFree(GetProcessHeap(),0,p);
    if (title) HeapFree(GetProcessHeap(),0,title);
    return hr;
}
