#include <cstdio>
#include <cstdlib>
#include <d3d12.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12RootSignature *xs_bvh_root_host(IUnknown *,IUnknown *);
#define CK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))
int main() {
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev,root;
    HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)dev.GetAddressOf()));
    D3D12_ROOT_PARAMETER1 p={};p.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p.Descriptor.RegisterSpace=3;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rs={};rs.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
    rs.Desc_1_1={1,&p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> wire;HR(D3D12SerializeVersionedRootSignature(&rs,&wire,nullptr));
    const void *capture=wire->GetBufferPointer();CK(wire->GetBufferSize()==92);
    using Create=HRESULT(WINAPI *)(IUnknown*,UINT,const void*,SIZE_T,REFIID,void**);
    Create create=(Create)(*(void***)(dev.Get()))[16];
    void* bad=dev.Get();
    CK(create(dev.Get(),0,capture,92,IID_ID3D12RootSignature,&bad)==E_NOTIMPL&&!bad);
    CK(create(dev.Get(),1,capture,91,IID_ID3D12RootSignature,&bad)==E_NOTIMPL&&!bad);
    p.Descriptor.RegisterSpace=2;ComPtr<ID3DBlob> other;HR(D3D12SerializeVersionedRootSignature(&rs,&other,nullptr));
    CK(create(dev.Get(),1,other->GetBufferPointer(),other->GetBufferSize(),IID_ID3D12RootSignature,&bad)==E_NOTIMPL&&!bad);
    HR(create(dev.Get(),1,capture,92,IID_ID3D12RootSignature,(void**)root.GetAddressOf()));
    auto host=xs_bvh_root_host(dev.Get(),root.Get());CK(host);ComPtr<ID3D12Device>d;HR(host->GetDevice(IID_PPV_ARGS(&d)));
    BYTE shaders[2][8192];D3D12_SHADER_BYTECODE code[2];
    for (int i=0;i<2;i++) {
        FILE *f=fopen(i?"ps.dxil":"vs.dxil","rb");CK(f);auto n=fread(shaders[i],1,sizeof shaders[i],f);CK(n&&feof(f));fclose(f);
        code[i]={shaders[i],n};
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd={};pd.pRootSignature=host;pd.VS=code[0];pd.PS=code[1];pd.SampleMask=~0u;
    pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=DXGI_FORMAT_R32_UINT;pd.SampleDesc.Count=1;
    ComPtr<ID3D12PipelineState>pipeline;HR(d->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&pipeline)));
    auto resource=[&](D3D12_HEAP_TYPE t,D3D12_RESOURCE_STATES st,ID3D12Resource**o){
        D3D12_HEAP_PROPERTIES hp={};hp.Type=t;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=256;
        rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(t==D3D12_HEAP_TYPE_DEFAULT){rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Format=DXGI_FORMAT_R32_UINT;rd.Width=4;
            rd.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;}
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,st,nullptr,IID_PPV_ARGS(o));
    };
    ComPtr<ID3D12Resource> src,rt,rb;HR(resource(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,&src));
    HR(resource(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RENDER_TARGET,&rt));HR(resource(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,&rb));
    const UINT vals[]={19,37,0xfedcba98,0x7abc1234};void* m;HR(src->Map(0,nullptr,&m));memcpy(m,vals,sizeof vals);src->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};ComPtr<ID3D12DescriptorHeap>heap;
    HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));auto h=heap->GetCPUDescriptorHandleForHeapStart();d->CreateRenderTargetView(rt.Get(),nullptr,h);
    ComPtr<ID3D12CommandQueue>q;D3D12_COMMAND_QUEUE_DESC qd={};HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ComPtr<ID3D12CommandAllocator>a;HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    ComPtr<ID3D12GraphicsCommandList>l;HR(d->CreateCommandList(1,qd.Type,a.Get(),pipeline.Get(),IID_PPV_ARGS(&l)));
    D3D12_VIEWPORT vp={0,0,4,1,0,1};D3D12_RECT sc={0,0,4,1};l->RSSetViewports(1,&vp);l->RSSetScissorRects(1,&sc);
    l->OMSetRenderTargets(1,&h,FALSE,nullptr);l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    l->SetGraphicsRootSignature(host);l->SetGraphicsRootShaderResourceView(0,src->GetGPUVirtualAddress());l->DrawInstanced(3,1,0,0);
    D3D12_RESOURCE_BARRIER b={};b.Transition={rt.Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION to={},from={};from.pResource=rt.Get();to.pResource=rb.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint.Footprint={DXGI_FORMAT_R32_UINT,4,1,1,256};l->CopyTextureRegion(&to,0,0,0,&from,nullptr);HR(l->Close());
    ID3D12CommandList*ls[]={l.Get()};q->ExecuteCommandLists(1,ls);ComPtr<ID3D12Fence>fence;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE evt=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(evt);HR(q->Signal(fence.Get(),1));HR(fence->SetEventOnCompletion(1,evt));CK(WaitForSingleObject(evt,10000)==WAIT_OBJECT_0);CloseHandle(evt);
    HR(rb->Map(0,nullptr,&m));CK(!memcmp(m,vals,sizeof vals));rb->Unmap(0,nullptr);
    puts("PASS node/size/space negatives; t0 space3 fenced draw returned four exact R32_UINT pixels");return 0;
}
