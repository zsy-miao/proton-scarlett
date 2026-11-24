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
    for(UINT m=0;!line && m<7;m++){
        UINT row=512>>m,hgt=72>>m,bytes=row*hgt,stride=m?256:512;
        // Reference spans include internal blank rows at mip 3/4/5/6.
        const UINT starts[]={0x4900,0x2500,0x1300,0xa00,0x500,0x200,0};
        CK(starts[m]+(hgt-1)*stride+row<=end);end=starts[m];
        for(UINT j=0;j<bytes;j++)src[j]=BYTE(17*m+j*73+(j>>8));
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
    CK(place(off)==DXGI_ERROR_INVALID_CALL && !obj);
    using Make=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**);using Start=SIZE_T(WINAPI*)(IUnknown*);
    UINT hd[]={0,4,1,1};ComPtr<IUnknown> slots;
    CK(SUCCEEDED(((Make)dv[0x70/8])(device.Get(),hd,IID_ID3D12DescriptorHeap,(void**)slots.GetAddressOf())));
    SIZE_T slot=((Start)((*(void***)slots.Get())[0x48/8]))(slots.Get());
    DWORD view[]={41,4,0x1688,0,0,7,0,0,0,0};BYTE before[64];memcpy(before,(void*)slot,sizeof before);
    CK(FAILED(xs_write_view(device.Get(),0x90,title.Get(),view,slot)) && !memcmp(before,(void*)slot,sizeof before));
    title.Reset();CK(place(off)==DXGI_ERROR_INVALID_CALL && !obj);
#endif
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_READBACK;hp.CreationNodeMask=hp.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=total;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> read;CK(SUCCEEDED(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read))));
    D3D12_COMMAND_QUEUE_DESC qd={};ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;
    CK(SUCCEEDED(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))) && SUCCEEDED(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a))) && SUCCEEDED(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l))));
#ifdef TEST_SEVEN_PLACE
    l->CopyBufferRegion(read.Get(),guard,placed.Get(),0,span);
    D3D12_RESOURCE_BARRIER barrier={};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    barrier.Aliasing={placed.Get(),raw};l->ResourceBarrier(1,&barrier);
    l->CopyBufferRegion(read.Get(),0,raw,off-guard,guard);
    l->CopyBufferRegion(read.Get(),guard+span,raw,off+span,guard);
    // Late writes distinguish the original WC range from a detached copy.
    dst[0]^=0x57;expect[guard]^=0x57;MemoryBarrier();
    CK(VirtualFree(b,0,MEM_RELEASE) && VirtualAlloc(b,pool,MEM_RESERVE,PAGE_NOACCESS)==b);
    CK(VirtualAlloc(dst-guard,total,MEM_COMMIT,PAGE_READWRITE));memset(dst-guard,0x8d,total);
    CK(FAILED(place(off)) && !obj);
#else
    l->CopyBufferRegion(read.Get(),0,raw,off-guard,total);
#endif
    CK(SUCCEEDED(l->Close()));ID3D12CommandList *ls[]={l.Get()};q->ExecuteCommandLists(1,ls);
    ComPtr<ID3D12Fence> f;HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev && SUCCEEDED(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f))) && SUCCEEDED(q->Signal(f.Get(),1)) && SUCCEEDED(f->SetEventOnCompletion(1,ev)) && WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);
    void *mapped;CK(SUCCEEDED(read->Map(0,nullptr,&mapped))&&!memcmp(mapped,expect.data(),total));read->Unmap(0,nullptr);CloseHandle(ev);
#ifdef TEST_SEVEN_PLACE
    for(UINT i=0;i<total;i++)CK(dst[(ptrdiff_t)i-guard]==0x8d);
    CK(SUCCEEDED(d->GetDeviceRemovedReason()));
#endif
    CK(VirtualFree(b,0,MEM_RELEASE));printf("PASS %s: %s original WC GPU readback, all bytes, padding, adjacent guards and negatives\n",line?"A8 line from thread stack":"seven XG copies",native?"native":"section");return 0;
}
#ifdef TEST_SEVEN_PLACE
int main(){return exercise(false);}
#else
int main(){return exercise(false)||exercise(true); }
#endif
