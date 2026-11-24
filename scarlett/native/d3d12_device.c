#define COBJMACROS
#define CONST_VTABLE
#define INITGUID
#include <windows.h>
#include <d3d12.h>
#ifdef SCARLETT_DEVICE_BOOTSTRAP
#include "queue/queue.h"
#include "queue/present.h"
#include "queue/bootstrap_guard.h"
#include "frame.h"
#include "xg/r8_layout.h"
#include <stddef.h>
#include <stdio.h>
#endif

#ifdef SCARLETT_DEVICE_BOOTSTRAP
struct frame_record {
    UINT64 token;
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
    ID3D12CommandQueue *queue;
    ID3D12Fence *sync_end,*batch_end;
#endif
    LONGLONG origin, target;
    IDXGISwapChain *chain;
    UINT present_id;
    HRESULT status; /* S_FALSE: issued but not presented; E_PENDING: present submitted. */
    BOOL presenting;
};
struct frame_clock {
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
    BOOL bridge_presenting;
    UINT interval_us;
#endif
    SRWLOCK lock;
    CONDITION_VARIABLE change;
    LONGLONG step, due, freq, base;
    UINT period, registered, epoch, cursor;
    struct frame_record history[16];
};
static void drop_submissions(struct frame_record *r)
{
#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
    if (r->sync_end) ID3D12Fence_Release(r->sync_end);
    if (r->batch_end) ID3D12Fence_Release(r->batch_end);
    if (r->queue) ID3D12CommandQueue_Release(r->queue);
#else
    (void)r;
#endif
}
static void frame_clear(struct frame_clock *f)
{
    for (unsigned i=0; i<ARRAYSIZE(f->history); ++i) {
        drop_submissions(&f->history[i]);
        if (f->history[i].chain) IDXGISwapChain_Release(f->history[i].chain);
    }
    memset(f->history, 0, sizeof(f->history));
}
#endif

struct xbox_device {
    IUnknown iface;
    LONG refs;
    ID3D12Device *host;
#ifdef SCARLETT_DEVICE_BOOTSTRAP
    BYTE inline_descriptor_mode;
    SRWLOCK descriptor_lock;
    struct desc_heap *heaps;
    struct page_pool *page_pools;
    struct desc_heap *rtv_heaps,*dsv_heaps;
    struct desc_heap *sampler_heaps;
    struct xs_buffer_binding *buffers;
    struct row_buffer *rows;
    struct bvh_root *bvh_roots; /* live identities under descriptor_lock */
    struct color_resource *colors; /* weak registry under descriptor_lock */
#ifdef SCARLETT_STAGED_PLACED
    struct placed_backing *placed; /* weak, guarded by descriptor_lock */
    HANDLE pool_section;
    ID3D12Resource *pool_buffer;
#endif
    UINT64 descriptor_serial;
    struct frame_clock clock;
#endif
};

#ifdef SCARLETT_DEVICE_BOOTSTRAP
_Static_assert(offsetof(struct xbox_device,inline_descriptor_mode)==0x18,"inline descriptor flag");
#include "descriptor_heap.h"
#include "color_resource.h"
#ifdef SCARLETT_STAGED_PLACED
#include "placed_resource.h"
#endif
#include "descriptor_writer.h"
#include "page_pool.h"
#include "bvh_root.h"
#include "bvh_pipeline.h"
#include "clear_pipeline.h"
#ifdef SCARLETT_STAGED_PLACED
#include "placed_shader.h"
#endif
#endif

/* Private host-side operations. No console method returns a host interface. */
static inline HRESULT device_heap_properties(D3D12_HEAP_PROPERTIES *out,
        struct xbox_device *device, UINT node_mask, D3D12_HEAP_TYPE type)
{
    if (!out) return E_POINTER;
    *out = (D3D12_HEAP_PROPERTIES){0};
    if (node_mask != 1 || (type != D3D12_HEAP_TYPE_DEFAULT &&
            type != D3D12_HEAP_TYPE_UPLOAD && type != D3D12_HEAP_TYPE_READBACK))
        return E_INVALIDARG;
    /* MinGW's typed host method takes This before the hidden return destination. */
    device->host->lpVtbl->GetCustomHeapProperties(device->host, out, node_mask, type);
    return S_OK;
}

static inline HRESULT device_direct_queue(struct xbox_device *device,
        const D3D12_COMMAND_QUEUE_DESC *desc, ID3D12CommandQueue **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!desc || desc->Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
            desc->Priority != D3D12_COMMAND_QUEUE_PRIORITY_NORMAL ||
            desc->Flags != D3D12_COMMAND_QUEUE_FLAG_NONE || desc->NodeMask != 1)
        return E_INVALIDARG;
    return ID3D12Device_CreateCommandQueue(device->host, desc, &IID_ID3D12CommandQueue, (void **)out);
}

static ULONG WINAPI device_addref(IUnknown *iface)
{
    struct xbox_device *device = CONTAINING_RECORD(iface, struct xbox_device, iface);
    return InterlockedIncrement(&device->refs);
}

static ULONG WINAPI device_release(IUnknown *iface)
{
    struct xbox_device *device = CONTAINING_RECORD(iface, struct xbox_device, iface);
    ULONG refs = InterlockedDecrement(&device->refs);
    if (!refs) {
#ifdef SCARLETT_DEVICE_BOOTSTRAP
        frame_clear(&device->clock);
        while (device->page_pools) {
            struct page_pool *p=device->page_pools;
            device->page_pools=p->next;
            pool_destroy(p);
        }
#ifdef SCARLETT_STAGED_PLACED
        if (device->pool_buffer) ID3D12Resource_Release(device->pool_buffer);
        if (device->pool_section) CloseHandle(device->pool_section);
#endif
#endif
        ID3D12Device_Release(device->host);
        HeapFree(GetProcessHeap(), 0, device);
    }
    return refs;
}

static const IUnknownVtbl device_vtbl;
#ifdef SCARLETT_DEVICE_BOOTSTRAP
static const IID bootstrap_iid = {0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};

static HRESULT WINAPI bootstrap_queue(IUnknown *iface, const D3D12_COMMAND_QUEUE_DESC *d,
        REFIID iid, void **out)
{
    IUnknown *queue = NULL;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid || !d) return E_INVALIDARG;
    IID id;
    D3D12_COMMAND_QUEUE_DESC desc;
    if (!copy_in(&id, iid, sizeof(id)) || !copy_in(&desc, d, sizeof(desc))) return E_POINTER;
    if (!IsEqualIID(&id, &IID_ID3D12CommandQueue)) return E_NOINTERFACE;
    if (desc.Priority != D3D12_COMMAND_QUEUE_PRIORITY_NORMAL ||
            desc.Flags != D3D12_COMMAND_QUEUE_FLAG_NONE || desc.NodeMask != 1) return E_NOTIMPL;
    ID3D12Device *host = CONTAINING_RECORD(iface, struct xbox_device, iface)->host;
    if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) hr = xs_console_direct_queue(host, iface, &queue);
    else if (desc.Type == D3D12_COMMAND_LIST_TYPE_COPY) hr = xs_copy_queue(host, &queue);
    else return E_NOTIMPL;
    *out = queue;
    return hr;
}

static HRESULT WINAPI bootstrap_fence(IUnknown *iface, UINT64 initial, D3D12_FENCE_FLAGS flags,
        REFIID iid, void **out)
{
    IUnknown *fence = NULL;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid) return E_INVALIDARG;
    if (!IsEqualIID(iid, &IID_ID3D12Fence)) return E_NOINTERFACE;
    if (initial || flags != D3D12_FENCE_FLAG_SHARED) return E_NOTIMPL;
    hr = xs_shared_fence(CONTAINING_RECORD(iface, struct xbox_device, iface)->host, &fence);
    *out = fence;
    return hr;
}

static HRESULT WINAPI bootstrap_allocator(IUnknown *iface, D3D12_COMMAND_LIST_TYPE type, REFIID iid, void **out)
{
    IUnknown *allocator = NULL;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid) return E_INVALIDARG;
    if (!IsEqualIID(iid, &IID_ID3D12CommandAllocator)) return E_NOINTERFACE;
    HRESULT hr = xs_allocator(CONTAINING_RECORD(iface, struct xbox_device, iface)->host, type, &allocator);
    *out = allocator;
    return hr;
}

static HRESULT WINAPI bootstrap_list(IUnknown *iface, const void *desc, IUnknown *allocator,
        IUnknown *initial_state, REFIID iid, void **out)
{
    BOOL pooled;
    UINT words[4];
    SIZE_T copied;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!desc || !allocator || !iid) return E_INVALIDARG;
    if (!ReadProcessMemory(GetCurrentProcess(), desc, words, sizeof(words), &copied) || copied != sizeof(words))
        return E_POINTER;
    pooled = IsEqualIID(iid, &xs_pool_list_id);
    if (!pooled && !IsEqualIID(iid, &xs_sync_list_id)) {
        char msg[144];
        snprintf(msg, sizeof(msg), "scarlett: device+0x248 unsupported list IID Data1=%08lx desc=(%u,%u,%u,%u)\n",
                 iid->Data1, words[0], words[1], words[2], words[3]);
        OutputDebugStringA(msg);
        return E_NOINTERFACE;
    }
    if (!xs_allocator_host(allocator, words[0])) return E_INVALIDARG;
    if (initial_state || words[1] != (pooled ? 6u : 2u) || words[2] != 1 || words[3]) return E_NOTIMPL;
    IUnknown *list = NULL;
    HRESULT hr = xs_device_list(CONTAINING_RECORD(iface, struct xbox_device, iface)->host,
            words[0], allocator, pooled, iface, &list);
    *out = list;
    return hr;
}

