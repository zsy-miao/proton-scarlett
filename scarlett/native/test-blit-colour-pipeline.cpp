#include <initializer_list>
#include "test-clear-common.hpp"

extern "C" ID3D12PipelineState *xs_pin_clear4(IUnknown*,ID3D12Device*,IUnknown*,IUnknown**);
static ComPtr<IUnknown> colour_root(IUnknown *d){
    D3D12_DESCRIPTOR_RANGE1 range={D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,27,1,D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE,UINT_MAX};
    D3D12_ROOT_PARAMETER1 p={};p.DescriptorTable={1,&range};
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc={};desc.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1={1,&p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ComPtr<ID3DBlob> blob;HR(D3D12SerializeVersionedRootSignature(&desc,&blob,nullptr));
    using Make=HRESULT(WINAPI*)(IUnknown*,UINT,const void*,SIZE_T,REFIID,void**);
    ComPtr<IUnknown> r;HR(((Make)(*(void***)d)[16])(d,1,blob->GetBufferPointer(),blob->GetBufferSize(),IID_ID3D12RootSignature,(void**)r.GetAddressOf()));return r;
}
int main(){
    auto d=device(),other=device(),r=colour_root(d.Get()),foreign=colour_root(other.Get()),wrong=root(d.Get());
    UINT_PTR q[86];BYTE vs[1100],ps[1180];load("stream.bin",q,sizeof q);load("VS.bin",vs,sizeof vs);load("PS.bin",ps,sizeof ps);
    D3D12_INPUT_ELEMENT_DESC ia[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    q[1]=(SIZE_T)r.Get();q[3]=(SIZE_T)vs;q[6]=(SIZE_T)ps;q[0x268/8]=(SIZE_T)ia;
    D3D12_PIPELINE_STATE_STREAM_DESC sd={sizeof q,q};
    using Make=HRESULT(WINAPI*)(IUnknown*,const D3D12_PIPELINE_STATE_STREAM_DESC*,REFIID,void**);
    auto make=(Make)(*(void***)d.Get())[0x2b0/8];
    auto bad=[&](HRESULT want){void *out=r.Get();CK(make(d.Get(),&sd,IID_ID3D12PipelineState,&out)==want && !out);};
    ia[1].Format=DXGI_FORMAT_B8G8R8A8_UNORM;bad(E_NOTIMPL);ia[1].Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    ia[1].AlignedByteOffset++;bad(E_NOTIMPL);ia[1].AlignedByteOffset--;
    ia[2].SemanticName="POSITION";bad(E_NOTIMPL);ia[2].SemanticName="TEXCOORD";
    vs[100]^=1;bad(E_NOTIMPL);vs[100]^=1;ps[600]^=1;bad(E_NOTIMPL);ps[600]^=1;
    for(UINT off:{0x278u,0x280u,0x2a8u,0x210u}){q[off/8]^=1ull<<32;bad(E_NOTIMPL);q[off/8]^=1ull<<32;}
    q[1]=(SIZE_T)wrong.Get();bad(E_INVALIDARG);q[1]=(SIZE_T)foreign.Get();bad(E_INVALIDARG);q[1]=(SIZE_T)r.Get();
    ComPtr<IUnknown> p;HR(make(d.Get(),&sd,IID_ID3D12PipelineState,(void**)p.GetAddressOf()));
    auto host=clear_test_host(p.Get());ComPtr<ID3D12Device> hd;CK(host);HR(host->GetDevice(IID_PPV_ARGS(&hd)));
    IUnknown *unused=nullptr;CK(!xs_pin_clear4(d.Get(),hd.Get(),p.Get(),&unused));
    auto resource=[&](D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES st,DXGI_FORMAT fmt,D3D12_RESOURCE_FLAGS flags){
        D3D12_HEAP_PROPERTIES hp={};hp.Type=type;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={};rd.Width=fmt==DXGI_FORMAT_UNKNOWN?1024:8;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;
        rd.Dimension=fmt==DXGI_FORMAT_UNKNOWN?D3D12_RESOURCE_DIMENSION_BUFFER:D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Layout=fmt==DXGI_FORMAT_UNKNOWN?D3D12_TEXTURE_LAYOUT_ROW_MAJOR:D3D12_TEXTURE_LAYOUT_UNKNOWN;rd.Format=fmt;rd.Flags=flags;
        ComPtr<ID3D12Resource> v;HR(hd->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,st,nullptr,IID_PPV_ARGS(&v)));return v;};
    auto heap=[&](D3D12_DESCRIPTOR_HEAP_TYPE type,UINT n,D3D12_DESCRIPTOR_HEAP_FLAGS flags){D3D12_DESCRIPTOR_HEAP_DESC desc={type,n,flags,1};ComPtr<ID3D12DescriptorHeap> h;HR(hd->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&h)));return h;};
    auto up=resource(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,DXGI_FORMAT_UNKNOWN,D3D12_RESOURCE_FLAG_NONE);
    auto rb=resource(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,DXGI_FORMAT_UNKNOWN,D3D12_RESOURCE_FLAG_NONE);
    auto rt=resource(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RENDER_TARGET,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    auto ds=resource(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_DEPTH_WRITE,DXGI_FORMAT_D32_FLOAT_S8X24_UINT,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    auto rtH=heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE),dsH=heap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE),
        table=heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
    auto hc=rtH->GetCPUDescriptorHandleForHeapStart(),dc=dsH->GetCPUDescriptorHandleForHeapStart();hd->CreateRenderTargetView(rt.Get(),nullptr,hc);hd->CreateDepthStencilView(ds.Get(),nullptr,dc);
    const UINT stride=hd->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    BYTE *m;HR(up->Map(0,nullptr,(void**)&m));memset(m,0xcc,1024);
    struct Vertex {float x,y,z;DWORD rgba;float u,v;};static_assert(sizeof(Vertex)==24);
    for(UINT j=0;j<2;j++){
        float scale[4]={1,1,1,1};if(!j){float thresholds[]={30.501f,75.501f,125.501f,190.501f};for(UINT k=0;k<4;k++)scale[k]=thresholds[k]/255.f;}
        memcpy(m+j*256+32,scale,16);DWORD c=j?0xdd893511:0xffffffff;
        Vertex v[]={{-1,-1,0,c,0,1},{3,-1,0,c,2,1},{-1,3,0,c,0,-1}};memcpy(m+512+j*sizeof v,v,sizeof v);
        D3D12_CONSTANT_BUFFER_VIEW_DESC cb={up->GetGPUVirtualAddress()+j*256,256};auto h=table->GetCPUDescriptorHandleForHeapStart();h.ptr+=j*stride;hd->CreateConstantBufferView(&cb,h);
    }up->Unmap(0,nullptr);
    ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;ComPtr<ID3D12CommandQueue>queue;D3D12_COMMAND_QUEUE_DESC qd={};
    HR(hd->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));HR(hd->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(hd->CreateCommandList(0,qd.Type,a.Get(),host,IID_PPV_ARGS(&l)));
    D3D12_VIEWPORT viewport={0,0,8,1,0,1};l->RSSetViewports(1,&viewport);l->OMSetRenderTargets(1,&hc,FALSE,&dc);const float black[]={0,0,0,0};l->ClearRenderTargetView(hc,black,0,nullptr);
    l->SetGraphicsRootSignature(xs_bvh_root_host(d.Get(),r.Get()));auto bank=table.Get();l->SetDescriptorHeaps(1,&bank);
    D3D12_VERTEX_BUFFER_VIEW vb={up->GetGPUVirtualAddress()+512,144,24};l->IASetVertexBuffers(0,1,&vb);l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for(UINT j=0;j<2;j++){D3D12_RECT sc={(LONG)(j*3+1),0,(LONG)(j*3+4),1};l->RSSetScissorRects(1,&sc);auto g=table->GetGPUDescriptorHandleForHeapStart();g.ptr+=j*stride;l->SetGraphicsRootDescriptorTable(0,g);l->DrawInstanced(3,1,3*j,0);}
    D3D12_RESOURCE_BARRIER tr={};tr.Transition={rt.Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&tr);
    D3D12_TEXTURE_COPY_LOCATION from={},to={};from.pResource=rt.Get();to.pResource=rb.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,8,1,1,256};l->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    HR(l->Close());ID3D12CommandList *lists[]={l.Get()};queue->ExecuteCommandLists(1,lists);ComPtr<ID3D12Fence> f;HR(hd->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));HR(queue->Signal(f.Get(),1));HR(f->SetEventOnCompletion(1,nullptr));HR(hd->GetDeviceRemovedReason());
    HR(rb->Map(0,nullptr,(void**)&m));for(UINT k=0;k<8;k++){DWORD pixel;memcpy(&pixel,m+k*4,4);DWORD want=k==0||k==7?0:k<4?0xbe7d4b1e:0xdd893511;
        printf("RG pixel %u = %08lx want %08lx\n",k,pixel,want);CK(pixel==want);}rb->Unmap(0,nullptr);
    puts("PASS slot42 factory: exact shaders, fetch and state; owner and clear4 rejection, RTZ threshold, RGBA swizzle and c2 through returned host PSO");return 0;
}
