/* One call sequence, borrowed only by the pinned writer. No startup-list
 * vtable changes. Trusted binding inputs must be pinned before guest entry. */
#include <cstddef>
extern "C" HRESULT clear_test_resolve(IUnknown*,IUnknown*,IUnknown*,IUnknown*,SIZE_T,UINT64,ID3D12Resource*,IUnknown*,ID3D12Resource*,ID3D12Device*);
struct ClearBindings {
    ComPtr<IUnknown> device,pso,root,heap,generation;
    ComPtr<ID3D12Resource> target,source;
    SIZE_T slot;
    UINT64 serial,alias,offset;
};
struct ClearRecorder {
    struct Guest { void **table; BYTE scratch[0x564-8]; };
    struct Prefix { ClearRecorder *owner; Guest guest; };
    static_assert(offsetof(Prefix,guest)==8 && sizeof(Guest)==0x568,"x64 guest layout");
    BYTE *reservation=nullptr; Guest *guest=nullptr;
    ClearBindings b; ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Resource> snapshot;
    ComPtr<ID3D12DescriptorHeap> privateRTV;
    D3D12_RECT box{}; D3D12_VIEWPORT vp{};
    unsigned step=0; HRESULT status=S_OK;
    ClearRecorder(const ClearBindings& bindings,ID3D12GraphicsCommandList *l):b(bindings),list(l){
        SYSTEM_INFO info;GetSystemInfo(&info);
        reservation=(BYTE*)VirtualAlloc(nullptr,2*info.dwPageSize,MEM_RESERVE,PAGE_NOACCESS);
        if(!reservation || !VirtualAlloc(reservation,info.dwPageSize,MEM_COMMIT,PAGE_READWRITE)){status=E_OUTOFMEMORY;return;}
        auto frame=(Prefix*)(reservation+info.dwPageSize-sizeof(Prefix));
        frame->owner=this;guest=&frame->guest;guest->table=methods();
    }
    ~ClearRecorder(){if(reservation)VirtualFree(reservation,0,MEM_RELEASE);}
    ClearRecorder(const ClearRecorder&)=delete;
    static ClearRecorder *self(Guest *g){return ((Prefix*)((BYTE*)g - offsetof(Prefix,guest)))->owner;}
    bool next(unsigned s,bool valid) {
        if(FAILED(status)) return false;
        if(step!=s || !valid){status=E_INVALIDARG;return false;} ++step;return true;
    }
    static bool read(void *out,const void *in,size_t n){SIZE_T got;return in && ReadProcessMemory(GetCurrentProcess(),in,out,n,&got) && got==n;}
    static void topology(Guest*g,UINT v){auto c=self(g);c->next(0,v==4);}
    static void pipeline(Guest*g,IUnknown*p){auto c=self(g);c->next(1,p==c->b.pso.Get());}
    static void signature(Guest*g,IUnknown*r){auto c=self(g);c->next(2,r==c->b.root.Get());}
    static void targets(Guest*g,UINT count,const SIZE_T *slots,BOOL range,void *z){auto c=self(g);SIZE_T slot;
        c->next(3,count==1 && !range && !z && read(&slot,slots,sizeof slot) && slot==c->b.slot);}
    static void depth(Guest*g,float lo,float hi){auto c=self(g);c->next(4,lo==0 && hi==1);}
    static void scissor(Guest*g,UINT n,const D3D12_RECT*r){auto c=self(g);c->next(5,n==1 && read(&c->box,r,sizeof c->box));}
    static void viewport(Guest*g,UINT n,const D3D12_VIEWPORT*v){auto c=self(g);c->next(6,n==1 && read(&c->vp,v,sizeof c->vp));}
    static void srv(Guest*g,UINT index,UINT64 va){auto c=self(g);c->next(7,index==0 && va>=c->b.alias && va-c->b.alias==c->b.offset);}
    static void draw(Guest*g,UINT v,UINT n,UINT first,UINT inst){auto c=self(g);if(c->next(8,v==3 && n==1 && !first && !inst))c->status=c->record();}
    static void unsupported(){RaiseFailFastException(nullptr,nullptr,0);}
    static void **methods(){static void *t[0x480/8]; static const bool init=[](){
        for(auto &x:t)x=(void*)unsupported;
        t[0xa0/8]=(void*)topology;t[0xc8/8]=(void*)pipeline;t[0x110/8]=(void*)signature;t[0x190/8]=(void*)targets;
        t[0x230/8]=(void*)depth;t[0xb0/8]=(void*)scissor;t[0xa8/8]=(void*)viewport;t[0x160/8]=(void*)srv;t[0x60/8]=(void*)draw;return true;
    }();(void)init;return t;}
    virtual HRESULT record(){
        if(!b.device || !b.pso || !b.root || !b.generation || !b.source || !b.heap || !b.target || !list)return E_INVALIDARG;
        if(list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT)return E_INVALIDARG;
        ComPtr<ID3D12Device> d; HRESULT hr=list->GetDevice(IID_PPV_ARGS(&d));if(FAILED(hr))return hr;
        hr=clear_test_resolve(b.device.Get(),b.pso.Get(),b.root.Get(),b.heap.Get(),b.slot,b.serial,b.target.Get(),b.generation.Get(),b.source.Get(),d.Get());if(FAILED(hr))return hr;
        auto rt=b.target->GetDesc(),src=b.source->GetDesc();
        if(rt.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || rt.Format!=DXGI_FORMAT_R8G8B8A8_UNORM || rt.SampleDesc.Count!=1 || rt.MipLevels!=1 || rt.DepthOrArraySize!=1 || !(rt.Flags&D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ||
           src.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || (b.offset&15) || b.offset>src.Width || src.Width-b.offset<100 ||
           !(vp.TopLeftX>=0 && vp.TopLeftY>=0 && vp.Width>0 && vp.Height>0 && vp.TopLeftX+vp.Width<=rt.Width && vp.TopLeftY+vp.Height<=rt.Height && vp.MinDepth==0 && vp.MaxDepth==1) ||
           box.left<0 || box.top<0 || box.right<=box.left || box.bottom<=box.top || (UINT)box.right>rt.Width || (UINT)box.bottom>rt.Height)return E_INVALIDARG;
        D3D12_HEAP_PROPERTIES hp;D3D12_HEAP_FLAGS flags;
        hr=b.source->GetHeapProperties(&hp,&flags);if(FAILED(hr))return hr;
        if(hp.Type!=D3D12_HEAP_TYPE_UPLOAD)return E_INVALIDARG;
        // Snapshot while the writer owns this CPU-only allocation. Its cleanup may recycle it before submit.
        DWORD words[25];BYTE *data;D3D12_RANGE range={(SIZE_T)b.offset,(SIZE_T)b.offset+sizeof words},none={};
        hr=b.source->Map(0,&range,(void**)&data);if(FAILED(hr))return hr;
        memcpy(words,data+b.offset,sizeof words);b.source->Unmap(0,&none);
        const DWORD header[]={0x8e3,0,0,0xee4};const float xy[]={-1,1,-1,-3,3,1};
        if(memcmp(words,header,16))return E_INVALIDARG;
        for(unsigned i=0;i<3;i++){
            if(memcmp(words+4+i*7,xy+i*2,8) || memcmp(words+6+i*7,words+6,20))return E_INVALIDARG;
        }
        float zrgba[5];memcpy(zrgba,words+6,20);for(float f:zrgba)if(!(f>=0 && f<=1))return E_INVALIDARG;
        src.Width=sizeof words;src.Alignment=0;src.Flags=D3D12_RESOURCE_FLAG_NONE;
        hr=d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&src,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&snapshot));if(FAILED(hr))return hr;
        hr=snapshot->Map(0,&none,(void**)&data);if(FAILED(hr))return hr;memcpy(data,words,sizeof words);snapshot->Unmap(0,nullptr);
        D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};
        hr=d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&privateRTV));if(FAILED(hr))return hr;
        auto handle=privateRTV->GetCPUDescriptorHandleForHeapStart();d->CreateRenderTargetView(b.target.Get(),nullptr,handle);
        list->SetPipelineState(clear_test_host(b.pso.Get()));list->SetGraphicsRootSignature(xs_bvh_root_host(b.device.Get(),b.root.Get()));
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->OMSetRenderTargets(1,&handle,FALSE,nullptr);
        list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&box);list->SetGraphicsRootShaderResourceView(0,snapshot->GetGPUVirtualAddress());list->DrawInstanced(3,1,0,0);
        return S_OK;
    }
};
