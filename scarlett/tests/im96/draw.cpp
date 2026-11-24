// Standalone D3D12 control; device/queue setup adapted from ../im-transfer/draw.cpp.
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using Microsoft::WRL::ComPtr;
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define HR(x) do{HRESULT h_=(x);if(FAILED(h_)){fprintf(stderr,"FAIL %d: %s %08lx\n",__LINE__,#x,(unsigned long)h_);exit(1);}}while(0)
static std::vector<unsigned char> file(const char *name,size_t expected=0) {
    auto f=fopen(name,"rb"); CK(f && !fseek(f,0,SEEK_END)); auto n=ftell(f);
    CK(n>0 && n<=65536 && (!expected || size_t(n)==expected)); rewind(f);
    std::vector<unsigned char> b(n); CK(fread(b.data(),1,n,f)==size_t(n)); CK(!fclose(f)); return b;
}
#ifdef TEST_IM96_EXACT_ROOT
#include "test-exact.hpp"
#endif
#ifdef TEST_IM96_USED
#include "used-table.hpp"
#endif
int main() {
#ifdef TEST_IM96_USED
    auto guest=owner();auto root=exact_root(guest.Get());UsedTable used(guest.Get());ComPtr<ID3D12Device>d;HR(root->GetDevice(IID_PPV_ARGS(&d)));
#elif defined(TEST_IM96_EXACT_ROOT)
    auto root=exact_root();ComPtr<ID3D12Device> d;HR(root->GetDevice(IID_PPV_ARGS(&d)));
#else
    ComPtr<ID3D12Device> d; HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d)));
#endif
    const auto rgba=DXGI_FORMAT_R32G32B32A32_FLOAT;
    auto create=[&](D3D12_HEAP_TYPE heap,UINT64 bytes,D3D12_RESOURCE_STATES state){
        D3D12_HEAP_PROPERTIES h={}; h.Type=heap; h.CreationNodeMask=h.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC a={}; a.Dimension=bytes?D3D12_RESOURCE_DIMENSION_BUFFER:D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        a.Width=bytes?bytes:8; a.Height=bytes?1:8; a.DepthOrArraySize=a.MipLevels=a.SampleDesc.Count=1;
        a.Format=bytes?DXGI_FORMAT_UNKNOWN:rgba; a.Layout=bytes?D3D12_TEXTURE_LAYOUT_ROW_MAJOR:D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if(!bytes) a.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        ComPtr<ID3D12Resource> r; HR(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&a,state,nullptr,IID_PPV_ARGS(&r))); return r;
    };
    auto upload=[&](const char*name,UINT size){
        auto b=file(name,size); auto r=create(D3D12_HEAP_TYPE_UPLOAD,size,D3D12_RESOURCE_STATE_GENERIC_READ);
        void*p; HR(r->Map(0,nullptr,&p)); memcpy(p,b.data(),size); r->Unmap(0,nullptr); return r;
    };
    auto object=upload("object.bin",64),camera=upload("camera.bin",512),vb=upload("vertices.bin",96);
    auto heap=[&](D3D12_DESCRIPTOR_HEAP_TYPE t,UINT n,bool visible){
        D3D12_DESCRIPTOR_HEAP_DESC h={t,n,visible?D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE:D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};
        ComPtr<ID3D12DescriptorHeap> r; HR(d->CreateDescriptorHeap(&h,IID_PPV_ARGS(&r))); return r;
    };
    // The exact table uses slot 2 for the camera; place the successor outside it.
#ifdef TEST_IM96_EXACT_ROOT
    constexpr UINT successor=3;
#else
    constexpr UINT successor=2;
#endif
    auto srv=heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,successor+1,true),rtv=heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,4,false);
    auto slot=[&](ID3D12DescriptorHeap*h,UINT i){auto s=h->GetCPUDescriptorHandleForHeapStart();
        s.ptr+=d->GetDescriptorHandleIncrementSize(h->GetDesc().Type)*i; return s;};
    D3D12_SHADER_RESOURCE_VIEW_DESC obj={}; obj.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
    obj.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; obj.Buffer.NumElements=1; obj.Buffer.StructureByteStride=64;
    d->CreateShaderResourceView(object.Get(),&obj,slot(srv.Get(),0));
