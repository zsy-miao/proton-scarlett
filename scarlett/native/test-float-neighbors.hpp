/* All three placements go through +428, including the collisions. A second overlapping
 * section handle on RTX and the native heap on RADV must find the same live lease. */
static int check_neighbors(IUnknown *dev,BYTE *base,bool native)
{
    constexpr UINT start=0xf000000;
    const UINT a=native?0x880000:0x800000,b=native?0x1010000:0x1000000,f=native?0x1ce0000:0x1e00000;
    using Place=HRESULT(WINAPI*)(IUnknown*,UINT64,const void*,UINT,const void*,REFIID,void**);
    auto place=(Place)((*(void***)dev)[0x428/8]);
    auto make=[&](UINT off,DXGI_FORMAT fmt,IUnknown **out){
        bool fp=fmt==DXGI_FORMAT_R16G16B16A16_FLOAT,alpha=fmt==DXGI_FORMAT_A8_UNORM;
        DWORD wire[]={3,0,0,0,fp?2560u:alpha?3840u:2048u,0,fp?1440u:alpha?2160u:2048u,0x10001,(DWORD)fmt,1,0,0,fp?0x300005u:0x300001u};
        D3D12_CLEAR_VALUE cv={};cv.Format=fmt;
        return place(dev,(UINT_PTR)base+off,wire,fp?3:6,&cv,IID_ID3D12Resource,(void**)out);
    };
    const auto bg=DXGI_FORMAT_B8G8R8A8_UNORM,fp=DXGI_FORMAT_R16G16B16A16_FLOAT;
    ComPtr<IUnknown> alpha;HR(make(start,DXGI_FORMAT_A8_UNORM,alpha.GetAddressOf()));
    IUnknown *reject=(IUnknown*)1;
    if(!native)CK(make(start,fp,&reject)==DXGI_ERROR_INVALID_CALL && !reject);
    for(UINT order=0;order<3;order++) {
        ComPtr<IUnknown> live[3];UINT pos=start+a;
        for(UINT i=0;i<3;i++) {
            bool isFloat=i==order;HR(make(pos,isFloat?fp:bg,live[i].GetAddressOf()));
            if(i==0){CK(make(pos,isFloat?bg:fp,&reject)==DXGI_ERROR_INVALID_CALL && !reject);
                if(isFloat)CK(make(pos+b,bg,&reject)==DXGI_ERROR_INVALID_CALL && !reject);}
            pos+=isFloat?f:b;
        }
    }
    puts("PASS live A8 + float and two BGRA in all three orders; cross-format overlap rejected, released ranges reusable");return 0;
}
