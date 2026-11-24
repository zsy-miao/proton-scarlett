#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include "../../native/im_ui_sdr_bind.hpp"
using Microsoft::WRL::ComPtr;
#define CK(x) do { if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);exit(1);} } while(0)
#define HR(x) do { HRESULT h_=(x);if(FAILED(h_)){fprintf(stderr,"FAIL %d %s %08lx\n",__LINE__,#x,(unsigned long)h_);exit(1);} } while(0)
static std::vector<unsigned char> load(const char *name,size_t expect=0){
    FILE *f=fopen(name,"rb");CK(f);CK(!fseek(f,0,SEEK_END));long n=ftell(f);CK(n>0&&n<=65536&&(!expect||expect==size_t(n)));rewind(f);
    std::vector<unsigned char> b(n);CK(fread(b.data(),1,n,f)==size_t(n));CK(!fclose(f));return b;
}
static void test(ID3D12Device *d,int mode) {
    const UINT w=10,h=8;
    const auto rgba=DXGI_FORMAT_R32G32B32A32_FLOAT;
    auto resource=[&](D3D12_HEAP_TYPE heap, bool buf, UINT64 width,UINT height,UINT16 layers,DXGI_FORMAT fmt,D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE){
        D3D12_HEAP_PROPERTIES hp={};hp.Type=heap;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC desc={};desc.Dimension=buf?D3D12_RESOURCE_DIMENSION_BUFFER:D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=width;desc.Height=height;desc.DepthOrArraySize=layers;desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=fmt;desc.Flags=flags;
        desc.Layout=buf?D3D12_TEXTURE_LAYOUT_ROW_MAJOR:D3D12_TEXTURE_LAYOUT_UNKNOWN;
        ComPtr<ID3D12Resource> r;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&r)));return r;
    };
    auto upload=[&](size_t n){return resource(D3D12_HEAP_TYPE_UPLOAD,true,n,1,1,DXGI_FORMAT_UNKNOWN,D3D12_RESOURCE_STATE_GENERIC_READ);};
    auto write=[](ID3D12Resource*r,const void*p,size_t n){void*m;HR(r->Map(0,nullptr,&m));memcpy(m,p,n);r->Unmap(0,nullptr);};
    auto heap=[&](D3D12_DESCRIPTOR_HEAP_TYPE t,UINT n,bool visible){D3D12_DESCRIPTOR_HEAP_DESC hd={t,n,visible?D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE:D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};
        ComPtr<ID3D12DescriptorHeap> result;HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&result)));return result;};
    D3D12_ROOT_PARAMETER table={};table.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;table.DescriptorTable={3,im_ui_sdr::ranges};table.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    auto ss=im_ui_sdr::sampler();
    D3D12_ROOT_SIGNATURE_DESC rd={1,&table,1,&ss,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ComPtr<ID3DBlob> blob;HR(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));ComPtr<ID3D12RootSignature> root;
    HR(d->CreateRootSignature(1,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    auto vs=load("vs.dxil"),ps=load("ps.dxil");
    D3D12_INPUT_ELEMENT_DESC il[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd={};pd.pRootSignature=root.Get();pd.InputLayout={il,3};pd.VS={vs.data(),vs.size()};pd.PS={ps.data(),ps.size()};
    auto &rt=pd.BlendState.RenderTarget[0];rt.SrcBlend=rt.SrcBlendAlpha=D3D12_BLEND_ONE;rt.DestBlend=rt.DestBlendAlpha=D3D12_BLEND_ZERO;rt.BlendOp=rt.BlendOpAlpha=D3D12_BLEND_OP_ADD;rt.LogicOp=D3D12_LOGIC_OP_NOOP;rt.RenderTargetWriteMask=15;
    pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;
    pd.SampleMask=~0u;pd.SampleDesc.Count=pd.NumRenderTargets=1;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.RTVFormats[0]=rgba;
    ComPtr<ID3D12PipelineState> pso;HR(d->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&pso)));
    D3D12_COMMAND_QUEUE_DESC qd={};ComPtr<ID3D12CommandQueue> queue;HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> alloc;HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> l;HR(d->CreateCommandList(0,qd.Type,alloc.Get(),pso.Get(),IID_PPV_ARGS(&l)));
    auto barrier=[&](ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER t={};t.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;t.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&t);};
    auto sub=[](ID3D12Resource*r,UINT i){D3D12_TEXTURE_COPY_LOCATION t={};t.pResource=r;t.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;t.SubresourceIndex=i;return t;};
    auto placed=[](ID3D12Resource*r,D3D12_PLACED_SUBRESOURCE_FOOTPRINT f){D3D12_TEXTURE_COPY_LOCATION t={};t.pResource=r;t.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;t.PlacedFootprint=f;return t;};
    char file[32];snprintf(file,sizeof file,"cb%d.bin",mode);auto cbdata=load(file,512);auto cb=upload(512);write(cb.Get(),cbdata.data(),512);
    auto srv=heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,4,true),catalog=heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,8,false);
    const UINT inc=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto slot=[&](UINT index){auto at=catalog->GetCPUDescriptorHandleForHeapStart();at.ptr+=index*inc;return at;};
    ComPtr<ID3D12Resource> tex[2],up[2];auto sourceRTV=heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,false);
    for(UINT i=0;i<2;++i){
        UINT width=i?w:4,height=i?h:3,layers=i?32:1,bpp=i?4:16;auto fmt=i?DXGI_FORMAT_R32_FLOAT:rgba;
        auto image=load(i?"noise.bin":"ui.bin",width*height*layers*bpp);
        tex[i]=resource(D3D12_HEAP_TYPE_DEFAULT,false,width,height,layers,fmt,i?D3D12_RESOURCE_STATE_COPY_DEST:D3D12_RESOURCE_STATE_RENDER_TARGET,i?D3D12_RESOURCE_FLAG_NONE:D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        if(!i){
            auto at=sourceRTV->GetCPUDescriptorHandleForHeapStart();d->CreateRenderTargetView(tex[i].Get(),nullptr,at);
            for(LONG y=0;y<3;++y)for(LONG x=0;x<4;++x){D3D12_RECT rect={x,y,x+1,y+1};float color[4];memcpy(color,image.data()+(4*y+x)*16,16);l->ClearRenderTargetView(at,color,1,&rect);}
            barrier(tex[i].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }else{
        auto desc=tex[i]->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT f[32];UINT64 n;
        d->GetCopyableFootprints(&desc,0,layers,0,f,nullptr,nullptr,&n);up[i]=upload(n);BYTE*p;HR(up[i]->Map(0,nullptr,(void**)&p));
        for(UINT z=0;z<layers;++z){for(UINT y=0;y<height;++y)memcpy(p+f[z].Offset+y*f[z].Footprint.RowPitch,image.data()+(z*height+y)*width*bpp,width*bpp);
            auto dst=sub(tex[i].Get(),z),src=placed(up[i].Get(),f[z]);l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}
        up[i]->Unmap(0,nullptr);barrier(tex[i].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        d->CreateShaderResourceView(tex[i].Get(),nullptr,slot(i?2:7));
    }
    for(UINT j=0;j<2;++j){D3D12_CONSTANT_BUFFER_VIEW_DESC c={cb->GetGPUVirtualAddress()+256*j,256};d->CreateConstantBufferView(&c,slot(j?0:4));}
    auto start=srv->GetCPUDescriptorHandleForHeapStart();im_ui_sdr::Views views={slot(7),slot(2),slot(4),slot(0)};
    CK(im_ui_sdr::populate(d,start,3,views)==E_INVALIDARG);HR(im_ui_sdr::populate(d,start,4,views));
    auto target=resource(D3D12_HEAP_TYPE_DEFAULT,false,w,h,1,rgba,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    auto rh=heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,false);auto rtv=rh->GetCPUDescriptorHandleForHeapStart();d->CreateRenderTargetView(target.Get(),nullptr,rtv);
    const float sentinel[]={-7,-5,-3,-1};l->ClearRenderTargetView(rtv,sentinel,0,nullptr);l->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
    D3D12_VIEWPORT vp={1,1,8,6,0,1};D3D12_RECT sc={1,1,9,7};l->RSSetViewports(1,&vp);l->RSSetScissorRects(1,&sc);
    struct Vertex{float x,y,z;UINT c;float u,v;};static_assert(sizeof(Vertex)==24);
    Vertex data[]={{-1,1,0,0x00010203,1,0},{3,1,0,0x20304050,-1,0},{-1,-3,0,0x50607080,1,2}};
    auto vb=upload(sizeof data);write(vb.Get(),data,sizeof data);D3D12_VERTEX_BUFFER_VIEW view={vb->GetGPUVirtualAddress(),sizeof data,sizeof(Vertex)};
    l->SetGraphicsRootSignature(root.Get());ID3D12DescriptorHeap *heaps[]={srv.Get()};l->SetDescriptorHeaps(1,heaps);l->SetGraphicsRootDescriptorTable(0,srv->GetGPUDescriptorHandleForHeapStart());
    l->IASetVertexBuffers(0,1,&view);l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);l->DrawInstanced(3,1,0,0);
    // Check destination and source on the same completed list.
    ComPtr<ID3D12Resource> rb[2];D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[2];
    for(UINT i=0;i<2;++i){auto*r=i?tex[0].Get():target.Get();auto desc=r->GetDesc();UINT64 n;
        barrier(r,i?D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
        d->GetCopyableFootprints(&desc,0,1,0,&fp[i],nullptr,nullptr,&n);rb[i]=resource(D3D12_HEAP_TYPE_READBACK,true,n,1,1,DXGI_FORMAT_UNKNOWN,D3D12_RESOURCE_STATE_COPY_DEST);
        auto dst=placed(rb[i].Get(),fp[i]),src=sub(r,0);l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}
    HR(l->Close());ID3D12CommandList*lists[]={l.Get()};queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HR(queue->Signal(fence.Get(),1));
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev);HR(fence->SetEventOnCompletion(1,ev));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);CloseHandle(ev);HR(d->GetDeviceRemovedReason());CK(fence->GetCompletedValue()==1);
    for(UINT i=0;i<2;++i){snprintf(file,sizeof file,"gold%d.bin",mode);UINT width=i?4:w,height=i?3:h;auto gold=load(i?"ui.bin":file,width*height*16);
        BYTE*p;HR(rb[i]->Map(0,nullptr,(void**)&p));for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x)for(UINT c=0;c<4;++c){
            float got,want;memcpy(&got,p+fp[i].Offset+y*fp[i].Footprint.RowPitch+x*16+c*4,4);memcpy(&want,gold.data()+((y*width+x)*4+c)*4,4);
            // Adjacent half allowed for transcendental / reciprocal implementation error only.
            bool exact=i||x==0||y==0||x==w-1||y==h-1;
            float step=ldexpf(1.f,std::max(-24,ilogbf(fmaxf(fabsf(want),0x1p-24f))-10));
            float quantum=ldexpf(1.f,std::max(-24,ilogbf(fmaxf(fabsf(got),0x1p-24f))-10));
            if(!(exact?got==want:std::isfinite(got)&&fabsf(got-want)<=step&&got/quantum==truncf(got/quantum))){
                fprintf(stderr,"case=%d source=%u xy=%u,%u lane=%u want %.9g got %.9g\n",mode,i,x,y,c,want,got);exit(1);}
        }rb[i]->Unmap(0,nullptr);}
    printf("PASS im transfer mode5 control %d: all 80 target and 12 source pixels\n",mode);
}
int main(){ComPtr<ID3D12Device>d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d)));for(int i=0;i<4;++i)test(d.Get(),i);}
