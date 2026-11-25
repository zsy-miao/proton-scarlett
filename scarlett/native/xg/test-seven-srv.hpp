// Fixture-only: bind the SRV produced from the guest slot, keeping the recorded use until its fence.
#include "test-seven-srv-dxil.h"
extern "C" void fixture_seven_srv(IUnknown*,const void*,const void*,SIZE_T);
extern "C" char fixture_srv_return;
extern "C" HRESULT xs_write_address_srv(IUnknown*,const void*,const void*,SIZE_T);
extern "C" HRESULT test_seven_wrong_site(IUnknown*,const void*,const void*,SIZE_T);
struct xs_texture_use;
extern "C" HRESULT xs_refresh_wc_texture(IUnknown*,SIZE_T,ID3D12GraphicsCommandList*,ID3D12DescriptorHeap**,xs_texture_use**);
extern "C" void xs_drop_texture_use(xs_texture_use*);
constexpr UINT_PTR srv_site=0x4ecc821;
constexpr UINT srv_bytes=7*72*128*sizeof(DWORD);
static DWORD seven_bits(UINT mip,UINT x,UINT y)
{
    return 0x3f800000u+(mip<<16)+(y<<8)+x; // Finite, unique float bit pattern for each texel.
}
struct SevenLoad {
    ComPtr<ID3D12PipelineState> ps;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12Resource> out;
    xs_texture_use *hold=nullptr;
    int record(ID3D12Device *d,ID3D12GraphicsCommandList *l,IUnknown *heap,SIZE_T slot,ID3D12Resource *read,UINT at){
        ID3D12DescriptorHeap *table=nullptr;
        auto refresh=[&]{return xs_refresh_wc_texture(heap,slot,l,&table,&hold);};
        UINT64 *tag=(UINT64*)slot;
        tag[0]^=1;CK(FAILED(refresh()) && !table && !hold);tag[0]^=1;
        CK(refresh()==S_OK && table && hold);
        CK(SUCCEEDED(d->CreateRootSignature(0,seven_dxil,sizeof seven_dxil,IID_PPV_ARGS(&root))));
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd={};pd.pRootSignature=root.Get();pd.CS={seven_dxil,sizeof seven_dxil};
        CK(SUCCEEDED(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&ps))));
        D3D12_HEAP_PROPERTIES hp={};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        auto rd=read->GetDesc();rd.Width=srv_bytes;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        CK(SUCCEEDED(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&out))));
        l->SetPipelineState(ps.Get());l->SetComputeRootSignature(root.Get());l->SetDescriptorHeaps(1,&table);
        l->SetComputeRootDescriptorTable(0,table->GetGPUDescriptorHandleForHeapStart());
        l->SetComputeRootUnorderedAccessView(1,out->GetGPUVirtualAddress());l->Dispatch(16,9,7);
        D3D12_RESOURCE_BARRIER br={};br.Transition={out.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
        l->ResourceBarrier(1,&br);l->CopyBufferRegion(read,at,out.Get(),0,srv_bytes);
        return 0;
    }
    int verify(const DWORD *p){
        UINT count=0;
        for(UINT m=0;m<7;m++)for(UINT y=0;y<(72u>>m);y++)for(UINT x=0;x<(128u>>m);x++){
            DWORD want=seven_bits(m,x,y),got=p[(m*72+y)*128+x];
            if(!x && !y)want^=0x57; // The original WC backing was modified after submission.
            if(got!=want){fprintf(stderr,"FAIL seven230 mip=%u x=%u y=%u got=%08lx want=%08lx\n",m,x,y,got,want);return 1;}
            ++count;
        }
        xs_drop_texture_use(hold);hold=nullptr;
        printf("PASS seven230 shader: mips=7 texels=%u exact R32_FLOAT bits, including late WC write\n",count);
        return 0;
    }
};
