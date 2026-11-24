#include <stdint.h>
#include "bvh_record.h"
#include "shader_digest.h"
#include "bvh_init_dxil.h"
#include "bvh_offline_dxil.h"
#include "bvh_partitions_dxil.h"

enum bvh_kind { BVH_INIT, BVH_OFFLINE, BVH_PARTITIONS, BVH_RESET_COUNTERS, BVH_INSTANCES, BVH_RESET_INDEX, BVH_COPY_UPDATE, BVH_COPY_COUNTERS, BVH_LOAD_INSTANCES, BVH_BEGIN_PASS, BVH_HEADERS, BVH_MORTON32, BVH_MORTON64,
    BVH_BITONIC_PRE, BVH_BITONIC_OUTER, BVH_BITONIC_INNER, BVH_BITONIC_PRE64, BVH_BITONIC_OUTER64, BVH_BITONIC_INNER64 };
static const struct bvh_candidate {
    SIZE_T bytes;
    BYTE digest[32];
    const WCHAR *name;
    UINT constants;
    D3D12_SHADER_BYTECODE cs;
} bvh_candidates[]={
    [BVH_HEADERS]={0x1488,{0x7b,0xdd,0x8b,0x5b,0xf4,0x76,0xe7,0xf2,0x09,0x51,0xa9,0x2c,0x55,0x6d,0x6f,0x39,
        0x4d,0x4e,0xf9,0xbd,0x4c,0xa3,0x39,0x69,0x69,0x29,0x8d,0x3e,0xe1,0xd8,0xdd,0x7d},L"XDXR InitHeaders",1,{0}},
    [BVH_BEGIN_PASS]={0x1118,{0x9b,0xa6,0x1b,0x8f,0x5b,0x27,0x54,0x1f,0x62,0xe4,0x92,0x2d,0x1c,0x97,0xa2,0x0c,
        0xef,0x3c,0x5d,0x26,0xf4,0xef,0xb2,0x96,0x30,0x8e,0x32,0x70,0x07,0xa6,0xb4,0x39},L"XDXR BeginPass",5,{NULL,0}},
    [BVH_BITONIC_PRE]={0x20c8,{0x5b,0xc2,0x4d,0xd2,0xf2,0x93,0x64,0x76,0xc0,0xe7,0xc8,0x04,0x46,0xa9,0x97,0x95,
        0xdf,0x05,0xa7,0xc7,0xcb,0x46,0xad,0xe3,0xc0,0xe8,0x49,0x67,0xa5,0x64,0x45,0x87},L"XDXR BitonicPreSort 0",2,{0}},
    [BVH_BITONIC_OUTER]={0x1468,{0xb2,0xe5,0x9c,0x48,0x6e,0x9e,0xb7,0x21,0x4b,0x10,0x47,0x17,0x18,0xc1,0x45,0x7e,
        0x9f,0x78,0x09,0x29,0xae,0x35,0xe5,0xc7,0x2c,0xf2,0x8a,0x8b,0x19,0x43,0x76,0x13},L"XDXR BitonicOuterSort 0",2,{0}},
    [BVH_BITONIC_INNER]={0x1f5c,{0xba,0x19,0x42,0x68,0xb4,0xe8,0xbf,0xff,0xe4,0x38,0xcd,0x47,0xe4,0x25,0x45,0x58,
        0xd2,0x2a,0xdc,0xcb,0xa4,0xd2,0x9c,0x96,0xb7,0x1f,0x99,0x0b,0xb3,0xaf,0x58,0x4d},L"XDXR BitonicInnerSort 0",2,{0}},
    [BVH_BITONIC_PRE64]={0x2204,{0x00,0x66,0x3d,0x62,0xd6,0x05,0x2e,0xbe,0x98,0x6e,0x1d,0x27,0xb4,0x0d,0xf9,0xf7,
        0x4c,0x16,0x04,0x01,0x88,0x76,0xcf,0xa7,0x24,0x29,0xd6,0x0d,0xe0,0x99,0xf9,0xa9},L"XDXR BitonicPreSort 1",2,{0}},
    [BVH_BITONIC_OUTER64]={0x1554,{0x12,0x62,0xd4,0x90,0x94,0xb2,0x8f,0xe9,0x68,0x69,0x88,0x23,0xff,0x23,0xcb,0x61,
        0xc4,0xbb,0x1e,0xa9,0x0c,0x13,0xd4,0x02,0xee,0x78,0x92,0xe1,0x83,0xde,0x95,0xb8},L"XDXR BitonicOuterSort 1",2,{0}},
    [BVH_BITONIC_INNER64]={0x2094,{0x72,0x8a,0x1f,0xa6,0x66,0x58,0x41,0xb9,0x9c,0x34,0xd6,0x46,0xb2,0xb1,0x18,0xe9,
        0x0b,0xee,0x76,0xa8,0x32,0xbb,0x10,0x5a,0x11,0xeb,0x90,0xe4,0x0b,0xed,0xe0,0x84},L"XDXR BitonicInnerSort 1",2,{0}},
    [BVH_INIT]={0x1410,{0xad,0xa7,0x13,0x02,0x58,0x9d,0xae,0x66,0x27,0xfb,0x5d,0xa6,0xd0,0xe7,0xc1,0x34,
        0x00,0x10,0x7c,0x6a,0xe5,0x6b,0xb1,0x5e,0x1d,0xca,0x58,0x48,0x79,0x23,0x23,0xad},
        L"XDXR InitPartitionedHeaders",8,{bvh_init_dxil,sizeof bvh_init_dxil}},
    [BVH_OFFLINE]={0x1ec4,{0xed,0x66,0x93,0xd0,0x74,0x05,0x32,0xcb,0x77,0xd3,0xb6,0xa1,0x6b,0x26,0x2e,0xfd,
        0x01,0x26,0xa7,0x2b,0xb4,0x4e,0x0a,0x96,0x70,0x25,0x36,0x61,0x4b,0x3c,0x00,0x3c},
        L"XDXR WriteOfflinePartition",40,{bvh_offline_dxil,sizeof bvh_offline_dxil}},
    [BVH_PARTITIONS]={0x1580,{0x2a,0xf6,0xb2,0xe1,0x61,0xe1,0xbe,0xa2,0xc9,0x3c,0x43,0xb4,0x0d,0xc7,0xb1,0x6f,
        0x77,0xb8,0x13,0x23,0x83,0x92,0x1f,0x31,0xa4,0xaf,0x0e,0xbd,0x34,0x5c,0x11,0x96},
        L"XDXR WritePartitions",36,{bvh_partitions_dxil,sizeof bvh_partitions_dxil}},
    [BVH_RESET_COUNTERS]={0xfe8,{0xe2,0x30,0x9a,0x6f,0x2c,0x95,0xd0,0x97,0xe5,0x00,0xd4,0xc3,0x8e,0x20,0x58,0x81,
        0xa0,0xc2,0x43,0x3f,0x17,0x54,0xf9,0x7b,0xae,0xf0,0x6e,0x17,0x5e,0xd2,0xb0,0x2e},
        L"XDXR ResetCounters",3,{NULL,0}},
    [BVH_RESET_INDEX]={0x1004,{0xaf,0xc3,0xab,0x7f,0x0a,0x51,0x29,0xc1,0x10,0x07,0x8a,0x8e,0x56,0xb3,0xb8,0x4f,
        0x85,0x91,0x2e,0xaa,0xe0,0xa9,0xba,0xb1,0x7e,0x8b,0x95,0x00,0xac,0x2f,0x4c,0xca},
        L"XDXR ResetToIndexBuffer",3,{NULL,0}},
    [BVH_INSTANCES]={0x493c,{0xda,0x1d,0x4b,0xc3,0xb8,0xc1,0xf6,0x85,0x19,0x76,0xd8,0x99,0x18,0xba,0xb9,0x96,
        0x78,0xd1,0xb0,0xf7,0x4b,0x0c,0x91,0x8f,0xf1,0x58,0xe9,0xe9,0x20,0x54,0xb6,0x20},L"XDXR WriteInstances",16,{0}},
    [BVH_COPY_UPDATE]={0x126c,{0x96,0xfd,0xbb,0x6d,0x8c,0x64,0xad,0x8a,0x39,0x5f,0xc8,0x8f,0x28,0x4b,0xcd,0x48,
        0x3d,0xa7,0x71,0x94,0x37,0x6d,0xc5,0x29,0x07,0xed,0x93,0x1a,0x25,0x5e,0xbd,0x12},L"XDXR CopyForUpdate",2,{0}},
    [BVH_LOAD_INSTANCES]={0x752c,{0xee,0x24,0xd3,0x76,0x1d,0xc2,0xfa,0x3a,0x74,0xf3,0x9e,0x97,0xfd,0x6b,0xf6,0xe8,
        0x68,0xf4,0x15,0x65,0xf6,0x0f,0x8b,0xdc,0x46,0x94,0x27,0x69,0xb5,0x5a,0x73,0x21},L"XDXR LoadInstances",16,{0}},
    [BVH_COPY_COUNTERS]={0x10e8,{0x1b,0x08,0x3c,0x49,0xc7,0x32,0xe1,0x85,0x49,0xcf,0xb7,0x53,0x78,0xe9,0x14,0xdb,
        0xca,0x80,0xb1,0xf1,0x22,0xc7,0x58,0xb3,0x55,0xc8,0xaa,0x3f,0x56,0xed,0x93,0x61},L"XDXR CopyCounters",2,{0}},
    [BVH_MORTON32]={0x33a0,{0xc3,0x42,0x0b,0x9e,0x30,0x8e,0x68,0xe9,0x17,0x22,0x14,0xef,0x4a,0x6d,0xa6,0xf9,
        0xb2,0x3f,0x34,0x9a,0xf6,0xcf,0x71,0x77,0x02,0x13,0x6f,0xd4,0x9f,0x4f,0xe8,0xff},L"XDXR CalculateMortonCodes 0",8,{0}},
    [BVH_MORTON64]={0x33fc,{0x94,0x16,0xd3,0x89,0x81,0x62,0xdb,0x0c,0x77,0x35,0xb0,0xb4,0x6f,0xb3,0xa8,0x0a,
        0xe7,0xdf,0x9e,0x6f,0x99,0x44,0x5b,0x74,0xca,0x32,0x20,0x77,0x08,0x16,0x46,0x1e},L"XDXR CalculateMortonCodes 1",8,{0}},
};

