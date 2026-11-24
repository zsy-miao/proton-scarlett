#include <d3d12.h>
#include <cstdio>
#include "bvh_record.h"
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12RootSignature *xs_bvh_root_host(IUnknown *,IUnknown *);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
#define CK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
#define HR(x) CK(SUCCEEDED(x))

static size_t load(const char *name,BYTE *data,size_t capacity)
{
    FILE *f=fopen(name,"rb"); if (!f) return 0;
    size_t n=fread(data,1,capacity,f);
    bool ok=fgetc(f)==EOF && !ferror(f); fclose(f);
    return ok?n:0;
}
int main()
{
    DWORD params[20]={}; params[0]=0x20004; params[2]=0x100000; params[10]=1; params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev,morton,utility;
    HR(D3D12XboxCreateDevice(nullptr,params,boot,(void **)dev.GetAddressOf()));
    D3D12_ROOT_PARAMETER1 p[6]={};
    p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[0].Constants.Num32BitValues=8;
    for (UINT i=1;i<6;i++) { p[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV; p[i].Descriptor.ShaderRegister=i-1; }
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC sig={}; sig.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
    sig.Desc_1_1={6,p,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
    ComPtr<ID3DBlob> blob;
    HR(D3D12SerializeVersionedRootSignature(&sig,&blob,nullptr)); CK(blob->GetBufferSize()==212);
    BYTE wire[212]; memcpy(wire,blob->GetBufferPointer(),sizeof wire); blob.Reset();
    CK(!memcmp(wire+36,"RTS0\xa8\0\0\0",8));
    using Root=HRESULT(WINAPI *)(IUnknown *,UINT,const void *,SIZE_T,REFIID,void **);
    auto root=[&](const void *s,SIZE_T n,void **o) {
        return SLOT(dev.Get(),0x80,Root)(dev.Get(),1,s,n,IID_ID3D12RootSignature,o);
    };
    void *bad=dev.Get();
    wire[211]^=1; CK(root(wire,sizeof wire,&bad)==E_NOTIMPL && !bad); wire[211]^=1;
    HR(root(wire,sizeof wire,(void **)morton.GetAddressOf()));
    memset(p,0,sizeof p); sig.Desc_1_1.NumParameters=3;
    p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV; p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[2].Constants.Num32BitValues=16;
    HR(D3D12SerializeVersionedRootSignature(&sig,&blob,nullptr));
    HR(root(blob->GetBufferPointer(),blob->GetBufferSize(),(void **)utility.GetAddressOf()));
    using Name=HRESULT(WINAPI *)(IUnknown *,const WCHAR *);
    static const WCHAR mn[]=L"XDXR MortonCodesCalculator",un[sizeof mn/sizeof *mn]=L"XDXR UtilityShaders";
    HR(SLOT(morton.Get(),0x30,Name)(morton.Get(),mn));
    CK(SLOT(morton.Get(),0x30,Name)(morton.Get(),un)==E_NOTIMPL);
    HR(SLOT(utility.Get(),0x30,Name)(utility.Get(),un));
    CK(SLOT(utility.Get(),0x30,Name)(utility.Get(),mn)==E_NOTIMPL);
    BYTE cs[0x33fc];
    struct Wire { IUnknown *root; const void *cs; SIZE_T n; BYTE tail[32]; };
    using Factory=HRESULT(WINAPI *)(IUnknown *,const Wire *,UINT64,UINT64,REFIID,void **);
    auto make=[&](IUnknown *r,SIZE_T n) {
        Wire w={r,cs,n,{}}; bad=dev.Get();
        return SLOT(dev.Get(),0x1f0,Factory)(dev.Get(),&w,0,0,IID_ID3D12PipelineState,&bad);
    };
    ComPtr<IUnknown> bitonic;
    memset(p,0,sizeof p); sig.Desc_1_1.NumParameters=4;
    for (UINT i=0;i<2;i++) {
        p[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[i].Constants={1-i,0,2};
        p[i+2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV; p[i+2].Descriptor.ShaderRegister=i;
    }
    blob.Reset(); HR(D3D12SerializeVersionedRootSignature(&sig,&blob,nullptr));
    CK(blob->GetBufferSize()==164);
    const auto *chunk=(const BYTE *)blob->GetBufferPointer()+36;
    BYTE embedded[128];
    for (unsigned i=0;i<6;i++) {
        char file[32]; snprintf(file,sizeof file,"sortroot%u.bin",i);
        CK(load(file,embedded,sizeof embedded)==sizeof embedded && !memcmp(embedded,chunk,sizeof embedded));
    }
    HR(root(blob->GetBufferPointer(),blob->GetBufferSize(),(void **)bitonic.GetAddressOf()));
    HR(SLOT(bitonic.Get(),0x30,Name)(bitonic.Get(),L"XDXR BitonicSort"));
    ComPtr<IUnknown> pipelines[2];
    const bool capable=GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettBoundedMortonV1")!=nullptr;
    for (int i=0;i<2;i++) {
        size_t n=load(i?"morton64.bin":"morton32.bin",cs,sizeof cs); CK(n==(i?0x33fcu:0x33a0u));
        // Both hash-pinned containers have their 168-byte RTS0 at chunk offset 0x144.
        CK(!memcmp(cs+0x144,wire+36,176));
        CK(make(utility.Get(),n)==E_INVALIDARG && !bad);
        CK(make(bitonic.Get(),n)==E_INVALIDARG && !bad);
        cs[n-1]^=1; CK(make(morton.Get(),n)==E_NOTIMPL && !bad); cs[n-1]^=1;
        if (!capable) { CK(make(morton.Get(),n)==E_NOTIMPL && !bad); continue; }
        HR(make(morton.Get(),n)); pipelines[i].Attach((IUnknown *)bad);
        HR(SLOT(bad,0x30,Name)(pipelines[i].Get(),i?L"XDXR CalculateMortonCodes 1":L"XDXR CalculateMortonCodes 0"));
    }
    size_t n=load("reset.bin",cs,sizeof cs); CK(n==0xfe8);
    CK(make(morton.Get(),n)==E_INVALIDARG && !bad);
    CK(make(bitonic.Get(),n)==E_INVALIDARG && !bad);
    puts("PASS Bitonic root: all six embedded signatures match; utility and Morton PSOs rejected");
    HR(make(utility.Get(),n)); CK(bad); ((IUnknown *)bad)->Release();

    if (!capable) { puts("PASS Morton backend absent: both exact PSOs declined"); return 0; }
    auto host=xs_bvh_root_host(dev.Get(),morton.Get()); CK(host);
    ComPtr<ID3D12Device> d; HR(host->GetDevice(IID_PPV_ARGS(&d)));
    n=load("morton-test.dxil",cs,sizeof cs); CK(n);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={}; pd.pRootSignature=host; pd.CS={cs,n};
    ComPtr<ID3D12PipelineState> ps; HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps)));
    constexpr UINT runs=6, runBytes=1280, total=160+runs*runBytes;
    UINT expected[total/4]; for (UINT i=0;i<total/4;i++) expected[i]=0x789095abu+377*i;
    const UINT off[]={16,176,464,1248};
    const UINT pos[4][3]={{17,271,63},{512,512,512},{111,405,193},{333,9,21}}; // fractions of 512
    for (UINT run=0;run<runs;run++) {
        BYTE *data=(BYTE *)expected+160+runBytes*run;
        const float scene[]={0,0,0,run?16.f:2.f,run?8.f:0x1p-31f,4};
        memcpy(data+off[3],scene,24);
        for (UINT i=0;i<4;i++) {
            float box[6]; for (UINT j=0;j<3;j++) {
                float center=scene[3+j]*pos[i][j]/512; box[j]=center-.25f; box[j+3]=center+.25f;
            }
            memcpy(data+off[2]+24*i,box,24);
        }
    }
    auto buffer=[&](D3D12_HEAP_TYPE type,ID3D12Resource **out) {
        D3D12_HEAP_PROPERTIES hp={}; hp.Type=type; hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={}; rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width=total;
        rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1; rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        auto state=D3D12_RESOURCE_STATE_COPY_DEST;
        if (type==D3D12_HEAP_TYPE_DEFAULT) { rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS; state=D3D12_RESOURCE_STATE_COPY_DEST; }
        if (type==D3D12_HEAP_TYPE_UPLOAD) state=D3D12_RESOURCE_STATE_GENERIC_READ;
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(out));
    };
    ComPtr<ID3D12Resource> dst,rb,seed;
    HR(buffer(D3D12_HEAP_TYPE_DEFAULT,&dst)); HR(buffer(D3D12_HEAP_TYPE_READBACK,&rb));
    HR(buffer(D3D12_HEAP_TYPE_UPLOAD,&seed)); UINT *mapped;
    HR(seed->Map(0,nullptr,(void **)&mapped)); memcpy(mapped,expected,total); seed->Unmap(0,nullptr);
    D3D12_COMMAND_QUEUE_DESC qd={}; qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> queue; HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> alloc; HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> list; HR(d->CreateCommandList(1,qd.Type,alloc.Get(),ps.Get(),IID_PPV_ARGS(&list)));
    list->CopyResource(dst.Get(),seed.Get());
    D3D12_RESOURCE_BARRIER start={}; start.Transition={dst.Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    list->ResourceBarrier(1,&start);
    UINT k[8]; for (UINT i=0;i<8;i++) k[i]=0x72000201u*(i+1);
    list->SetComputeRootSignature(host); list->SetComputeRoot32BitConstants(0,8,k,0);
    for (UINT i=0;i<5;i++) list->SetComputeRootUnorderedAccessView(i+1,dst->GetGPUVirtualAddress()+32*i);
    list->Dispatch(1,1,1);
    for (UINT i=0;i<40;i++) expected[i]=k[i%8]+1+i/8;
    xs_bvh_hold *holds[runs]={};
    for (UINT run=0;run<runs;run++) {
        bool wide=run&1; UINT mode=run/2;
        UINT64 base=0x48abffffff00ull,elem=base+256;
        UINT header[160]={}; header[3]=2; header[16]=1; header[17]=4; header[20]=3;
        header[64+15]=header[96+15]=1;
        UINT args[]={(UINT)base,(UINT)(base>>32),(UINT)elem,(UINT)(elem>>32),4,mode?5u:0u,0,mode?~0u:0u};
        xs_bvh_range dr={dst.Get(),0x76110000ull+runBytes*run,160+run*runBytes,runBytes};
        // Mode 2: only 12 bytes visible; the scalar load at byte 12 must return zero.
        auto record=[&](){return xs_record_bvh_morton(pipelines[wide].Get(),list.Get(),&dr,off,args,base,header,mode==2?12:sizeof header,&holds[run]);};
        dr.bytes=off[3]+23; CK(record()==E_INVALIDARG && !holds[run]); dr.bytes=runBytes;
        HR(record()); CK(holds[run]); memset(header,0xff,sizeof header);
        UINT *w=expected+dr.resource_offset/4;
        for (UINT i=0;i<(mode?4u:3u);i++) {
            w[off[0]/4+i]=i;
            UINT bits[]={wide?22u:11u,wide?21u:10u,wide?20u:9u};
            if (!run) { bits[0]=15; bits[1]=0; bits[2]=16; } // thin y: redistribute the 31-bit budget
            UINT64 code=0; unsigned dstbit=0;
            for (UINT bit=0;bit<32;bit++) for (UINT axis=0;axis<3;axis++) if(bit<bits[axis]) {
                UINT value=pos[i][axis]==512 ? (1u<<bits[axis])-1 : pos[i][axis]<<(bits[axis]-9);
                code|=(UINT64)((value>>bit)&1)<<dstbit++;
            }
            if (mode==2 || (mode==1 && i>=2)) code=~(UINT64)0;
            memcpy(w+off[1]/4+i*(1+wide),&code,4*(1+wide));
        }
    }
    for(auto &pipeline:pipelines) pipeline.Reset();
    D3D12_RESOURCE_BARRIER b={}; b.Transition={dst.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
    list->ResourceBarrier(1,&b); list->CopyResource(rb.Get(),dst.Get()); HR(list->Close());
    ID3D12CommandList *lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(event);
    HR(queue->Signal(fence.Get(),1)); HR(fence->SetEventOnCompletion(1,event)); CK(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);
    CloseHandle(event); CK(fence->GetCompletedValue()==1);
    for(auto hold:holds) xs_drop_bvh_hold(hold);
    HR(rb->Map(0,nullptr,(void **)&mapped));
    for (UINT i=0;i<total/4;i++) if (mapped[i]!=expected[i]) {
        fprintf(stderr,"FAIL word %u: got %08x expected %08x\n",i,mapped[i],expected[i]); return 1;
    }
    rb->Unmap(0,nullptr);
    puts("PASS Morton root: both embedded RTS0 match, PSO/root pairing checked, both original PSOs, partition and instance codes, bounded scalar miss, 1960 fenced words");
}
