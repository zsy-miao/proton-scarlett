#include "gpu_buffer.hpp"
#include "code_store.hpp"
#include <cstdio>
#include <cstdint>
#include <initializer_list>

extern "C" {
__declspec(dllimport) BOOL WINAPI XMemAllocatePhysicalPages(ULONGLONG,PULONG_PTR,PULONG_PTR);
__declspec(dllimport) BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemMapPhysicalPages(void *,ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *,SIZE_T,DWORD,ULONGLONG,DWORD);
}
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%d: %s (error %lu)\n",__LINE__,#x,GetLastError()); return 1; } } while (0)
#define HR(x) CHECK(SUCCEEDED(x))

// Test only: the caller already owns both data heaps from the same live run.
// Consume one 12-byte Dispatch record; no operation exposes the code extent.
static HRESULT dispatch_record(ID3D12GraphicsCommandList *l, ID3D12CommandSignature *sig,
        const ImportedBuffer &wc, const ImportedBuffer &wb, UINT64 at)
{
    constexpr UINT64 data = 27ull << 20, cached = 74ull << 20;
    if ((at & 3) || at < data) return E_INVALIDARG;
    auto resource = (at < cached ? wc : wb).resource();
    UINT64 offset = at - (at < cached ? data : cached);
    UINT64 size = resource->GetDesc().Width;
    if (offset > size || sizeof(D3D12_DISPATCH_ARGUMENTS) > size-offset) return E_INVALIDARG;
    l->ExecuteIndirect(sig,1,resource,offset,nullptr,0);
    return S_OK;
}

