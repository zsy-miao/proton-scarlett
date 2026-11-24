/* The title sees only the console wrapper. */
struct bvh_root {
    IUnknown iface;
    LONG refs;
    struct xbox_device *owner;
    ID3D12RootSignature *host;
    BOOL morton;
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
    /* The supplied helper passes exactly this literal immediately after success. */
    struct bvh_root *r=CONTAINING_RECORD(i,struct bvh_root,iface);
    const WCHAR *expected=r->morton?L"XDXR MortonCodesCalculator":L"XDXR UtilityShaders";
    WCHAR copy[ARRAYSIZE(L"XDXR MortonCodesCalculator")];
    SIZE_T bytes=(wcslen(expected)+1)*sizeof(WCHAR);
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

/* Borrowed for a checked translator; caller holds a live wrapper through use. */
ID3D12RootSignature *xs_bvh_root_host(IUnknown *device,IUnknown *i)
{
    const IUnknownVtbl *v;
    if (!copy_in(&v,i,sizeof v) || v!=&root_methods.unknown) return NULL;
    struct bvh_root *r=CONTAINING_RECORD(i,struct bvh_root,iface);
    return &r->owner->iface==device ? r->host : NULL;
}

static HRESULT WINAPI bootstrap_root(IUnknown *device,UINT node,const void *blob,SIZE_T n,REFIID iid,void **out)
{
    if (!out) return E_POINTER;
    *out=NULL;
    if (!iid || !blob || !n) return E_INVALIDARG;
    if (!IsEqualIID(iid,&IID_ID3D12RootSignature)) return E_NOINTERFACE;
    BYTE wire[212];
    BOOL morton=n==sizeof wire;
    if (node!=1 || (!morton && n!=140)) return E_NOTIMPL;
    if (!copy_in(wire,blob,n)) return E_POINTER;
    D3D12_ROOT_PARAMETER1 p[6]={
        {.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV},
        {.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV},
        {.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0,16}},
    };
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc={.Version=D3D_ROOT_SIGNATURE_VERSION_1_1,
        .Desc_1_1={3,p,0,NULL,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000}};
    if (morton) {
        desc.Desc_1_1.NumParameters=6;
        p[0]=(D3D12_ROOT_PARAMETER1){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,.Constants={0,0,8}};
        for (UINT i=1;i<6;i++)
            p[i]=(D3D12_ROOT_PARAMETER1){.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV,.Descriptor={i-1,0,0}};
    }
    ID3DBlob *serialized=NULL;
    HRESULT hr=D3D12SerializeVersionedRootSignature(&desc,&serialized,NULL);
    if (FAILED(hr)) return hr;
    BOOL match=n==ID3D10Blob_GetBufferSize(serialized) && !memcmp(wire,ID3D10Blob_GetBufferPointer(serialized),n);
    ID3D10Blob_Release(serialized);
    if (!match) return E_NOTIMPL;
    desc.Desc_1_1.Flags=D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr=D3D12SerializeVersionedRootSignature(&desc,&serialized,NULL);
    if (FAILED(hr)) return hr;
    struct xbox_device *d=CONTAINING_RECORD(device,struct xbox_device,iface);
    struct bvh_root *r=HeapAlloc(GetProcessHeap(),0,sizeof *r);
    if (r) {
        hr=ID3D12Device_CreateRootSignature(d->host,node,ID3D10Blob_GetBufferPointer(serialized),
                ID3D10Blob_GetBufferSize(serialized),&IID_ID3D12RootSignature,(void **)&r->host);
        if (SUCCEEDED(hr)) {
            IUnknown_AddRef(device); r->owner=d; r->refs=1; r->morton=morton;
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
