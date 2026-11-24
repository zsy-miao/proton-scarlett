/* Private compute recorder. No guest COM slot or desktop descriptor accessor. */
#include "shader_digest.h"
#include "xmem/placed_table.h"

struct xs_placed_dispatch {
    IUnknown *identity; /* also pins xmem.dll for table release */
    struct XsPlacedTable *table;
    __typeof__(&ScarlettReleasePlacedTableV1) drop;
    ID3D12RootSignature *root;
    ID3D12PipelineState *ps;
    ID3D12Resource *report;
};
void xs_drop_placed_dispatch(struct xs_placed_dispatch *s)
{
    if (!s) return;
    if (s->table) s->drop(s->table);
    if (s->ps) ID3D12PipelineState_Release(s->ps);
    if (s->root) ID3D12RootSignature_Release(s->root);
    if (s->report) ID3D12Resource_Release(s->report);
    IUnknown_Release(s->identity);
    HeapFree(GetProcessHeap(),0,s);
}

static BOOL placed_shader_digest(const BYTE *code,DWORD n)
{
    /* logical.hlsl, dxc -T cs_6_0 -E main -Qstrip_debug. Exact bytecode only. */
    static const BYTE approved[32]={0x82,0xe0,0x90,0x74,0x81,0xdf,0xbe,0x24,0xfa,0xd4,0x7e,0xe8,0x80,0xb4,0xd3,0x70,
        0xe0,0x20,0x12,0xfe,0x9d,0xab,0xd2,0x50,0x18,0x2f,0x1c,0x19,0xa3,0x51,0x23,0xb4};
    return shader_digest_matches(code,n,approved);
}

static IUnknown *placed_slot_identity(struct desc_heap *h,SIZE_T slot,UINT kind)
{
    D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    if (FAILED(descriptor_slot(&h->iface,slot,NULL,&gpu))) return NULL;
    const struct descriptor_record *r=h->records+(slot-h->start)/h->stride;
    UINT64 tag[2];
    return r->placed && r->kind==kind && r->serial && copy_in(tag,(void *)slot,sizeof tag) &&
        tag[0]==r->serial && tag[1]==(0x5853444553430000ull|kind) ? r->placed : NULL;
}

/* Both list and report are trusted host objects. Require COMMON/promotable arena
 * buffers and an independently owned report in UAV state. Drop hold after fence. */
HRESULT xs_record_placed_dispatch(IUnknown *device,IUnknown *heap,SIZE_T srv,SIZE_T uav,
        const void *shader,SIZE_T bytes,ID3D12GraphicsCommandList *list,ID3D12Resource *report,
        struct xs_placed_dispatch **hold)
{
    if (!hold) return E_POINTER;
    *hold=NULL;
    if (!heap || heap->lpVtbl!=&desc_vtbl.unknown || !device || !list || !report) return E_INVALIDARG;
    struct desc_heap *h=CONTAINING_RECORD(heap,struct desc_heap,iface);
    if (!h->owner || &h->owner->iface!=device || !h->visible || !h->records ||
        ID3D12GraphicsCommandList_GetType(list)!=D3D12_COMMAND_LIST_TYPE_COMPUTE) return E_INVALIDARG;
    struct xbox_device *d=h->owner;
    AcquireSRWLockShared(&d->descriptor_lock);
    IUnknown *id=placed_slot_identity(h,srv,SRV);
    if (id && placed_slot_identity(h,uav,UAV)==id) IUnknown_AddRef(id); else id=NULL;
    ReleaseSRWLockShared(&d->descriptor_lock);
    if (!id) return E_INVALIDARG;
    struct xs_placed_dispatch *s=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *s);
    if (!s) { IUnknown_Release(id); return E_OUTOFMEMORY; }
    s->identity=id;
    struct placed_backing *p=CONTAINING_RECORD(id,struct placed_identity,iface)->private;
    __typeof__(&ScarlettCreatePlacedTableV1) create=(void *)GetProcAddress(p->module,"ScarlettCreatePlacedTableV1");
    __typeof__(&ScarlettBindPlacedTableV1) bind=(void *)GetProcAddress(p->module,"ScarlettBindPlacedTableV1");
    s->drop=(void *)GetProcAddress(p->module,"ScarlettReleasePlacedTableV1");
    ID3D12Device *ld=NULL,*rd=NULL;
    HRESULT hr=E_INVALIDARG;
    D3D12_RESOURCE_DESC desc; report->lpVtbl->GetDesc(report,&desc);
    D3D12_GPU_VIRTUAL_ADDRESS va=ID3D12Resource_GetGPUVirtualAddress(report);
    if (FAILED(ID3D12GraphicsCommandList_GetDevice(list,&IID_ID3D12Device,(void **)&ld)) || ld!=d->host ||
        FAILED(ID3D12Resource_GetDevice(report,&IID_ID3D12Device,(void **)&rd)) || rd!=d->host ||
        desc.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || desc.Width!=PLACED_WIDTH+24 ||
        !(desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) || !va) goto done;
    hr=E_NOTIMPL;
    BYTE code[5820];
    if (!create || !bind || !s->drop || bytes!=sizeof code) goto done;
    if (!copy_in(code,shader,sizeof code)) { hr=E_POINTER; goto done; }
    if (!placed_shader_digest(code,sizeof code)) goto done;
    D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,1}};
    D3D12_ROOT_PARAMETER rp[3]={{0}};
    rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[0].DescriptorTable=(D3D12_ROOT_DESCRIPTOR_TABLE){2,ranges};
    rp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rp[1].Constants=(D3D12_ROOT_CONSTANTS){0,0,4};
    rp[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV; rp[2].Descriptor.ShaderRegister=2;
    D3D12_ROOT_SIGNATURE_DESC rs={3,rp,0,NULL,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob *blob=NULL;
    hr=D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,NULL);
    if (FAILED(hr)) goto done;
    hr=ID3D12Device_CreateRootSignature(d->host,0,blob->lpVtbl->GetBufferPointer(blob),
        blob->lpVtbl->GetBufferSize(blob),&IID_ID3D12RootSignature,(void **)&s->root);
    blob->lpVtbl->Release(blob);
    if (FAILED(hr)) goto done;
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={.pRootSignature=s->root,.CS={code,sizeof code}};
    hr=ID3D12Device_CreateComputePipelineState(d->host,&pd,&IID_ID3D12PipelineState,(void **)&s->ps);
    if (FAILED(hr)) goto done;
    hr=create(p->capture,&s->table);
    if (FAILED(hr)) goto done;
    ID3D12GraphicsCommandList_SetPipelineState(list,s->ps);
    ID3D12GraphicsCommandList_SetComputeRootSignature(list,s->root);
    hr=bind(s->table,list);
    if (FAILED(hr)) goto done;
    ID3D12Resource_AddRef(report); s->report=report;
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list,2,va);
    const UINT params[]={0,27u<<20,74u<<20,174u<<20};
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(list,1,4,params,0);
    ID3D12GraphicsCommandList_Dispatch(list,512,(PLACED_WIDTH/4)/(512*64),1);
    D3D12_RESOURCE_BARRIER sync={.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV};
    ID3D12GraphicsCommandList_ResourceBarrier(list,1,&sync);
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(list,1,1,0);
    ID3D12GraphicsCommandList_Dispatch(list,1,1,1);
    *hold=s;
done:
    if (rd) ID3D12Device_Release(rd);
    if (ld) ID3D12Device_Release(ld);
    if (FAILED(hr)) xs_drop_placed_dispatch(s);
    return hr;
}
