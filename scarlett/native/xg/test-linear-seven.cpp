#include "../xmem/gpu_buffer.hpp"
#include "../xmem/native_pool.h"
#include <wrl/client.h>
#include <cstdio>
#include <vector>
using Microsoft::WRL::ComPtr;
#define CK(e) do {if(!(e)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#e);return 1;}}while(0)
int main(){
    constexpr UINT pool=0x89700000,off=0x1c3eb000,span=0xd900,guard=256,total=span+2*guard;
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
    UINT end=span;
    for(UINT m=0;m<7;m++){
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
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_READBACK;hp.CreationNodeMask=hp.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=total;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> read;CK(SUCCEEDED(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read))));
    D3D12_COMMAND_QUEUE_DESC qd={};ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;
    CK(SUCCEEDED(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))) && SUCCEEDED(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a))) && SUCCEEDED(d->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l))));
    l->CopyBufferRegion(read.Get(),0,raw,off-guard,total);CK(SUCCEEDED(l->Close()));ID3D12CommandList *ls[]={l.Get()};q->ExecuteCommandLists(1,ls);
    ComPtr<ID3D12Fence> f;HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev && SUCCEEDED(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f))) && SUCCEEDED(q->Signal(f.Get(),1)) && SUCCEEDED(f->SetEventOnCompletion(1,ev)) && WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);
    void *mapped;CK(SUCCEEDED(read->Map(0,nullptr,&mapped))&&!memcmp(mapped,expect.data(),total));read->Unmap(0,nullptr);CloseHandle(ev);
    CK(VirtualFree(b,0,MEM_RELEASE));printf("PASS seven XG copies: %s original WC GPU readback, all bytes, padding, adjacent guards and negatives\n",native?"native":"section");
}
