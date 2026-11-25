#include "../xmem/gpu_buffer.hpp"
#include "../xmem/native_pool.h"
#include <wrl/client.h>
#include <cstdio>
#include <vector>
using Microsoft::WRL::ComPtr;
#define CK(e) do {if(!(e)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#e);return 1;}}while(0)
#ifdef TEST_SEVEN_PLACE
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void*,const void*,REFIID,void**);
extern "C" ID3D12Resource *test_row_backing(IUnknown*);
extern "C" HRESULT xs_write_view(IUnknown*,UINT,IUnknown*,const void*,SIZE_T);
#endif
#ifdef TEST_SEVEN_VA
extern "C" UINT64 test_seven_guest_va(IUnknown*,UINT_PTR);
extern "C" UINT64 fixture_row_va(IUnknown*);
extern "C" char fixture_row_return;
#endif
#ifdef TEST_SEVEN_SRV
#include "test-seven-srv.hpp"
#endif
static int exercise(bool line){
    const UINT pool=0x89700000,off=line?0xe6cf800:0x1c3eb000,span=line?256:0xd900,guard=256,total=span+2*guard;
    auto xm=LoadLibraryA("xmem.dll"),xg=LoadLibraryA("xg_xs.dll");CK(xm && xg);
    auto alloc=(void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD))(void*)GetProcAddress(xm,"XMemVirtualAlloc");CK(alloc);
    BYTE *b=(BYTE*)alloc(nullptr,pool,0x20003000,0x283,0x2404);CK(b);
    ComPtr<ID3D12Device3>d;CK(SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d))));
    auto get=(HRESULT(WINAPI*)(ID3D12Device*,const void*,SIZE_T,ID3D12Heap**,ID3D12Resource**,UINT*))(void*)GetProcAddress(xm,"ScarlettAcquireNativePoolV1");CK(get);
    UINT actual=0;ComPtr<ID3D12Resource> native;ImportedBuffer section;ID3D12Resource *raw=native.Get();
    HRESULT pin=get(d.Get(),b+off,span,nullptr,native.GetAddressOf(),&actual);
    if(pin==S_OK){CK(actual==off);raw=native.Get();}
    else{SIZE_T pos=0;CK(pin==S_FALSE && SUCCEEDED(section.acquire(d.Get(),b+off,span,&pos)) && pos==off);raw=section.resource();}
    auto create=(HRESULT(WINAPI*)(const void*,void**))(void*)GetProcAddress(xg,"XGCreateTextureComputer");CK(create);
    DWORD desc[]={3,0,0,0,128,0,72,0x70001,41,1,0,0x100,0x300000};
    if(line){desc[0]=2;desc[6]=1;desc[7]=0x10001;desc[8]=65;desc[11]=0;}
    void *c=nullptr;for(UINT i=0;i<13;i++)if(i!=1){desc[i]^=1;CK(FAILED(create(desc,&c))&&!c);desc[i]^=1;}
    CK(create(desc,&c)==S_OK && c);auto vt=*(void***)c;
    auto copy=(HRESULT(WINAPI*)(void*,void*,UINT,UINT,const void*,UINT,UINT))(void*)vt[8];
    HANDLE h=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,65536,nullptr);CK(h);
    auto src=(BYTE*)MapViewOfFile(h,FILE_MAP_WRITE,0,0,65536);CK(src);
    auto dst=b+off;memset(dst-guard,0xa7,total);std::vector<BYTE> expect(total,0xa7);
    CK(FAILED(copy(c,dst,1,0,src,512,36864)) && FAILED(copy(c,dst,0,7,src,8,8)) &&
       FAILED(copy(c,dst,0,0,src,511,36864)) && FAILED(copy(c,dst,0,6,src,8,9)) &&
       FAILED(copy(c,dst+1,0,6,src,8,8)) && FAILED(copy(c,b+pool-4096,0,6,src,8,8)) &&
       FAILED(copy(c,dst,0,0,src+65536-512,512,36864)) && FAILED(copy(c,src,0,6,src,8,8)));
    CK(!memcmp(dst-guard,expect.data(),total));
    if(line){
        BYTE stack[128];for(UINT j=0;j<128;j++)stack[j]=BYTE(13+117*j);
        CK(FAILED(copy(c,dst,0,0,src,128,128)) && FAILED(copy(c,dst,1,0,stack,128,128)) && FAILED(copy(c,dst,0,1,stack,128,128)) &&
            FAILED(copy(c,dst,0,0,stack,127,128)) && FAILED(copy(c,dst,0,0,stack,128,127)) && FAILED(copy(c,dst+1,0,0,stack,128,128)));
        CK(!memcmp(dst-guard,expect.data(),total) && copy(c,dst,0,0,stack,128,128)==S_OK);
        memcpy(expect.data()+guard,stack,128);memset(stack,0,128);CK(!memcmp(dst-guard,expect.data(),total));
    }
    UINT end=span;
    const UINT starts[]={0x4900,0x2500,0x1300,0xa00,0x500,0x200,0};
    for(UINT m=0;!line && m<7;m++){
        UINT row=512>>m,hgt=72>>m,bytes=row*hgt,stride=m?256:512;
        // Reference spans include internal blank rows at mip 3/4/5/6.
        CK(starts[m]+(hgt-1)*stride+row<=end);end=starts[m];
#ifdef TEST_SEVEN_SRV
        for(UINT y=0;y<hgt;y++)for(UINT x=0;x<row/4;x++)((DWORD*)src)[y*row/4+x]=seven_bits(m,x,y);
#else
        for(UINT j=0;j<bytes;j++)src[j]=BYTE(17*m+j*73+(j>>8));
#endif
        CK(copy(c,dst,0,m,src,row,bytes)==S_OK);
        for(UINT y=0;y<hgt;y++)memcpy(expect.data()+guard+starts[m]+y*stride,src+y*row,row);
        CK(!memcmp(dst-guard,expect.data(),total));
    }
    CK(((ULONG(WINAPI*)(void*))vt[1])(c)==0);UnmapViewOfFile(src);CloseHandle(h);