static int test_code(ID3D12Device3 *d, BYTE *base, SIZE_T size, BYTE *ordinary, ULONG_PTR *ids,
        const ImportedBuffer &wc, const ImportedBuffer &wb)
{
    D3D12_ROOT_PARAMETER rp = {};
    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC rs = {1,&rp,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob *blob;
    ID3D12RootSignature *root;
    HR(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));
    HR(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    blob->Release();
    BYTE code[8192];
    UINT n = 0;
    ID3D12PipelineState *ps[2], *bad;
    MEMORY_BASIC_INFORMATION m;
    CHECK(VirtualQuery(base,&m,sizeof m) && m.RegionSize==size && m.State==MEM_COMMIT &&
          m.Protect==(PAGE_READWRITE|PAGE_WRITECOMBINE));
    DWORD old_protect;
    CHECK(!VirtualProtect(base,65536,PAGE_EXECUTE_READWRITE,&old_protect));
    for (int i=0;i<2;i++) {
        FILE *file=fopen(i?"second.dxil":"first.dxil","rb");
        CHECK(file);
        n=fread(code,1,sizeof code,file);
        CHECK(n && n<sizeof code && feof(file)); fclose(file);
        auto slot=base+size-n;
        D3D12_SHADER_BYTECODE allowed={code,n};
        memcpy(slot,code,n); MemoryBarrier();
        HR(xs_code_pipeline(d,root,slot,allowed,&ps[i]));
        CHECK(FAILED(xs_code_pipeline(d,root,slot+1,allowed,&bad)) && !bad);
        slot[n-1]^=1; MemoryBarrier();
        CHECK(xs_code_pipeline(d,root,slot,allowed,&bad)==E_NOTIMPL && !bad);
        memcpy(ordinary,code,n); MemoryBarrier();
        CHECK(FAILED(xs_code_pipeline(d,root,ordinary,allowed,&bad)) && !bad);
    }
    scarlett_section_info qi;
    for (ULONG op: {0x53430002u,0x53430005u,0x5343000au,0x5343000cu,0x53430010u,0x53430011u})
        CHECK(FAILED(scarlett_query_section(op,base,4,&qi)));
    BYTE *last=base+size-1;
    ImportedReadSpan held, denied;
    CHECK(FAILED(denied.acquire(d,last,2)) && FAILED(denied.acquire(d,last,0)) &&
          FAILED(denied.acquire(d,last,SIZE_T(-1))) && FAILED(denied.acquire(d,last+1,1)));
    ImportedBuffer writable;
    SIZE_T offset;
    CHECK(FAILED(writable.acquire(d,last,1,&offset)) && !writable.resource());
    BYTE *slot=base+size-n;
    memcpy(slot,code,n); MemoryBarrier();
    HR(held.acquire(d,last,1));
    CHECK(XMemMapPhysicalPages(base,size/65536,nullptr)==base);
    CHECK(FAILED(denied.acquire(d,last,1)));
    CHECK(FAILED(xs_code_pipeline(d,root,slot,{code,n},&bad)) && !bad);
    CHECK(XMemMapPhysicalPages(base,size/65536,ids)==base && !memcmp(slot,code,n));
    *last = code[n-1]^0xff; MemoryBarrier(); // change original backing after acquisition
    CHECK(XMemMapPhysicalPages(base,size/65536,nullptr)==base && VirtualFree(base,0,MEM_RELEASE));
    CHECK(VirtualAlloc(base,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==base);
    memcpy(slot,code,n);
    CHECK(FAILED(denied.acquire(d,last,1)));
    CHECK(FAILED(xs_code_pipeline(d,root,slot,{code,n},&bad)) && !bad);

    D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ID3D12CommandQueue *q; ID3D12CommandAllocator *a; ID3D12GraphicsCommandList *l;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    HR(d->CreateCommandList(0,qd.Type,a,ps[0],IID_PPV_ARGS(&l)));
    D3D12_HEAP_PROPERTIES h={}; h.Type=D3D12_HEAP_TYPE_DEFAULT; h.CreationNodeMask=h.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC r={}; r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; r.Width=8;
    r.Height=r.DepthOrArraySize=r.MipLevels=r.SampleDesc.Count=1; r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    r.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource *dst,*rb;
    HR(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&dst)));
    h.Type=D3D12_HEAP_TYPE_READBACK; r.Flags=D3D12_RESOURCE_FLAG_NONE; r.Width=16;
    HR(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    l->SetComputeRootSignature(root); l->SetComputeRootUnorderedAccessView(0,dst->GetGPUVirtualAddress());
    constexpr UINT64 mib = 1ull << 20, end = 174*mib, boundary = 74*mib;
    CHECK(size == 27*mib && wc.resource()->GetDesc().Width==47*mib && wb.resource()->GetDesc().Width==100*mib);
    D3D12_INDIRECT_ARGUMENT_DESC arg={}; arg.Type=D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC sd={sizeof(D3D12_DISPATCH_ARGUMENTS),1,&arg,0};
    ID3D12CommandSignature *sig;
    HR(d->CreateCommandSignature(&sd,nullptr,IID_PPV_ARGS(&sig)));
    for (UINT64 at : {UINT64(0),size-4,boundary-8,boundary+1,end-8,end,UINT64_MAX-3})
        CHECK(dispatch_record(l,sig,wc,wb,at)==E_INVALIDARG);
    UINT64 pos[2]={boundary-12,end-16}; // preserve the WB marker at end-4
    for (unsigned i=0;i<2;i++) {
        D3D12_DISPATCH_ARGUMENTS counts={1,1,1};
        memcpy((void *)((UINT_PTR)base+pos[i]),&counts,sizeof counts);
        l->SetPipelineState(ps[i]);
        HR(dispatch_record(l,sig,wc,wb,pos[i]));
    }
    MemoryBarrier();
    D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource=dst; b.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    l->ResourceBarrier(1,&b); l->CopyBufferRegion(rb,0,dst,0,8);
    CHECK(FAILED(held.copy_to(l,rb,r.Width)) && FAILED(held.copy_to(l,rb,UINT64(-1))) &&
          FAILED(denied.copy_to(l,rb,0)));
    HR(held.copy_to(l,rb,r.Width-1)); HR(l->Close());
    // Recycle both data VAs as zero-filled private RAM before executing. A VA
    // lookup at submit time would now produce zero groups (or fail import).
    for (UINT64 off : {UINT64(size),boundary}) {
        auto extent=(void *)((UINT_PTR)base+off);
        SIZE_T bytes=(off==size?47:100)*mib;
        CHECK(XMemMapPhysicalPages(extent,bytes/65536,nullptr)==extent && VirtualFree(extent,0,MEM_RELEASE));
        CHECK(VirtualAlloc(extent,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==extent);
    }
    ID3D12CommandList *batch[]={l}; q->ExecuteCommandLists(1,batch);
    ID3D12Fence *f; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));
    HR(q->Signal(f,1)); HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr); CHECK(e);
    HR(f->SetEventOnCompletion(1,e)); CHECK(WaitForSingleObject(e,10000)==WAIT_OBJECT_0);
    UINT *data; HR(rb->Map(0,nullptr,(void **)&data));
    CHECK(data[0]==0xc0de8404 && data[1]==0x8404beef);
    CHECK(((BYTE *)data)[r.Width-1]==(BYTE)(*last^0xff));
    rb->Unmap(0,nullptr);
    sig->Release();
    puts("PASS segmented placed Dispatch: WC/WB operands survived exact VA reuse; code, crossing and out-of-range operands rejected");
    CloseHandle(e); f->Release(); l->Release(); a->Release(); q->Release(); dst->Release(); rb->Release();
    ps[0]->Release(); ps[1]->Release(); root->Release();
    puts("PASS 8404 read span: fenced final byte from original backing after remap, write and exact VA reuse; overruns and writable imports refused");
    puts("PASS code store: two immutable PSOs from the same last-byte-aligned slot, post-reuse fenced GPU results c0de8404/8404beef; imports and unknown code denied");
    return 0;
}

