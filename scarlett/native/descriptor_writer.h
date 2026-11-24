/* Device-local, identity-qualified resource bridge for a future owning resource factory.
 * A binding is removed before its identity is destroyed; title objects are never cast to host COM. */
struct xs_buffer_binding {
    struct xbox_device *owner;
    IUnknown *identity;
    ID3D12Resource *host;
    UINT64 bytes;
    struct xs_buffer_binding *next;
};
HRESULT xs_bind_view_buffer(IUnknown *device,IUnknown *identity,ID3D12Resource *host,struct xs_buffer_binding **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!device || !identity || !host) return E_INVALIDARG;
    struct xbox_device *d = CONTAINING_RECORD(device,struct xbox_device,iface);
    D3D12_RESOURCE_DESC rd;
    ID3D12Device *hd = NULL;
    host->lpVtbl->GetDesc(host,&rd);
    if (rd.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
        (rd.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) ||
        FAILED(ID3D12Resource_GetDevice(host,&IID_ID3D12Device,(void **)&hd))) return E_INVALIDARG;
    BOOL same = hd == d->host;
    ID3D12Device_Release(hd);
    if (!same) return E_INVALIDARG;
    struct xs_buffer_binding *b = HeapAlloc(GetProcessHeap(),0,sizeof *b);
    if (!b) return E_OUTOFMEMORY;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    for (struct xs_buffer_binding *p=d->buffers;p;p=p->next) if (p->identity == identity) {
        ReleaseSRWLockExclusive(&d->descriptor_lock);
        HeapFree(GetProcessHeap(),0,b);
        return E_INVALIDARG;
    }
    *b=(struct xs_buffer_binding){d,identity,host,rd.Width,d->buffers};
    IUnknown_AddRef(device);
    IUnknown_AddRef(identity);
    ID3D12Resource_AddRef(host);
    d->buffers=b;
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    *out=b;
    return S_OK;
}
void xs_unbind_view_buffer(struct xs_buffer_binding *b)
{
    struct xbox_device *d=b->owner;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    struct xs_buffer_binding **p=&d->buffers;
    while (*p!=b) p=&(*p)->next;
    *p=b->next;
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    ID3D12Resource_Release(b->host);
    IUnknown_Release(b->identity);
    IUnknown_Release(&d->iface);
    HeapFree(GetProcessHeap(),0,b);
}

enum { CBV=0x88, SRV=0x90, UAV=0x98, RTV=0xa0, DSV=0xa8 };
static const UINT64 inline_null_cbv[2]={0x10000000000000ull,0x2403c00000000ull};
static BOOL copy_in(void *out,const void *in,SIZE_T size)
{
    SIZE_T n;
    return in && ReadProcessMemory(GetCurrentProcess(),in,out,size,&n) && size==n;
}

/* The heap owner keeps the selected slot live. On this Wine build a
 * self-process read copies into its local output under exception protection;
 * WriteProcessMemory instead asks the server to write a DMA mapping. */
static BOOL store_slot_tag(SIZE_T slot,const UINT64 tag[2])
{
    MEMORY_BASIC_INFORMATION m;
    if (VirtualQuery((void *)slot,&m,sizeof m)!=sizeof m || m.State!=MEM_COMMIT ||
        (m.Protect & ~PAGE_WRITECOMBINE)!=PAGE_READWRITE) return FALSE;
    SIZE_T off=slot-(SIZE_T)m.BaseAddress;
    if (off>=m.RegionSize || 16>m.RegionSize-off) return FALSE;
    return copy_in((void *)slot,tag,16);
}

