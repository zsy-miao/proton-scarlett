#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "blit_colour_rect_dxil.h"
#include "rect-test-stages.h"
using Microsoft::WRL::ComPtr;
#define CK(x) do { if(!(x)) { fprintf(stderr,"FAIL %d %s\n",__LINE__,#x); exit(1); } } while(0)
#define HR(x) do { HRESULT hr=(x); if(FAILED(hr)){fprintf(stderr,"line %d: %s: %08lx\n",__LINE__,#x,(unsigned long)hr); exit(1);} } while(0)
// Offscreen host geometry control, adapted from tests/native-ps/d3d12-draw.cpp.
static void draw(ID3D12Device *d,UINT draws,bool rect,bool colour,bool gradient=false,bool swapped=false){
    UINT width=6, height=6; DXGI_FORMAT fmt=DXGI_FORMAT_R8G8B8A8_UNORM;
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
    D3D12_ROOT_SIGNATURE_DESC rd={};rd.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    D3D12_DESCRIPTOR_RANGE range={D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,27,1,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
    D3D12_ROOT_PARAMETER param={};param.DescriptorTable={1,&range};rd.NumParameters=1;rd.pParameters=&param;
    ComPtr<ID3DBlob> blob; HR(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));
    ComPtr<ID3D12RootSignature> root; HR(d->CreateRootSignature(1,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    D3D12_INPUT_ELEMENT_DESC il[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd={};pd.InputLayout={il,3};pd.pRootSignature=root.Get();
    pd.VS={vs,sizeof vs};pd.PS=colour?D3D12_SHADER_BYTECODE{ps,sizeof ps}:D3D12_SHADER_BYTECODE{probe,sizeof probe};
    if(rect)pd.GS={blit_colour_rect_gs,sizeof blit_colour_rect_gs};
    auto &rt=pd.BlendState.RenderTarget[0];rt.BlendEnable=!colour;rt.SrcBlend=rt.SrcBlendAlpha=rt.DestBlend=rt.DestBlendAlpha=D3D12_BLEND_ONE;
    rt.BlendOp=rt.BlendOpAlpha=D3D12_BLEND_OP_ADD;rt.LogicOp=D3D12_LOGIC_OP_NOOP;rt.RenderTargetWriteMask=15;
    auto &rs=pd.RasterizerState;rs.FillMode=D3D12_FILL_MODE_SOLID;rs.CullMode=D3D12_CULL_MODE_BACK;rs.FrontCounterClockwise=TRUE;rs.DepthClipEnable=TRUE;
    pd.SampleMask=~0u;pd.SampleDesc.Count=1;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.RTVFormats[0]=fmt;pd.NumRenderTargets=1;
    ComPtr<ID3D12PipelineState> pso; HR(d->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&pso)));
    D3D12_COMMAND_QUEUE_DESC qd={};ComPtr<ID3D12CommandQueue> q; HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ComPtr<ID3D12CommandAllocator> alloc; HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> l; HR(d->CreateCommandList(0,qd.Type,alloc.Get(),pso.Get(),IID_PPV_ARGS(&l)));
    auto barrier=[&](ID3D12Resource*r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
        D3D12_RESOURCE_BARRIER b={};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,0,before,after};l->ResourceBarrier(1,&b); };
    auto footprint=[&](ID3D12Resource*r,UINT64 &size){ D3D12_PLACED_SUBRESOURCE_FOOTPRINT f={};auto desc=r->GetDesc();d->GetCopyableFootprints(&desc,0,1,0,&f,nullptr,nullptr,&size);return f;};
    auto loc=[](ID3D12Resource*r){D3D12_TEXTURE_COPY_LOCATION x={};x.pResource=r;x.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;return x;};
    auto placed=[&](ID3D12Resource*r,D3D12_PLACED_SUBRESOURCE_FOOTPRINT f){auto x=loc(r);x.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;x.PlacedFootprint=f;return x;};
    l->SetGraphicsRootSignature(root.Get());
    struct V{float x,y,z;UINT color;float u,v;};static_assert(sizeof(V)==24);
    V a={-1,1,0,~0u,0,0},b={-1,-1,0,~0u,0,1},c={1,-1,0,~0u,1,1};
    if(gradient){a.color=0xffffff40;b.color=0xffffff00;c.color=0xffffff80;}
    // Zero-area poison before firstVertex=1; exactly three records remain after it.
    V input[]={{0,0,0,0,0,0},a,b,c};
    if(swapped){input[1]=b;input[2]=c;input[3]=a;} // same winding, wrong elbow
    auto upload=resource(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_DIMENSION_BUFFER,512,1,DXGI_FORMAT_UNKNOWN,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto vb=resource(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_DIMENSION_BUFFER,sizeof input,1,DXGI_FORMAT_UNKNOWN,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    void *map;HR(upload->Map(0,nullptr,&map));memset(map,0,512);memcpy(map,input,sizeof input);
    float scale[]={30.501f/255,75.501f/255,125.501f/255,190.501f/255};
    memcpy((BYTE*)map+256+32,scale,sizeof scale);upload->Unmap(0,nullptr);
    l->CopyBufferRegion(vb.Get(),0,upload.Get(),0,sizeof input);
    barrier(vb.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    auto table=heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,true);
    D3D12_CONSTANT_BUFFER_VIEW_DESC cb={upload->GetGPUVirtualAddress()+256,256};
    d->CreateConstantBufferView(&cb,table->GetCPUDescriptorHandleForHeapStart());
    auto bank=table.Get();l->SetDescriptorHeaps(1,&bank);l->SetGraphicsRootDescriptorTable(0,table->GetGPUDescriptorHandleForHeapStart());
    D3D12_VERTEX_BUFFER_VIEW view={vb->GetGPUVirtualAddress(),sizeof input,sizeof(V)};l->IASetVertexBuffers(0,1,&view);
    auto target=resource(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_DIMENSION_TEXTURE2D,width,height,fmt,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto rh=heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,false);auto handle=rh->GetCPUDescriptorHandleForHeapStart();d->CreateRenderTargetView(target.Get(),nullptr,handle);
    float clear[]={0,0,0,0};l->ClearRenderTargetView(handle,clear,0,nullptr);l->OMSetRenderTargets(1,&handle,false,nullptr);
    D3D12_VIEWPORT vp={1,1,4,4,0,1};D3D12_RECT sc={0,0,6,6};l->RSSetViewports(1,&vp);l->RSSetScissorRects(1,&sc);
    l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);for(UINT i=0;i<draws;i++)l->DrawInstanced(3,1,1,0);
    barrier(target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    UINT64 n;auto f=footprint(target.Get(),n);f.Offset=512;
    UINT64 tail=f.Offset+height*f.Footprint.RowPitch,len=tail+16;CK(f.Footprint.RowPitch>=width*4 && n<=tail-f.Offset);
    auto rb=resource(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_DIMENSION_BUFFER,len,1,DXGI_FORMAT_UNKNOWN,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    HR(rb->Map(0,nullptr,&map));memset(map,0xa5,len);rb->Unmap(0,nullptr);
    auto dst=placed(rb.Get(),f),src=loc(target.Get());l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);HR(l->Close());
    ID3D12CommandList *ls[]={l.Get()};q->ExecuteCommandLists(1,ls);
    ComPtr<ID3D12Fence> fence;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HR(q->Signal(fence.Get(),1));HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev);HR(fence->SetEventOnCompletion(1,ev));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0&&fence->GetCompletedValue()==1);CloseHandle(ev);
    HR(d->GetDeviceRemovedReason());BYTE*p; HR(rb->Map(0,nullptr,(void**)&p));
    for(UINT i=0;i<f.Offset;i++) CK(p[i]==0xa5);
    for(UINT64 i=tail;i<len;i++) CK(p[i]==0xa5);
    UINT count=0;
    for(UINT y=0;y<height;y++)for(UINT x=0;x<width;x++){
        bool covered=!swapped&&x>=1&&x<=4&&y>=1&&y<=4&&(rect || x<y);
        UINT h=covered?draws:0;
        BYTE want[]={BYTE(h*(2*x-1)),BYTE(h*(2*y-1)),BYTE(!h?0:gradient?64+16*(2*x-1)-8*(2*y-1):255),BYTE(h)};
        if(colour && covered){const BYTE modulated[]={30,75,125,190};memcpy(want,modulated,4);}
        const BYTE *got=p+f.Offset+y*f.Footprint.RowPitch+x*4;
        if(memcmp(got,want,4))fprintf(stderr,"(%u,%u) got %u %u %u %u; want %u %u %u %u\n",x,y,got[0],got[1],got[2],got[3],want[0],want[1],want[2],want[3]);
        CK(!memcmp(got,want,4));count+=h;
    }
    rb->Unmap(0,nullptr);printf("PASS %s %s GS=%d draws=%u covered-visits=%u; guarded 6x6\n",colour?"b27/c2":"UV",swapped?"wrong-order":gradient?"gradient":"white",rect,draws,count);
}
int main(){
    ComPtr<ID3D12Device> d; HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d)));
    draw(d.Get(),1,false,false);draw(d.Get(),1,true,false);draw(d.Get(),2,true,false);
    draw(d.Get(),1,true,false,true);draw(d.Get(),1,true,true);
    draw(d.Get(),1,true,false,false,true);
}