struct bvh_pipeline {
    IUnknown iface;
    LONG refs;
    struct xbox_device *owner;
    ID3D12RootSignature *root; /* private translation layout */
    ID3D12PipelineState *host;
    enum bvh_kind kind;
};
static ULONG WINAPI bp_addref(IUnknown *i)
{
    return InterlockedIncrement(&CONTAINING_RECORD(i,struct bvh_pipeline,iface)->refs);
}
static ULONG WINAPI bp_release(IUnknown *i)
{
    struct bvh_pipeline *p=CONTAINING_RECORD(i,struct bvh_pipeline,iface);
    ULONG refs=InterlockedDecrement(&p->refs);
    if (!refs) {
        ID3D12PipelineState_Release(p->host);
        ID3D12RootSignature_Release(p->root);
        IUnknown_Release(&p->owner->iface);
        HeapFree(GetProcessHeap(),0,p);
    }
    return refs;
}
static HRESULT WINAPI bp_query(IUnknown *i,REFIID iid,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    IID id;
    if (!copy_in(&id,iid,sizeof id)) return E_POINTER;
    if (!IsEqualIID(&id,&IID_IUnknown) && !IsEqualIID(&id,&IID_ID3D12PipelineState)) return E_NOINTERFACE;
    bp_addref(i); *out=i;
    return S_OK;
}
static HRESULT WINAPI bp_name(IUnknown *i,const WCHAR *name)
{
    struct bvh_pipeline *p=CONTAINING_RECORD(i,struct bvh_pipeline,iface);
    const WCHAR *expected=bvh_candidates[p->kind].name;
    WCHAR s[32]; SIZE_T bytes=(wcslen(expected)+1)*sizeof(WCHAR);
    if (!copy_in(s,name,bytes)) return E_POINTER;
    if (memcmp(s,expected,bytes)) return E_NOTIMPL;
    return ID3D12PipelineState_SetName(p->host,expected);
}
static const struct bp_methods {
    IUnknownVtbl unknown;
    xs_stop_fn gap18[3];
    __typeof__(&bp_name) name;
    xs_stop_fn get_device, cached_blob;
} bp_methods={{bp_query,bp_addref,bp_release},{xs_stop,xs_stop,xs_stop},bp_name,xs_stop,xs_stop};
_Static_assert(offsetof(struct bp_methods,name)==0x30,"PSO name slot");

