/* Fixture only: fail the next host PSO creation after the factory acquired its root. */
#define COBJMACROS
#define CONST_VTABLE
#define INITGUID
#include <windows.h>
#include <d3d12.h>
static BOOL fail_next;
static ID3D12RootSignature *held;
static ULONG held_count;
static HRESULT create_pso(ID3D12Device2 *device,const D3D12_PIPELINE_STATE_STREAM_DESC *sd,REFIID iid,void **out)
{
    if(!fail_next)return ID3D12Device2_CreatePipelineState(device,sd,iid,out);
    fail_next=FALSE;
    held=(ID3D12RootSignature*)((const UINT_PTR*)sd->pPipelineStateSubobjectStream)[1];
    held_count=ID3D12RootSignature_AddRef(held);
    *out=NULL;
    return E_OUTOFMEMORY;
}
#undef ID3D12Device2_CreatePipelineState
#define ID3D12Device2_CreatePipelineState create_pso
#include "d3d12_device.c"

void test_fail_next_pso(void){fail_next=TRUE;}
BOOL test_failure_released(ID3D12RootSignature *guest_host,BOOL exact)
{
    if(!held)return FALSE;
    BOOL same=held==guest_host;
    ULONG remaining=ID3D12RootSignature_Release(held);
    held=NULL;
    return same==exact && held_count>=2 && remaining==held_count-2;
}
HRESULT test_canonical_root(IUnknown *d,IUnknown **out)
{
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rs={.Version=D3D_ROOT_SIGNATURE_VERSION_1_1,.Desc_1_1=im96_layout()};
    ID3DBlob *b=NULL;
    HRESULT hr=D3D12SerializeVersionedRootSignature(&rs,&b,NULL);
    if(FAILED(hr))return hr;
    hr=bootstrap_root(d,1,ID3D10Blob_GetBufferPointer(b),ID3D10Blob_GetBufferSize(b),&IID_ID3D12RootSignature,(void**)out);
    ID3D10Blob_Release(b);
    return hr;
}
