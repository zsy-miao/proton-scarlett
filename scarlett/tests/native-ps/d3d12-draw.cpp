#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <cstring>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT get_test_device(IUnknown**,ID3D12Device**);
#define CK(x) do { if(!(x)) { fprintf(stderr,"FAIL %d %s\n",__LINE__,#x); exit(1); } } while(0)
#define HR(x) do { HRESULT hr=(x); if(FAILED(hr)){fprintf(stderr,"line %d: %s: %08lx\n",__LINE__,#x,(unsigned long)hr); exit(1);} } while(0)
static std::vector<char> load(const std::string &file){
    FILE *f=fopen(file.c_str(),"rb"); CK(f); fseek(f,0,SEEK_END); long n=ftell(f); CK(n>0&&n<65536); rewind(f);
    std::vector<char> b(n); CK(fread(b.data(),1,n,f)==(size_t)n); fclose(f); return b;
}
static void draw(ID3D12Device2 *d,const std::string &name,bool sampled){
    auto vs=load(name+".vs.dxil"),ps=load(name+".ps.dxil");
    UINT width=sampled?16:256, height=16; DXGI_FORMAT fmt=DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto heap=[&](D3D12_DESCRIPTOR_HEAP_TYPE ty,UINT n,bool visible){
        D3D12_DESCRIPTOR_HEAP_DESC desc={ty,n,visible?D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE:D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};
        ComPtr<ID3D12DescriptorHeap> h; HR(d->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&h))); return h;
    };
    auto resource=[&](D3D12_HEAP_TYPE heap,D3D12_RESOURCE_DIMENSION dim,UINT64 w,UINT h,DXGI_FORMAT format,D3D12_RESOURCE_FLAGS flags,D3D12_RESOURCE_STATES state){
        D3D12_HEAP_PROPERTIES hp={}; hp.Type=heap; hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC r={}; r.Dimension=dim; r.Width=w; r.Height=h; r.DepthOrArraySize=r.MipLevels=r.SampleDesc.Count=1;
        r.Format=format; r.Layout=dim==D3D12_RESOURCE_DIMENSION_BUFFER?D3D12_TEXTURE_LAYOUT_ROW_MAJOR:D3D12_TEXTURE_LAYOUT_UNKNOWN; r.Flags=flags;
        ComPtr<ID3D12Resource> obj; HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&r,state,nullptr,IID_PPV_ARGS(&obj)));return obj;
    };
    D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,72,1,0},{D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,2,3,1,0}};
    D3D12_ROOT_PARAMETER params[2]={}; for(int i=0;i<2;i++){ params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable={1,&ranges[i]};params[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL; }
    D3D12_ROOT_SIGNATURE_DESC rd={};rd.NumParameters=sampled?2:0; rd.pParameters=params;
    ComPtr<ID3DBlob> blob; HR(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));
    ComPtr<ID3D12RootSignature> root; HR(d->CreateRootSignature(1,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    // Observed x64 stream order, private host root and translated DXIL in place of guest pointers.
    alignas(8) unsigned char stream[0x2b0]={};
    auto put=[&](unsigned off,auto value){ CK(off+sizeof(value)<=sizeof stream); memcpy(stream+off,&value,sizeof value); };
    const UINT offsets[]={0,0x10,0x28,0x40,0x58,0x70,0x88,0x1d8,0x1e0,0x210,0x250,0x260,0x278,0x280,0x2a8};
    const UINT tags[]={0,1,2,3,4,5,8,9,10,21,17,12,14,15,16};
    for(UINT i=0;i<ARRAYSIZE(tags);i++)put(offsets[i],tags[i]);
    put(8,root.Get()); put(0x18,D3D12_SHADER_BYTECODE{vs.data(),vs.size()}); put(0x30,D3D12_SHADER_BYTECODE{ps.data(),ps.size()});
    D3D12_BLEND_DESC blend={}; auto &rt=blend.RenderTarget[0];rt.SrcBlend=rt.SrcBlendAlpha=D3D12_BLEND_ONE;rt.DestBlend=rt.DestBlendAlpha=D3D12_BLEND_ZERO;
    rt.BlendOp=rt.BlendOpAlpha=D3D12_BLEND_OP_ADD;rt.LogicOp=D3D12_LOGIC_OP_NOOP;rt.RenderTargetWriteMask=15;
    put(0x8c,blend);put(0x1dc,~0u);
    D3D12_RASTERIZER_DESC raster={};raster.FillMode=D3D12_FILL_MODE_SOLID;raster.CullMode=D3D12_CULL_MODE_NONE;raster.DepthClipEnable=1;put(0x1e4,raster);
    put(0x254,DXGI_SAMPLE_DESC{1,0});put(0x27c,D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);put(0x284,fmt);put(0x2a4,1u);
    D3D12_PIPELINE_STATE_STREAM_DESC sd={sizeof stream,stream};
    ComPtr<ID3D12PipelineState> pso; HR(d->CreatePipelineState(&sd,IID_PPV_ARGS(&pso)));
    D3D12_COMMAND_QUEUE_DESC qd={};ComPtr<ID3D12CommandQueue> q; HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ComPtr<ID3D12CommandAllocator> a; HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    ComPtr<ID3D12GraphicsCommandList> l; HR(d->CreateCommandList(0,qd.Type,a.Get(),pso.Get(),IID_PPV_ARGS(&l)));
    auto barrier=[&](ID3D12Resource*r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
        D3D12_RESOURCE_BARRIER b={};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,0,before,after};l->ResourceBarrier(1,&b); };
    auto footprint=[&](ID3D12Resource*r,UINT64 &size){ D3D12_PLACED_SUBRESOURCE_FOOTPRINT f={};auto desc=r->GetDesc();d->GetCopyableFootprints(&desc,0,1,0,&f,nullptr,nullptr,&size);return f;};
    auto loc=[](ID3D12Resource*r){D3D12_TEXTURE_COPY_LOCATION x={};x.pResource=r;x.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;return x;};
    auto placed=[&](ID3D12Resource*r,D3D12_PLACED_SUBRESOURCE_FOOTPRINT f){auto x=loc(r);x.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;x.PlacedFootprint=f;return x;};
    l->SetGraphicsRootSignature(root.Get());
    ComPtr<ID3D12Resource> tex[2],up[2]; ComPtr<ID3D12DescriptorHeap> srvs,samplers;
    if(sampled){
        srvs=heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,true);samplers=heap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,2,true);
        auto sh=srvs->GetCPUDescriptorHandleForHeapStart(),mh=samplers->GetCPUDescriptorHandleForHeapStart();
        for(int k=0;k<2;k++){
            tex[k]=resource(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_DIMENSION_TEXTURE2D,4,3,DXGI_FORMAT_R32G32B32A32_FLOAT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
            UINT64 n;auto f=footprint(tex[k].Get(),n);up[k]=resource(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_DIMENSION_BUFFER,n,1,DXGI_FORMAT_UNKNOWN,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_GENERIC_READ);
            char *p; HR(up[k]->Map(0,nullptr,(void**)&p));for(int y=0;y<3;y++)for(int x=0;x<4;x++){
                float v[]={(x+4*y)/16.f,(3-x+k)/8.f,(2*y+k)/8.f,((x^y)+k)/8.f};memcpy(p+y*f.Footprint.RowPitch+x*16,v,16);}
            up[k]->Unmap(0,nullptr); auto dst=loc(tex[k].Get()),src=placed(up[k].Get(),f);l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);barrier(tex[k].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            d->CreateShaderResourceView(tex[k].Get(),nullptr,sh);sh.ptr+=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            D3D12_SAMPLER_DESC ss={};ss.Filter=k?D3D12_FILTER_MIN_MAG_MIP_POINT:D3D12_FILTER_MIN_MAG_MIP_LINEAR;ss.AddressU=ss.AddressV=ss.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;ss.MaxAnisotropy=1;ss.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;d->CreateSampler(&ss,mh);mh.ptr+=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        }
        ID3D12DescriptorHeap *heaps[]={srvs.Get(),samplers.Get()};l->SetDescriptorHeaps(2,heaps);
        for(int i=0;i<2;i++)l->SetGraphicsRootDescriptorTable(i,heaps[i]->GetGPUDescriptorHandleForHeapStart());
    }
    auto target=resource(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_DIMENSION_TEXTURE2D,width,height,fmt,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto rh=heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,false);auto handle=rh->GetCPUDescriptorHandleForHeapStart();d->CreateRenderTargetView(target.Get(),nullptr,handle);
    float clear[]={1,0,1,1};l->ClearRenderTargetView(handle,clear,0,nullptr);l->OMSetRenderTargets(1,&handle,false,nullptr);
    D3D12_VIEWPORT vp={0,0,float(width),float(height),0,1};D3D12_RECT sc={1,1,LONG(width-1),LONG(height-1)};l->RSSetViewports(1,&vp);l->RSSetScissorRects(1,&sc);
    l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);l->DrawInstanced(3,1,0,0);barrier(target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    UINT64 n;auto f=footprint(target.Get(),n);auto rb=resource(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_DIMENSION_BUFFER,n,1,DXGI_FORMAT_UNKNOWN,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    auto dst=placed(rb.Get(),f),src=loc(target.Get());l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);HR(l->Close());
    ID3D12CommandList *ls[]={l.Get()};q->ExecuteCommandLists(1,ls);
    ComPtr<ID3D12Fence> fence;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HR(q->Signal(fence.Get(),1));HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev);HR(fence->SetEventOnCompletion(1,ev));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0&&fence->GetCompletedValue()==1);CloseHandle(ev);
    char tmp[MAX_PATH]; auto len=GetTempPathA(sizeof tmp,tmp); CK(len&&len<sizeof tmp);
    FILE *fp=fopen((std::string(tmp)+"native-draw-"+name+".pixels").c_str(),"wb"); CK(fp);
    char*p; HR(rb->Map(0,nullptr,(void**)&p));
    for(UINT y=0;y<height;y++) CK(fwrite(p+f.Offset+y*f.Footprint.RowPitch,8,width,fp)==width);
    CK(!fclose(fp)); rb->Unmap(0,nullptr);
    printf("READBACK %s %ux%u rowPitch=%u fence=1\n",name.c_str(),width,height,f.Footprint.RowPitch);
}
int main(){
    ComPtr<IUnknown> owner;ID3D12Device *host;HR(get_test_device(&owner,&host));ComPtr<ID3D12Device2>d;HR(host->QueryInterface(IID_PPV_ARGS(&d)));
    draw(d.Get(),"pair0",false); draw(d.Get(),"sample0",true);
    if(GetFileAttributesA("pair1.vs.dxil")!=INVALID_FILE_ATTRIBUTES){
        draw(d.Get(),"pair1",false); draw(d.Get(),"sample1",true);
    }
}