static HRESULT WINAPI bootstrap_bvh_pipeline(IUnknown *device,const void *desc,UINT64 x,UINT64 y,REFIID iid,void **out)
{
    struct { IUnknown *root; const void *shader; SIZE_T n; BYTE tail[32]; } w;
    _Static_assert(sizeof w==0x38,"console +1f0 descriptor");
    if (!out) return E_POINTER;
    *out=NULL;
    IID id;
    if (!copy_in(&id,iid,sizeof id)) return E_POINTER;
    if (!IsEqualIID(&id,&IID_ID3D12PipelineState)) return E_NOINTERFACE;
    if (x || y) return E_NOTIMPL;
    if (!copy_in(&w,desc,sizeof w)) return E_POINTER;
    BYTE code[0x752c];
    enum bvh_kind kind=BVH_INIT;
    while (kind<ARRAYSIZE(bvh_candidates) && w.n!=bvh_candidates[kind].bytes) ++kind;
    if (kind==ARRAYSIZE(bvh_candidates) || w.n>sizeof code || memcmp(w.tail,(BYTE[32]){0},sizeof w.tail)) return E_NOTIMPL;
    struct xbox_device *d=CONTAINING_RECORD(device,struct xbox_device,iface);
    AcquireSRWLockShared(&d->descriptor_lock);
    struct bvh_root *r=d->bvh_roots;
    while (r && &r->iface!=w.root) r=r->next;
    int root_kind=r ? (int)r->kind : -1;
    ReleaseSRWLockShared(&d->descriptor_lock);
    if (root_kind<0) return E_INVALIDARG;
    if (!copy_in(code,w.shader,w.n)) return E_POINTER;
    /* Size bounds the copy. The digest, not the size, selects the kernel and its root. */
    while (kind<ARRAYSIZE(bvh_candidates) &&
            (w.n!=bvh_candidates[kind].bytes || !shader_digest_matches(code,w.n,bvh_candidates[kind].digest))) ++kind;
    if (kind==ARRAYSIZE(bvh_candidates)) return E_NOTIMPL;
    const struct bvh_candidate *candidate=&bvh_candidates[kind];
    BOOL morton=kind==BVH_MORTON32 || kind==BVH_MORTON64;
    BOOL bitonic=kind>=BVH_BITONIC_PRE && kind<=BVH_BITONIC_INNER64;
    if (root_kind!=(bitonic ? ROOT_BITONIC : morton ? ROOT_MORTON : ROOT_UTILITY)) return E_INVALIDARG;

    D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,1}};
    D3D12_ROOT_PARAMETER rp[7]={
        {.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,.DescriptorTable={kind==BVH_COPY_COUNTERS ? 2u : 1u,ranges}},
        {.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0,candidate->constants}}};
    D3D12_ROOT_SIGNATURE_DESC rs={2,rp,0,NULL,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    if (kind==BVH_HEADERS) {
        HMODULE core=GetModuleHandleW(L"d3d12core.dll");
        if (!core || !GetProcAddress(core,"ScarlettBoundedInitHeadersV1")) return E_NOTIMPL;
        rp[0]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV};
        rp[2]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,3}};
        rp[3]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={0,0x5358}};
        rs.NumParameters=4;
    }
    if (kind==BVH_BEGIN_PASS) {
        HMODULE core=GetModuleHandleW(L"d3d12core.dll");
        if (!core || !GetProcAddress(core,"ScarlettBoundedBeginPassV1")) return E_NOTIMPL;
        rp[2]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,12}};
        rp[3]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={0,0x5358}};
        rp[4]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={1,0x5358}};
        rs.NumParameters=5;
    }
    if (bitonic) {
        HMODULE core=GetModuleHandleW(L"d3d12core.dll");
        if (!core || !GetProcAddress(core,"ScarlettBoundedBitonicV1")) return E_NOTIMPL;
        rp[0]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={1,0,2}};
        for (UINT i=0;i<2;i++) rp[2+i]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={i,0}};
        rp[4]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,3}};
        rp[5]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV,.Descriptor={0,0x5358}};
        rs.NumParameters=6;
    }
    if (kind==BVH_COPY_UPDATE) {
        rp[2]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV};
        rs.NumParameters=3;
    }
    if (morton) {
        HMODULE core=GetModuleHandleW(L"d3d12core.dll");
        if (!core || !GetProcAddress(core,"ScarlettBoundedMortonV1")) return E_NOTIMPL;
        rp[0]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV};
        for (UINT i=1;i<=4;i++) rp[i]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={i-1,0}};
        rp[5]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,3}};
        rp[6]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV,.Descriptor={0,0x5358}};
        rs.NumParameters=7;
    }
    if (kind==BVH_INSTANCES) {
        HMODULE core=GetModuleHandleW(L"d3d12core.dll");
        if (!core || !GetProcAddress(core,"ScarlettBoundedInstanceReadsV1")) return E_NOTIMPL;
        rp[2]=rp[1];
        rp[1]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV};
        rp[3]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,3}};
        rp[4]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV,.Descriptor={0,0x5358}};
        rs.NumParameters=5;
    }
    if (kind==BVH_LOAD_INSTANCES) {
        HMODULE core=GetModuleHandleW(L"d3d12core.dll");
        if (!core || !GetProcAddress(core,"ScarlettBoundedLoadInstancesV1")) return E_NOTIMPL;
        rp[2]=rp[1];
        rp[0]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV};
        rp[1]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV};
        rp[3]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0x5358,6}};
        rp[4]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV,.Descriptor={0,0x5358}};
        rp[5]=(D3D12_ROOT_PARAMETER){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={0,0x5358}};
        rs.NumParameters=6;
    }
    ID3DBlob *blob=NULL;
    HRESULT hr=D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,NULL);
    if (FAILED(hr)) return hr;
    struct bvh_pipeline *p=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *p);
    if (!p) { ID3D10Blob_Release(blob); return E_OUTOFMEMORY; }
    hr=ID3D12Device_CreateRootSignature(d->host,1,ID3D10Blob_GetBufferPointer(blob),ID3D10Blob_GetBufferSize(blob),
            &IID_ID3D12RootSignature,(void **)&p->root);
    ID3D10Blob_Release(blob);
    if (SUCCEEDED(hr)) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd={.pRootSignature=p->root,.CS=candidate->cs};
        if (!candidate->cs.pShaderBytecode) pd.CS=(D3D12_SHADER_BYTECODE){code,w.n};
        hr=ID3D12Device_CreateComputePipelineState(d->host,&pd,&IID_ID3D12PipelineState,(void **)&p->host);
    }
    if (FAILED(hr)) {
        if (p->root) ID3D12RootSignature_Release(p->root);
        HeapFree(GetProcessHeap(),0,p);
        return hr;
    }
    IUnknown_AddRef(device); p->owner=d; p->refs=1; p->iface.lpVtbl=&bp_methods.unknown; p->kind=kind;
    *out=&p->iface;
    return S_OK;
}

