// Fixture setup for the same shader and fenced readback as the RAW38 control.
extern "C" {
__declspec(dllimport) BOOL WINAPI XMemAllocatePhysicalPages(ULONGLONG,PULONG_PTR,PULONG_PTR);
__declspec(dllimport) BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemMapPhysicalPages(void *,ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *,SIZE_T,DWORD,ULONGLONG,DWORD);
HRESULT xs_descriptor_slot(IUnknown*,SIZE_T,D3D12_CPU_DESCRIPTOR_HANDLE*,D3D12_GPU_DESCRIPTOR_HANDLE*);
IUnknown *test_pin_raw(IUnknown*,SIZE_T,UINT64);
void test_lease(UINT_PTR,UINT,UINT,UINT64);
}
struct Raw32Setup {
    ComPtr<ID3D12Device3>d;
    ComPtr<ID3D12DescriptorHeap>visible;
    ComPtr<IUnknown>guest,placed,src,fence_hold;
    BYTE *at[5]; ULONG_PTR ids[2960]; const UINT pages[5]={160,16,432,752,1600},first[5]={2352,2512,2528,0,752};
    SIZE_T slot; DWORD rd[13]={1,0,0,0,128,0,1,0x10001,0,1,0,1,0};
    DWORD view[16]={39,1,0x1688,0x6fff,0,0,32,0,1,0,0,0,0x16};
    Raw32Setup(IUnknown *use_owner=nullptr,bool keep=false){
        ULONG_PTR n=2960;
        BYTE *v=(BYTE*)XMemVirtualAlloc(nullptr,n*65536,0x42000,7,PAGE_NOACCESS);CK(v);
        const DWORD policy[]={0x1004,0x1404,0x8404,0x2404,0x2004};
        for(UINT i=0;i<5;i++){at[i]=v;if(i<4)CK(VirtualFree(v,pages[i]*65536,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER));v+=pages[i]*65536;}
        for(UINT i=0;i<5;i++)CK(XMemVirtualAlloc(at[i],pages[i]*65536,0x20406000,0x27,policy[i])==at[i]);
        CK(XMemAllocatePhysicalPages(0x280,&n,ids) && n==2960);
        for(UINT i=2;i<5;i++)CK(XMemMapPhysicalPages(at[i],pages[i],ids+first[i])==at[i]);
        DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
        const IID id={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
        if(use_owner)guest=use_owner;else HR(D3D12XboxCreateDevice(nullptr,params,id,(void**)guest.GetAddressOf()));
        HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
        using Place=HRESULT(WINAPI*)(IUnknown*,UINT64,const void*,UINT,const void*,REFIID,void**);
        auto make=SLOT(guest.Get(),0x428,Place);
        DWORD arena[]={1,0,0,0,174u<<20,0,1,0x10001,0,1,0,1,0x20004};
        HR(make(guest.Get(),(SIZE_T)at[2],arena,UINT_MAX,nullptr,IID_ID3D12Resource,(void**)placed.GetAddressOf()));
        UINT hd[]={0,0xc38,0,1};HR(SLOT(guest.Get(),0x70,Make)(guest.Get(),hd,IID_ID3D12DescriptorHeap,(void**)src.GetAddressOf()));
        slot=SLOT(src.Get(),0x48,Start)(src.Get())+690*32;
        BYTE *p=at[2]+0x3de20;memcpy(view+10,&p,sizeof p);
        view[3]=0xf1e2d3c4;view[9]=0x84510276;view[14]=0xa9988776;
        HR(xs_write_address_srv(guest.Get(),rd,view,slot));
        if(keep)return;
        auto serial=*(UINT64*)slot;CK(serial);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu;HR(xs_descriptor_slot(src.Get(),slot,&cpu,nullptr));
        D3D12_DESCRIPTOR_HEAP_DESC bank={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
        HR(d->CreateDescriptorHeap(&bank,IID_PPV_ARGS(&visible)));
        auto old=visible->GetCPUDescriptorHandleForHeapStart();old.ptr+=d->GetDescriptorHandleIncrementSize(bank.Type);
        d->CopyDescriptorsSimple(1,old,cpu,bank.Type);
        p=at[2]+0x5de20;memcpy(view+10,&p,sizeof p);
        view[3]=0x6fff;view[9]=view[14]=0;
        BYTE saved[32];memcpy(saved,(void*)slot,sizeof saved);
        auto reject=[&](SIZE_T target){
            CK(FAILED(xs_write_address_srv(guest.Get(),rd,view,target)));
            CK(!memcmp(saved,(void*)slot,sizeof saved));
        };
        UINT_PTR base=(UINT_PTR)at[2];
        test_lease(base,0,4,~1ull);reject(slot);
        test_lease(base,1,3,~1ull);reject(slot);
        test_lease(base,1,4,0);reject(slot);
        test_lease(base,1,4,~1ull);reject(slot+32);
        *(UINT64*)slot^=1;CK(FAILED(xs_write_address_srv(guest.Get(),rd,view,slot)));*(UINT64*)slot^=1;
        ComPtr<IUnknown> duplicate;
        HR(make(guest.Get(),(SIZE_T)at[2],arena,UINT_MAX,nullptr,IID_ID3D12Resource,(void**)duplicate.GetAddressOf()));
        CK(duplicate.Get()!=placed.Get()); reject(slot); duplicate.Reset();
        for (SIZE_T bad:{(SIZE_T)at[3]-16,(SIZE_T)at[2]+(174u<<20)-16,~SIZE_T(15)}) {
            memcpy(view+10,&bad,sizeof bad);reject(slot);
        }
        memcpy(view+10,&p,sizeof p);
        view[4]=1;reject(slot);view[4]=0;
        rd[4]-=4;reject(slot);rd[4]+=4;
        reject(slot+1);
        for(UINT i:{0u,6u,8u,10u,12u}){view[i]^=1;CK(FAILED(xs_write_address_srv(guest.Get(),rd,view,slot)));view[i]^=1;}
        CK(*(UINT64*)slot==serial);
        SLOT(guest.Get(),0x230,Writer)(guest.Get(),rd,view,slot);
        CK(*(UINT64*)slot!=serial);serial=*(UINT64*)slot;
        CK(FAILED(xs_write_address_srv(guest.Get(),rd,view,slot)) && *(UINT64*)slot==serial);
        // Only the registered descriptor owns the original now. Pin it for the private host descriptor.
        auto *identity=placed.Detach();CK(identity->Release()==1);
        CK(!test_pin_raw(src.Get(),slot,serial-1));
        fence_hold.Attach(test_pin_raw(src.Get(),slot,serial));CK(fence_hold.Get()==identity);
        d->CopyDescriptorsSimple(1,visible->GetCPUDescriptorHandleForHeapStart(),cpu,bank.Type);
        DWORD nullview[10]={42,4,0x1688};HR(xs_write_view(guest.Get(),0x90,nullptr,nullview,slot));
        CK(*(UINT64*)slot!=serial);
        CK(!test_pin_raw(src.Get(),slot,serial));
        CK(identity->AddRef()==2 && identity->Release()==1);
    }
    void replace(const void *object64=nullptr){
        DWORD *data=(DWORD*)(at[2]+0x5de20);
        DWORD *old=(DWORD*)(at[2]+0x3de20);
        for(UINT i=0;i<64;i++)old[i]=0x27940013+i*13;
        if(object64)memcpy(data,object64,64);
        else for(UINT i=0;i<64;i++)data[i]=0x11002347+i*73;
        MemoryBarrier();
        for(UINT i=2;i<5;i++)CK(XMemMapPhysicalPages(at[i],pages[i],nullptr)==at[i]);
        CK(XMemFreePhysicalPages(2960,ids));
        for(auto v:at)CK(VirtualFree(v,0,MEM_RELEASE));
        CK(VirtualAlloc(at[2],27u<<20,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==at[2]);
        memset(data,0xcd,256);MemoryBarrier();
        UINT64 before=*(UINT64*)slot;
        CK(FAILED(xs_write_address_srv(guest.Get(),rd,view,slot)) && *(UINT64*)slot==before);
        src.Reset();
    }
    ~Raw32Setup(){CK(VirtualFree(at[2],0,MEM_RELEASE));}
};
