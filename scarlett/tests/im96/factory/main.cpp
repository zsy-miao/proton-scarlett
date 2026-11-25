#include "test-clear-common.hpp"
#include "test-colour-root.hpp"
using Create=HRESULT(WINAPI*)(IUnknown*,const D3D12_PIPELINE_STATE_STREAM_DESC*,REFIID,void**);
extern "C" BOOL exercise(IUnknown*,ID3D12Device*,IUnknown*,IUnknown*,IUnknown*,HRESULT (*)(IUnknown**));
static Create create;
static IUnknown *owner;
static UINT_PTR q[86];
static HRESULT publish(IUnknown **p){
    D3D12_PIPELINE_STATE_STREAM_DESC sd={sizeof q,q};
    return create(owner,&sd,IID_ID3D12PipelineState,(void**)p);
}
int main(){
    setvbuf(stdout,nullptr,_IONBF,0);puts("F96 synthetic qualified factory fixture");
    auto d=device(),r=root(d.Get()),alias=colour_root(d.Get());
    owner=d.Get();create=(Create)(*(void***)owner)[0x2b0/8];
    auto host=xs_bvh_root_host(owner,r.Get());CK(host);
    ComPtr<ID3D12Device> hd;HR(host->GetDevice(IID_PPV_ARGS(&hd)));
    BYTE v[1712],p[1588];load("stream.bin",q,sizeof q);load("vs.bin",v,992);load("ps.bin",p,964);
    q[1]=(UINT_PTR)r.Get();q[3]=(UINT_PTR)v;q[6]=(UINT_PTR)p;q[85]=((UINT64)20<<32)|16;
    ComPtr<IUnknown> clear;HR(publish(clear.GetAddressOf()));CK(clear.Get());
    D3D12_INPUT_ELEMENT_DESC ia[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    load("fixed96.bin",q,sizeof q);load("vs96.bin",v,sizeof v);load("ps96.bin",p,sizeof p);
    q[1]=(UINT_PTR)alias.Get();q[3]=(UINT_PTR)v;q[6]=(UINT_PTR)p;q[77]=(UINT_PTR)ia;
    IUnknown *bad=r.Get();q[61]^=1;CK(publish(&bad)==E_NOTIMPL && !bad);q[61]^=1;
    q[1]=(UINT_PTR)r.Get();CK(publish(&bad)==E_INVALIDARG && !bad);q[1]=(UINT_PTR)alias.Get();
    CK(exercise(owner,hd.Get(),clear.Get(),r.Get(),alias.Get(),publish));return 0;
}
