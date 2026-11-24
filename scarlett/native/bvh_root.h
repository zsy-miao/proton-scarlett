enum bvh_root_kind {
    ROOT_UTILITY, ROOT_MORTON, ROOT_BITONIC, ROOT_RADIX, ROOT_PREFIX, ROOT_REARRANGE,
    ROOT_COPY, ROOT_DECOMPRESS, ROOT_APEX, ROOT_BVH4, ROOT_HIERARCHY, ROOT_LOAD_INSTANCES,
    ROOT_LOAD_PRIMITIVES, ROOT_POSTBUILD, ROOT_SPLIT_INSTANCES, ROOT_TREELET, ROOT_SRV_SPACE3, ROOT_BINK_GROUP, ROOT_BLIT_COLOUR, ROOT_IM96
};

/* Root parameter index is part of the ABI; register order need not be ascending. */
#define T(r) {.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV,.Descriptor={r,0,0}}
#define U(r) {.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={r,0,0}}
#define C(r,n) {.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={r,0,n}}
#define SCHEMA(name,...) {L"XDXR " name,sizeof((D3D12_ROOT_PARAMETER1[]){__VA_ARGS__})/sizeof(D3D12_ROOT_PARAMETER1), \
        (const D3D12_ROOT_PARAMETER1[]){__VA_ARGS__}}
static const struct {
    const WCHAR *name;
    UINT count;
    const D3D12_ROOT_PARAMETER1 *params;
} root_schemas[]={
    [ROOT_UTILITY]=SCHEMA(L"UtilityShaders",T(0),U(0),C(0,16)),
    [ROOT_MORTON]=SCHEMA(L"MortonCodesCalculator",C(0,8),U(0),U(1),U(2),U(3),U(4)),
    [ROOT_BITONIC]=SCHEMA(L"BitonicSort",C(1,2),C(0,2),U(0),U(1)),
    [ROOT_RADIX]=SCHEMA(L"RadixSortCountAndAlloc",U(0),U(1),U(2),U(3),U(4),U(5),C(0,3)),
    [ROOT_PREFIX]=SCHEMA(L"RadixSortPrefixSum",U(0),U(1),C(0,1)),
    [ROOT_REARRANGE]=SCHEMA(L"RearrangeElementsPass",U(0),U(1),U(2),U(3),U(4),U(5),U(6),C(0,4)),
    [ROOT_COPY]=SCHEMA(L"CopyShaders",U(0),U(1),C(0,16)),
    [ROOT_DECOMPRESS]=SCHEMA(L"BVHDecompressor",C(0,19),U(0),U(1),T(0)),
    [ROOT_APEX]=SCHEMA(L"ApexPointMapComputer",U(0),U(1),C(0,6)),
    [ROOT_BVH4]=SCHEMA(L"BVH4Builder",U(0),U(1),U(2),U(3),U(4),U(5),U(6),U(7),C(0,3)),
    [ROOT_HIERARCHY]=SCHEMA(L"ConstructHierarchyPass",U(1),U(2),U(0),C(0,3)),
    [ROOT_LOAD_INSTANCES]=SCHEMA(L"LoadInstancesPass",U(0),U(1),U(2),C(0,10)),
    [ROOT_LOAD_PRIMITIVES]=SCHEMA(L"LoadPrimitivesPass",U(0),U(1),U(2),U(3),U(4),T(0),T(1),T(2),C(0,14)),
    [ROOT_POSTBUILD]=SCHEMA(L"PostBuildInfoQuery",U(0),U(1),U(2),C(0,4)),
    [ROOT_SPLIT_INSTANCES]=SCHEMA(L"SplitInstancesPass",U(0),U(1),U(2),U(3),C(0,8)),
    [ROOT_TREELET]=SCHEMA(L"TreeletReorder",U(0),U(1),U(2),U(3),U(4),U(5),C(0,5)),
    [ROOT_SRV_SPACE3]={NULL,1,(const D3D12_ROOT_PARAMETER1[]){
            {.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV,.Descriptor={0,3,0}}}},
};
#undef SCHEMA
#undef C
#undef U
#undef T