static D3D12_HEAP_PROPERTIES *WINAPI bootstrap_heap(D3D12_HEAP_PROPERTIES *out, IUnknown *iface,
        UINT mask, D3D12_HEAP_TYPE type)
{
    if (FAILED(device_heap_properties(out, CONTAINING_RECORD(iface, struct xbox_device, iface), mask, type))
            || out->Type != D3D12_HEAP_TYPE_CUSTOM || !out->CreationNodeMask || !out->VisibleNodeMask)
        xs_stop(); /* Aggregate return has no HRESULT channel. */
    return out;
}

static BOOL msaa_desc(const D3D12_RESOURCE_DESC *r)
{
    return r->Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && !r->Alignment && r->Width==1 &&
        r->Height==1 && r->DepthOrArraySize==1 && r->MipLevels==1 &&
        r->SampleDesc.Count==4 && !r->SampleDesc.Quality && r->Layout==D3D12_TEXTURE_LAYOUT_UNKNOWN &&
        ((r->Format==DXGI_FORMAT_R8G8B8A8_UNORM && r->Flags==D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ||
         (r->Format==DXGI_FORMAT_R32G8X24_TYPELESS && r->Flags==D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));
}

static BOOL small_rt(const D3D12_RESOURCE_DESC *r)
{
    return (r->Width==3840 && r->Height==2160 && r->Format==DXGI_FORMAT_R8G8B8A8_UNORM) ||
            (r->Width==256 && r->Height==256 && (r->Format==DXGI_FORMAT_R16G16B16A16_FLOAT ||
            r->Format==DXGI_FORMAT_R11G11B10_FLOAT || r->Format==DXGI_FORMAT_R16G16_FLOAT)) ||
            (r->Width==64 && r->Height==2 && r->Format==DXGI_FORMAT_R11G11B10_FLOAT) ||
            (r->Width==127 && r->Height==128 && r->Format==DXGI_FORMAT_R16G16_UNORM) ||
            (r->Width==64 && r->Height==64 && r->Format==DXGI_FORMAT_R16G16B16A16_FLOAT);
}
static BOOL single_desc(const D3D12_RESOURCE_DESC *r)
{
    return r->Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && !r->Alignment &&
        r->DepthOrArraySize==1 && r->MipLevels==1 && r->SampleDesc.Count==1 && !r->SampleDesc.Quality && !r->Layout &&
        ((r->Format==DXGI_FORMAT_R32G8X24_TYPELESS && ((r->Width==2560 && r->Height==1440 && (UINT)r->Flags==0x4800002) ||
            (r->Width==3840 && r->Height==2160 && (UINT)r->Flags==2))) ||
         (r->Format==DXGI_FORMAT_R8_UINT && r->Width==640 && r->Height==360 && (UINT)r->Flags==0xc00005) ||
         (r->Format==DXGI_FORMAT_R16G16B16A16_FLOAT && r->Width==2560 && r->Height==1440 && (UINT)r->Flags==0x300005) ||
         ((UINT)r->Flags==0x300001 && (small_rt(r) ||
         (r->Format==DXGI_FORMAT_A8_UNORM && r->Width==3840 && r->Height==2160) ||
         (r->Format==DXGI_FORMAT_B8G8R8A8_UNORM && r->Width==2048 && r->Height==2048))));
}
#include "queue/rtx3070.h"
static UINT small_tile_bytes(const D3D12_RESOURCE_DESC *r,BOOL aggregate)
{
    if (r->Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || r->Width!=160 || r->Height!=90 || r->DepthOrArraySize!=1 ||
        r->MipLevels!=1 || r->SampleDesc.Count!=1 || r->SampleDesc.Quality || (UINT)r->Layout!=0x102 || (UINT)r->Flags!=0x300004) return 0;
    if (r->Format==DXGI_FORMAT_R32G32_FLOAT && !r->Alignment) return 0x1cc00;
    if (r->Alignment && r->Alignment!=4096) return 0;
    if (r->Format==DXGI_FORMAT_R32_FLOAT || r->Format==DXGI_FORMAT_R32_UINT) return 0xf000;
    if (aggregate && r->Format==DXGI_FORMAT_R8_UINT) return 0x3c00;
    return 0;
}
static BOOL tiled_r32_desc(const D3D12_RESOURCE_DESC *rd)
{
    return rd->Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && !rd->Alignment && rd->Width==1280 && rd->Height==720 &&
        rd->DepthOrArraySize==1 && rd->MipLevels==1 && rd->Format==DXGI_FORMAT_R32_UINT && rd->SampleDesc.Count==1 &&
        !rd->SampleDesc.Quality && (UINT)rd->Layout==0x102 && (UINT)rd->Flags==0x300004;
}
struct page_group { DXGI_FORMAT fmt; UINT width,height,flags,pages[2],host_flags; BOOL detail; };
static const struct page_group *deferred_pages(const D3D12_RESOURCE_DESC *rd)
{
    /* Deferred target owners consume only a size: qualify the native page counts,
     * not the size of a linear array of guest texels (notably for depth/stencil). */
    static const struct page_group groups[]={
        {DXGI_FORMAT_R32G8X24_TYPELESS,1024,1024,2,{128,82},0,FALSE},
        {DXGI_FORMAT_R32G8X24_TYPELESS,994,624,0x800002,{79,53},0,FALSE},
        {DXGI_FORMAT_R32G8X24_TYPELESS,2560,1440,0x800002,{480,304},0,FALSE},
        {DXGI_FORMAT_R32G8X24_TYPELESS,3840,2160,0x800002,{1020,655},0,FALSE},
        {DXGI_FORMAT_B8G8R8A8_UNORM,994,624,0,{40,41},0,FALSE},
        {DXGI_FORMAT_B8G8R8A8_UNORM,3840,2160,0,{510,513},0,FALSE},
        {DXGI_FORMAT_R32_UINT,640,1440,0x300005,{60,60},0,FALSE},
        {DXGI_FORMAT_R32_FLOAT,2560,1440,0xc00001,{240,241},0,FALSE},
        {DXGI_FORMAT_R32G32_FLOAT,2560,1440,0xc00005,{480,462},5,FALSE},
        {DXGI_FORMAT_R8_UNORM,2560,1440,0xc00005,{60,61},5,FALSE},
        {DXGI_FORMAT_R32G32_UINT,2560,1440,0xc00005,{480,460},5,FALSE},
        {DXGI_FORMAT_R32_UINT,1280,720,0xc00005,{60,60},5,FALSE},
        {DXGI_FORMAT_R8_UINT,640,360,0xc00005,{4,4},5,TRUE},
        {DXGI_FORMAT_R8_UNORM,2560,1440,0x300005,{60,61},5,FALSE},
        {DXGI_FORMAT_R32_UINT,2560,1440,0xc00005,{240,240},5,FALSE},
        /* Desktop DS+UAV is invalid; this deferred owner consumes only DS size. */
        {DXGI_FORMAT_R32G8X24_TYPELESS,2560,1440,0x80300006,{480,304},2,FALSE},
        {DXGI_FORMAT_R32G8X24_TYPELESS,3840,2160,0x80300002,{1020,655},2,FALSE},
        {DXGI_FORMAT_R16G16B16A16_FLOAT,256,256,0x300001,{8,8},1,TRUE},
        {DXGI_FORMAT_R11G11B10_FLOAT,256,256,0x300001,{4,4},1,TRUE},
        {DXGI_FORMAT_R16G16_FLOAT,256,256,0x300001,{4,4},1,TRUE},
        {DXGI_FORMAT_R11G11B10_FLOAT,64,2,0x300001,{1,1},1,TRUE},
        {DXGI_FORMAT_R16G16_UNORM,127,128,0x300001,{1,1},1,TRUE},
        {DXGI_FORMAT_R16G16B16A16_FLOAT,64,64,0x300001,{1,1},1,TRUE},
        {DXGI_FORMAT_R16G16_FLOAT,1280,720,0x300001,{60,61},1,FALSE},
        {DXGI_FORMAT_R32G8X24_TYPELESS,2560,1440,0x4800002,{480,304},2,TRUE},
        {DXGI_FORMAT_R32G8X24_TYPELESS,3840,2160,2,{1020,655},2,TRUE},
        {DXGI_FORMAT_R32G8X24_TYPELESS,2560,1440,2,{480,304},2,FALSE},
        {DXGI_FORMAT_R32G8X24_TYPELESS,1280,720,0x80300002,{120,77},2,FALSE},
        {DXGI_FORMAT_R10G10B10A2_UNORM,2560,1440,0xc00005,{240,241},5,FALSE},
        {DXGI_FORMAT_R10G10B10A2_UNORM,2560,1440,0x300005,{240,241},5,FALSE},
        {DXGI_FORMAT_R11G11B10_FLOAT,3840,2160,0x300001,{510,513},1,FALSE},
        {DXGI_FORMAT_R8G8B8A8_UNORM,3840,2160,0x300001,{510,513},1,TRUE},
        {DXGI_FORMAT_R8G8B8A8_UNORM,3840,2160,0xc00001,{510,513},1,FALSE},
        {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,2560,1440,0xc00001,{240,241},1,FALSE},
        {DXGI_FORMAT_R8G8B8A8_UNORM,2560,1440,0xc00001,{240,241},1,FALSE}
    };
    for (UINT i=0;i<ARRAYSIZE(groups);i++)
        if (rd->Format==groups[i].fmt && rd->Width==groups[i].width && rd->Height==groups[i].height &&
            (rd->Format==DXGI_FORMAT_B8G8R8A8_UNORM || (UINT)rd->Flags==groups[i].flags)) return &groups[i];
    return NULL;
}

/* Both public slots require matching aggregate and per-resource results. */
static HRESULT target_info(ID3D12Device *host,D3D12_RESOURCE_DESC *rd,
        D3D12_RESOURCE_ALLOCATION_INFO *out,D3D12_RESOURCE_ALLOCATION_INFO1 *detail,const struct page_group *group)
{
    UINT bytes=small_tile_bytes(rd,!detail);
    if (bytes) {
        *out=(D3D12_RESOURCE_ALLOCATION_INFO){bytes,256};
        if(detail)*detail=(D3D12_RESOURCE_ALLOCATION_INFO1){.Offset=0,.Alignment=256,.SizeInBytes=bytes};
        return S_OK;
    }
    if (tiled_r32_desc(rd)) {
        /* 160 x 90 complete 256-byte guest tiles. */
        *out=(D3D12_RESOURCE_ALLOCATION_INFO){0x384000,256};
        if(detail)*detail=(D3D12_RESOURCE_ALLOCATION_INFO1){.Offset=0,.Alignment=256,.SizeInBytes=0x384000};
        return S_OK;
    }
    BOOL a8=rd->Format==DXGI_FORMAT_A8_UNORM, bgra=rd->Format==DXGI_FORMAT_B8G8R8A8_UNORM;
    BOOL fp=rd->Format==DXGI_FORMAT_R16G16B16A16_FLOAT;
    BOOL packed=rd->Format==DXGI_FORMAT_B5G5R5A1_UNORM;
    UINT expected_bytes=0;
    if (packed) {
        /* The observed owner asks for a size; RTX has no typed packed UAV. */
        if (detail || rd->Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || rd->Alignment || rd->Layout ||
            rd->Width!=2560 || rd->Height!=1440 || rd->DepthOrArraySize!=1 || rd->MipLevels!=1 ||
            rd->SampleDesc.Count!=1 || rd->SampleDesc.Quality || (UINT)rd->Flags!=0x300005) return E_NOTIMPL;
        if (xs_is_rtx3070(host)) {rd->Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;expected_bytes=0x780000;}
        else if (xs_is_adapter(host,0x1002,0x164e)) {rd->Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;expected_bytes=0x790000;}
        else return E_NOTIMPL;
    } else if (group) {
        if ((detail && !group->detail) ||
            rd->Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || rd->Alignment || rd->Layout ||
            rd->DepthOrArraySize!=1 || rd->MipLevels!=1 || rd->SampleDesc.Count!=1 || rd->SampleDesc.Quality ||
            (bgra && (UINT)rd->Flags!=0xc00005 && (UINT)rd->Flags!=0x300001 && (UINT)rd->Flags!=0xc00001)) return E_NOTIMPL;
        if (group->host_flags) {
            if (xs_is_adapter(host,0x1002,0x164e)) expected_bytes=group->pages[1]*65536;
            else if (xs_is_rtx3070(host)) expected_bytes=group->pages[0]*65536;
            else return E_NOTIMPL;
            rd->Flags=(D3D12_RESOURCE_FLAGS)group->host_flags;
        } else rd->Flags=(D3D12_RESOURCE_FLAGS)((UINT)rd->Flags & 7);
    } else if (fp) {
        /* The half-size probe is query-only. */
        if (rd->Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || rd->Alignment || rd->Layout || rd->DepthOrArraySize!=1 ||
            rd->MipLevels!=1 || rd->SampleDesc.Count!=1 || rd->SampleDesc.Quality) return E_NOTIMPL;
        if (rd->Width==1280 && rd->Height==720 && (UINT)rd->Flags==0xc00001 && !detail)
            rd->Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        else if (single_desc(rd))
            rd->Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        else return E_NOTIMPL;
    } else if (a8 || bgra) {
        /* read_texture_desc validated the 2D shape. Only the low RT flag is translated. */
        if (!single_desc(rd)) return E_NOTIMPL;
        rd->Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    } else if (!msaa_desc(rd)) return E_NOTIMPL;
    ID3D12Device4 *d=NULL;
    HRESULT hr=ID3D12Device_QueryInterface(host,&IID_ID3D12Device4,(void **)&d);
    if (FAILED(hr)) return hr;
    D3D12_RESOURCE_ALLOCATION_INFO info={0};
    D3D12_RESOURCE_ALLOCATION_INFO1 one={0};
    d->lpVtbl->GetResourceAllocationInfo1(d,&info,1,1,rd,&one);
    ID3D12Device4_Release(d);
    if (one.Offset || one.SizeInBytes!=info.SizeInBytes || one.Alignment!=info.Alignment) return E_NOTIMPL;
    if (expected_bytes) {
        if (info.Alignment!=65536 || info.SizeInBytes!=expected_bytes) return E_NOTIMPL;
    } else if (group) {
        if (info.Alignment!=65536 || (info.SizeInBytes!=group->pages[0]*info.Alignment &&
            info.SizeInBytes!=group->pages[1]*info.Alignment)) return E_NOTIMPL;
    } else if (fp) {
        if (info.Alignment!=65536 || (rd->Width==1280 ? info.SizeInBytes!=0x780000 && info.SizeInBytes!=0x790000 :
            info.SizeInBytes!=0x1e00000 && info.SizeInBytes!=0x1ce0000)) return E_NOTIMPL;
    } else if (bgra) {
        if (info.Alignment!=65536 || (info.SizeInBytes!=0x1000000 && info.SizeInBytes!=0x1010000)) return E_NOTIMPL;
    } else if (a8) {
        if (info.Alignment!=65536 || (info.SizeInBytes!=0x880000 && info.SizeInBytes!=0x800000)) return E_NOTIMPL;
    } else if (info.SizeInBytes!=D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT || info.Alignment!=info.SizeInBytes) return E_NOTIMPL;
    *out=info;
    if (detail) *detail=one;
    return S_OK;
}

#include "xg/r11_mips.h"
static BOOL tiled_r11_chain(const D3D12_RESOURCE_DESC *r)
{
    return r->Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && !r->Alignment &&
        ((r->Width==2560 && r->Height==1440 && r->MipLevels==6) ||
         (r->Width==1280 && r->Height==720 && r->MipLevels==5)) &&
        r->DepthOrArraySize==1 && r->Format==DXGI_FORMAT_R11G11B10_FLOAT &&
        r->SampleDesc.Count==1 && !r->SampleDesc.Quality && (UINT)r->Layout==0x102 && (UINT)r->Flags==0x300004;
}
static HRESULT validate_texture_desc(const D3D12_RESOURCE_DESC *out)
{
    if (is_srgb_row(out) || msaa_desc(out) || is_layered_texel(out)) return S_OK;
    if (is_volume1(out) && (UINT)out->Layout==R8_LAYOUT && out->Alignment==R8_ALIGNMENT) return S_OK;
    if (out->Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || out->DepthOrArraySize!=1 ||
        out->MipLevels!=1 || out->SampleDesc.Count!=1 || out->SampleDesc.Quality) return E_NOTIMPL;
    return S_OK;
}

static HRESULT read_texture_desc(const void *wire,D3D12_RESOURCE_DESC *out)
{
    if (!copy_in(out,wire,offsetof(D3D12_RESOURCE_DESC,Flags)+sizeof out->Flags)) return E_POINTER;
    return validate_texture_desc(out);
}

static BOOL primary_desc(const D3D12_RESOURCE_DESC *rd)
{
    return rd->Layout==D3D12_TEXTURE_LAYOUT_UNKNOWN && !rd->Alignment && rd->Width==3840 && rd->Height==2160 &&
        rd->Format==DXGI_FORMAT_R8G8B8A8_UNORM && (UINT)rd->Flags==0x300005;
}

static HRESULT primary_info(ID3D12Device *device,D3D12_RESOURCE_ALLOCATION_INFO *out)
{
    HMODULE core=GetModuleHandleW(L"d3d12core.dll"); /* held by the live host device */
    HRESULT (WINAPI *info)(ID3D12Device *,D3D12_RESOURCE_ALLOCATION_INFO *)=
        core?(void *)GetProcAddress(core,"ScarlettPrimaryImageInfoV1"):NULL;
    if (!info) return E_NOTIMPL;
    D3D12_RESOURCE_ALLOCATION_INFO i={0};
    HRESULT hr=info(device,&i);
    if (FAILED(hr)) return hr;
    /* Two whole slots must fit the 64 MiB arena with less than 2 MiB slack. */
    if (i.Alignment!=65536 || i.SizeInBytes<3840ull*2160*4 || i.SizeInBytes>(32u<<20) ||
        i.SizeInBytes%i.Alignment || (64u<<20)-2*i.SizeInBytes>=(2u<<20)) return E_NOTIMPL;
    *out=i;
    return S_OK;
}

/* 344182: console sret precedes This; the desktop C vtable takes This first. */
HRESULT xs_color_allocation_info(IUnknown *iface,UINT node,UINT count,const void *wire,
        D3D12_RESOURCE_ALLOCATION_INFO *out)
{
    D3D12_RESOURCE_DESC rd={0};
    if (!out) return E_POINTER;
    *out=(D3D12_RESOURCE_ALLOCATION_INFO){0};
    if (!iface || node!=1 || count!=1) return E_INVALIDARG;
    if (!copy_in(&rd,wire,offsetof(D3D12_RESOURCE_DESC,Flags)+sizeof rd.Flags)) return E_POINTER;
    if (rd.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && (rd.Alignment==4096 || !rd.Alignment) &&
        rd.Width==160 && rd.Height==90 && rd.DepthOrArraySize==1 && rd.MipLevels==5 &&
        rd.Format==DXGI_FORMAT_R16_FLOAT && rd.SampleDesc.Count==1 && !rd.SampleDesc.Quality &&
        (UINT)rd.Layout==0x102 && (UINT)rd.Flags==0x300004) {
        /* 164 guest tiles of 16x8 halfwords, including per-level padding. */
        *out=(D3D12_RESOURCE_ALLOCATION_INFO){0xa400,256};
        return S_OK;
    }
    if (rd.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && !rd.Alignment && rd.DepthOrArraySize==1 &&
        rd.Format==DXGI_FORMAT_R32_FLOAT && rd.SampleDesc.Count==1 && !rd.SampleDesc.Quality &&
        (UINT)rd.Layout==0x102 && (UINT)rd.Flags==0x300004 &&
        ((rd.Width==2560 && rd.Height==1440 && rd.MipLevels==1) ||
         (rd.Width==1280 && rd.Height==720 && (rd.MipLevels==1 || rd.MipLevels==2)))) {
        /* Complete 256-byte tiles at every measured mip; these deferred owners request only +c8. */
        *out=(D3D12_RESOURCE_ALLOCATION_INFO){rd.Width==2560?0xe10000:rd.MipLevels==1?0x384000:0x465000,256};
        return S_OK;
    }
    if (tiled_r11_chain(&rd)) {
        *out=(D3D12_RESOURCE_ALLOCATION_INFO){r11_end[rd.MipLevels],R11_ALIGNMENT};
        return S_OK;
    }
    HRESULT hr=validate_texture_desc(&rd);
    if (FAILED(hr)) return hr;
    ID3D12Device *host=CONTAINING_RECORD(iface,struct xbox_device,iface)->host;
    const struct page_group *group=deferred_pages(&rd);
    if (small_tile_bytes(&rd,TRUE) || group || (rd.Format==DXGI_FORMAT_R32_UINT && (UINT)rd.Layout==0x102) || msaa_desc(&rd) ||
        rd.Format==DXGI_FORMAT_B5G5R5A1_UNORM || rd.Format==DXGI_FORMAT_R32G8X24_TYPELESS ||
        rd.Format==DXGI_FORMAT_A8_UNORM || rd.Format==DXGI_FORMAT_B8G8R8A8_UNORM ||
        rd.Format==DXGI_FORMAT_R16G16B16A16_FLOAT) return target_info(host,&rd,out,NULL,group);
    if (primary_desc(&rd)) return primary_info(host,out);
    BOOL layered=is_layered_texel(&rd),texel32=layered || is_texel32(&rd);
    BOOL row=is_srgb_row(&rd);
    if ((row || texel32) && rd.Alignment!=R8_ALIGNMENT) return E_NOTIMPL;
    if ((!row && !texel32 && !is_r8(&rd)) || (rd.Alignment && rd.Alignment!=R8_ALIGNMENT)) return E_NOTIMPL;
    if ((UINT)rd.Layout==(row?SRGB_ROW_LAYOUT:R8_LAYOUT) && rd.Alignment==R8_ALIGNMENT) {
        /* Runtime R8 occupies eight tiles; the single RGBA8 texel occupies one. */
        *out=(D3D12_RESOURCE_ALLOCATION_INFO){layered?rd.DepthOrArraySize*R8_ALIGNMENT:(row || texel32)?R8_ALIGNMENT:R8_BYTES,R8_ALIGNMENT};
        return S_OK;
    }
    if (rd.Layout!=D3D12_TEXTURE_LAYOUT_UNKNOWN) return E_NOTIMPL;
    rd.Flags=D3D12_RESOURCE_FLAG_NONE;
    host->lpVtbl->GetResourceAllocationInfo(host,out,node,1,&rd);
    /* +34414d compares Alignment only. Never let a failed probe masquerade as small. */
    if (out->Alignment!=(rd.Alignment ? rd.Alignment : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) ||
        (out->SizeInBytes!=((row || texel32) ? R8_ALIGNMENT : rd.Alignment ? R8_BYTES : 65536) &&
         !((row || (layered && rd.DepthOrArraySize==CUBE_PAIR_FACES)) && out->SizeInBytes==8192))) {
        *out=(D3D12_RESOURCE_ALLOCATION_INFO){UINT64_MAX,D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT};
        return S_FALSE;
    }
    return S_OK;
}

static void log_rejected_query(UINT slot, UINT node, UINT count, const void *wire, HRESULT hr)
{
    D3D12_RESOURCE_DESC r={0};
    if (count!=1 || !copy_in(&r,wire,offsetof(D3D12_RESOURCE_DESC,Flags)+sizeof r.Flags)) return;
    char msg[256];
    snprintf(msg,sizeof msg,"scarlett: device+%x rejected allocation node=%u dim=%u %llux%ux%u mips=%u "
        "fmt=%u sample=%u:%u alignment=%llu layout=%#x flags=%#x hr=%#lx\n",
        slot,node,r.Dimension,(unsigned long long)r.Width,r.Height,r.DepthOrArraySize,r.MipLevels,r.Format,
        r.SampleDesc.Count,r.SampleDesc.Quality,(unsigned long long)r.Alignment,r.Layout,r.Flags,(unsigned long)hr);
    OutputDebugStringA(msg);
}
static D3D12_RESOURCE_ALLOCATION_INFO *WINAPI bootstrap_allocation_info(D3D12_RESOURCE_ALLOCATION_INFO *out,
        IUnknown *iface,UINT node,UINT count,const void *wire)
{
    HRESULT hr=xs_color_allocation_info(iface,node,count,wire,out);
    if (FAILED(hr)) { log_rejected_query(0xc8,node,count,wire,hr); xs_stop(); }
    return out;
}

HRESULT xs_msaa_allocation_info1(IUnknown *iface,UINT node,UINT count,const void *wire,
        D3D12_RESOURCE_ALLOCATION_INFO *out,D3D12_RESOURCE_ALLOCATION_INFO1 *detail)
{
    D3D12_RESOURCE_DESC rd={0};
    if (!out || !detail) return E_POINTER;
    if (!iface || count!=1 || node!=1) return E_INVALIDARG;
    if (!copy_in(&rd,wire,offsetof(D3D12_RESOURCE_DESC,Flags)+sizeof rd.Flags)) return E_POINTER;
    if (rd.MipLevels==5 && tiled_r11_chain(&rd)) {
        *out=(D3D12_RESOURCE_ALLOCATION_INFO){r11_end[rd.MipLevels],R11_ALIGNMENT};
        *detail=(D3D12_RESOURCE_ALLOCATION_INFO1){.Offset=0,.Alignment=out->Alignment,.SizeInBytes=out->SizeInBytes};
        return S_OK;
    }
    HRESULT hr=validate_texture_desc(&rd);
    if (FAILED(hr)) return hr;
    return target_info(CONTAINING_RECORD(iface,struct xbox_device,iface)->host,&rd,out,detail,deferred_pages(&rd));
}
static D3D12_RESOURCE_ALLOCATION_INFO *WINAPI bootstrap_allocation_info1(D3D12_RESOURCE_ALLOCATION_INFO *out,
        IUnknown *iface,UINT node,UINT count,const void *wire,D3D12_RESOURCE_ALLOCATION_INFO1 *detail)
{
    HRESULT hr=xs_msaa_allocation_info1(iface,node,count,wire,out,detail);
    if (FAILED(hr)) { log_rejected_query(0x360,node,count,wire,hr); xs_stop(); }
    return out;
}

static HRESULT WINAPI bootstrap_extended(IUnknown *iface, const void *desc, REFIID iid, void **out)
{
    UINT words[4];
    SIZE_T copied;
    IUnknown *queue = NULL;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!desc || !iid) return E_INVALIDARG;
    if (!IsEqualIID(iid, &IID_ID3D12CommandQueue)) return E_NOINTERFACE;
    if (!ReadProcessMemory(GetCurrentProcess(), desc, words, sizeof(words), &copied) || copied != sizeof(words))
        return E_POINTER;
    /* The host schedules each logical stream; physical engine placement is not guaranteed. */
    if (words[0] != D3D12_COMMAND_LIST_TYPE_COMPUTE || words[1] ||
        !((words[2] <= 3 && words[3] == 0) || (words[2] == 0 && words[3] == 1))) {
        char msg[128];
        snprintf(msg, sizeof(msg), "scarlett: device+0x180 unsupported queue descriptor (%#x,%#x,%#x,%#x)\n",
                 words[0], words[1], words[2], words[3]);
        OutputDebugStringA(msg);
        return E_NOTIMPL;
    }
    hr = xs_compute_queue(CONTAINING_RECORD(iface, struct xbox_device, iface)->host, &queue);
    *out = queue;
    return hr;
}