#ifdef SCARLETT_STAGED_PLACED
/* S_FALSE means this is not a placed identity. Never install a dummy host view. */
static HRESULT placed_write(struct xbox_device *d,UINT kind,IUnknown *object,const DWORD *w,SIZE_T dest)
{
    HRESULT hr=S_FALSE;
    struct descriptor_record old={0};
    AcquireSRWLockExclusive(&d->descriptor_lock);
    struct placed_backing *p=d->placed;
    while (p && &p->title->iface!=object) p=p->next;
    if (!p) goto done;
    hr=E_NOTIMPL;
    if (w[0]!=DXGI_FORMAT_R32_TYPELESS || w[1]!=1 || w[8]!=1) goto done;
    if (kind==SRV) {
        if (w[2]!=0x1688 || w[4] || w[5] || w[6]!=PLACED_WIDTH/4 || w[7]) goto done;
    } else if (kind==UAV) {
        if (w[2] || w[3] || w[4]!=PLACED_WIDTH/4 || w[5] || w[6] || w[7]) goto done;
    } else goto done;
    hr=E_INVALIDARG;
    struct desc_heap *h=NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    for (struct desc_heap *it=d->heaps;it;it=it->next) {
        if (FAILED(descriptor_slot(&it->iface,dest,&cpu,NULL))) continue;
        if (h) goto done;
        h=it;
    }
    if (!h) goto done;
    struct descriptor_record *r=h->records+(dest-h->start)/h->stride;
    UINT64 tag[2]={++d->descriptor_serial,0x5853444553430000ull|kind};
    if (!store_slot_tag(dest,tag)) {
        hr=E_POINTER; goto done;
    }
    old=*r;
    IUnknown_AddRef(object);
    *r=(struct descriptor_record){.serial=tag[0],.kind=kind,.placed=object};
    hr=S_OK;
done:
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    drop_record(old);
    return hr;
}
#endif
/* Hold the owner lock until the host write finishes. Ambiguous overlapping title ranges fail. */
static HRESULT write_view(IUnknown *device,UINT kind,IUnknown *object,const void *input,SIZE_T dest,ID3D12Resource *wc)
{
    struct xbox_device *d=CONTAINING_RECORD(device,struct xbox_device,iface);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv={0};
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav={0};
    D3D12_RENDER_TARGET_VIEW_DESC rtv={0};
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv={0};
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv={0};
    DWORD words[10]={0};
    SIZE_T size=kind==DSV?24:kind==RTV || kind==CBV || (object && kind==UAV)?16:(object && kind==SRV)?32:40;
    if (kind!=CBV && kind!=SRV && kind!=UAV && kind!=RTV && kind!=DSV) return E_INVALIDARG;
    if (!copy_in(words,input,size)) return E_POINTER;
    BOOL ms_srv=object && kind==SRV && words[1]==D3D12_SRV_DIMENSION_TEXTURE2DMS;
    if (object && (kind==UAV || kind==SRV) && !ms_srv && words[1]!=D3D12_SRV_DIMENSION_TEXTURE2D &&
        !copy_in(words,input,sizeof words)) return E_POINTER;
#ifdef SCARLETT_STAGED_PLACED
    IUnknown *old_placed=NULL;
    if (object && (kind==SRV || kind==UAV)) {
        HRESULT placed=placed_write(d,kind,object,words,dest);
        if (placed!=S_FALSE) return placed;
    }
#endif
    BOOL single_color=words[0]==DXGI_FORMAT_A8_UNORM || words[0]==DXGI_FORMAT_B8G8R8A8_UNORM;
    if (wc) {
        srv.Format=DXGI_FORMAT_R16_UINT; srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
        memcpy(&srv.Buffer.FirstElement,words+4,sizeof srv.Buffer.FirstElement);
        srv.Shader4ComponentMapping=words[2]; srv.Buffer.NumElements=words[6];
    } else if (kind==DSV) {
        if (!object || words[0]!=DXGI_FORMAT_D32_FLOAT_S8X24_UINT || words[1]!=D3D12_DSV_DIMENSION_TEXTURE2DMS ||
            memcmp(words+2,(DWORD[4]){0},16)) return E_NOTIMPL;
        dsv.Format=words[0]; dsv.ViewDimension=words[1];
    } else if (kind==RTV) {
        if (!object || (words[0]!=DXGI_FORMAT_R8G8B8A8_UNORM && !single_color) || words[2] || words[3]) return E_NOTIMPL;
        if (words[1]==D3D12_RTV_DIMENSION_TEXTURE2DMS) {
            if (single_color) return E_NOTIMPL;
            /* +cc2985 zeros 24 bytes; the MS form has no active union fields. */
            if (!copy_in(words+4,(const BYTE *)input+16,8)) return E_POINTER;
            if (words[4] || words[5]) return E_NOTIMPL;
        } else if (words[1]!=D3D12_RTV_DIMENSION_TEXTURE2D) return E_NOTIMPL;
        rtv.Format=words[0];
        rtv.ViewDimension=words[1];
    } else if (!object && kind!=CBV) {
        if (words[0]!=42 || words[2]!=(kind==SRV?0x1688u:0u) ||
            memcmp(words+3,(DWORD[7]){0},28)) return E_NOTIMPL;
        UINT dim=words[1];
        if (kind==SRV) {
            if (dim!=1 && dim!=2 && dim!=4 && dim!=8 && dim!=9) return E_NOTIMPL;
            srv.Format=DXGI_FORMAT_R32_UINT; srv.ViewDimension=dim; srv.Shader4ComponentMapping=words[2];
            if (dim==1) srv.Buffer.NumElements=1;
            else srv.Texture1D.MipLevels=1;
        } else {
            if (dim!=1 && dim!=2 && dim!=4 && dim!=5 && dim!=8) return E_NOTIMPL;
            uav.Format=DXGI_FORMAT_R32_UINT; uav.ViewDimension=dim;
            if (dim==1) uav.Buffer.NumElements=1;
            else if (dim==5) uav.Texture2DArray.ArraySize=1;
            else if (dim==8) uav.Texture3D.WSize=1;
        }
    } else if (!object) {
        if (memcmp(words,(DWORD[4]){0},16)) return E_NOTIMPL;
    } else if (kind==UAV && words[0]==DXGI_FORMAT_R8G8B8A8_UNORM &&
               words[1]==D3D12_UAV_DIMENSION_TEXTURE2D && !words[2] && !words[3]) {
        uav.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    } else if (ms_srv) {
        if ((words[0]!=DXGI_FORMAT_R8G8B8A8_UNORM && words[0]!=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS) || words[2]!=0x1688 ||
                memcmp(words+3,(DWORD[5]){0},5*sizeof(DWORD))) return E_NOTIMPL;
        srv.Format=words[0];
        srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DMS;
        srv.Shader4ComponentMapping=words[2];
    } else if (kind==SRV && (words[0]==DXGI_FORMAT_R8G8B8A8_UNORM || (single_color && !words[3])) &&
               words[1]==D3D12_SRV_DIMENSION_TEXTURE2D && words[2]==0x1688 &&
               !words[4] && words[5]==1 && !words[6] && !words[7]) {
        srv.Format=words[0];
        srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=words[2];
        srv.Texture2D.MipLevels=1;
    } else {
        if (kind!=SRV || words[0]!=DXGI_FORMAT_R32_TYPELESS || words[1]!=1 || words[2]!=0x1688 ||
            !words[6] || words[7] || words[8]!=D3D12_BUFFER_SRV_FLAG_RAW) return E_NOTIMPL;
        srv.Format=DXGI_FORMAT_R32_TYPELESS; srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
        srv.Shader4ComponentMapping=words[2]; memcpy(&srv.Buffer.FirstElement,words+4,8);
        srv.Buffer.NumElements=words[6]; srv.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
    }
    HRESULT hr=E_INVALIDARG;
    ID3D12Resource *resource=wc,*old=NULL;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    if (object && (kind==DSV || kind==RTV || kind==UAV || ms_srv || srv.ViewDimension==D3D12_SRV_DIMENSION_TEXTURE2D)) {
        for (struct color_resource *c=d->colors;c;c=c->next)
            if (&c->iface==object && c->msaa==(kind==DSV || ms_srv || rtv.ViewDimension==D3D12_RTV_DIMENSION_TEXTURE2DMS) &&
                c->depth==(kind==DSV || srv.Format==DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS) && c->single_format==(single_color?(DXGI_FORMAT)words[0]:DXGI_FORMAT_UNKNOWN)) {
                resource=c->host; break;
            }
        if (!resource) goto done;
    } else if (object) {
        for (struct xs_buffer_binding *b=d->buffers;b;b=b->next) if (b->identity==object) {
            if (srv.Buffer.FirstElement>b->bytes/4 ||
                srv.Buffer.NumElements>b->bytes/4-srv.Buffer.FirstElement) goto done;
            resource=b->host;
            break;
        }
        if (!resource) goto done;
    }
    struct desc_heap *h=NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    for (struct desc_heap *p=kind==DSV?d->dsv_heaps:kind==RTV?d->rtv_heaps:d->heaps;p;p=p->next) {
        if (FAILED(descriptor_slot(&p->iface,dest,&cpu,NULL))) continue;
        if (h) goto done;
        h=p;
    }
    if (!h || (wc && (!h->visible || h->count!=DESC_STARTUP_COUNT)) ||
        FAILED(descriptor_slot(&h->iface,dest,&cpu,NULL))) goto done;
    struct descriptor_record *r=h->records+(dest-h->start)/h->stride;
    UINT64 tag[2]={++d->descriptor_serial,0x5853444553430000ull|kind};
    if (!store_slot_tag(dest,kind==CBV?inline_null_cbv:tag)) {
        hr=E_POINTER; goto done;
    }
    if (kind==DSV) ID3D12Device_CreateDepthStencilView(d->host,resource,&dsv,cpu);
    else if (kind==RTV) ID3D12Device_CreateRenderTargetView(d->host,resource,&rtv,cpu);
    else if (kind==SRV) ID3D12Device_CreateShaderResourceView(d->host,resource,&srv,cpu);
    else if (kind==UAV) ID3D12Device_CreateUnorderedAccessView(d->host,resource,NULL,&uav,cpu);
    else ID3D12Device_CreateConstantBufferView(d->host,&cbv,cpu);
    old=r->resource;
#ifdef SCARLETT_STAGED_PLACED
    old_placed=r->placed;
#endif
    if (resource) ID3D12Resource_AddRef(resource);
    *r=(struct descriptor_record){.serial=tag[0],.kind=kind,.resource=resource};
    hr=S_OK;
done:
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    if (old) ID3D12Resource_Release(old);
#ifdef SCARLETT_STAGED_PLACED
    if (old_placed) IUnknown_Release(old_placed);
#endif
    return hr;
}

