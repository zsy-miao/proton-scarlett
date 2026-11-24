/* Included by the A8 allocation fixture with TEST_ATLAS. */
#include "xmem/marked_section.h"
extern "C" HRESULT xs_write_view(IUnknown*,UINT,IUnknown*,const void*,SIZE_T);
extern "C" HRESULT xs_copy_wc_view(IUnknown*,SIZE_T,SIZE_T);
extern "C" HRESULT xs_resolve_view(IUnknown*,SIZE_T,UINT,ID3D12DescriptorHeap**,D3D12_GPU_DESCRIPTOR_HANDLE*);
struct xs_texture_use;
extern "C" HRESULT xs_refresh_wc_texture(IUnknown*,SIZE_T,ID3D12GraphicsCommandList*,ID3D12DescriptorHeap**,xs_texture_use**);
extern "C" void xs_drop_texture_use(xs_texture_use*);
static BYTE *atlasPool(){
    auto alloc=(void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD))(void*)GetProcAddress(GetModuleHandleW(L"xmem.dll"),"XMemVirtualAlloc");
    return alloc?(BYTE*)alloc(nullptr,0x89700000,0x20003000,0x283,0x2404):nullptr;
}
static int checkAtlas(IUnknown *dev,ID3D12Device *d,UINT side,BYTE *base){
    const UINT bytes=side*side,guard=65536,pool=0x89700000;CK(base);auto xm=GetModuleHandleW(L"xmem.dll");scarlett_section_info id={};
    auto take=(HRESULT(WINAPI*)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*))(void*)GetProcAddress(xm,"ScarlettAcquireNativePoolV1");CK(take);
    ComPtr<ID3D12Resource> proof;UINT offset=0;HRESULT pin=take(d,base,1,nullptr,proof.GetAddressOf(),&offset);bool section=pin==S_FALSE;
    if(section){HR(scarlett_query_section(0x53430005,base,1,&id));CloseHandle(id.section);}else CK(pin==S_OK && !offset && proof->GetDesc().Width==pool);
    UINT off=section?0x1bfa0000:0x19d70000;BYTE *src=base+off;memset(src-guard,0xa7,bytes+2*guard);
    DWORD wire[]={3,0,0,0,side,0,side,0x10001,65,1,0,0x100,0x300000};auto vt=*(void***)dev;
    using Place=HRESULT(WINAPI*)(IUnknown*,UINT64,const void*,UINT,const void*,REFIID,void**);
    auto place=[&](BYTE *va,UINT state,const void *cv,REFIID iid,IUnknown **obj){return ((Place)vt[0x428/8])(dev,(SIZE_T)va,wire,state,cv,iid,(void**)obj);};
    auto xg=LoadLibraryW(L"xg_xs.dll");CK(xg); auto create=(HRESULT(WINAPI*)(const void*,void**))(void*)GetProcAddress(xg,"XGCreateTextureComputer");CK(create);
    void *computer=nullptr;wire[2]=256;CK(create(wire,&computer)==E_NOTIMPL && !computer);wire[2]=0;HR(create(wire,&computer));
    auto slots=*(void***)computer;BYTE layout[0x1b50];memset(layout,0xad,sizeof layout);
    ((void(WINAPI*)(void*,void*))slots[2])(computer,layout+8);UINT64 hash=14695981039346656037ull;
    for(UINT j=0;j<0x1b40;j++)hash=(hash^layout[j+8])*1099511628211ull;
    for(UINT j=0;j<8;j++)CK(layout[j]==0xad && layout[0x1b48+j]==0xad);
    CK(hash==(side==1024?0xadca60948a178898ull:0x7e393bd76c35e6c8ull));CK(!((ULONG(WINAPI*)(void*))slots[1])(computer));FreeLibrary(xg);
    IUnknown *bad=nullptr;CK(FAILED(place(src+256,6,nullptr,IID_ID3D12Resource,&bad))&&!bad);CK(FAILED(place(src,3,nullptr,IID_ID3D12Resource,&bad))&&!bad);
    CK(FAILED(place(src,6,wire,IID_ID3D12Resource,&bad))&&!bad && FAILED(place(src,6,nullptr,IID_ID3D12Heap,&bad))&&!bad);
    for(UINT j=0;j<13;j++)if(j!=1){wire[j]^=1;CK(FAILED(place(src,6,nullptr,IID_ID3D12Resource,&bad))&&!bad);wire[j]^=1;}
    ComPtr<IUnknown> atlas;HRESULT placed=place(src,6,nullptr,IID_ID3D12Resource,atlas.GetAddressOf());printf("RECORD native side=%u section=%d offset=%x pin=%lx placement=%lx\n",side,section,off,pin,placed);HR(placed);wire[4]=wire[6]=side==2048?1024:2048;
    CK(place(src,6,nullptr,IID_ID3D12Resource,&bad)==DXGI_ERROR_INVALID_CALL && !bad);wire[4]=wire[6]=side;
    using Heap=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**);using Start=SIZE_T(WINAPI*)(IUnknown*);
    DWORD hd[]={0,0xc38,0,1};ComPtr<IUnknown> staging,visible;auto heap=(Heap)vt[0x70/8];HR(heap(dev,hd,IID_ID3D12DescriptorHeap,(void**)staging.GetAddressOf()));hd[1]=4;hd[2]=1;HR(heap(dev,hd,IID_ID3D12DescriptorHeap,(void**)visible.GetAddressOf()));
    auto start=[](IUnknown *h){return ((Start)((*(void***)h)[0x48/8]))(h);};SIZE_T from=start(staging.Get()),to=start(visible.Get());
    DWORD sw[]={65,4,0x1688,0,0,1,0,0};HR(xs_write_view(dev,0x90,atlas.Get(),sw,from));UINT64 tag[2];memcpy(tag,(void*)from,16);
    for(auto &v:sw){v^=1;CK(FAILED(xs_write_view(dev,0x90,atlas.Get(),sw,from)));v^=1;}
    DWORD other[]={65,4,0,0};CK(FAILED(xs_write_view(dev,0xa0,atlas.Get(),other,from)) && FAILED(xs_write_view(dev,0x98,atlas.Get(),other,from)) && !memcmp(tag,(void*)from,16));
    HR(xs_copy_wc_view(dev,to,from));staging.Reset();CK(place(src,6,nullptr,IID_ID3D12Resource,&bad)==DXGI_ERROR_INVALID_CALL && !bad);
    ID3D12DescriptorHeap *bank=nullptr;D3D12_GPU_DESCRIPTOR_HANDLE g={};CK(xs_resolve_view(visible.Get(),to,0x90,&bank,&g)==E_NOTIMPL&&!bank&&!g.ptr);
    BYTE dxil[8192];FILE *f=fopen("atlas.dxil","rb");CK(f);size_t len=fread(dxil,1,sizeof dxil,f);CK(len&&feof(f));fclose(f);
    ComPtr<ID3D12RootSignature> root;HR(d->CreateRootSignature(0,dxil,len,IID_PPV_ARGS(&root)));ComPtr<ID3D12PipelineState> ps;D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=root.Get();pd.CS={dxil,len};HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps)));
    D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes*4;rd.Height=rd.MipLevels=rd.DepthOrArraySize=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.CreationNodeMask=hp.VisibleNodeMask=1;ComPtr<ID3D12Resource> out,read;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&out)));rd.Flags=D3D12_RESOURCE_FLAG_NONE;hp.Type=D3D12_HEAP_TYPE_READBACK;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read)));
    ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator> a;ComPtr<ID3D12GraphicsCommandList> l;D3D12_COMMAND_QUEUE_DESC qd={};HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,qd.Type,a.Get(),ps.Get(),IID_PPV_ARGS(&l)));
    xs_texture_use *hold=nullptr;HR(xs_refresh_wc_texture(visible.Get(),to,l.Get(),&bank,&hold));CK(bank&&hold);visible.Reset();l->SetDescriptorHeaps(1,&bank);l->SetComputeRootSignature(root.Get());l->SetComputeRootDescriptorTable(0,bank->GetGPUDescriptorHandleForHeapStart());l->SetComputeRootUnorderedAccessView(1,out->GetGPUVirtualAddress());l->Dispatch(side/8,side/8,1);
    D3D12_RESOURCE_BARRIER b={};b.Transition={out.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);l->CopyResource(read.Get(),out.Get());HR(l->Close());ComPtr<ID3D12Fence> gate,end;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&end)));HR(q->Wait(gate.Get(),1));ID3D12CommandList *lists[]={l.Get()};q->ExecuteCommandLists(1,lists);HR(q->Signal(end.Get(),1));
    using Map=HRESULT(WINAPI*)(IUnknown*,UINT,const D3D12_RANGE*,void**);
    using Unmap=void(WINAPI*)(IUnknown*,UINT,const D3D12_RANGE*);
    auto map=(Map)(*(void***)atlas.Get())[8];auto unmap=(Unmap)(*(void***)atlas.Get())[9];
    void *ptr=nullptr,*invalid=(void*)1;D3D12_RANGE range={0,bytes},back={bytes,1},over={0,bytes+1};
    CK(map(atlas.Get(),1,&range,&invalid)==E_INVALIDARG && !invalid && map(atlas.Get(),0,&back,&ptr)==E_INVALIDARG && !ptr);
    CK(map(dev,0,&range,&ptr)==E_INVALIDARG && !ptr && map(atlas.Get(),0,&over,&ptr)==E_INVALIDARG && !ptr);
    CK(map(atlas.Get(),0,&range,nullptr)==E_POINTER);HR(map(atlas.Get(),0,&range,&ptr));CK(ptr && (section || ptr==src));
    CK(map(atlas.Get(),0,&range,&invalid)==DXGI_ERROR_INVALID_CALL && !invalid);
    if(!section) CK(!VirtualFree(base,0,MEM_RELEASE));
    auto value=[](UINT i){return BYTE((i*73)^(i>>8)^(i>>16));};if(!section){for(UINT i=0;i<bytes;i++)((BYTE*)ptr)[i]=value(i);unmap(atlas.Get(),0,nullptr);}
    CK(VirtualFree(base,0,MEM_RELEASE));CK(VirtualAlloc(base,pool,MEM_RESERVE,PAGE_NOACCESS)==base && VirtualAlloc(src-guard,bytes+2*guard,MEM_COMMIT,PAGE_READWRITE)==src-guard);memset(src-guard,0xbd,bytes+2*guard);
    if(section){for(UINT i=0;i<bytes;i++)((BYTE*)ptr)[i]=value(i);unmap(atlas.Get(),0,nullptr);}
    CK(FAILED(map(atlas.Get(),0,&range,&invalid)) && !invalid);atlas.Reset();
    CK(FAILED(place(src,6,nullptr,IID_ID3D12Resource,&bad))&&!bad);HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);HR(end->SetEventOnCompletion(1,e));CK(WaitForSingleObject(e,50)==WAIT_TIMEOUT);HR(gate->Signal(1));CK(WaitForSingleObject(e,10000)==WAIT_OBJECT_0);HR(d->GetDeviceRemovedReason());
    UINT *pixels;HR(read->Map(0,nullptr,(void**)&pixels));for(UINT i=0;i<bytes;i++)CK(pixels[i]==UINT(value(i))<<24);read->Unmap(0,nullptr);for(UINT i=0;i<bytes+2*guard;i++)CK((src-guard)[i]==0xbd);
    xs_drop_texture_use(hold);CloseHandle(e);CK(VirtualFree(base,0,MEM_RELEASE));printf("PASS atlas %u: %u A8 shader loads from original WC +%x after VA reuse, lease, copied SRV and negatives\n",side,bytes,off);return 0;
}