static HRESULT WINAPI bootstrap_query_heap(IUnknown *iface, const void *wire, REFIID iid, void **out)
{
    D3D12_QUERY_HEAP_DESC d;
    IID id;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!wire || !iid) return E_INVALIDARG;
    if (!copy_in(&id,iid,sizeof id) || !copy_in(&d,wire,sizeof d)) return E_POINTER;
    if (!IsEqualIID(&id,&IID_ID3D12QueryHeap)) return E_NOINTERFACE;
    if (!d.Count) return E_INVALIDARG;
    if (d.Type != D3D12_QUERY_HEAP_TYPE_OCCLUSION || d.Count != 2460 || d.NodeMask != 1)
        return E_NOTIMPL;
    return xs_query_heap(CONTAINING_RECORD(iface,struct xbox_device,iface)->host,&d,out);
}

static HRESULT WINAPI bootstrap_signature(IUnknown *iface, const D3D12_COMMAND_SIGNATURE_DESC *desc,
        IUnknown *root, REFIID iid, void **out)
{
    D3D12_COMMAND_SIGNATURE_DESC d;
    D3D12_INDIRECT_ARGUMENT_TYPE type;
    SIZE_T n;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!desc || !iid) return E_INVALIDARG;
    if (!IsEqualIID(iid, &IID_ID3D12CommandSignature)) return E_NOINTERFACE;
    if (root) return E_NOTIMPL;
    /* NodeMask is the last field written. Do not read padding or inactive union members. */
    const SIZE_T size = offsetof(D3D12_COMMAND_SIGNATURE_DESC, NodeMask) + sizeof(d.NodeMask);
    if (!ReadProcessMemory(GetCurrentProcess(), desc, &d, size, &n) || n != size) return E_POINTER;
    if (d.NodeMask || d.NumArgumentDescs != 1) return E_NOTIMPL;
    if (!ReadProcessMemory(GetCurrentProcess(), d.pArgumentDescs, &type, sizeof(type), &n) || n != sizeof(type))
        return E_POINTER;
    if (type == XS_INDIRECT_TYPE_69) {
        /* Only this form has a known second dword, explicitly zero at the call site. */
        UINT word;
        if (!ReadProcessMemory(GetCurrentProcess(), (const BYTE *)d.pArgumentDescs + sizeof(type),
                &word, sizeof(word), &n) || n != sizeof(word)) return E_POINTER;
        if (word) return E_NOTIMPL;
    }
    IUnknown *signature = NULL;
    HRESULT hr = xs_signature(CONTAINING_RECORD(iface, struct xbox_device, iface)->host, type, d.ByteStride, &signature);
    *out = signature;
    return hr;
}