HRESULT xs_write_view(IUnknown *device,UINT kind,IUnknown *object,const void *input,SIZE_T dest)
{
    return write_view(device,kind,object,input,dest,NULL);
}

/* Consuming adapter holds the heap through retirement; revalidate title writes at use. */
HRESULT xs_resolve_view(IUnknown *heap,SIZE_T slot,UINT kind,ID3D12DescriptorHeap **host,D3D12_GPU_DESCRIPTOR_HANDLE *gpu)
{
    if (!host || !gpu) return E_POINTER;
    *host=NULL; gpu->ptr=0;
    if (!heap || heap->lpVtbl!=&desc_vtbl.unknown) return E_INVALIDARG;
    struct desc_heap *h=CONTAINING_RECORD(heap,struct desc_heap,iface);
    if (!h->records || (kind!=CBV && kind!=SRV && kind!=UAV)) return E_INVALIDARG;
    struct xbox_device *d=h->owner;
#ifdef SCARLETT_STAGED_PLACED
    IUnknown *old_placed=NULL;
#endif
    AcquireSRWLockExclusive(&d->descriptor_lock);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    ID3D12DescriptorHeap *bank;
    HRESULT hr=descriptor_range(heap,slot,1,&bank,&cpu,gpu);
    if (SUCCEEDED(hr)) {
        UINT64 bits[2];
        struct descriptor_record *r=h->records+(slot-h->start)/h->stride;
        if (!copy_in(bits,(void *)slot,sizeof bits)) hr=E_POINTER;
        else if (kind==SRV && refresh_required(r->kind)) hr=E_NOTIMPL;
        else if (kind==SRV && r->kind==RAW38_VIEW) {
            if (!r->resource || bits[0]!=r->serial || bits[1]!=(0x5853444553430000ull|RAW38_VIEW)) hr=E_INVALIDARG;
        }
        else if (kind==CBV && !memcmp(bits,inline_null_cbv,16)) {
            if (r->kind!=CBV) {
                D3D12_CONSTANT_BUFFER_VIEW_DESC zero={0};
                ID3D12Device_CreateConstantBufferView(d->host,&zero,cpu);
                if (r->resource) ID3D12Resource_Release(r->resource);
#ifdef SCARLETT_STAGED_PLACED
                old_placed=r->placed;
#endif
                *r=(struct descriptor_record){.kind=CBV};
            }
        } else if (!r->serial || r->kind!=kind || bits[0]!=r->serial || bits[1]!=(0x5853444553430000ull|kind))
             hr=E_INVALIDARG;
#ifdef SCARLETT_STAGED_PLACED
        else if (r->placed) hr=E_NOTIMPL; /* no single desktop descriptor */
#endif
        if (SUCCEEDED(hr)) *host=bank; /* borrowed */
    }
    ReleaseSRWLockExclusive(&d->descriptor_lock);
#ifdef SCARLETT_STAGED_PLACED
    if (old_placed) IUnknown_Release(old_placed);
#endif
    if (FAILED(hr)) gpu->ptr=0;
    return hr;
}
static void WINAPI write_cbv(IUnknown *d,const void *desc,SIZE_T dest)
{
    if (FAILED(xs_write_view(d,CBV,NULL,desc,dest))) xs_stop();
}