struct xs_bvh_hold { IUnknown *pipeline; ID3D12Resource *dst,*upload,*extra; ID3D12DescriptorHeap *heap; };
void xs_drop_bvh_hold(struct xs_bvh_hold *h)
{
    if (!h) return;
    if (h->upload) ID3D12Resource_Release(h->upload);
    if (h->extra) ID3D12Resource_Release(h->extra);
    if (h->heap) ID3D12DescriptorHeap_Release(h->heap);
    ID3D12Resource_Release(h->dst); IUnknown_Release(h->pipeline);
    HeapFree(GetProcessHeap(),0,h);
}
static HRESULT bvh_view_offset(ID3D12Device *device,const struct xs_bvh_range *range,UINT64 addr,UINT size,BOOL uav,UINT64 *offset)
{
    if (!range || !range->resource) return E_INVALIDARG;
    ID3D12Device *other;
    HRESULT hr=ID3D12Resource_GetDevice(range->resource,&IID_ID3D12Device,(void **)&other);
    if (FAILED(hr)) return hr;
    BOOL match=other==device; ID3D12Device_Release(other);
    if (!match) return E_INVALIDARG;
    D3D12_RESOURCE_DESC rd; range->resource->lpVtbl->GetDesc(range->resource,&rd);
    UINT64 delta=addr-range->guest_base;
    if (rd.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER ||
        (uav ? !(rd.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) : (rd.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) ||
        !range->guest_base || range->bytes>UINT64_MAX-range->guest_base || addr<range->guest_base ||
        (addr&3) || range->resource_offset>rd.Width || range->bytes>rd.Width-range->resource_offset ||
        delta>range->bytes || size>range->bytes-delta) return E_INVALIDARG;
    *offset=range->resource_offset+delta;
    return *offset%D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT ? E_INVALIDARG : S_OK;
}
static HRESULT bvh_list_device(IUnknown *pipeline,enum bvh_kind kind,ID3D12GraphicsCommandList *list,ID3D12Device **out)
{
    if (!pipeline || pipeline->lpVtbl!=&bp_methods.unknown || !list) return E_INVALIDARG;
    if (CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface)->kind!=kind) return E_INVALIDARG;
    if (ID3D12GraphicsCommandList_GetType(list)!=D3D12_COMMAND_LIST_TYPE_COMPUTE) return E_INVALIDARG;
    ID3D12Device *device=CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface)->owner->host;
    ID3D12Device *other=NULL;
    HRESULT hr=ID3D12GraphicsCommandList_GetDevice(list,&IID_ID3D12Device,(void **)&other);
    if (FAILED(hr)) return hr;
    BOOL match=other==device; ID3D12Device_Release(other);
    if (!match) return E_INVALIDARG;
    *out=device;
    return S_OK;
}
static HRESULT bvh_record_buffers(IUnknown *pipeline,enum bvh_kind kind,ID3D12GraphicsCommandList *list,
        const struct xs_bvh_range *range,UINT64 addr,UINT size,const struct xs_bvh_range *src,struct xs_bvh_hold **out)
{
    if (!range || !range->resource) return E_INVALIDARG;
    ID3D12Device *device=NULL;
    HRESULT hr=bvh_list_device(pipeline,kind,list,&device);
    if (FAILED(hr)) return hr;
    ID3D12Resource *dst=range->resource;
    UINT64 first,src_first=0;
    hr=bvh_view_offset(device,range,addr,size,TRUE,&first);
    if (FAILED(hr)) return hr;
    if (src) {
        if (src->resource==dst) return E_INVALIDARG;
        hr=bvh_view_offset(device,src,src->guest_base,size,FALSE,&src_first);
        if (FAILED(hr)) return hr;
    }
    struct xs_bvh_hold *h=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *h);
    if (!h) return E_OUTOFMEMORY;
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,src ? 2u : 1u,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    hr=ID3D12Device_CreateDescriptorHeap(device,&hd,&IID_ID3D12DescriptorHeap,(void **)&h->heap);
    if (FAILED(hr)) { HeapFree(GetProcessHeap(),0,h); return hr; }
    h->dst=dst; ID3D12Resource_AddRef(dst); h->pipeline=pipeline; IUnknown_AddRef(pipeline);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu; D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    h->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(h->heap,&cpu);
    h->heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(h->heap,&gpu);
    D3D12_UNORDERED_ACCESS_VIEW_DESC view={.Format=DXGI_FORMAT_R32_TYPELESS,.ViewDimension=D3D12_UAV_DIMENSION_BUFFER,
            .Buffer={.FirstElement=first/4,.NumElements=size/4,.Flags=D3D12_BUFFER_UAV_FLAG_RAW}};
    ID3D12Device_CreateUnorderedAccessView(device,dst,NULL,&view,cpu);
    if (src) {
        cpu.ptr+=ID3D12Device_GetDescriptorHandleIncrementSize(device,hd.Type);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv={.Format=DXGI_FORMAT_R32_TYPELESS,.ViewDimension=D3D12_SRV_DIMENSION_BUFFER,
            .Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Buffer={.FirstElement=src_first/4,.NumElements=size/4,.Flags=D3D12_BUFFER_SRV_FLAG_RAW}};
        ID3D12Device_CreateShaderResourceView(device,src->resource,&srv,cpu);
        h->upload=src->resource; ID3D12Resource_AddRef(h->upload);
    }
    struct bvh_pipeline *p=CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(list,1,&h->heap);
    ID3D12GraphicsCommandList_SetPipelineState(list,p->host);
    ID3D12GraphicsCommandList_SetComputeRootSignature(list,p->root);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list,0,gpu);
    *out=h;
    return S_OK;
}
static HRESULT bvh_record_target(IUnknown *pipeline,enum bvh_kind kind,ID3D12GraphicsCommandList *list,
        const struct xs_bvh_range *range,UINT64 addr,UINT size,struct xs_bvh_hold **out)
{
    return bvh_record_buffers(pipeline,kind,list,range,addr,size,NULL,out);
}
HRESULT xs_record_bvh_copy_counters(IUnknown *pipeline,ID3D12GraphicsCommandList *list,const struct xs_bvh_range *dst,
        const struct xs_bvh_range *src,UINT count,struct xs_bvh_hold **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!dst || !src || !count) return E_INVALIDARG;
    if (count>32u*D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION) return E_NOTIMPL;
    HRESULT hr=bvh_record_buffers(pipeline,BVH_COPY_COUNTERS,list,dst,dst->guest_base,16*count,src,out);
    if (FAILED(hr)) return hr;
    UINT constants[]={count,count};
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,2,constants,0);
    ID3D12GraphicsCommandList_Dispatch(list,(count+31)/32,1,1);
    return S_OK;
}
HRESULT xs_record_bvh_init(IUnknown *pipeline,ID3D12GraphicsCommandList *list,const struct xs_bvh_range *range,
        const void *initializer,SIZE_T bytes,struct xs_bvh_hold **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    struct { UINT64 addr; UINT words[8]; } init;
    _Static_assert(sizeof init==40,"PartitionedHeaderInitializer");
    if (bytes!=sizeof init) return E_INVALIDARG;
    if (!copy_in(&init,initializer,sizeof init)) return E_POINTER;
    const UINT n=init.words[5];
    if (n>65536) return E_NOTIMPL;
    HRESULT hr=bvh_record_target(pipeline,BVH_INIT,list,range,init.addr,n ? 84+28*(n-1) : 80,out);
    if (FAILED(hr)) return hr;
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,8,init.words,0);
    ID3D12GraphicsCommandList_Dispatch(list,1,1,1);
    return S_OK;
}
#include "bvh_offline_record.h"
#include "bvh_partitions_record.h"
#include "bvh_instances_record.h"
#include "bvh_copy_update_record.h"

HRESULT xs_record_bvh_reset(IUnknown *pipeline,ID3D12GraphicsCommandList *list,
        const struct xs_bvh_range *dst,UINT count,UINT value,struct xs_bvh_hold **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!dst || !count) return E_INVALIDARG;
    if (count>32u*D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION) return E_NOTIMPL;
    if (!pipeline || pipeline->lpVtbl!=&bp_methods.unknown) return E_INVALIDARG;
    enum bvh_kind kind=CONTAINING_RECORD(pipeline,struct bvh_pipeline,iface)->kind;
    if (kind!=BVH_RESET_COUNTERS && kind!=BVH_RESET_INDEX) return E_INVALIDARG;
    HRESULT hr=bvh_record_target(pipeline,kind,list,dst,dst->guest_base,16*count,out);
    if (SUCCEEDED(hr)) {
        UINT constants[]={count,count,value};
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,3,constants,0);
        ID3D12GraphicsCommandList_Dispatch(list,(count+31)/32,1,1);
    }
    return hr;
}

#include "bvh_beginpass_record.h"
#include "bvh_headers_record.h"
#include "bvh_morton_record.h"