#include "frame_clock.h"
#include "query_readback.h"

/* Read only the common caller-written prefix; the console layout/flags need
 * translation. Neither entry has the title's ring index or HWND. */
static HRESULT resource_gap(UINT slot, UINT64 address, const void *wire, UINT state,
        UINT heap_flags, const void *clear, void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    D3D12_RESOURCE_DESC d={0};
    SIZE_T copied;
    size_t n=offsetof(D3D12_RESOURCE_DESC,Flags)+sizeof(d.Flags);
    if (!wire || !ReadProcessMemory(GetCurrentProcess(),wire,&d,n,&copied) || copied!=n) return E_POINTER;
    char text[384];
    snprintf(text,sizeof(text),"scarlett: device +%#x unsupported: placement=%#llx heapFlags=%#x "
            "dim=%u width=%llu height=%u format=%u depth=%u mips=%u samples=%u:%u "
            "layout=%#x flags=%#x state=%#x clear=%p\n",slot,(unsigned long long)address,heap_flags,
            d.Dimension,(unsigned long long)d.Width,d.Height,d.Format,d.DepthOrArraySize,d.MipLevels,
            d.SampleDesc.Count,d.SampleDesc.Quality,d.Layout,d.Flags,state,clear);
    OutputDebugStringA(text);
    return E_NOTIMPL;
}
#ifdef SCARLETT_STAGED_PLACED
static HRESULT create_target(struct xbox_device *d,UINT64 va,const D3D12_RESOURCE_DESC *rd,UINT state,const void *clear,REFIID iid,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!iid || !va) return E_INVALIDARG;
    IID id;
    if (!copy_in(&id,iid,sizeof id)) return E_POINTER;
    if (!IsEqualIID(&id,&IID_ID3D12Resource)) return E_NOINTERFACE;
    DXGI_FORMAT format=rd->Format;
    BOOL ds=format==DXGI_FORMAT_R32G8X24_TYPELESS && rd->SampleDesc.Count==1;
    BOOL depth=format==DXGI_FORMAT_R32G8X24_TYPELESS,a8=format==DXGI_FORMAT_A8_UNORM,bgra=format==DXGI_FORMAT_B8G8R8A8_UNORM;
    BOOL small=rd->SampleDesc.Count==1 && small_rt(rd);
    BOOL fp=format==DXGI_FORMAT_R16G16B16A16_FLOAT,u8=format==DXGI_FORMAT_R8_UINT,single=ds || fp || a8 || bgra || u8 || small;
    D3D12_CLEAR_VALUE cv={0};
    if (!copy_in(&cv,clear,depth ? offsetof(D3D12_CLEAR_VALUE,DepthStencil.Stencil)+sizeof cv.DepthStencil.Stencil : sizeof cv)) return E_POINTER;
    if (state!=((fp && !small) || u8 ? 3u : 6u)) return E_NOTIMPL;
    if (depth) {
        if (cv.Format!=DXGI_FORMAT_D32_FLOAT_S8X24_UINT || cv.DepthStencil.Depth!=0 || cv.DepthStencil.Stencil!=(ds && rd->Width==2560?7:0)) return E_NOTIMPL;
    } else if (cv.Format!=format || memcmp(cv.Color,u8?(float[4]){0,0,0,1}:single?(float[4]){0}:(float[4]){1,1,1,1},16)) return E_NOTIMPL;
    struct scarlett_section_info i;
    UINT span=(small || ds)?deferred_pages(rd)->pages[1]*65536:u8?0x40000:fp?0x1ce0000:bgra?0x1010000:a8?0x880000:D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT;
    ID3D12Heap *native=NULL;UINT off=0;
    HRESULT hr=native_pool_ref(d->host,(void*)va,span,&native,NULL,&off);
    if(hr==S_FALSE) {
        if(small || ds) span=deferred_pages(rd)->pages[0]*65536; else if (fp) span=0x1e00000; else if (bgra) span=0x1000000; else if (a8) span=0x800000;
        hr=scarlett_query_section(0x53430005,(void *)va,span,&i);
    }
    else if(SUCCEEDED(hr))i=(struct scarlett_section_info){span,(void *)(va-off),WC_POOL_BYTES,off,NULL};
    if (FAILED(hr)) return hr;
    if (i.view_size!=WC_POOL_BYTES || i.offset!=va-(UINT_PTR)i.base || i.offset>i.view_size-span ||
        (single ? i.offset%0x10000 : i.offset%span)) hr=E_INVALIDARG;
    else {
        HRESULT (WINAPI *import)(ID3D12Device *,HANDLE,SIZE_T,SIZE_T,ID3D12Resource **)=
            (void *)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),fp?"ScarlettFloat1440V1":bgra?"ScarlettBgraImageV1":a8?"ScarlettA8ImageV1":depth?"ScarlettMsaaDepthV1":"ScarlettMsaaColorV1");
        struct color_resource *r=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *r);
        if (!r) hr=E_OUTOFMEMORY;
        else {
            if(small) {
                HRESULT(WINAPI *place)(ID3D12Device*,ID3D12Heap*,HANDLE,SIZE_T,SIZE_T,DXGI_FORMAT,UINT,UINT,ID3D12Resource**)=(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettSmallRTV1");
                hr=place?place(d->host,native,i.section,i.view_size,i.offset,format,rd->Width,rd->Height,&r->host):DXGI_ERROR_UNSUPPORTED;
            }else if(u8 || ds) {
                HRESULT(WINAPI *place)(ID3D12Device*,ID3D12Heap*,HANDLE,SIZE_T,SIZE_T,ID3D12Resource**)=(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),u8?"ScarlettR8UintImageV1":rd->Width==3840?"ScarlettDepth4kImageV1":"ScarlettDepth1440ImageV1");
                hr=place?place(d->host,native,i.section,i.view_size,i.offset,&r->host):DXGI_ERROR_UNSUPPORTED;
            }else if(native && single) {
                HRESULT(WINAPI *place)(ID3D12Device*,ID3D12Heap*,SIZE_T,ID3D12Resource**)=(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),fp?"ScarlettNativeFloat1440V1":bgra?"ScarlettNativeBgraImageV1":"ScarlettNativeA8ImageV1");
                hr=place?place(d->host,native,off,&r->host):DXGI_ERROR_UNSUPPORTED;
            }else if(native){
                HRESULT(WINAPI *place)(ID3D12Device*,ID3D12Heap*,SIZE_T,BOOL,ID3D12Resource**)=(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettNativeImageV1");
                hr=place?place(d->host,native,off,depth,&r->host):DXGI_ERROR_UNSUPPORTED;
            }else hr=import?import(d->host,i.section,i.view_size,i.offset,&r->host):DXGI_ERROR_UNSUPPORTED;
            if (FAILED(hr)) HeapFree(GetProcessHeap(),0,r);
            else {
                r->iface.lpVtbl=&color_vtbl.unknown; r->depth4k=ds && rd->Width==3840; r->small_rt=small; r->single_format=single?format:DXGI_FORMAT_UNKNOWN; r->msaa=!single; r->depth=depth; r->refs=1; r->owner=d; IUnknown_AddRef(&d->iface);
                AcquireSRWLockExclusive(&d->descriptor_lock); r->next=d->colors; d->colors=r; ReleaseSRWLockExclusive(&d->descriptor_lock);
                *out=&r->iface;
            }
        }
    }
    if(native)ID3D12Heap_Release(native);else CloseHandle(i.section);
    return hr;
}
#endif
#ifdef SCARLETT_STAGED_PLACED
#include "tiled_r32.h"
#include "row_buffer.h"
#endif
static HRESULT WINAPI bootstrap_placed(IUnknown *iface, UINT64 address, const void *desc, UINT state,
        const void *clear, REFIID iid, void **out)
{
#ifdef SCARLETT_STAGED_PLACED
    struct xbox_device *d=CONTAINING_RECORD(iface,struct xbox_device,iface);
    D3D12_RESOURCE_DESC rd={0};
    BOOL read=copy_in(&rd,desc,offsetof(D3D12_RESOURCE_DESC,Flags)+sizeof rd.Flags);
    if (read && rd.Dimension==D3D12_RESOURCE_DIMENSION_BUFFER && !rd.Alignment && (rd.Width==0x70800 || rd.Width==16) && rd.Height==1 &&
        rd.DepthOrArraySize==1 && rd.MipLevels==1 && !rd.Format && rd.SampleDesc.Count==1 && !rd.SampleDesc.Quality &&
        rd.Layout==D3D12_TEXTURE_LAYOUT_ROW_MAJOR && rd.Flags==D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
        return create_row_buffer(d,address,(UINT)rd.Width,state,clear,iid,out);
    BOOL small=read && !rd.Alignment && small_tile_bytes(&rd,FALSE);
    if (small || (read && ((rd.MipLevels==5 && tiled_r11_chain(&rd)) || tiled_r32_desc(&rd))))
        return create_tiled_buffer(d,rd.Format,small,address,state,clear,iid,out);
    if (read && (msaa_desc(&rd) || single_desc(&rd)))
        return create_target(d,address,&rd,state,clear,iid,out);
    return placed_create(d,address,desc,state,clear,iid,out);
#else
    (void)iface; (void)iid;
    return resource_gap(0x428,address,desc,state,0,clear,out);
#endif
}
static HRESULT WINAPI bootstrap_committed(IUnknown *iface, const void *props, UINT flags, const void *desc,
        UINT state, const void *clear, const void *extra1, UINT extra2, const void *extra3, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!iid) return E_INVALIDARG;
    if (!IsEqualIID(iid,&IID_ID3D12Resource)) return E_NOINTERFACE;
    if (flags!=8 || state!=3 || !clear || extra1 || extra2 || extra3)
        return resource_gap(0x3f8,0,desc,state,flags,clear,out);
    D3D12_HEAP_PROPERTIES hp;
    D3D12_RESOURCE_DESC rd={0};
    D3D12_CLEAR_VALUE cv;
    HRESULT hr=read_texture_desc(desc,&rd);
    if (FAILED(hr)) return hr;
    if (!primary_desc(&rd)) return E_NOTIMPL;
    rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (!copy_in(&hp,props,sizeof hp) || !copy_in(&cv,clear,sizeof cv)) return E_POINTER;
    if (hp.Type!=D3D12_HEAP_TYPE_DEFAULT || hp.CPUPageProperty || hp.MemoryPoolPreference ||
        hp.CreationNodeMask!=1 || hp.VisibleNodeMask!=1 || cv.Format!=rd.Format ||
        memcmp(cv.Color,(UINT[4]){0},sizeof cv.Color)) return E_NOTIMPL;
    struct xbox_device *d=CONTAINING_RECORD(iface,struct xbox_device,iface);
    struct color_resource *r=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *r);
    if (!r) return E_OUTOFMEMORY;
    D3D12_RESOURCE_ALLOCATION_INFO info;
    struct color_lease *lease=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *lease);
    if (!lease) { HeapFree(GetProcessHeap(),0,r); return E_OUTOFMEMORY; }
    lease->iface.lpVtbl=&lease_vtbl;
    lease->refs=1;
    HMODULE core=GetModuleHandleW(L"d3d12core.dll");
    HRESULT (WINAPI *import)(ID3D12Device *,void *,SIZE_T,SIZE_T,IUnknown *,ID3D12Resource **)=
        core?(void *)GetProcAddress(core,"ScarlettPrimaryImageV1"):NULL;
    hr=E_NOTIMPL;
    if (!import || !GetModuleHandleExW(0,L"xmem.dll",&lease->xm)) goto failed;
    void *(WINAPI *alloc)(SIZE_T,UINT64)=(void *)GetProcAddress(lease->xm,"XMemAlloc");
    r->xfree=(void *)GetProcAddress(lease->xm,"XMemFree");
    __typeof__(&ScarlettRetainDisplayBackingV1) pin=(void *)GetProcAddress(lease->xm,"ScarlettRetainDisplayBackingV1");
    lease->unpin=(void *)GetProcAddress(lease->xm,"ScarlettReleaseDisplayBackingV1");
    if (!alloc || !r->xfree || !pin || !lease->unpin) goto failed;
    hr=primary_info(d->host,&info);
    if (FAILED(hr)) goto failed;
    r->slice=alloc(info.SizeInBytes,XS_DISPLAY_ATTR);
    if (!r->slice) { hr=E_OUTOFMEMORY; goto failed; }
    struct XsDisplayBacking mapping;
    hr=pin(r->slice,info.SizeInBytes,&mapping);
    if (FAILED(hr)) goto failed;
    lease->base=mapping.base;
    if (mapping.size!=(64u<<20) || (mapping.offset && mapping.offset!=info.SizeInBytes) ||
        (UINT_PTR)r->slice-(UINT_PTR)mapping.base!=mapping.offset) { hr=E_INVALIDARG; goto failed; }
    hr=import(d->host,mapping.base,mapping.size,mapping.offset,&lease->iface,&r->host);
    if (FAILED(hr) || !r->host) { if (SUCCEEDED(hr)) hr=E_FAIL; goto failed; }
    lease_release(&lease->iface); /* imported image now owns the pin and module reference */
    r->iface.lpVtbl=&color_vtbl.unknown;
    r->refs=1; r->owner=d;
    IUnknown_AddRef(iface);
    AcquireSRWLockExclusive(&d->descriptor_lock);
    r->next=d->colors; d->colors=r;
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    *out=&r->iface;
    return S_OK;