#ifdef TEST_SEVEN_PLACE
    CK(!line && !native);
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> device;CK(SUCCEEDED(D3D12XboxCreateDevice(nullptr,params,boot,(void**)device.GetAddressOf())));
    auto dv=*(void***)device.Get();
    using Place=HRESULT(WINAPI*)(IUnknown*,UINT64,const void*,UINT,const void*,REFIID,void**);
    void *obj=nullptr;
    auto place=[&](UINT at,UINT state=6,const void *clear=nullptr,REFIID iid=IID_ID3D12Resource){
        obj=(void*)1;
        return ((Place)dv[0x428/8])(device.Get(),(UINT_PTR)b+at,desc,state,clear,iid,&obj);
    };
    for(UINT i=0;i<13;i++)if(i!=1){desc[i]^=1;CK(FAILED(place(off)) && !obj);desc[i]^=1;}
    CK(FAILED(place(off+256)) && !obj && FAILED(place(off,3)) && !obj && FAILED(place(off,6,params)) && !obj);
    CK(place(off,6,nullptr,IID_ID3D12Heap)==E_NOINTERFACE && !obj && SUCCEEDED(place(off)) && obj);
    ComPtr<IUnknown> title;title.Attach((IUnknown*)obj);
    ComPtr<ID3D12Resource> placed;placed.Attach(test_row_backing(title.Get()));
    CK(placed && placed->GetDesc().Width==span && placed->GetDesc().Flags==D3D12_RESOURCE_FLAG_NONE);
#ifdef TEST_SEVEN_VA
    constexpr UINT_PTR caller=0x4ecc936;
    scarlett_wc_view_info orig;
    CK(scarlett_acquire_wc_view(dst,span,&orig)==S_OK && orig.info.base==b && orig.info.offset==off &&
       orig.info.view_size==pool && orig.info.section && orig.generation);
    CK((UINT_PTR)&fixture_row_return-(UINT_PTR)GetModuleHandleW(nullptr)==caller);
    UINT64 live=fixture_row_va(title.Get());
    CK(live==(UINT_PTR)dst && live!=placed->GetGPUVirtualAddress());
    printf("PASS seven-va58 dispatch: site=%llx guest=%p result=%llx host=%llx\n",
           (unsigned long long)caller,dst,live,placed->GetGPUVirtualAddress());
    CK(!test_seven_guest_va(title.Get(),caller+1));
    puts("PASS seven-va58 helper: wrong caller on live row");
#endif
    CK(place(off)==DXGI_ERROR_INVALID_CALL && !obj);
    using Make=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**);using Start=SIZE_T(WINAPI*)(IUnknown*);
    UINT hd[]={0,4,1,1};ComPtr<IUnknown> slots;
#ifdef TEST_SEVEN_SRV
    hd[1]=0x130000;