/* Slot 0 adds t8 before t7; both tables use the same static sampler. */
static D3D12_ROOT_SIGNATURE_DESC1 bink_group_layout(BOOL alpha)
{
#define RANGE(type,reg) {D3D12_DESCRIPTOR_RANGE_TYPE_##type,1,reg,1,D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND}
#define HEAD RANGE(SRV,2),RANGE(SRV,6),RANGE(SRV,5)
#define TAIL RANGE(SRV,7),RANGE(SRV,10),RANGE(CBV,0),RANGE(CBV,3)
    static const D3D12_DESCRIPTOR_RANGE1 opaque[]={HEAD,TAIL},with_alpha[]={HEAD,RANGE(SRV,8),TAIL};
#undef HEAD
#undef TAIL
#undef RANGE
    static const D3D12_ROOT_PARAMETER1 tables[]={
        {.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,.DescriptorTable={ARRAYSIZE(opaque),opaque}},
        {.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,.DescriptorTable={ARRAYSIZE(with_alpha),with_alpha}}};
    static const D3D12_STATIC_SAMPLER_DESC sampler={D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,0,1,D3D12_COMPARISON_FUNC_NEVER,
        D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,0,12,3,1,D3D12_SHADER_VISIBILITY_ALL};
    return (D3D12_ROOT_SIGNATURE_DESC1){1,&tables[!!alpha],1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
}
static D3D12_ROOT_SIGNATURE_DESC1 blit_colour_layout(void)
{
    static const D3D12_DESCRIPTOR_RANGE1 range={D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,27,1,
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
    static const D3D12_ROOT_PARAMETER1 table={.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
        .DescriptorTable={1,&range},.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL};
    return (D3D12_ROOT_SIGNATURE_DESC1){1,&table,0,NULL,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
}
/* The title sees only the console wrapper. */
struct bvh_root {
    IUnknown iface;
    LONG refs;
    struct xbox_device *owner;
    ID3D12RootSignature *host;
    enum bvh_root_kind kind;
    struct bvh_root *next;
};
static ULONG WINAPI root_addref(IUnknown *i)
{
    return InterlockedIncrement(&CONTAINING_RECORD(i,struct bvh_root,iface)->refs);
}
static ULONG WINAPI root_release(IUnknown *i)
{
    struct bvh_root *r=CONTAINING_RECORD(i,struct bvh_root,iface);
    ULONG refs=InterlockedDecrement(&r->refs);
    if (!refs) {
        AcquireSRWLockExclusive(&r->owner->descriptor_lock);
        struct bvh_root **link=&r->owner->bvh_roots;
        while (*link!=r) link=&(*link)->next;
        *link=r->next;
        ReleaseSRWLockExclusive(&r->owner->descriptor_lock);
        ID3D12RootSignature_Release(r->host);
        IUnknown_Release(&r->owner->iface);
        HeapFree(GetProcessHeap(),0,r);
    }
    return refs;
}
static HRESULT WINAPI root_query(IUnknown *i,REFIID iid,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!iid) return E_INVALIDARG;
    if (!IsEqualIID(iid,&IID_IUnknown) && !IsEqualIID(iid,&IID_ID3D12RootSignature)) return E_NOINTERFACE;
    root_addref(i); *out=i;
    return S_OK;
}
static HRESULT WINAPI root_name(IUnknown *i,const WCHAR *name)
{
    struct bvh_root *r=CONTAINING_RECORD(i,struct bvh_root,iface);
    const WCHAR *expected=r->kind<ARRAYSIZE(root_schemas)?root_schemas[r->kind].name:NULL;
    if (!expected) return E_NOTIMPL;
    WCHAR copy[32];
    SIZE_T bytes=(wcslen(expected)+1)*sizeof(WCHAR);
    if (bytes>sizeof copy) return E_NOTIMPL;
    if (!copy_in(copy,name,bytes)) return E_POINTER;
    if (memcmp(copy,expected,bytes)) return E_NOTIMPL;
    return ID3D12RootSignature_SetName(r->host,expected);
}
static const struct root_methods {
    IUnknownVtbl unknown;
    xs_stop_fn gap18[3];
    __typeof__(&root_name) name;
    xs_stop_fn get_device;
} root_methods={{root_query,root_addref,root_release},{xs_stop,xs_stop,xs_stop},root_name,xs_stop};
_Static_assert(offsetof(struct root_methods,name)==0x30,"root SetName slot");

#include "im96_root.h"
static HRESULT WINAPI bootstrap_root(IUnknown *device,UINT node,const void *blob,SIZE_T n,REFIID iid,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!iid || !blob || !n) return E_INVALIDARG;
    IID id;
    if (!copy_in(&id,iid,sizeof id)) return E_POINTER;
    if (!IsEqualIID(&id,&IID_ID3D12RootSignature)) return E_NOINTERFACE;
    /* Canonical serialized signatures. Tables and static samplers have a different wire size. */
    BYTE wire[1304];
    BOOL bink=n==308 || n==332;
    if (node!=1 || (n!=sizeof wire && !bink && n!=112 && (n<68+24 || n>68+9*24 || (n-68)%24))) return E_NOTIMPL;
    if (!copy_in(wire,blob,n)) return E_POINTER;
    ID3DBlob *serialized=NULL;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc={.Version=D3D_ROOT_SIGNATURE_VERSION_1_1};
    HRESULT hr;
    enum bvh_root_kind kind;
    const enum bvh_root_kind last=n==sizeof wire?ROOT_IM96:ROOT_BLIT_COLOUR;
    for (kind=n==sizeof wire?ROOT_IM96:0;kind<=last;++kind) {
        if(kind==ROOT_IM96){
            desc.Desc_1_1=im96_layout();
        }else if(kind==ROOT_BLIT_COLOUR){
            if(n!=112)continue;
            desc.Desc_1_1=blit_colour_layout();
        }else if(kind==ROOT_BINK_GROUP){
            if(!bink)continue;
            desc.Desc_1_1=bink_group_layout(n==332);
        }else{
            if (n==112 || bink || root_schemas[kind].count!=(n-68)/24) continue;
            desc.Desc_1_1=(D3D12_ROOT_SIGNATURE_DESC1){root_schemas[kind].count,root_schemas[kind].params,
                    0,NULL,kind==ROOT_SRV_SPACE3 ? D3D12_ROOT_SIGNATURE_FLAG_NONE : (D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
        }
        hr=D3D12SerializeVersionedRootSignature(&desc,&serialized,NULL);
        if (FAILED(hr)) return hr;
        BOOL match=n==ID3D10Blob_GetBufferSize(serialized) && !memcmp(wire,ID3D10Blob_GetBufferPointer(serialized),n);
        ID3D10Blob_Release(serialized);
        if (match) break;
    }
    if (kind>last) return E_NOTIMPL;
    if(kind<ROOT_BINK_GROUP)desc.Desc_1_1.Flags=D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr=D3D12SerializeVersionedRootSignature(&desc,&serialized,NULL);
    if (FAILED(hr)) return hr;
    struct xbox_device *d=CONTAINING_RECORD(device,struct xbox_device,iface);
    struct bvh_root *r=HeapAlloc(GetProcessHeap(),0,sizeof *r);
    if (r) {
        hr=ID3D12Device_CreateRootSignature(d->host,node,ID3D10Blob_GetBufferPointer(serialized),
                ID3D10Blob_GetBufferSize(serialized),&IID_ID3D12RootSignature,(void **)&r->host);
        if (SUCCEEDED(hr)) {
            IUnknown_AddRef(device); r->owner=d; r->refs=1;
            r->kind=kind;
            r->iface.lpVtbl=&root_methods.unknown;
            AcquireSRWLockExclusive(&d->descriptor_lock);
            r->next=d->bvh_roots; d->bvh_roots=r;
            ReleaseSRWLockExclusive(&d->descriptor_lock);
            *out=&r->iface;
        } else HeapFree(GetProcessHeap(),0,r);
    } else hr=E_OUTOFMEMORY;
    ID3D10Blob_Release(serialized);
    return hr;
}