failed:
    if (r->slice) r->xfree(r->slice,XS_DISPLAY_ATTR);
    if (r->host) ID3D12Resource_Release(r->host);
    lease_release(&lease->iface);
    HeapFree(GetProcessHeap(),0,r);
    return hr;
}

/* Sampler title storage is VirtualAlloc-backed, disjoint across live heaps. Caller holds descriptor_lock. */
static UINT64 *sampler_slot(struct xbox_device *d,SIZE_T slot,BOOL visible,D3D12_CPU_DESCRIPTOR_HANDLE *cpu)
{
    for (struct desc_heap *h=d->sampler_heaps;h;h=h->next)
        if (h->visible==visible && SUCCEEDED(xs_descriptor_slot(&h->iface,slot,cpu,NULL)))
            return &h->sampler_serial[(slot-h->start)/h->stride];
    return NULL;
}
#define SAMPLER_TAG 0x58534445534301e8ull
/* Borrowed handles: caller keeps the bound heap live through retirement. */
HRESULT xs_resolve_sampler(IUnknown *iface,SIZE_T slot,ID3D12DescriptorHeap **bank,D3D12_GPU_DESCRIPTOR_HANDLE *gpu)
{
    if (!bank || !gpu) return E_POINTER;
    *bank=NULL; gpu->ptr=0;
    if (!iface || iface->lpVtbl!=&desc_vtbl.unknown) return E_INVALIDARG;
    struct desc_heap *h=CONTAINING_RECORD(iface,struct desc_heap,iface);
    if (!h->sampler || !h->visible) return E_INVALIDARG;
    AcquireSRWLockShared(&h->owner->descriptor_lock);
    HRESULT hr=xs_descriptor_range(iface,slot,1,bank,NULL,gpu);
    if (SUCCEEDED(hr)) {
        UINT64 tag[2],serial=h->sampler_serial[(slot-h->start)/h->stride];
        if (!serial || !copy_in(tag,(void *)slot,sizeof tag) || tag[0]!=serial || tag[1]!=SAMPLER_TAG) {
            *bank=NULL; gpu->ptr=0; hr=E_INVALIDARG;
        }
    }
    ReleaseSRWLockShared(&h->owner->descriptor_lock);
    return hr;
}
HRESULT xs_write_sampler(IUnknown *iface,const void *desc,SIZE_T dest)
{
    /* +12b96b0 expands compact filter 0x15 or 0. Match the whole 92-byte record. */
    static const DWORD linear[23]={0,1,1,2,2,0,1,1,1,0,0x41400000,0,0,1,0,0,1};
    static const DWORD point[23] ={0,2,2,1,1,0,1,1,1,0,0x41400000,0,0,1,0,0,1};
    DWORD w[23];
    if (!iface || !copy_in(w,desc,sizeof w)) return E_POINTER;
    D3D12_SAMPLER_DESC s={.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR,.AddressU=D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .AddressV=D3D12_TEXTURE_ADDRESS_MODE_WRAP,.AddressW=D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        .MaxAnisotropy=1,.ComparisonFunc=D3D12_COMPARISON_FUNC_NEVER,.MaxLOD=12.0f};
    /* +1dc6d3b..+1dc6de6 pair each filter with wrap, clamp or mirror UV and clamped W. */
    if (w[6]==w[7] && w[7]==w[8] && (w[8]==D3D12_TEXTURE_ADDRESS_MODE_MIRROR || w[8]==D3D12_TEXTURE_ADDRESS_MODE_BORDER)) {
        s.AddressU=s.AddressV=s.AddressW=(D3D12_TEXTURE_ADDRESS_MODE)w[8];
        if (w[8]==D3D12_TEXTURE_ADDRESS_MODE_BORDER) {
            const DWORD one=0x3f800000;
            if (w[17]!=w[18] || w[18]!=w[19] || (w[17] && w[17]!=one) || w[20]!=one) return E_NOTIMPL;
            memcpy(s.BorderColor,w+17,sizeof s.BorderColor);memset(w+17,0,sizeof s.BorderColor);
        }
        w[6]=w[7]=w[8]=D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        if (memcmp(w,linear,sizeof w)) return E_NOTIMPL;
    } else if (w[8]==D3D12_TEXTURE_ADDRESS_MODE_CLAMP) {
        if (w[6]==w[7] && (w[6]==D3D12_TEXTURE_ADDRESS_MODE_CLAMP || w[6]==D3D12_TEXTURE_ADDRESS_MODE_MIRROR)) {
            s.AddressU=s.AddressV=(D3D12_TEXTURE_ADDRESS_MODE)w[6];
            w[6]=w[7]=D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        }
        s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        w[8]=D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    }
    if (!memcmp(w,point,sizeof w)) s.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;
    else if (memcmp(w,linear,sizeof w)) return E_NOTIMPL;
    struct xbox_device *d=CONTAINING_RECORD(iface,struct xbox_device,iface);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    UINT64 *serial=sampler_slot(d,dest,FALSE,&cpu);
    UINT64 tag[2]={++d->descriptor_serial,SAMPLER_TAG};
    BOOL ok=serial && store_slot_tag(dest,tag);
    if (ok) {
        ID3D12Device_CreateSampler(d->host,&s,cpu);
        *serial=tag[0];
    }
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    return ok ? S_OK : E_INVALIDARG;
}
static void WINAPI sampler_write(IUnknown *iface,const void *desc,SIZE_T dest)
{
    if (FAILED(xs_write_sampler(iface,desc,dest))) xs_stop();
}
static void WINAPI sampler_copy(IUnknown *iface,UINT count,SIZE_T dest,SIZE_T src,UINT type)
{
#ifdef SCARLETT_STAGED_PLACED
    if (count==1 && type==D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
        if (FAILED(xs_copy_wc_view(iface,dest,src))) xs_stop();
        return;
    }
#endif
    if (count!=1 || type!=D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) xs_stop();
    struct xbox_device *d=CONTAINING_RECORD(iface,struct xbox_device,iface);
    D3D12_CPU_DESCRIPTOR_HANDLE from,to;
    UINT64 tag[2];
    AcquireSRWLockExclusive(&d->descriptor_lock);
    UINT64 *written=sampler_slot(d,src,FALSE,&from),*target=sampler_slot(d,dest,TRUE,&to);
    BOOL ok=written && target && *written && copy_in(tag,(void *)src,sizeof tag) &&
        tag[0]==*written && tag[1]==SAMPLER_TAG && store_slot_tag(dest,tag);
    if (ok) {
        ID3D12Device_CopyDescriptorsSimple(d->host,1,to,from,D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        *target=*written;
    }
    ReleaseSRWLockExclusive(&d->descriptor_lock);
    if (!ok) xs_stop();
}

struct bootstrap_vtbl {
    IUnknownVtbl unknown;
    xs_stop_fn gap18[5];
    __typeof__(&bootstrap_queue) queue;
    __typeof__(&bootstrap_allocator) allocator;
    xs_stop_fn gap50[4];
    __typeof__(&descriptor_heap) descriptors;
    __typeof__(&descriptor_stride) stride;
    __typeof__(&bootstrap_root) root;
    __typeof__(&write_cbv) cbv;
    __typeof__(&write_srv) srv;
    __typeof__(&write_uav) uav;
    __typeof__(&write_rtv) rtv;
    __typeof__(&write_dsv) dsv;
    xs_stop_fn gapb0[2];
    __typeof__(&sampler_copy) copy_simple;
    __typeof__(&bootstrap_allocation_info) allocation_info;
    __typeof__(&bootstrap_heap) heap;
    __typeof__(&bootstrap_readback) readback;
    xs_stop_fn gape0[8];
    __typeof__(&bootstrap_fence) fence;
    xs_stop_fn gap128[2];
    __typeof__(&bootstrap_query_heap) query_heap;
    xs_stop_fn gap140;
    __typeof__(&bootstrap_signature) signature;
    xs_stop_fn gap150[6];
    __typeof__(&bootstrap_extended) extended;
    xs_stop_fn gap188[12];
    __typeof__(&sampler_write) sampler;
    __typeof__(&bootstrap_bvh_pipeline) bvh_pipeline;
    xs_stop_fn gap1f8[7];
#ifdef SCARLETT_STAGED_PLACED
    __typeof__(&write_address_srv) address_srv;
#else
    xs_stop_fn address_srv;
#endif
    xs_stop_fn gap238[2];
    __typeof__(&bootstrap_list) list;
    HRESULT (WINAPI *register_pool)(IUnknown *,UINT64,UINT,HANDLE *);
    xs_stop_fn gap258[3];
    __typeof__(&bootstrap_frame_interval) frame_interval;
    __typeof__(&bootstrap_frame_event) frame_event;
    __typeof__(&bootstrap_frame_wait) frame_wait;
    xs_stop_fn gap288;
    xs_stop_fn gap290[4];
    __typeof__(&clear_pipeline) graphics_stream;
    xs_stop_fn gap2b8[21];
    __typeof__(&bootstrap_allocation_info1) allocation_info1;
    xs_stop_fn gap368[18];
    __typeof__(&bootstrap_committed) committed;
    xs_stop_fn gap400[5];
    __typeof__(&bootstrap_placed) placed;
};
_Static_assert(offsetof(struct bootstrap_vtbl, queue) == 0x40 &&
               offsetof(struct bootstrap_vtbl, sampler) == 0x1e8 &&
               offsetof(struct bootstrap_vtbl, copy_simple) == 0xc0 &&
               offsetof(struct bootstrap_vtbl, readback) == 0xd8 &&
               offsetof(struct bootstrap_vtbl, committed) == 0x3f8 &&
               offsetof(struct bootstrap_vtbl, graphics_stream) == 0x2b0 &&
               offsetof(struct bootstrap_vtbl, placed) == 0x428 &&
               offsetof(struct bootstrap_vtbl, address_srv) == 0x230 &&
               offsetof(struct bootstrap_vtbl, descriptors) == 0x70 &&
               offsetof(struct bootstrap_vtbl, stride) == 0x78 &&
               offsetof(struct bootstrap_vtbl, cbv) == 0x88 && offsetof(struct bootstrap_vtbl, srv) == 0x90 &&
               offsetof(struct bootstrap_vtbl, uav) == 0x98 &&
               offsetof(struct bootstrap_vtbl, frame_interval) == 0x270 &&
               offsetof(struct bootstrap_vtbl, frame_event) == 0x278 &&
               offsetof(struct bootstrap_vtbl, frame_wait) == 0x280 &&
               offsetof(struct bootstrap_vtbl, allocator) == 0x48 &&
               offsetof(struct bootstrap_vtbl, list) == 0x248 &&
               offsetof(struct bootstrap_vtbl, register_pool) == 0x250 &&
               offsetof(struct bootstrap_vtbl, allocation_info) == 0xc8 &&
               offsetof(struct bootstrap_vtbl, allocation_info1) == 0x360 &&
               offsetof(struct bootstrap_vtbl, root) == 0x80 &&
               offsetof(struct bootstrap_vtbl, bvh_pipeline) == 0x1f0 &&
               offsetof(struct bootstrap_vtbl, rtv) == 0xa0 && offsetof(struct bootstrap_vtbl, dsv) == 0xa8 &&
               offsetof(struct bootstrap_vtbl, heap) == 0xd0 &&
               offsetof(struct bootstrap_vtbl, fence) == 0x120 &&
               offsetof(struct bootstrap_vtbl, query_heap) == 0x138 &&
               offsetof(struct bootstrap_vtbl, signature) == 0x148 &&
               offsetof(struct bootstrap_vtbl, extended) == 0x180, "bootstrap x64 ABI");
static const struct bootstrap_vtbl bootstrap_vtbl;

/* Return the retained host image, never call through the guest's resource pointer. */
ID3D12Resource *xs_retain_primary(IUnknown *owner,const void *identity)
{
    if (!owner || owner->lpVtbl!=&bootstrap_vtbl.unknown) return NULL;
    struct xbox_device *d=CONTAINING_RECORD(owner,struct xbox_device,iface);
    AcquireSRWLockShared(&d->descriptor_lock);
    struct color_resource *r=d->colors;
    while (r && &r->iface!=identity) r=r->next;
    ID3D12Resource *host=r && !r->msaa && r->slice ? r->host : NULL;
    if (host) ID3D12Resource_AddRef(host);
    ReleaseSRWLockShared(&d->descriptor_lock);
    return host;
}

#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
/* Multi-queue history before the first present makes this source ineligible.
 * Once claimed, never let an unordered writer take over its layout. */
HRESULT xs_primary_writer(IUnknown *owner,ID3D12Resource *src,ID3D12CommandQueue *q)
{
    struct xbox_device *d=CONTAINING_RECORD(owner,struct xbox_device,iface);
    HRESULT hr=S_OK;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    for(struct color_resource *r=d->colors;r;r=r->next) if(r->slice && r->host==src) {
        if(r->flipping){hr=DXGI_ERROR_WAS_STILL_DRAWING;break;}
        if(!r->writer){r->writer=q;ID3D12CommandQueue_AddRef(q);}
        else if(r->writer!=q){if(r->claimed)hr=E_ACCESSDENIED;else r->multi_writer=TRUE;}
        break;
    }
    ReleaseSRWLockExclusive(&d->descriptor_lock);return hr;
}
IUnknown *xs_claim_view(IUnknown *owner,IUnknown *key,ID3D12Resource *src,ID3D12CommandQueue *q,SIZE_T slot,UINT64 serial,BOOL flip)
{
    struct xbox_device *d=CONTAINING_RECORD(owner,struct xbox_device,iface);IUnknown *pin=NULL;
    AcquireSRWLockExclusive(&d->descriptor_lock);
    for(struct color_resource *r=d->colors;r;r=r->next) if(&r->iface==key && r->slice && r->host==src && r->writer==q && !r->multi_writer && !r->flipping) {
        for(struct desc_heap *h=d->rtv_heaps;h;h=h->next){D3D12_CPU_DESCRIPTOR_HANDLE cpu;
            if(attachment_locked(h,slot,RTV,&cpu)==src && h->records[(slot-h->start)/h->stride].serial==serial){
                r->claimed=TRUE;r->flipping=flip;pin=key;IUnknown_AddRef(pin);break;
            }
        }
        break;
    }
    ReleaseSRWLockExclusive(&d->descriptor_lock);return pin;
}
void xs_finish_flip(IUnknown *pin)
{
    struct color_resource *r=CONTAINING_RECORD(pin,struct color_resource,iface);
    AcquireSRWLockExclusive(&r->owner->descriptor_lock);r->flipping=FALSE;ReleaseSRWLockExclusive(&r->owner->descriptor_lock);
}
#endif
/* Borrowed host root; caller holds the device and root live through use. */
ID3D12RootSignature *xs_bvh_root_host(IUnknown *device,IUnknown *i)
{
    const IUnknownVtbl *v;
    if (!copy_in(&v,device,sizeof v) || v!=&bootstrap_vtbl.unknown) return NULL;
    struct xbox_device *d=CONTAINING_RECORD(device,struct xbox_device,iface);
    AcquireSRWLockShared(&d->descriptor_lock);
    struct bvh_root *r=d->bvh_roots;
    while (r && &r->iface!=i) r=r->next;
    ID3D12RootSignature *host=r ? r->host : NULL;
    ReleaseSRWLockShared(&d->descriptor_lock);
    return host;
}

static HRESULT bootstrap_params(const void *params)
{
    DWORD w[20];
    SIZE_T copied;
    if (!ReadProcessMemory(GetCurrentProcess(), params, w, sizeof(w), &copied) || copied != sizeof(w))
        return E_POINTER;
    if (w[0] != 0x20004 || w[2] != 0x100000 || w[8] > 1 || w[9] > 1 ||
            w[10] != 1 || w[15] != 0x20) return E_NOTIMPL;
    for (unsigned i = 0; i < ARRAYSIZE(w); ++i)
        if (i != 0 && i != 2 && i != 8 && i != 9 && i != 10 && i != 15 && w[i]) return E_NOTIMPL;
    return S_OK;
}
#endif

static HRESULT WINAPI device_query(IUnknown *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid) return E_INVALIDARG;
    if (!IsEqualIID(iid, &IID_IUnknown)
#ifdef SCARLETT_DEVICE_BOOTSTRAP
            && !(iface->lpVtbl == &bootstrap_vtbl.unknown && IsEqualIID(iid, &bootstrap_iid))
#endif
            ) return E_NOINTERFACE;
    device_addref(iface);
    *out = iface;
    return S_OK;
}