#endif
    CK(SUCCEEDED(((Make)dv[0x70/8])(device.Get(),hd,IID_ID3D12DescriptorHeap,(void**)slots.GetAddressOf())));
    SIZE_T slot=((Start)((*(void***)slots.Get())[0x48/8]))(slots.Get());
    DWORD view[]={41,4,0x1688,0,0,7,0,0,0,0};BYTE before[64];memcpy(before,(void*)slot,sizeof before);
    CK(FAILED(xs_write_view(device.Get(),0x90,title.Get(),view,slot)) && !memcmp(before,(void*)slot,sizeof before));
#ifdef TEST_SEVEN_SRV
    slot+=135*32;
    DWORD addr[]={41,4,0x1688,0,0,7,0,0,0,0,0,0,0,0,0,3};memcpy(addr+10,&live,sizeof live);
    BYTE window[96];
    auto guard_slots=[&]{
        memset((void*)(slot-32),0xba,32);memset((void*)(slot+32),0xcd,32);
        memcpy(window,(void*)(slot-32),sizeof window);
    };
    auto denied=[&](SIZE_T target){
        return FAILED(xs_write_address_srv(device.Get(),desc,addr,target)) &&
                !memcmp(window,(void*)(slot-32),sizeof window);
    };
    guard_slots();
    for(UINT i=0;i<13;i++)if(i!=1){desc[i]^=1;CK(denied(slot));desc[i]^=1;}
    for(UINT i=0;i<16;i++){addr[i]^=1;CK(denied(slot));addr[i]^=1;}
    for(UINT64 va:{(UINT64)0,live+256,placed->GetGPUVirtualAddress()}){
        memcpy(addr+10,&va,sizeof va);CK(denied(slot));
    }
    memcpy(addr+10,&live,sizeof live);
    CK(denied(slot+1) && FAILED(test_seven_wrong_site(device.Get(),desc,addr,slot)) &&
            !memcmp(window,(void*)(slot-32),sizeof window));
    CK((UINT_PTR)&fixture_srv_return-(UINT_PTR)GetModuleHandleW(nullptr)==srv_site);
    fixture_seven_srv(device.Get(),desc,addr,slot);
    CK(*(UINT64*)slot && !memcmp(window,(void*)(slot-32),32) && !memcmp(window+64,(void*)(slot+32),32));
    puts("PASS seven230 dispatch: site=4ecc821 slot=135, wire negatives and adjacent descriptors intact");
#endif
#ifndef TEST_SEVEN_VA
    title.Reset();CK(place(off)==DXGI_ERROR_INVALID_CALL && !obj);
#endif
#endif
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_READBACK;hp.CreationNodeMask=hp.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=total;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
#ifdef TEST_SEVEN_SRV
    rd.Width+=srv_bytes;
#endif
    ComPtr<ID3D12Resource> read;CK(SUCCEEDED(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read))));
    D3D12_COMMAND_QUEUE_DESC qd={};ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;
    CK(SUCCEEDED(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))) && SUCCEEDED(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a))) && SUCCEEDED(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l))));
#ifdef TEST_SEVEN_PLACE
#ifdef TEST_SEVEN_SRV
    SevenLoad load;
    CK(!load.record(d.Get(),l.Get(),slots.Get(),slot,read.Get(),total));
    // Keep the recording, but test stale writes on a fresh slot 135 (never occupied).
    slots.Reset();
    CK(SUCCEEDED(((Make)dv[0x70/8])(device.Get(),hd,IID_ID3D12DescriptorHeap,(void**)slots.GetAddressOf())));
    slot=((Start)((*(void***)slots.Get())[0x48/8]))(slots.Get())+135*32;guard_slots();
#endif
    l->CopyBufferRegion(read.Get(),guard,placed.Get(),0,span);
    D3D12_RESOURCE_BARRIER barrier={};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    barrier.Aliasing={placed.Get(),raw};l->ResourceBarrier(1,&barrier);
    l->CopyBufferRegion(read.Get(),0,raw,off-guard,guard);
    l->CopyBufferRegion(read.Get(),guard+span,raw,off+span,guard);
#ifdef TEST_SEVEN_SRV
    ComPtr<ID3D12Fence> gate;CK(SUCCEEDED(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate))));
    CK(SUCCEEDED(l->Close()) && SUCCEEDED(q->Wait(gate.Get(),1)));
    ID3D12CommandList *batch[]={l.Get()};q->ExecuteCommandLists(1,batch);
#endif
    // Late writes distinguish the original WC range from a detached copy.
#ifdef TEST_SEVEN_SRV
    for(UINT at:starts){dst[at]^=0x57;expect[guard+at]^=0x57;}
#else
    dst[0]^=0x57;expect[guard]^=0x57;
#endif
    MemoryBarrier();
    CK(VirtualFree(b,0,MEM_RELEASE));
