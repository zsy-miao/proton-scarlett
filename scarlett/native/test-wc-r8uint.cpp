#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" HRESULT xs_write_view(IUnknown*,UINT,IUnknown*,const void*,SIZE_T);
extern "C" HRESULT xs_resolve_view(IUnknown*,SIZE_T,UINT,ID3D12DescriptorHeap**,D3D12_GPU_DESCRIPTOR_HANDLE*);
extern "C" HRESULT xs_resolve_color_rtv(IUnknown*,SIZE_T,D3D12_CPU_DESCRIPTOR_HANDLE*,ID3D12Resource**);

#define CK(v) do{if(!(v)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#v);return 1;}}while(0)
#define HR(e) do{HRESULT hr=(e);if(FAILED(hr)){fprintf(stderr,"FAIL %d %s = %lx\n",__LINE__,#e,hr);return 1;}}while(0)
#include "xmem/marked_section.h"
int main(){
    constexpr UINT off=0xe610000;
    constexpr UINT pool=0x89700000,span=0x40000,guard=65536,width=640,height=360,pixels=width*height,bytes=768*height;
    auto xm=LoadLibraryW(L"xmem.dll");CK(xm);
    auto alloc=(void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD))(void*)GetProcAddress(xm,"XMemVirtualAlloc");
    auto take=(HRESULT(WINAPI*)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*))(void*)GetProcAddress(xm,"ScarlettAcquireNativePoolV1");
    CK(alloc&&take);auto base=(BYTE*)alloc(nullptr,pool,0x20003000,0x283,0x2404);CK(base);
    memset(base+off-guard,0xa7,span+2*guard);MemoryBarrier();
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)dev.GetAddressOf()));
    ComPtr<ID3D12Device>d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    UINT actual=0;ComPtr<ID3D12Resource> backing;HRESULT pin=take(d.Get(),base+off,span,nullptr,backing.GetAddressOf(),&actual);
    BYTE *alias=nullptr;bool native=pin==S_OK;
    if(native){CK(actual==off && backing->GetDesc().Width==pool);}
    else {CK(pin==S_FALSE);scarlett_section_info held={},mapped={};HR(scarlett_query_section(0x53430005,base+off,span,&held));CK(held.offset==off && held.view_size==pool);
        HR(scarlett_query_section_handle(0x53430004,nullptr,pool,&mapped,held.section));CloseHandle(held.section);alias=(BYTE*)mapped.base;}
    DWORD wire[]={3,0,0,0,width,0,height,0x10001,62,1,0,0,0xc00005};D3D12_CLEAR_VALUE cv={};cv.Format=DXGI_FORMAT_R8_UINT;cv.Color[3]=1;
    using Place=HRESULT(WINAPI*)(IUnknown*,UINT64,const void*,UINT,const void*,REFIID,void**);
    auto place=(Place)((*(void***)dev.Get())[0x428/8]);void *guest=nullptr;
    auto create=[&](UINT pos,UINT state=3){return place(dev.Get(),(UINT_PTR)base+pos,wire,state,&cv,IID_ID3D12Resource,&guest);};
    CK(FAILED(create(off,6))&&!guest);for(UINT j=0;j<13;j++)if(j!=1){wire[j]^=1;CK(FAILED(create(off))&&!guest);wire[j]^=1;}
    CK(FAILED(create(off+guard))&&!guest);cv.Color[3]=0;CK(FAILED(create(off))&&!guest);cv.Color[3]=1;
    HR(create(off));CK(guest);ComPtr<IUnknown> obj;obj.Attach((IUnknown*)guest);
    using Heap=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**);using Start=SIZE_T(WINAPI*)(IUnknown*);
    using Writer=void(WINAPI*)(IUnknown*,IUnknown*,const void*,SIZE_T);
    ComPtr<IUnknown> rtvHeap,srvHeap;auto vt=*(void***)dev.Get();
    UINT r[]={2,4096,0,1},sh[]={0,4,1,1};
    HR(((Heap)vt[0x70/8])(dev.Get(),r,IID_ID3D12DescriptorHeap,(void**)rtvHeap.GetAddressOf()));
    HR(((Heap)vt[0x70/8])(dev.Get(),sh,IID_ID3D12DescriptorHeap,(void**)srvHeap.GetAddressOf()));
    SIZE_T rt=((Start)((*(void***)rtvHeap.Get())[0x48/8]))(rtvHeap.Get())+32;
    SIZE_T ss=((Start)((*(void***)srvHeap.Get())[0x48/8]))(srvHeap.Get())+32;
    DWORD rw[]={62,4,0,0},sw[]={62,4,0x1688,0,0,1,0,0};
    ((Writer)vt[0xa0/8])(dev.Get(),obj.Get(),rw,rt);((Writer)vt[0x90/8])(dev.Get(),obj.Get(),sw,ss);
    DWORD uw[]={62,4,0,0};SIZE_T us=ss+32;using UWriter=void(WINAPI*)(IUnknown*,IUnknown*,IUnknown*,const void*,SIZE_T);
    ((UWriter)vt[0x98/8])(dev.Get(),obj.Get(),nullptr,uw,us);
    UINT64 ustamp[2];memcpy(ustamp,(void*)us,16);
    for(auto &x:uw){x^=1;CK(FAILED(xs_write_view(dev.Get(),0x98,obj.Get(),uw,us)));x^=1;}CK(!memcmp(ustamp,(void*)us,16));
    UINT64 stamp[2];memcpy(stamp,(void*)ss,16);
    for(auto &x:sw){x^=1;CK(FAILED(xs_write_view(dev.Get(),0x90,obj.Get(),sw,ss)));x^=1;}
    CK(!memcmp(stamp,(void*)ss,16));memcpy(stamp,(void*)rt,16);
    for(auto &x:rw){x^=1;CK(FAILED(xs_write_view(dev.Get(),0xa0,obj.Get(),rw,rt)));x^=1;}
    CK(!memcmp(stamp,(void*)rt,16));
    UINT64 before=*(UINT64*)ss;sw[0]=28;CK(FAILED(xs_write_view(dev.Get(),0x90,obj.Get(),sw,ss))&&before==*(UINT64*)ss);sw[0]=62;
    rw[0]=28;CK(FAILED(xs_write_view(dev.Get(),0xa0,obj.Get(),rw,rt)));rw[0]=62;
    ID3D12DescriptorHeap *bank;D3D12_GPU_DESCRIPTOR_HANDLE gpu;HR(xs_resolve_view(srvHeap.Get(),ss,0x90,&bank,&gpu));
    ID3D12DescriptorHeap *ubank;D3D12_GPU_DESCRIPTOR_HANDLE ugpu;HR(xs_resolve_view(srvHeap.Get(),us,0x98,&ubank,&ugpu));CK(ubank==bank);
    ComPtr<ID3D12Resource> image;D3D12_CPU_DESCRIPTOR_HANDLE rtv;HR(xs_resolve_color_rtv(rtvHeap.Get(),rt,&rtv,image.GetAddressOf()));
    obj.Reset();CK(create(off)==DXGI_ERROR_INVALID_CALL && !guest);
    for(UINT i=0;i<span+2*guard;i++)CK((base+off-guard)[i]==0xa7);
    auto rd=image->GetDesc();CK(rd.Format==cv.Format && rd.Width==width && rd.Height==height && rd.Flags==(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;UINT rows;UINT64 row,total;d->GetCopyableFootprints(&rd,0,1,0,&fp,&rows,&row,&total);CK(rows==height && row==width && fp.Footprint.RowPitch==768 && total<=bytes);
    D3D12_DESCRIPTOR_HEAP_DESC dd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};ComPtr<ID3D12DescriptorHeap> cpu;HR(d->CreateDescriptorHeap(&dd,IID_PPV_ARGS(&cpu)));
    auto uav=cpu->GetCPUDescriptorHandleForHeapStart();d->CreateUnorderedAccessView(image.Get(),nullptr,nullptr,uav);
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_READBACK;hp.CreationNodeMask=hp.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC buf={};buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;const UINT shaderOff=bytes+2*(span+2*guard);buf.Width=shaderOff+32;buf.Height=buf.DepthOrArraySize=buf.MipLevels=buf.SampleDesc.Count=1;buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> rb;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;D3D12_COMMAND_QUEUE_DESC qd={};
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    D3D12_RESOURCE_BARRIER aliasBarrier={};aliasBarrier.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    if(native){aliasBarrier.Aliasing={backing.Get(),image.Get()};l->ResourceBarrier(1,&aliasBarrier);}
    UINT dark[]={71,0,0,0};float bright[]={35,0,0,0};D3D12_RECT band={13,3,93,height-9};
    l->SetDescriptorHeaps(1,&bank);
    l->ClearUnorderedAccessViewUint(ugpu,uav,image.Get(),dark,0,nullptr);
    D3D12_RESOURCE_BARRIER barrier={};barrier.Transition={image.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_RENDER_TARGET};l->ResourceBarrier(1,&barrier);
    l->ClearRenderTargetView(rtv,bright,1,&band);
    BYTE dxil[8192];auto file=fopen("r8uint.dxil","rb");CK(file);auto len=fread(dxil,1,sizeof dxil,file);CK(len && feof(file));fclose(file);
    ComPtr<ID3D12RootSignature> root;HR(d->CreateRootSignature(0,dxil,len,IID_PPV_ARGS(&root)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=root.Get();pd.CS={dxil,len};ComPtr<ID3D12PipelineState> pso;HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));
    hp.Type=D3D12_HEAP_TYPE_DEFAULT;buf.Width=256;buf.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ComPtr<ID3D12Resource> result;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&result)));
    D3D12_RESOURCE_BARRIER b={};b.Transition={image.Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};l->ResourceBarrier(1,&b);
    l->SetDescriptorHeaps(1,&bank);l->SetComputeRootSignature(root.Get());l->SetPipelineState(pso.Get());l->SetComputeRootDescriptorTable(0,gpu);l->SetComputeRootUnorderedAccessView(1,result->GetGPUVirtualAddress());l->Dispatch(1,1,1);
    b.Transition={result.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);l->CopyBufferRegion(rb.Get(),shaderOff,result.Get(),0,32);
    b.Transition={image.Get(),0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION src={},dst={};src.pResource=image.Get();dst.pResource=rb.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint=fp;l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    if(native){b={};b.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;b.Aliasing={image.Get(),backing.Get()};l->ResourceBarrier(1,&b);
    l->CopyBufferRegion(rb.Get(),bytes,backing.Get(),off-guard,span+2*guard);}HR(l->Close());
    ComPtr<ID3D12Fence> gate,end;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&end)));HR(q->Wait(gate.Get(),1));
    ID3D12CommandList *ls[]={l.Get()};q->ExecuteCommandLists(1,ls);HR(q->Signal(end.Get(),1));
    CK(VirtualFree(base,0,MEM_RELEASE));CK(VirtualAlloc(base,pool,MEM_RESERVE,PAGE_NOACCESS)==base);
    BYTE *reuse=base+off-guard;CK(VirtualAlloc(reuse,span+2*guard,MEM_COMMIT,PAGE_READWRITE)==reuse);memset(reuse,0x39,span+2*guard);
    CK(FAILED(create(off))&&!guest);HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);HR(end->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,50)==WAIT_TIMEOUT);
    HR(gate->Signal(1));CK(WaitForSingleObject(e,10000)==WAIT_OBJECT_0);HR(d->GetDeviceRemovedReason());
    BYTE *data;HR(rb->Map(0,nullptr,(void**)&data));for(UINT y=0;y<height;y++)for(UINT x=0;x<width;x++)CK(data[y*fp.Footprint.RowPitch+x]==(x>=13 && x<93 && y>=3 && y<height-9?35:71));
    CK(((UINT*)(data+shaderOff))[0]==71 && ((UINT*)(data+shaderOff))[1]==35);
    const BYTE *raw=native?data+bytes:alias+off-guard;UINT bg=0,fg=0;
    for(UINT i=0;i<span+2*guard;i++){CK(reuse[i]==0x39);if(i<guard || i>=guard+0x3c000)CK(raw[i]==0xa7);else{bg+=raw[i]==71;fg+=raw[i]==35;}}
    CK(fg>=80*(height-12) && bg>=pixels-80*(height-12));
    printf("PASS R8_UINT %s: %u texels, UAV/RTV stripe, SRV shader, original pool raw counts %u/%u, tail, guards, duplicate and VA reuse\n",native?"native":"section",pixels,bg,fg);
    rb->Unmap(0,nullptr);CloseHandle(e);CK(VirtualFree(base,0,MEM_RELEASE));if(alias)CK(VirtualFree(alias,0,MEM_RELEASE));return 0;
}
