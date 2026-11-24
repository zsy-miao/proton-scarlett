#include "test-clear-common.hpp"
static void draw(ID3D12Device *d,ID3D12RootSignature *r,ID3D12PipelineState *pso,int mode) {
    constexpr UINT W=16,H=8;
    auto resource=[&](D3D12_HEAP_TYPE t,D3D12_RESOURCE_STATES st) {
        D3D12_HEAP_PROPERTIES hp={};hp.Type=t;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=4096;
        rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(t==D3D12_HEAP_TYPE_DEFAULT){rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            rd.Width=W;rd.Height=H;rd.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;}
        ComPtr<ID3D12Resource> o; HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,st,nullptr,IID_PPV_ARGS(&o)));return o;
    };
    auto src=resource(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto rt=resource(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto rb=resource(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    BYTE *data;HR(src->Map(0,nullptr,(void**)&data)); memset(data,0,4096);
    // Use independent tables, gaps and nonzero root VA offset. Other two vertices
    // in each triangle carry unrelated colors, to expose last-vertex or smooth shading.
    data+=256;
    UINT posHeader=(128<<7)|(28<<3)|16|(mode==2?2:3);
    UINT colHeader=(512<<7)|(36<<3)|16|(mode==1?3:4);
    memcpy(data,&posHeader,4);memcpy(data+12,&colHeader,4);
    const float attrs[][4]={{.3000244f,.5f,.25f,1},{0,1,1,0},{0,0,1,0},
                            {1,.2176492f,.75f,0},{1,1,0,1},{1,0,0,1}};
    for (int i=0;i<6;++i) {
        float x=(i>=3?8.f:0.f)+(i%3==1?7.25f:1.f),y=i%3==2?7.25f:1.f;
        float pos[]={2*x/W-1,1-2*y/H,.5f};
        memcpy(data+128+28*i,pos,12);memcpy(data+512+36*i,attrs[i],16);
    }
    src->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};ComPtr<ID3D12DescriptorHeap>heap;
    HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));auto handle=heap->GetCPUDescriptorHandleForHeapStart();
    d->CreateRenderTargetView(rt.Get(),nullptr,handle);
    ComPtr<ID3D12CommandQueue>q;D3D12_COMMAND_QUEUE_DESC qd={};HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ComPtr<ID3D12CommandAllocator>a;HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    ComPtr<ID3D12GraphicsCommandList>l;HR(d->CreateCommandList(1,qd.Type,a.Get(),pso,IID_PPV_ARGS(&l)));
    D3D12_VIEWPORT vp={0,0,W,H,0,1};D3D12_RECT sc={0,0,W,H};l->RSSetViewports(1,&vp);l->RSSetScissorRects(1,&sc);
    const float guard[]={1,0,1,1};l->ClearRenderTargetView(handle,guard,0,nullptr);
    l->OMSetRenderTargets(1,&handle,FALSE,nullptr);l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    l->SetGraphicsRootSignature(r);l->SetGraphicsRootShaderResourceView(0,src->GetGPUVirtualAddress()+256);l->DrawInstanced(6,1,0,0);
    D3D12_RESOURCE_BARRIER b={};b.Transition={rt.Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION dst={},from={};from.pResource=rt.Get();dst.pResource=rb.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    auto desc=rt->GetDesc();d->GetCopyableFootprints(&desc,0,1,0,&dst.PlacedFootprint,nullptr,nullptr,nullptr);
    l->CopyTextureRegion(&dst,0,0,0,&from,nullptr);HR(l->Close());
    ID3D12CommandList*ls[]={l.Get()};q->ExecuteCommandLists(1,ls);ComPtr<ID3D12Fence>fence;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE evt=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(evt);HR(q->Signal(fence.Get(),1));HR(fence->SetEventOnCompletion(1,evt));
    CK(WaitForSingleObject(evt,10000)==WAIT_OBJECT_0 && fence->GetCompletedValue()==1);CloseHandle(evt);
    HR(rb->Map(0,nullptr,(void**)&data));unsigned colored=0;
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        unsigned local=x%8; bool covered=mode!=2 && local>=1 && y>=1 && local+y<=7;
        // Analytic triangle coverage at pixel centers. RTZ makes .3000244 and
        // .2176492 map to UNORM 76 and 55, whereas a direct/nearest export gives 77/56.
        BYTE expected[][4]={{255,0,255,255},{76,128,64,255},{255,55,191,0},{0,0,0,0}};
        const BYTE *want=expected[!covered?0:mode==1?3:x<8?1:2];
        const BYTE *got=data+dst.PlacedFootprint.Offset+y*dst.PlacedFootprint.Footprint.RowPitch+x*4;
        if(memcmp(got,want,4)){fprintf(stderr,"mode=%d (%u,%u) got %u,%u,%u,%u; expected %u,%u,%u,%u\n",
            mode,x,y,got[0],got[1],got[2],got[3],want[0],want[1],want[2],want[3]);exit(1);}
        colored+=covered;
    }
    rb->Unmap(0,nullptr);printf("PASS mode=%d: %u triangle pixels, %u guards after fence\n",mode,colored,W*H-colored);
}
int main(){
    auto d=device(),r=root(d.Get()),other=device(),foreign=root(other.Get());
    auto host=xs_bvh_root_host(d.Get(),r.Get());CK(host);ComPtr<ID3D12Device> hd;HR(host->GetDevice(IID_PPV_ARGS(&hd)));
    UINT_PTR q[0x2b0/8];BYTE vs[992],ps[964];load("stream.bin",q,sizeof q);load("vs.bin",vs,sizeof vs);load("ps.bin",ps,sizeof ps);
    q[3]=(UINT_PTR)vs;q[6]=(UINT_PTR)ps;q[1]=(UINT_PTR)foreign.Get();
    D3D12_PIPELINE_STATE_STREAM_DESC sd={sizeof q,q};
    using Create=HRESULT(WINAPI *)(IUnknown*,const D3D12_PIPELINE_STATE_STREAM_DESC*,REFIID,void**);
    auto create=(Create)(*(void***)(d.Get()))[0x2b0/8];
    auto bad=[&](HRESULT expected){void *out=r.Get();CK(create(d.Get(),&sd,IID_ID3D12PipelineState,&out)==expected && !out);};
    bad(E_INVALIDARG);q[1]=(UINT_PTR)r.Get();
    sd.SizeInBytes-=8;bad(E_NOTIMPL);sd.SizeInBytes+=8;q[0x280/8]^=1ull<<32;bad(E_NOTIMPL);q[0x280/8]^=1ull<<32;
    q[3]=1;bad(E_POINTER);q[3]=(UINT_PTR)vs;vs[900]^=1;bad(E_NOTIMPL);vs[900]^=1;ps[800]^=1;bad(E_NOTIMPL);ps[800]^=1;
    ComPtr<IUnknown> p;HR(create(d.Get(),&sd,IID_ID3D12PipelineState,(void**)p.GetAddressOf()));
    auto hp=clear_test_host(p.Get());CK(hp && (void*)hp!=(void*)p.Get());
    ComPtr<IUnknown> identity;HR(p->QueryInterface(IID_ID3D12PipelineState,(void**)identity.GetAddressOf()));CK(identity.Get()==p.Get());
    for(int mode=0;mode<3;++mode)draw(hd.Get(),host,hp,mode);
    puts("PASS +2b0 qualified creation, guest identity and rejection controls");
}