static void WINAPI write_rtv(IUnknown *d,IUnknown *r,const void *desc,SIZE_T dest)
{
    if (FAILED(xs_write_view(d,RTV,r,desc,dest))) xs_stop();
}

static void WINAPI write_dsv(IUnknown *d,IUnknown *r,const void *desc,SIZE_T dest)
{
    if (FAILED(xs_write_view(d,DSV,r,desc,dest))) xs_stop();
}

/* The owner lock protects the descriptor record, native slot and heap through the caller's use. */
static ID3D12Resource *attachment_locked(struct desc_heap *h,SIZE_T slot,UINT kind,D3D12_CPU_DESCRIPTOR_HANDLE *cpu)
{
    if (!h->records || FAILED(descriptor_slot(&h->iface,slot,cpu,NULL))) return NULL;
    struct descriptor_record *r=h->records+(slot-h->start)/h->stride;
    UINT64 tag[2];
    return r->kind==kind && copy_in(tag,(void *)slot,sizeof tag) && tag[0]==r->serial &&
            tag[1]==(0x5853444553430000ull|kind) ? r->resource : NULL;
}

/* Caller keeps heap and returned texture live through retirement, and
 * synchronizes descriptor rewrites as with xs_resolve_view. */
static HRESULT resolve_attachment(IUnknown *heap,SIZE_T slot,UINT kind,D3D12_CPU_DESCRIPTOR_HANDLE *handle,ID3D12Resource **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!handle) return E_POINTER;
    handle->ptr=0;
    if (!heap || heap->lpVtbl!=&desc_vtbl.unknown) return E_INVALIDARG;
    struct desc_heap *h=CONTAINING_RECORD(heap,struct desc_heap,iface);
    if (!(kind==DSV?h->dsv:h->rtv) || !h->records) return E_INVALIDARG;
    struct xbox_device *d=h->owner;
    AcquireSRWLockShared(&d->descriptor_lock);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    if ((*out=attachment_locked(h,slot,kind,&cpu))) {
        *handle=cpu;
        ID3D12Resource_AddRef(*out);
    }
    ReleaseSRWLockShared(&d->descriptor_lock);
    return *out ? S_OK : E_INVALIDARG;
}
/* Non-shader-visible RTVs are consumed at record time; serialize that read with all rewrites.
 * The +490 recording and its submission fence already own the destination image. */