int main()
{
    constexpr SIZE_T pg=65536, n=2960, fourth=752;
    const SIZE_T counts[]={160,16,432,fourth,1600};
    const DWORD prot[]={0x1004,0x1404,0x8404,0x2404,0x2004};
    auto p=(BYTE *)XMemVirtualAlloc(nullptr,n*pg,0x42000,7,1);
    BYTE *at[5];
    CHECK(p);
    for (SIZE_T i=0,off=0;i<5;++i) {
        at[i]=p+off;
        off+=pg*counts[i];
        if (i<4) CHECK(VirtualFree(at[i],counts[i]*pg,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER));
    }
    for (SIZE_T i=0;i<5;++i)
        CHECK(XMemVirtualAlloc(at[i],counts[i]*pg,0x20406000,0x27,prot[i])==at[i]);
    ULONG_PTR ids[n], count=n;
    CHECK(XMemAllocatePhysicalPages(0x280,&count,ids) && count==n);
    CHECK(!XMemMapPhysicalPages(at[3],fourth-1,ids) && !XMemMapPhysicalPages(at[3]+pg,fourth,ids));
    ids[fourth-1]--;
    CHECK(!XMemMapPhysicalPages(at[3],fourth,ids));
    ids[fourth-1]++;
    CHECK(XMemMapPhysicalPages(at[3],fourth,ids)==at[3]);
    CHECK(!XMemFreePhysicalPages(n,ids) && GetLastError()==ERROR_BUSY);
    CHECK(XMemMapPhysicalPages(at[4],counts[4],ids+fourth)==at[4]);
    CHECK(XMemMapPhysicalPages(at[0],counts[0],ids+2352)==at[0]);
    CHECK(XMemMapPhysicalPages(at[1],counts[1],ids+2512)==at[1]);
    CHECK(XMemMapPhysicalPages(at[2],counts[2],ids+2528)==at[2]);
    MEMORY_BASIC_INFORMATION m;
    CHECK(VirtualQuery(at[3],&m,sizeof m) && m.RegionSize==fourth*pg &&
          m.Protect==(PAGE_READWRITE|PAGE_WRITECOMBINE));
    auto last=(volatile DWORD *)(at[3]+fourth*pg-4);
    auto alias=XMemVirtualAlloc(nullptr,pg,0x20402000,0x23,0x2404);
    CHECK(alias && XMemMapPhysicalPages(alias,1,ids+fourth-1)==alias);
    auto aliasLast=(volatile DWORD *)((BYTE *)alias+pg-4);
    *last=0x839ab7c1;
    MemoryBarrier();
    CHECK(*aliasLast==0x839ab7c1);
    ID3D12Device3 *d;
    HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    ImportedBuffer held;
    SIZE_T offset;
    HR(held.acquire(d,(const void *)last,4,&offset));
    CHECK(offset==fourth*pg-4 && held.resource()->GetDesc().Width==fourth*pg);
    scarlett_section_info q;
    CHECK(FAILED(scarlett_query_section(0x53430005,(const void *)last,5,&q)));
    D3D12_COMMAND_QUEUE_DESC qd={};
    qd.Type=D3D12_COMMAND_LIST_TYPE_COPY;
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *alloc;
    ID3D12GraphicsCommandList *list;
    ID3D12Fence *fence;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&alloc)));
    HR(d->CreateCommandList(0,qd.Type,alloc,nullptr,IID_PPV_ARGS(&list)));
    HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    D3D12_HEAP_PROPERTIES hp={};
    hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    hp.CreationNodeMask=hp.VisibleNodeMask=1;
    auto desc=held.resource()->GetDesc();
    desc.Width=pg;
    ID3D12Resource *tmp;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&tmp)));
    list->CopyBufferRegion(tmp,0,held.resource(),offset,4);
    HR(list->Close());
    ID3D12CommandList *batch[]={list};
    queue->ExecuteCommandLists(1,batch);
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    CHECK(event);
    HR(queue->Signal(fence,1));
    HR(fence->SetEventOnCompletion(1,event));
    CHECK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    *aliasLast=0;
    MemoryBarrier();
    CHECK(!*last);
    HR(alloc->Reset());
    HR(list->Reset(alloc,nullptr));
    list->CopyBufferRegion(held.resource(),offset,tmp,0,4);
    HR(list->Close());
    queue->ExecuteCommandLists(1,batch);
    HR(queue->Signal(fence,2));
    HR(fence->SetEventOnCompletion(2,event));
    CHECK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0 && *last==0x839ab7c1 && *aliasLast==*last);
    // GPU reads from both restricted views; both directions on WB/RW. Verify independently of resource->Map().
    ImportedBuffer wb;
    auto wbLast=(DWORD *)(at[4]+counts[4]*pg-4);
    HR(wb.acquire(d,wbLast,4,&offset));
    CHECK(offset==counts[4]*pg-4 && wb.resource()->GetDesc().Width==counts[4]*pg);
    D3D12_HEAP_PROPERTIES wbProps; D3D12_HEAP_FLAGS wf;
    HR(wb.resource()->GetHeapProperties(&wbProps,&wf));
    CHECK(wbProps.CPUPageProperty==D3D12_CPU_PAGE_PROPERTY_WRITE_BACK);
    auto out=(volatile DWORD *)at[4];
    auto aLast=(DWORD *)(at[0]+counts[0]*pg-4), bLast=(DWORD *)(at[1]+counts[1]*pg-4);
    ImportedReadSpan ra,rb;
    HR(ra.acquire(d,aLast,4)); HR(rb.acquire(d,bLast,4));
    for (unsigned i: {0u,1u,4u}) {
        CHECK(VirtualQuery(at[i],&m,sizeof m) && m.RegionSize==counts[i]*pg &&
            m.Protect==(DWORD)(PAGE_READWRITE|(prot[i]&PAGE_WRITECOMBINE)));
        if (i!=4) { ImportedBuffer denied; SIZE_T o; CHECK(FAILED(denied.acquire(d,at[i],4,&o))); }
    }
    *aLast=0xb5f30001; *bLast=0xb5f30002; *wbLast=0xb5f30003; MemoryBarrier();
    HR(alloc->Reset()); HR(list->Reset(alloc,nullptr));
    HR(ra.copy_to(list,tmp,0)); HR(rb.copy_to(list,tmp,4));
    list->CopyBufferRegion(tmp,8,wb.resource(),offset,4);
    HR(list->Close()); queue->ExecuteCommandLists(1,batch); HR(queue->Signal(fence,3));
    HR(fence->SetEventOnCompletion(3,event)); CHECK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    HR(alloc->Reset()); HR(list->Reset(alloc,nullptr));
    list->CopyBufferRegion(wb.resource(),0,tmp,0,12);
    HR(list->Close()); queue->ExecuteCommandLists(1,batch); HR(queue->Signal(fence,4));
    HR(fence->SetEventOnCompletion(4,event)); CHECK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    CHECK(out[0]==*aLast && out[1]==*bLast && out[2]==*wbLast);
    CHECK(XMemMapPhysicalPages(alias,1,nullptr)==alias);
    CHECK(!XMemMapPhysicalPages(alias,1,ids+fourth)); // WC alias of a WB ID denied
    CHECK(!XMemMapPhysicalPages(alias,1,ids+2512)); // cannot get RW alias via this worker arena
    CHECK(!XMemMapPhysicalPages(alias,1,ids+2528));
    ImportedBuffer impossible;
    SIZE_T deniedOffset;
    CHECK(FAILED(impossible.acquire(d,at[2],174ull<<20,&deniedOffset)) && !impossible.resource());
    CHECK(FAILED(impossible.acquire(d,at[3]+counts[3]*pg-4,8,&deniedOffset)) && !impossible.resource());
    CHECK(XMemMapPhysicalPages(at[3],fourth,nullptr)==at[3]);
    CHECK(!XMemMapPhysicalPages(at[3],fourth,ids+fourth)); // no WC map covering WB IDs
    CHECK(XMemMapPhysicalPages(at[3],fourth,ids)==at[3] && *last==0x839ab7c1);
    CHECK(!test_code(d,at[2],counts[2]*pg,at[1],ids+2528,held,wb));
    for (unsigned i: {0u,1u}) CHECK(XMemMapPhysicalPages(at[i],counts[i],nullptr)==at[i]);
    CHECK(VirtualFree(alias,0,MEM_RELEASE));
    for (auto a:at) CHECK(VirtualFree(a,0,MEM_RELEASE));
    CHECK(VirtualAlloc(at[3],fourth*pg,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==at[3]);
    *last=999;
    CHECK(!XMemMapPhysicalPages(at[3],fourth,ids) && *last==999 && VirtualFree(at[3],0,MEM_RELEASE));
    auto va2=(Alloc2)(void *)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"VirtualAlloc2");
    CHECK(va2 && va2(nullptr,at[3],fourth*pg,MEM_RESERVE|MEM_RESERVE_PLACEHOLDER,PAGE_NOACCESS,nullptr,0)==at[3]);
    CHECK(!XMemMapPhysicalPages(at[3],fourth,ids)); /* same base, size and MBI; old policy expired */
    CHECK(XMemVirtualAlloc(at[3],fourth*pg,0x20406000,0x27,0x2004)==at[3]);
    CHECK(!XMemMapPhysicalPages(at[3],fourth,ids) && VirtualFree(at[3],0,MEM_RELEASE));
    CHECK(XMemFreePhysicalPages(n,ids) && *(volatile DWORD *)((BYTE *)wb.cpu()+offset)==0xb5f30003);
    CloseHandle(event);
    tmp->Release(); fence->Release(); list->Release(); alloc->Release(); queue->Release(); d->Release();
    puts("PASS actual 2960 IDs: all five slices, fenced GPU copies and compiled shader consumption, unmap/reuse/free");
}
