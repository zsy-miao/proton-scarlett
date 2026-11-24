#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" HRESULT xs_write_view(IUnknown*,UINT,IUnknown*,const void*,SIZE_T);
extern "C" HRESULT xs_resolve_view(IUnknown*,SIZE_T,UINT,ID3D12DescriptorHeap**,D3D12_GPU_DESCRIPTOR_HANDLE*);
extern "C" HRESULT xs_resolve_depth_dsv(IUnknown*,SIZE_T,D3D12_CPU_DESCRIPTOR_HANDLE*,ID3D12Resource**);

#define CK(v) do{if(!(v)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#v);return 1;}}while(0)
#define HR(e) do{HRESULT hr=(e);if(FAILED(hr)){fprintf(stderr,"FAIL %d %s = %lx\n",__LINE__,#e,hr);return 1;}}while(0)
#include "xmem/marked_section.h"
int main(){

    constexpr UINT pool=0x89700000,guard=65536,width=2560,height=1440;
    auto xm=LoadLibraryW(L"xmem.dll");CK(xm);
    auto alloc=(void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD))(void*)GetProcAddress(xm,"XMemVirtualAlloc");
    auto take=(HRESULT(WINAPI*)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*))(void*)GetProcAddress(xm,"ScarlettAcquireNativePoolV1");
    CK(alloc&&take);auto base=(BYTE*)alloc(nullptr,pool,0x20003000,0x283,0x2404);CK(base);

    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)dev.GetAddressOf()));
    ComPtr<ID3D12Device>d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    UINT actual=0;ComPtr<ID3D12Resource> backing;HRESULT pin=take(d.Get(),base,1,nullptr,backing.GetAddressOf(),&actual);
    const bool native=pin==S_OK;const UINT off=native?0x14140000:0x141d0000,span=native?0x1300000:0x1e00000;
    BYTE *alias=nullptr;if(native){CK(actual==0 && backing->GetDesc().Width==pool);}
    else {CK(pin==S_FALSE);scarlett_section_info held={},mapped={};HR(scarlett_query_section(0x53430005,base+off,span,&held));CK(held.offset==off && held.view_size==pool);
        HR(scarlett_query_section_handle(0x53430004,nullptr,pool,&mapped,held.section));CloseHandle(held.section);alias=(BYTE*)mapped.base;}
    memset(base+off-guard,0xa7,span+2*guard);MemoryBarrier();
    DWORD wire[]={3,0,0,0,width,0,height,0x10001,19,1,0,0,0x4800002};D3D12_CLEAR_VALUE cv={};cv.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;cv.DepthStencil.Stencil=7;
    using Place=HRESULT(WINAPI*)(IUnknown*,UINT64,const void*,UINT,const void*,REFIID,void**);
    auto place=(Place)((*(void***)dev.Get())[0x428/8]);void *guest=nullptr;
    auto create=[&](UINT pos,UINT state=6){return place(dev.Get(),(UINT_PTR)base+pos,wire,state,&cv,IID_ID3D12Resource,&guest);};
    CK(FAILED(create(off,3))&&!guest);for(UINT j=0;j<13;j++)if(j!=1){wire[j]^=1;CK(FAILED(create(off))&&!guest);wire[j]^=1;}
    CK(FAILED(create(off+guard))&&!guest);cv.DepthStencil.Stencil=0;CK(FAILED(create(off))&&!guest);cv.DepthStencil.Stencil=7;
    const DWORD ms[]={3,0,0,0,1,0,1,0x10001,28,4,0,0,1};D3D12_CLEAR_VALUE white={DXGI_FORMAT_R8G8B8A8_UNORM,{1,1,1,1}};ComPtr<IUnknown> msaa;
    auto overlap=[&](){return place(dev.Get(),(UINT_PTR)base+(off&~0x3fffffu),ms,6,&white,IID_ID3D12Resource,(void**)msaa.GetAddressOf());};
    HR(overlap());CK(create(off)==DXGI_ERROR_INVALID_CALL && !guest);msaa.Reset();
    HR(create(off));CK(guest);ComPtr<IUnknown> obj;obj.Attach((IUnknown*)guest);
    using Heap=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**);using Start=SIZE_T(WINAPI*)(IUnknown*);
    using Writer=void(WINAPI*)(IUnknown*,IUnknown*,const void*,SIZE_T);
    ComPtr<IUnknown> dsvHeap,srvHeap;auto vt=*(void***)dev.Get();
    UINT r[]={3,2048,0,1},sh[]={0,4,1,1};
    HR(((Heap)vt[0x70/8])(dev.Get(),r,IID_ID3D12DescriptorHeap,(void**)dsvHeap.GetAddressOf()));
    HR(((Heap)vt[0x70/8])(dev.Get(),sh,IID_ID3D12DescriptorHeap,(void**)srvHeap.GetAddressOf()));
    SIZE_T rt=((Start)((*(void***)dsvHeap.Get())[0x48/8]))(dsvHeap.Get())+32;
    SIZE_T ss=((Start)((*(void***)srvHeap.Get())[0x48/8]))(srvHeap.Get())+32;
    DWORD rw[]={20,3,0,0,0,0},sw[]={21,4,0x1688,0,0,1,0,0};
    ((Writer)vt[0xa8/8])(dev.Get(),obj.Get(),rw,rt);((Writer)vt[0x90/8])(dev.Get(),obj.Get(),sw,ss);
    UINT64 stamp[2];memcpy(stamp,(void*)ss,16);
    for(auto &x:sw){x^=1;CK(FAILED(xs_write_view(dev.Get(),0x90,obj.Get(),sw,ss)));x^=1;}
    CK(!memcmp(stamp,(void*)ss,16));memcpy(stamp,(void*)rt,16);
    for(UINT i=0;i<6;i++){DWORD mask=i==2?4:1;rw[i]^=mask;CK(FAILED(xs_write_view(dev.Get(),0xa8,obj.Get(),rw,rt)));rw[i]^=mask;}
    CK(!memcmp(stamp,(void*)rt,16));
    ID3D12DescriptorHeap *bank;D3D12_GPU_DESCRIPTOR_HANDLE gpu;HR(xs_resolve_view(srvHeap.Get(),ss,0x90,&bank,&gpu));
    ComPtr<ID3D12Resource> image;D3D12_CPU_DESCRIPTOR_HANDLE h;HR(xs_resolve_depth_dsv(dsvHeap.Get(),rt,&h,image.GetAddressOf()));
    sw[0]=DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;sw[6]=0;CK(FAILED(xs_write_view(dev.Get(),0x90,obj.Get(),sw,ss)));
    sw[6]=1;((Writer)vt[0x90/8])(dev.Get(),obj.Get(),sw,ss+32);
    ID3D12DescriptorHeap *sbank;D3D12_GPU_DESCRIPTOR_HANDLE sg;HR(xs_resolve_view(srvHeap.Get(),ss+32,0x90,&sbank,&sg));
    CK(sbank==bank && sg.ptr==gpu.ptr+d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
    D3D12_CPU_DESCRIPTOR_HANDLE ro[4]={};
    for(UINT flags:{3u,1u,2u}){
        rw[2]=flags;SIZE_T slot=rt+32*flags;
        ((Writer)vt[0xa8/8])(dev.Get(),obj.Get(),rw,slot);
        ComPtr<ID3D12Resource> resolved;HR(xs_resolve_depth_dsv(dsvHeap.Get(),slot,&ro[flags],&resolved));
        CK(resolved.Get()==image.Get() && ro[flags].ptr!=h.ptr);
        UINT64 tag[2];memcpy(tag,(void*)slot,16);rw[2]|=4;
        CK(FAILED(xs_write_view(dev.Get(),0xa8,obj.Get(),rw,slot)));rw[2]=flags;
        CK(FAILED(xs_write_view(dev.Get(),0xa8,dev.Get(),rw,slot)) && !memcmp((void*)slot,tag,16));
    }
    // Recycle a native slot and verify the serial changes without touching its neighbor.
    UINT64 before=*(UINT64*)(rt+96);rw[2]=0;((Writer)vt[0xa8/8])(dev.Get(),obj.Get(),rw,rt+96);
    rw[2]=3;((Writer)vt[0xa8/8])(dev.Get(),obj.Get(),rw,rt+96);CK(before!=*(UINT64*)(rt+96) && !memcmp(stamp,(void*)rt,16));
    obj.Reset();CK(create(off)==DXGI_ERROR_INVALID_CALL && !guest);CK(overlap()==DXGI_ERROR_INVALID_CALL && !msaa);
    auto rd=image->GetDesc();CK(rd.Format==DXGI_FORMAT_R32G8X24_TYPELESS && rd.Width==width && rd.Height==height && rd.Flags==D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[2];UINT64 bytes;d->GetCopyableFootprints(&rd,0,2,0,fp,nullptr,nullptr,&bytes);
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_READBACK;hp.CreationNodeMask=hp.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC buf={};buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;const UINT64 shaderOff=bytes+span+2*guard;buf.Width=shaderOff+96;buf.Height=buf.DepthOrArraySize=buf.MipLevels=buf.SampleDesc.Count=1;buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> rb;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;D3D12_COMMAND_QUEUE_DESC qd={};
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    D3D12_RESOURCE_BARRIER aliasBarrier={};aliasBarrier.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    if(native){aliasBarrier.Aliasing={backing.Get(),image.Get()};l->ResourceBarrier(1,&aliasBarrier);}
    D3D12_RECT zrect={width-2,0,width,height},srect=zrect;srect.left++;
    l->ClearDepthStencilView(h,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,.25f,0x31,0,nullptr);
    auto plane=[&](UINT i,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){D3D12_RESOURCE_BARRIER b={};b.Transition={image.Get(),i,before,after};l->ResourceBarrier(1,&b);};
    plane(1,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_DEPTH_READ);
    l->ClearDepthStencilView(ro[2],D3D12_CLEAR_FLAG_DEPTH,.75f,0,1,&zrect);
    plane(1,D3D12_RESOURCE_STATE_DEPTH_READ,D3D12_RESOURCE_STATE_DEPTH_WRITE);
    plane(0,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_DEPTH_READ);
    l->ClearDepthStencilView(ro[1],D3D12_CLEAR_FLAG_STENCIL,0,0xa5,1,&srect);
    plane(0,D3D12_RESOURCE_STATE_DEPTH_READ,D3D12_RESOURCE_STATE_DEPTH_WRITE);
    BYTE dxil[8192];auto file=fopen("depth1024.dxil","rb");CK(file);auto len=fread(dxil,1,sizeof dxil,file);CK(len && feof(file));fclose(file);
    ComPtr<ID3D12RootSignature> root;HR(d->CreateRootSignature(0,dxil,len,IID_PPV_ARGS(&root)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=root.Get();pd.CS={dxil,len};ComPtr<ID3D12PipelineState> pso;HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));
    hp.Type=D3D12_HEAP_TYPE_DEFAULT;buf.Width=256;buf.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ComPtr<ID3D12Resource> result;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&result)));
    // Bind the all-read-only DSV and sample both original planes in the same draw.
    const auto psRead=D3D12_RESOURCE_STATE_DEPTH_READ|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    plane(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_DEPTH_WRITE,psRead);
    BYTE vs[8192],ps[8192];FILE *vf=fopen("ro-vs.dxil","rb"),*pf=fopen("ro-ps.dxil","rb");CK(vf&&pf);
    auto vlen=fread(vs,1,sizeof vs,vf),plen=fread(ps,1,sizeof ps,pf);CK(vlen && plen && feof(vf) && feof(pf));fclose(vf);fclose(pf);
    ComPtr<ID3D12RootSignature> gr;HR(d->CreateRootSignature(0,ps,plen,IID_PPV_ARGS(&gr)));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gp={};gp.pRootSignature=gr.Get();gp.VS={vs,vlen};gp.PS={ps,plen};gp.SampleMask=~0u;
    gp.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;gp.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;gp.RasterizerState.DepthClipEnable=TRUE;
    gp.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;gp.SampleDesc.Count=1;gp.DSVFormat=cv.Format;
    auto &st=gp.DepthStencilState;st.DepthEnable=st.StencilEnable=TRUE;st.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;st.StencilReadMask=0xff;
    st.FrontFace=st.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
    ComPtr<ID3D12PipelineState> draw;HR(d->CreateGraphicsPipelineState(&gp,IID_PPV_ARGS(&draw)));
    D3D12_VIEWPORT vp={width-4,height-1,4,1,0,1};D3D12_RECT rect={width-4,height-1,width,height};
    l->RSSetViewports(1,&vp);l->RSSetScissorRects(1,&rect);l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    l->OMSetRenderTargets(0,nullptr,FALSE,&ro[3]);l->SetDescriptorHeaps(1,&bank);l->SetPipelineState(draw.Get());l->SetGraphicsRootSignature(gr.Get());
    l->SetGraphicsRootDescriptorTable(0,gpu);l->SetGraphicsRootUnorderedAccessView(1,result->GetGPUVirtualAddress()+64);l->DrawInstanced(3,1,0,0);
    l->OMSetRenderTargets(0,nullptr,FALSE,nullptr);
    D3D12_RESOURCE_BARRIER b={};b.Transition={image.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,psRead,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};l->ResourceBarrier(1,&b);
    l->SetDescriptorHeaps(1,&bank);l->SetComputeRootSignature(root.Get());l->SetPipelineState(pso.Get());l->SetComputeRootDescriptorTable(0,gpu);l->SetComputeRootUnorderedAccessView(1,result->GetGPUVirtualAddress());l->Dispatch(1,1,1);
    b.Transition={result.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);l->CopyBufferRegion(rb.Get(),shaderOff,result.Get(),0,96);
    b.Transition={image.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION src={},dst={};src.pResource=image.Get();dst.pResource=rb.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    for(UINT i=0;i<2;i++){src.SubresourceIndex=i;dst.PlacedFootprint=fp[i];l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}
    if(native){b={};b.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;b.Aliasing={image.Get(),backing.Get()};l->ResourceBarrier(1,&b);
    l->CopyBufferRegion(rb.Get(),bytes,backing.Get(),off-guard,span+2*guard);}HR(l->Close());
    ComPtr<ID3D12Fence> gate,end;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&end)));HR(q->Wait(gate.Get(),1));
    ID3D12CommandList *ls[]={l.Get()};q->ExecuteCommandLists(1,ls);HR(q->Signal(end.Get(),1));
    CK(VirtualFree(base,0,MEM_RELEASE));CK(VirtualAlloc(base,pool,MEM_RESERVE,PAGE_NOACCESS)==base);
    BYTE *reuse=base+off-guard;CK(VirtualAlloc(reuse,span+2*guard,MEM_COMMIT,PAGE_READWRITE)==reuse);memset(reuse,0x39,span+2*guard);
    CK(FAILED(create(off))&&!guest);HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);HR(end->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,50)==WAIT_TIMEOUT);
    HR(gate->Signal(1));CK(WaitForSingleObject(e,10000)==WAIT_OBJECT_0);HR(d->GetDeviceRemovedReason());
    BYTE *p;HR(rb->Map(0,nullptr,(void**)&p));for(UINT y=0;y<height;y++)for(UINT x=0;x<width;x++){
        const DWORD *depth=(const DWORD*)(p+fp[0].Offset+y*fp[0].Footprint.RowPitch);CK(depth[x]==(x>=width-2?0x3f400000u:0x3e800000u));
        CK(p[fp[1].Offset+y*fp[1].Footprint.RowPitch+x]==(x==width-1?0xa5:0x31));
    }
    for(UINT i=0;i<4;i++){auto v=(DWORD*)(p+shaderOff)+4*i;printf("sample %u: depth=%08lx stencil=%02lx compare=%lu select=%04lx\n",i,v[0],v[1],v[2],v[3]);
        auto pixel=(DWORD*)(p+shaderOff+64)+2*i;CK(pixel[0]==v[0] && pixel[1]==v[1]);
        CK(v[0]==(i<2?0x3e800000u:0x3f400000u) && v[1]==(i==3?0xa5u:0x31u) && v[2]==(i>=2) && v[3]==(i==3?0xcafeu:0x1234u));}
    const BYTE *raw=native?p+bytes:alias+off-guard;UINT changed=0,zwords=0,stencils=0;
    for(UINT i=0;i<span+2*guard;i++){CK(reuse[i]==0x39);if(i<guard || i>=guard+(native?0x12fc008:span))CK(raw[i]==0xa7);
        else {changed+=raw[i]!=0xa7;stencils+=raw[i]==0x31;if(!(i%4))zwords+=*(const DWORD*)(raw+i)==0x3e800000;}}
    printf("raw depth words=%u stencil bytes=%u changed=%u\n",zwords,stencils,changed);CK(zwords>=width && stencils>=width);
    printf("PASS depth1440 %s: three read-only DSVs, simultaneous pixel SRV/DSV, both plane readbacks after VA reuse; original span, guards, partial overlap both directions\n",native?"native":"section");
    rb->Unmap(0,nullptr);CloseHandle(e);CK(VirtualFree(base,0,MEM_RELEASE));if(alias)CK(VirtualFree(alias,0,MEM_RELEASE));return 0;
}
