// Fixture setup for the same shader and fenced readback as the RAW38 control.
extern "C" {
__declspec(dllimport) BOOL WINAPI XMemAllocatePhysicalPages(ULONGLONG,PULONG_PTR,PULONG_PTR);
__declspec(dllimport) BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemMapPhysicalPages(void *,ULONG_PTR,PULONG_PTR);
__declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *,SIZE_T,DWORD,ULONGLONG,DWORD);
HRESULT xs_descriptor_slot(IUnknown*,SIZE_T,D3D12_CPU_DESCRIPTOR_HANDLE*,D3D12_GPU_DESCRIPTOR_HANDLE*);
}
struct Raw32Setup {
    ComPtr<ID3D12Device3>d;
    ComPtr<ID3D12DescriptorHeap>visible;
    ComPtr<IUnknown>guest,placed,src;
    BYTE *at[5]; ULONG_PTR ids[2960]; const UINT pages[5]={160,16,432,752,1600},first[5]={2352,2512,2528,0,752};
    SIZE_T slot; DWORD rd[13]={1,0,0,0,128,0,1,0x10001,0,1,0,1,0};
    DWORD view[16]={39,1,0x1688,0x6fff,0,0,32,0,1,0,0,0,0x16};
    Raw32Setup(){
        ULONG_PTR n=2960;
        BYTE *v=(BYTE*)XMemVirtualAlloc(nullptr,n*65536,0x42000,7,PAGE_NOACCESS);CK(v);
        const DWORD policy[]={0x1004,0x1404,0x8404,0x2404,0x2004};
        for(UINT i=0;i<5;i++){at[i]=v;if(i<4)CK(VirtualFree(v,pages[i]*65536,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER));v+=pages[i]*65536;}
        for(UINT i=0;i<5;i++)CK(XMemVirtualAlloc(at[i],pages[i]*65536,0x20406000,0x27,policy[i])==at[i]);
        CK(XMemAllocatePhysicalPages(0x280,&n,ids) && n==2960);
        for(UINT i=2;i<5;i++)CK(XMemMapPhysicalPages(at[i],pages[i],ids+first[i])==at[i]);
        DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
        const IID id={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
        HR(D3D12XboxCreateDevice(nullptr,params,id,(void**)guest.GetAddressOf()));
        HR(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
        using Place=HRESULT(WINAPI*)(IUnknown*,UINT64,const void*,UINT,const void*,REFIID,void**);
        auto make=SLOT(guest.Get(),0x428,Place);
        DWORD arena[]={1,0,0,0,174u<<20,0,1,0x10001,0,1,0,1,0x20004};
        HR(make(guest.Get(),(SIZE_T)at[2],arena,UINT_MAX,nullptr,IID_ID3D12Resource,(void**)placed.GetAddressOf()));
        UINT hd[]={0,0xc38,0,1};HR(SLOT(guest.Get(),0x70,Make)(guest.Get(),hd,IID_ID3D12DescriptorHeap,(void**)src.GetAddressOf()));
        slot=SLOT(src.Get(),0x48,Start)(src.Get());
        BYTE *p=at[2]+0x3de20;memcpy(view+10,&p,sizeof p);
        view[3]=0xf1e2d3c4;view[9]=0x84510276;view[14]=0xa9988776;
        HR(xs_write_address_srv(guest.Get(),rd,view,slot));
        auto serial=*(UINT64*)slot;CK(serial);
        for(UINT i:{0u,6u,8u,10u,12u}){view[i]^=1;CK(FAILED(xs_write_address_srv(guest.Get(),rd,view,slot)));view[i]^=1;}
        CK(*(UINT64*)slot==serial);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu;HR(xs_descriptor_slot(src.Get(),slot,&cpu,nullptr));
        D3D12_DESCRIPTOR_HEAP_DESC bank={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
        HR(d->CreateDescriptorHeap(&bank,IID_PPV_ARGS(&visible)));
        d->CopyDescriptorsSimple(1,visible->GetCPUDescriptorHandleForHeapStart(),cpu,bank.Type);
        DWORD nullview[10]={42,4,0x1688};HR(xs_write_view(guest.Get(),0x90,nullptr,nullview,slot));
        CK(*(UINT64*)slot!=serial);
    }
    void replace(){
        DWORD *data=(DWORD*)(at[2]+0x3de20);
        for(UINT i=0;i<64;i++)data[i]=0x11002347+i*73;
        MemoryBarrier();
        for(UINT i=2;i<5;i++)CK(XMemMapPhysicalPages(at[i],pages[i],nullptr)==at[i]);
        CK(XMemFreePhysicalPages(2960,ids));
        for(auto v:at)CK(VirtualFree(v,0,MEM_RELEASE));
        CK(VirtualAlloc(at[2],27u<<20,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==at[2]);
        memset(data,0xcd,256);MemoryBarrier();
        CK(FAILED(xs_write_address_srv(guest.Get(),rd,view,slot)));
    }
    ~Raw32Setup(){CK(VirtualFree(at[2],0,MEM_RELEASE));}
};