HRESULT xs_clear_primary_rtv(IUnknown *owner,ID3D12GraphicsCommandList7 *list,SIZE_T slot,ID3D12Resource *primary)
{
    if (!owner || !list || !primary) return E_INVALIDARG;
    struct xbox_device *d=CONTAINING_RECORD(owner,struct xbox_device,iface);
    HRESULT hr=E_INVALIDARG;
    AcquireSRWLockShared(&d->descriptor_lock);
    for (struct desc_heap *h=d->rtv_heaps;h;h=h->next) {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu;
        if (attachment_locked(h,slot,RTV,&cpu)!=primary) continue;
        ID3D12GraphicsCommandList7_ClearRenderTargetView(list,cpu,(const FLOAT[4]){0},0,NULL);
        hr=S_OK;
        break;
    }
    ReleaseSRWLockShared(&d->descriptor_lock);
    return hr;
}
HRESULT xs_resolve_color_rtv(IUnknown *heap,SIZE_T slot,D3D12_CPU_DESCRIPTOR_HANDLE *handle,ID3D12Resource **out)
{
    return resolve_attachment(heap,slot,RTV,handle,out);
}
HRESULT xs_resolve_depth_dsv(IUnknown *heap,SIZE_T slot,D3D12_CPU_DESCRIPTOR_HANDLE *handle,ID3D12Resource **out)
{
    return resolve_attachment(heap,slot,DSV,handle,out);
}
static void WINAPI write_srv(IUnknown *d,IUnknown *r,const void *desc,SIZE_T dest)
{
    if (FAILED(xs_write_view(d,SRV,r,desc,dest))) xs_stop();
}
#ifdef SCARLETT_STAGED_PLACED
#include "xmem/marked_section.h"
/* Each lookup pins the live section; a reused VA cannot hit another generation's import. */
enum { WC_POOL_BYTES=0x89700000u };
static HRESULT native_pool_ref(ID3D12Device *host,const void *va,SIZE_T bytes,ID3D12Heap **heap,ID3D12Resource **buffer,UINT *off)
{
    typedef HRESULT(WINAPI *Fn)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*);
    Fn f=(Fn)(void*)GetProcAddress(GetModuleHandleW(L"xmem.dll"),"ScarlettAcquireNativePoolV1");return f?f(host,va,bytes,heap,buffer,off):S_FALSE;
}
static HRESULT retain_wc_buffer(IUnknown *device,const void *va,UINT64 bytes,ID3D12Resource **out,UINT *offset)
{
    const SIZE_T pool_size=WC_POOL_BYTES;
    struct scarlett_section_info i;
    HRESULT hr=native_pool_ref(CONTAINING_RECORD(device,struct xbox_device,iface)->host,va,bytes,NULL,out,offset);
    if(hr!=S_FALSE)return hr;
    hr=scarlett_query_section(0x53430005,va,bytes,&i);
    if (FAILED(hr)) return hr;
    if (!i.section) return E_INVALIDARG;
    struct xbox_device *d=CONTAINING_RECORD(device,struct xbox_device,iface);
    ID3D12Resource *resource=NULL;
    if (i.view_size!=pool_size || i.offset!=(UINT_PTR)va-(UINT_PTR)i.base ||
        bytes>pool_size || i.offset>pool_size-bytes) hr=E_INVALIDARG;
    else {
        AcquireSRWLockExclusive(&d->descriptor_lock);
        BOOL (WINAPI *same)(HANDLE,HANDLE)=(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles");
        if (!d->pool_section || !same || !same(i.section,d->pool_section)) {
            HRESULT (WINAPI *open_wc)(ID3D12Device *,SIZE_T,HANDLE,HANDLE *,ID3D12Heap **)=
                (void *)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettWcHeapV1");
            ID3D12Heap *heap=NULL;
            hr=open_wc ? open_wc(d->host,pool_size,i.section,NULL,&heap) : E_NOTIMPL;
            if (SUCCEEDED(hr)) {
                D3D12_RESOURCE_DESC rd={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=pool_size,.Height=1,
                    .DepthOrArraySize=1,.MipLevels=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
                hr=ID3D12Device_CreatePlacedResource(d->host,heap,0,&rd,D3D12_RESOURCE_STATE_COMMON,NULL,&IID_ID3D12Resource,(void **)&resource);
                ID3D12Heap_Release(heap);
                if (SUCCEEDED(hr)) {
                    if (d->pool_buffer) ID3D12Resource_Release(d->pool_buffer);
                    if (d->pool_section) CloseHandle(d->pool_section);
                    d->pool_buffer=resource; d->pool_section=i.section; i.section=NULL;
                }
            }
        }
        if (SUCCEEDED(hr)) { resource=d->pool_buffer; ID3D12Resource_AddRef(resource); }
        ReleaseSRWLockExclusive(&d->descriptor_lock);
    }
    if (i.section) CloseHandle(i.section);
    if (FAILED(hr)) return hr;
    *out=resource; *offset=(UINT)i.offset;
    return S_OK;
}

static HRESULT write_pool_srv(IUnknown *device,D3D12_RESOURCE_DESC *rd,DWORD *w,const void *va,SIZE_T dest)
{
    const SIZE_T pool_size=WC_POOL_BYTES;
    UINT64 first; memcpy(&first,w+4,sizeof first);
    if (((UINT_PTR)va&255) || !rd->Width || rd->Width>pool_size || (rd->Width&15) ||
        !w[6] || w[6]>(1u<<D3D12_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP) ||
        first>rd->Width/2 || w[6]>rd->Width/2-first) return E_INVALIDARG;
    ID3D12Resource *resource;
    UINT offset;
    HRESULT hr=retain_wc_buffer(device,va,rd->Width,&resource,&offset);
    if (FAILED(hr)) return hr;
    first+=offset/2; memcpy(w+4,&first,sizeof first);
    hr=write_view(device,SRV,NULL,w,dest,resource);
    ID3D12Resource_Release(resource);
    return hr;
}

#include "r8_view.h"

/* 5bdba51..5bdba67: SRV by title VA, not by COM identity or desktop GPU VA. */
HRESULT xs_write_address_srv(IUnknown *device,const void *resource,const void *view,SIZE_T dest)
{
    D3D12_RESOURCE_DESC rd;
    DWORD w[16];
    if (!copy_in(&rd,resource,offsetof(D3D12_RESOURCE_DESC,Flags)+sizeof rd.Flags) ||
        !copy_in(w,view,sizeof w)) return E_POINTER;
    if (!rd.Alignment && (UINT)rd.Layout==SRGB_ROW_LAYOUT && is_srgb_row(&rd)) return write_wc_address_view(device,SRGB_ROW_VIEW,w,dest);
    if (!rd.Alignment && (UINT)rd.Layout==R8_LAYOUT) {
        if (is_layered_texel(&rd)) {
            UINT layers=rd.DepthOrArraySize,kind=ARRAY_VIEW_BASE+layers;
            if (layers==CUBE_PAIR_FACES) kind=CUBE_PAIR_VIEW;
            else if (layers==CUBE1_FACES && w[1]==D3D12_SRV_DIMENSION_TEXTURECUBE) kind=CUBE1_VIEW;
            return write_wc_address_view(device,kind,w,dest);
        }
        if (is_r8(&rd)) return write_wc_address_view(device,R8_VIEW,w,dest);
        if (is_texel32(&rd)) return write_wc_address_view(device,is_volume1(&rd)?VOLUME1_VIEW:TEXEL32_BASE+rd.Format,w,dest);
    }
    if (rd.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || rd.Alignment ||
        rd.Height!=1 || rd.DepthOrArraySize!=1 || rd.MipLevels!=1 || rd.Format ||
        rd.SampleDesc.Count!=1 || rd.SampleDesc.Quality || rd.Layout!=D3D12_TEXTURE_LAYOUT_ROW_MAJOR || rd.Flags ||
        w[1]!=1 || w[2]!=0x1688 || w[12] || w[13] || w[14] || w[15]) return E_NOTIMPL;
    if (w[0]==DXGI_FORMAT_UNKNOWN && w[6] && w[7] && w[7]<=2048 && !(w[7]&3) &&
            rd.Width<=STRUCT_MAX_BYTES && rd.Width==(UINT64)w[6]*w[7])
        return write_wc_address_view(device,STRUCT_VIEW,w,dest);
    if (w[7]) return E_NOTIMPL;
    UINT64 va; memcpy(&va,w+10,sizeof va);
    if (w[0]==DXGI_FORMAT_R16_UINT && !w[8])
        return write_pool_srv(device,&rd,w,(const void *)va,dest);
    if (rd.Width==160 && w[0]==DXGI_FORMAT_R32_TYPELESS)
        return write_wc_address_view(device,RAW38_VIEW,w,dest);
    if (rd.Width!=PLACED_WIDTH || w[0]!=DXGI_FORMAT_R32_TYPELESS || w[4] || w[5] || w[6]!=PLACED_WIDTH/4 || w[8]!=1)
        return E_NOTIMPL;
    struct xbox_device *d=CONTAINING_RECORD(device,struct xbox_device,iface);
    struct placed_backing *found=NULL;
    AcquireSRWLockShared(&d->descriptor_lock);
    for (struct placed_backing *p=d->placed;p;p=p->next) if (p->guest_va==va) {
        if (found) { found=NULL; break; } /* address alone cannot choose between wrappers */
        found=p;
    }
    IUnknown *id=NULL;
    if (found && found->matches(found->capture,(const void *)va)) {
        id=&found->title->iface;
        IUnknown_AddRef(id);
    }
    ReleaseSRWLockShared(&d->descriptor_lock);
    if (!id) return E_INVALIDARG;
    HRESULT hr=placed_write(d,SRV,id,w,dest);
    IUnknown_Release(id);
    return hr==S_FALSE ? E_INVALIDARG : hr;
}
static void WINAPI write_address_srv(IUnknown *d,const void *rd,const void *desc,SIZE_T dest)
{
    if (FAILED(xs_write_address_srv(d,rd,desc,dest))) xs_stop();
}
#endif
static void WINAPI write_uav(IUnknown *d,IUnknown *r,IUnknown *counter,const void *desc,SIZE_T dest)
{
    if (counter || FAILED(xs_write_view(d,UAV,r,desc,dest))) xs_stop();
}

#ifdef SCARLETT_STAGED_PLACED
/* Private recorder: open same-device copy list and readback destination owned
 * by the caller. Return one title reference for the caller's retirement fence. */
HRESULT xs_placed_copy_readback(IUnknown *heap,SIZE_T slot,UINT64 at,UINT64 bytes,
        ID3D12GraphicsCommandList *list,ID3D12Resource *dst,UINT64 offset,IUnknown **hold)
{
    if (!hold) return E_POINTER;
    *hold=NULL;
    if (!heap || heap->lpVtbl!=&desc_vtbl.unknown || !list || !dst || !bytes) return E_INVALIDARG;
    struct desc_heap *h=CONTAINING_RECORD(heap,struct desc_heap,iface);
    if (!h->records || ID3D12GraphicsCommandList_GetType(list)!=D3D12_COMMAND_LIST_TYPE_COPY) return E_INVALIDARG;
    D3D12_RESOURCE_DESC desc;
    D3D12_HEAP_PROPERTIES hp;
    D3D12_HEAP_FLAGS flags;
    dst->lpVtbl->GetDesc(dst,&desc);
    if (desc.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || offset>desc.Width || bytes>desc.Width-offset ||
        FAILED(ID3D12Resource_GetHeapProperties(dst,&hp,&flags)) || hp.Type!=D3D12_HEAP_TYPE_READBACK)
        return E_INVALIDARG;
    struct xbox_device *d=h->owner;
    ID3D12Device *ld=NULL,*dd=NULL;
    HRESULT hr=ID3D12GraphicsCommandList_GetDevice(list,&IID_ID3D12Device,(void **)&ld);
    if (SUCCEEDED(hr)) hr=ID3D12Resource_GetDevice(dst,&IID_ID3D12Device,(void **)&dd);
    BOOL same=SUCCEEDED(hr) && ld==d->host && dd==d->host;
    if (ld) ID3D12Device_Release(ld);
    if (dd) ID3D12Device_Release(dd);
    if (!same) return E_INVALIDARG;
    IUnknown *id=NULL;
    AcquireSRWLockShared(&d->descriptor_lock);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    if (SUCCEEDED(descriptor_slot(heap,slot,&cpu,NULL))) {
        UINT64 tag[2];
        struct descriptor_record *r=h->records+(slot-h->start)/h->stride;
        if (r->placed && r->kind==SRV && copy_in(tag,(void *)slot,sizeof tag) &&
            tag[0]==r->serial && tag[1]==(0x5853444553430000ull|SRV)) {
            id=r->placed;
            IUnknown_AddRef(id);
        }
    }
    ReleaseSRWLockShared(&d->descriptor_lock);
    if (!id) return E_INVALIDARG;
    struct placed_backing *p=CONTAINING_RECORD(id,struct placed_identity,iface)->private;
    struct XsDataOperand op;
    hr=p->data(p->capture,at,bytes,&op);
    if (FAILED(hr)) { IUnknown_Release(id); return hr; }
    ID3D12GraphicsCommandList_CopyBufferRegion(list,dst,offset,op.resource,op.offset,op.bytes);
    *hold=id;
    return S_OK;
}
#endif