static const IUnknownVtbl device_vtbl = { device_query, device_addref, device_release };
#ifdef SCARLETT_DEVICE_BOOTSTRAP
static HRESULT WINAPI builder_root_call(IUnknown *device,UINT node,const void *blob,SIZE_T bytes,REFIID iid,void **out)
{
    HRESULT hr=bootstrap_root(device,node,blob,bytes,iid,out);
    if (hr==E_NOTIMPL) {
        HMODULE builder=GetModuleHandleW(L"XboxBvhBuilder.dll");
        void *frames[8];
        USHORT n=CaptureStackBackTrace(0,ARRAYSIZE(frames),frames,NULL);
        for (USHORT i=0;builder && i<n;i++) {
            HMODULE module;
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    (const char *)frames[i],&module) || module!=builder) continue;
            char text[144];
            snprintf(text,sizeof text,"scarlett: device +0x80 unsupported: %zu-byte root, XboxBvhBuilder.dll return +%llx\n",
                    bytes,(unsigned long long)((UINT_PTR)frames[i]-(UINT_PTR)builder));
            OutputDebugStringA(text);
        }
    }
    return hr;
}
static HRESULT WINAPI builder_pipeline_call(IUnknown *device,const void *desc,UINT64 x,UINT64 y,REFIID iid,void **out)
{
    HRESULT hr=bootstrap_bvh_pipeline(device,desc,x,y,iid,out);
    struct { IUnknown *root; const void *shader; SIZE_T size; } w;
    HMODULE builder=GetModuleHandleW(L"XboxBvhBuilder.dll"),module;
    if (hr==E_NOTIMPL && builder && copy_in(&w,desc,sizeof w) &&
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (const char *)w.shader,&module) && module==builder) {
        char text[144];
        snprintf(text,sizeof text,"scarlett: device +0x1f0 unsupported: XboxBvhBuilder.dll shader +%llx, %zu bytes\n",
                (unsigned long long)((UINT_PTR)w.shader-(UINT_PTR)builder),w.size);
        OutputDebugStringA(text);
    }
    return hr;
}
static const struct bootstrap_vtbl bootstrap_vtbl = {
    .unknown = { device_query, device_addref, device_release },
    .gap18 = { [0 ... 4] = xs_stop }, .queue = bootstrap_queue,
    .allocator = bootstrap_allocator, .gap50 = { [0 ... 3] = xs_stop },
    .descriptors = descriptor_heap, .stride = descriptor_stride, .root = builder_root_call,
    .cbv = write_cbv, .srv = write_srv, .uav = write_uav, .rtv = write_rtv, .dsv = write_dsv, .gapb0 = {xs_stop,xs_stop},
    .copy_simple = sampler_copy,
    .allocation_info = bootstrap_allocation_info, .heap = bootstrap_heap,
    .readback = bootstrap_readback, .gape0 = { [0 ... 7] = xs_stop }, .fence = bootstrap_fence,
    .gap128 = { xs_stop, xs_stop }, .query_heap = bootstrap_query_heap, .gap140 = xs_stop,
    .signature = bootstrap_signature,
    .gap150 = { [0 ... 5] = xs_stop }, .extended = bootstrap_extended,
    .gap188 = { [0 ... 11] = xs_stop }, .sampler=sampler_write, .bvh_pipeline = builder_pipeline_call,
    .gap1f8 = { [0 ... 6] = xs_stop },
#ifdef SCARLETT_STAGED_PLACED
    .address_srv = write_address_srv,
#else
    .address_srv = xs_stop,
#endif
    .gap238 = {xs_stop,xs_stop}, .list = bootstrap_list, .register_pool = register_page_pool,
    .gap258 = {xs_stop,xs_stop,xs_stop},
    .frame_interval = bootstrap_frame_interval, .frame_event = bootstrap_frame_event,
    .frame_wait = bootstrap_frame_wait, .gap288 = xs_stop, .gap290 = { [0 ... 3] = xs_stop },
    .graphics_stream = clear_pipeline, .gap2b8 = { [0 ... 20] = xs_stop },
    .allocation_info1=bootstrap_allocation_info1, .gap368={ [0 ... 17]=xs_stop },
    .committed = bootstrap_committed, .gap400 = { [0 ... 4] = xs_stop }, .placed = bootstrap_placed
};
#endif

__declspec(dllexport) HRESULT WINAPI D3D12XboxCreateDevice(
        void *adapter, const void *params, REFIID iid, void **out)
{
    struct xbox_device *device;
    const IUnknownVtbl *vtbl = &device_vtbl;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (adapter || !params || !iid) return E_INVALIDARG;
#ifdef SCARLETT_DEVICE_BOOTSTRAP
    if (IsEqualIID(iid, &bootstrap_iid)) {
        if (FAILED(hr = bootstrap_params(params))) return hr;
        vtbl = &bootstrap_vtbl.unknown;
    } else
#endif
    if (!IsEqualIID(iid, &IID_IUnknown)) return E_NOINTERFACE;

    device = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*device));
    if (!device) return E_OUTOFMEMORY;
    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&device->host);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, device);
        return hr;
    }
    device->iface.lpVtbl = vtbl;
    device->refs = 1;
    *out = &device->iface;
    return S_OK;
}