#ifdef TEST_SEVEN_VA
    CK(!test_seven_guest_va(title.Get(),caller));
#ifdef TEST_SEVEN_SRV
    CK(denied(slot));
#endif
    puts("PASS seven-va58 helper: freed VA");
#endif
    CK(VirtualAlloc(b,pool,MEM_RESERVE,PAGE_NOACCESS)==b);
    CK(VirtualAlloc(dst-guard,total,MEM_COMMIT,PAGE_READWRITE));memset(dst-guard,0x8d,total);
#ifdef TEST_SEVEN_VA
    CK(!test_seven_guest_va(title.Get(),caller));
#ifdef TEST_SEVEN_SRV
    CK(denied(slot));
#endif
    puts("PASS seven-va58 helper: same VA private reuse");
#endif
    CK(FAILED(place(off)) && !obj);
#else
    l->CopyBufferRegion(read.Get(),0,raw,off-guard,total);
#endif
#ifdef TEST_SEVEN_SRV
    CK(SUCCEEDED(gate->Signal(1)));
#else
    CK(SUCCEEDED(l->Close()));ID3D12CommandList *ls[]={l.Get()};q->ExecuteCommandLists(1,ls);
#endif
    ComPtr<ID3D12Fence> f;HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev && SUCCEEDED(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f))) && SUCCEEDED(q->Signal(f.Get(),1)) && SUCCEEDED(f->SetEventOnCompletion(1,ev)) && WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);
    void *mapped;CK(SUCCEEDED(read->Map(0,nullptr,&mapped))&&!memcmp(mapped,expect.data(),total));
#ifdef TEST_SEVEN_SRV
    CK(!load.verify((const DWORD*)((BYTE*)mapped+total)));
#endif
    read->Unmap(0,nullptr);CloseHandle(ev);
#ifdef TEST_SEVEN_PLACE
    for(UINT i=0;i<total;i++)CK(dst[(ptrdiff_t)i-guard]==0x8d);
    CK(SUCCEEDED(d->GetDeviceRemovedReason()));
#endif
#ifdef TEST_SEVEN_VA
    CK(VirtualFree(b,0,MEM_RELEASE));
    using Same=BOOL(WINAPI*)(HANDLE,HANDLE);
    auto same=(Same)(void*)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles");CK(same);
    using Wc=HRESULT(WINAPI*)(ID3D12Device3*,SIZE_T,HANDLE,HANDLE*,ID3D12Heap**);
    auto wc=(Wc)(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettWcHeapV1");CK(wc);
    ComPtr<ID3D12Heap> otherHeap;HANDLE other=nullptr;
    CK(wc(d.Get(),pool,nullptr,&other,otherHeap.GetAddressOf())==S_OK && other && !same(other,orig.info.section));
    for(unsigned original=0;original<2;original++){
        HANDLE input=original?orig.info.section:other;
        scarlett_section_info remap;scarlett_wc_view_info now;
        CK(scarlett_query_section_handle(0x53430004,b,pool,&remap,input)==S_OK && remap.base==b);
        CK(scarlett_acquire_wc_view(dst,span,&now)==S_OK && now.info.base==b && now.info.offset==off &&
           now.info.view_size==pool && now.info.section && same(input,now.info.section));
        CK(!!same(orig.info.section,now.info.section)==!!original &&
           now.generation && now.generation!=orig.generation && CloseHandle(now.info.section));
        if(original) CK(!memcmp(dst-guard,expect.data(),total));
        CK(!test_seven_guest_va(title.Get(),caller));
#ifdef TEST_SEVEN_SRV
        CK(denied(slot));
#endif
        printf("PASS seven-va58 helper: same VA %s section generation=%llu original=%llu\n",
               original?"original":"different",now.generation,orig.generation);
        if(!original){CK(VirtualFree(b,0,MEM_RELEASE) && CloseHandle(other));otherHeap.Reset();}
    }
#ifdef TEST_SEVEN_SRV
    puts("PASS seven230 stale: freed, private, different section, original section/new generation; slot and neighbors intact");
#endif
    title.Reset();CK(place(off)==DXGI_ERROR_INVALID_CALL && !obj);
    CK(CloseHandle(orig.info.section));
#endif
    CK(VirtualFree(b,0,MEM_RELEASE));printf("PASS %s: %s original WC GPU readback, all bytes, padding, adjacent guards and negatives\n",line?"A8 line from thread stack":"seven XG copies",native?"native":"section");return 0;
}
#ifdef TEST_SEVEN_PLACE
int main(){return exercise(false);}
#else
int main(){return exercise(false)||exercise(true); }
#endif
