#include "gpu_buffer.hpp"
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
using Microsoft::WRL::ComPtr;
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d %s err=%lu\n",__LINE__,#x,GetLastError());ExitProcess(1);}}while(0)
#define HR(x) do{HRESULT h_=(x);if(FAILED(h_)){fprintf(stderr,"FAIL %d %s hr=%08lx\n",__LINE__,#x,(unsigned long)h_);ExitProcess(1);}}while(0)
extern "C" __declspec(dllimport) HRESULT WINAPI XGCreateTextureComputer(const void *,void **);
extern "C" __declspec(dllimport) HRESULT WINAPI XGSuggestSwizzleMode(const void *,uintptr_t,DWORD *);
using Alloc=void*(WINAPI*)(void*,SIZE_T,DWORD,UINT64,DWORD);
using Copy=HRESULT(WINAPI*)(void*,void*,UINT,UINT,const void*,UINT,UINT);
static constexpr UINT N=32768,Bytes=4*N;
static constexpr SIZE_T span=0x89700000u, offset=8192;
int main(){
    auto alloc=(Alloc)(void*)GetProcAddress(LoadLibraryW(L"xmem.dll"),"XMemVirtualAlloc"); CK(alloc);
    BYTE *s=(BYTE*)alloc(nullptr,0x49400000,0x20003000,0x183,0x22004);
    BYTE *b=(BYTE*)alloc(nullptr,span,0x20003000,0x283,0x2404);CK(s&&b);
    DWORD desc[]={3,0,0,0,256,0,128,0x10001,61,1,0,0,0x300000};
    CK(XGSuggestSwizzleMode(desc,0,&desc[11])==S_OK && desc[11]==0x105);
    desc[2]=4096;
    void **c=nullptr;HR(XGCreateTextureComputer(desc,(void**)&c));auto v=*(void***)c;auto copy=(Copy)v[8];
    ComPtr<ID3D12Device3>d;HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    scarlett_section_info held;HR(scarlett_query_section(0x53430005,b+offset,N,&held));CK(held.section&&held.base==b&&held.offset==offset&&held.view_size==span);
    ImportedBuffer binding; HR(binding.wc(d.Get(),span,held.section,nullptr)); CK(CloseHandle(held.section));
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    ComPtr<ID3D12DescriptorHeap> table;HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&table)));
    D3D12_SHADER_RESOURCE_VIEW_DESC view={};view.Format=DXGI_FORMAT_R32_TYPELESS;
    view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Buffer.FirstElement=offset/4;view.Buffer.NumElements=N/4;view.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
    d->CreateShaderResourceView(binding.resource(),&view,table->GetCPUDescriptorHandleForHeapStart());
    D3D12_DESCRIPTOR_RANGE range={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};
    D3D12_ROOT_PARAMETER p[2]={};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={1,&range};
    p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC rs={2,p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob;HR(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));
    ComPtr<ID3D12RootSignature> root;HR(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    FILE *f=fopen("sample.dxil","rb");CK(f);BYTE dxil[8192];size_t n=fread(dxil,1,sizeof dxil,f);CK(n && feof(f));fclose(f);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=root.Get();pd.CS={dxil,n};
    ComPtr<ID3D12PipelineState> ps;HR(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps)));
    D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.CreationNodeMask=hp.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=Bytes;rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;
    rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> out,read;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&out)));
    hp.Type=D3D12_HEAP_TYPE_READBACK;rd.Width=2*Bytes;rd.Flags=D3D12_RESOURCE_FLAG_NONE;HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read)));
    D3D12_COMMAND_QUEUE_DESC qd={};qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> q;HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));ComPtr<ID3D12Fence> done,gate;
    HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    ComPtr<ID3D12CommandAllocator> a;HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    ComPtr<ID3D12GraphicsCommandList> l;HR(d->CreateCommandList(0,qd.Type,a.Get(),ps.Get(),IID_PPV_ARGS(&l)));
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(ev);
    memset(b,0xd7,65536);
    for(unsigned pass=0;pass<2;pass++){
        if(pass){HR(a->Reset());HR(l->Reset(a.Get(),ps.Get()));}
        auto transition=[&](D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){
            D3D12_RESOURCE_BARRIER br={};br.Transition={out.Get(),0,from,to};l->ResourceBarrier(1,&br);
        };
        if(pass)transition(D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        l->SetComputeRootSignature(root.Get());ID3D12DescriptorHeap *heaps[]={table.Get()};
        l->SetDescriptorHeaps(1,heaps);l->SetComputeRootDescriptorTable(0,table->GetGPUDescriptorHandleForHeapStart());l->SetComputeRootUnorderedAccessView(1,out->GetGPUVirtualAddress());
        l->Dispatch(16,16,1);transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        l->CopyBufferRegion(read.Get(),pass*Bytes,out.Get(),0,Bytes);HR(l->Close());
        HR(q->Wait(gate.Get(),pass+1));ID3D12CommandList *batch[]={l.Get()};q->ExecuteCommandLists(1,batch);HR(q->Signal(done.Get(),pass+1));
        HR(done->SetEventOnCompletion(pass+1,ev));CK(WaitForSingleObject(ev,50)==WAIT_TIMEOUT);
        // Change the original only after submission so a recording-time snapshot fails.
        for(UINT i=0;i<N;i++)s[i]=pass?i>>8:i&255;
        HR(copy(c,b+offset,0,0,s,256,N));MemoryBarrier();
        if(pass){
            for(UINT i=0;i<65536;i++)if(i<offset || i>=offset+N)CK(b[i]==0xd7);
            CK(VirtualFree(b,0,MEM_RELEASE));scarlett_section_info stale;CK(FAILED(scarlett_query_section(0x53430005,b+offset,N,&stale)));
            CK(VirtualAlloc(b,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==b);memset(b,0xad,65536);MemoryBarrier();
        }
        HR(gate->Signal(pass+1));CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0);
        float *map;HR(read->Map(0,nullptr,(void**)&map));for(unsigned i=0;i<N;i++){
            float value=map[pass*N+i];unsigned want=pass?i>>8:i&255;
            if(!std::isfinite(value)||fabs(value-want/255.f)>1e-7){fprintf(stderr,"pass %u index %u got %g expected %u/255\n",pass,i,value,want);ExitProcess(1);}
        }read->Unmap(0,nullptr);
        printf("PASS pass %u: all 32768 %c coordinates as R8_UNORM; checked gate held before CPU producer\n",pass,pass?'Y':'X');
    }
    for(UINT i=0;i<65536;i++)CK(b[i]==0xad);
    CK(VirtualFree(b,0,MEM_RELEASE)&&VirtualFree(s,0,MEM_RELEASE));CloseHandle(ev);
    CK(((ULONG(WINAPI*)(void*))v[1])(c)==0);HR(d->GetDeviceRemovedReason());
    puts("PASS gated direct raw SRV read of original WC pool after exact VA reuse; replacement and adjacent byte guards intact");
}