#ifdef TEST_IM96_EXACT_ROOT
    D3D12_CONSTANT_BUFFER_VIEW_DESC cb={camera->GetGPUVirtualAddress(),512};d->CreateConstantBufferView(&cb,slot(srv.Get(),2));
#else
    D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,7,1,0},{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,277,1,1}};
    D3D12_ROOT_PARAMETER params[3]={};
    for(UINT i=0;i<2;++i){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[i].DescriptorTable={1,&ranges[i]}; params[i].ShaderVisibility=i?D3D12_SHADER_VISIBILITY_PIXEL:D3D12_SHADER_VISIBILITY_VERTEX;}
    params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV; params[2].Descriptor={0,1}; params[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_STATIC_SAMPLER_DESC s={}; s.Filter=D3D12_FILTER_ANISOTROPIC; s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    s.MaxAnisotropy=4; s.ComparisonFunc=D3D12_COMPARISON_FUNC_NEVER; s.MaxLOD=12; s.ShaderRegister=14; s.RegisterSpace=1; s.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rd={3,params,1,&s,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ComPtr<ID3DBlob>blob; HR(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));
    ComPtr<ID3D12RootSignature>root; HR(d->CreateRootSignature(1,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
#endif
    auto vs=file("vs.dxil"),ps=file("ps.dxil");
    D3D12_INPUT_ELEMENT_DESC il[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd={}; pd.pRootSignature=root.Get(); pd.InputLayout={il,3}; pd.VS={vs.data(),vs.size()}; pd.PS={ps.data(),ps.size()};
    auto &rt=pd.BlendState.RenderTarget[0]; rt.SrcBlend=rt.SrcBlendAlpha=D3D12_BLEND_ONE; rt.DestBlend=rt.DestBlendAlpha=D3D12_BLEND_ZERO;
    rt.BlendOp=rt.BlendOpAlpha=D3D12_BLEND_OP_ADD; rt.LogicOp=D3D12_LOGIC_OP_NOOP; rt.RenderTargetWriteMask=15;
    pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID; pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE; pd.RasterizerState.DepthClipEnable=TRUE;
    pd.SampleMask=~0u; pd.SampleDesc.Count=pd.NumRenderTargets=1; pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; pd.RTVFormats[0]=rgba;
    ComPtr<ID3D12PipelineState>pso; HR(d->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&pso)));
    D3D12_COMMAND_QUEUE_DESC qd={}; ComPtr<ID3D12CommandQueue>q; HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ComPtr<ID3D12CommandAllocator>a; HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    ComPtr<ID3D12GraphicsCommandList>l; HR(d->CreateCommandList(0,qd.Type,a.Get(),pso.Get(),IID_PPV_ARGS(&l)));
    auto barrier=[&](ID3D12Resource*r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
        D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition={r,0,before,after}; l->ResourceBarrier(1,&b);};
    const char* names[]={"src17.bin","src18.bin","dst40.bin","dst41.bin"}; ComPtr<ID3D12Resource>image[4],rb[4];
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[4];
    for(UINT i=0;i<4;++i){
        image[i]=create(D3D12_HEAP_TYPE_DEFAULT,0,D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto r=slot(rtv.Get(),i); d->CreateRenderTargetView(image[i].Get(),nullptr,r);
        auto data=file(names[i],1024);
        for(LONG y=0;y<8;++y)for(LONG x=0;x<8;++x){float c[4]; memcpy(c,data.data()+(8*y+x)*16,16);
            D3D12_RECT rect={x,y,x+1,y+1}; l->ClearRenderTargetView(r,c,1,&rect);}
        if(i<2){barrier(image[i].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            d->CreateShaderResourceView(image[i].Get(),nullptr,slot(srv.Get(),i?successor:1));}
    }
    auto current=slot(rtv.Get(),3); l->OMSetRenderTargets(1,&current,FALSE,nullptr);
    D3D12_RECT sc={0,0,8,8}; D3D12_VIEWPORT vp={0,0,8,8,0,1}; l->RSSetScissorRects(1,&sc); l->RSSetViewports(1,&vp);
    l->SetGraphicsRootSignature(root.Get()); ID3D12DescriptorHeap*heaps[]={srv.Get()}; l->SetDescriptorHeaps(1,heaps);
#ifdef TEST_IM96_USED
    used.setup(d.Get(),l.Get());*(UINT64*)used.u.raw_slot^=1;CK(FAILED(used.consume(l.Get())));*(UINT64*)used.u.raw_slot^=1;
    HR(used.consume(l.Get()));
#elif defined(TEST_IM96_EXACT_ROOT)
    l->SetGraphicsRootDescriptorTable(0,srv->GetGPUDescriptorHandleForHeapStart());
#else
    for(UINT i=0;i<2;++i) l->SetGraphicsRootDescriptorTable(i,srv->GetGPUDescriptorHandleForHeapStart());
    l->SetGraphicsRootConstantBufferView(2,camera->GetGPUVirtualAddress());
#endif
    D3D12_VERTEX_BUFFER_VIEW view={vb->GetGPUVirtualAddress(),96,24}; l->IASetVertexBuffers(0,1,&view);
    l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP); l->DrawInstanced(4,1,0,0);
    for(UINT i=0;i<4;++i){
        auto desc=image[i]->GetDesc(); UINT64 bytes; d->GetCopyableFootprints(&desc,0,1,0,&fp[i],nullptr,nullptr,&bytes);
        rb[i]=create(D3D12_HEAP_TYPE_READBACK,bytes,D3D12_RESOURCE_STATE_COPY_DEST);
        barrier(image[i].Get(),i<2?D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION from={},to={}; from.pResource=image[i].Get(); from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.pResource=rb[i].Get(); to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint=fp[i]; l->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    }
#ifdef TEST_IM96_USED
    used.copy_checks(l.Get());HR(l->Close());
    ComPtr<ID3D12Fence> gate;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));HR(q->Wait(gate.Get(),1));
    *(UINT64*)used.u.cb_slot^=16;CK(FAILED(used.submit(q.Get(),l.Get())));*(UINT64*)used.u.cb_slot^=16;HR(used.submit(q.Get(),l.Get()));
#else
    HR(l->Close()); ID3D12CommandList*lists[]={l.Get()}; q->ExecuteCommandLists(1,lists);
#endif
    ComPtr<ID3D12Fence>f; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f))); HR(q->Signal(f.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(event); HR(f->SetEventOnCompletion(1,event));
#ifdef TEST_IM96_USED
    CK(WaitForSingleObject(event,0)==WAIT_TIMEOUT);used.replace();HR(gate->Signal(1));
#endif
    CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0); CloseHandle(event); HR(d->GetDeviceRemovedReason()); CK(f->GetCompletedValue()==1);
    for(UINT i=0;i<4;++i){auto gold=file(i==3?"expected.bin":names[i],1024);
        BYTE*p; HR(rb[i]->Map(0,nullptr,(void**)&p)); for(UINT y=0;y<8;++y)for(UINT x=0;x<8;++x){
#ifdef TEST_IM96_USED
            if(i==3 && x>=2 && x<6 && y>=2 && y<6){
                float u=(x-2+.5f)/4,v=(y-2+.5f)/4;const float c[]={u,0,1-u,1-v};memcpy(gold.data()+(y*8+x)*16,c,16);
            }
#endif
            if(memcmp(p+fp[i].Offset+y*fp[i].Footprint.RowPitch+x*16,gold.data()+(y*8+x)*16,16)){
                fprintf(stderr,"FAIL %s at (%u,%u)\n",names[i],x,y); return 1;}}
        rb[i]->Unmap(0,nullptr);}
    puts("PASS im96 src17 -> dst41: 16 shaded, 48 border, src17/src18/dst40 intact");
#ifdef TEST_IM96_USED
    used.verify();
#endif
}
