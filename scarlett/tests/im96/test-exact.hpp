extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void*,const void*,REFIID,void**);
extern "C" ID3D12RootSignature *test_retain_im96(IUnknown*,IUnknown*);
extern "C" ID3D12RootSignature *xs_retain_colour_root(IUnknown*,IUnknown*);
extern "C" HRESULT xs_im96_private_layout(IUnknown*,IUnknown*,ID3D12RootSignature**);
static ComPtr<IUnknown> owner(){
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> o;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)o.GetAddressOf()));return o;
}
static ComPtr<ID3D12RootSignature> exact_root(IUnknown *supplied=nullptr){
    ComPtr<IUnknown> d=supplied;if(!d)d=owner();auto other=owner();
    using Create=HRESULT(WINAPI*)(IUnknown*,UINT,const void*,SIZE_T,REFIID,void**);
    auto create=(Create)(*(void***)d.Get())[0x80/8];
    auto wire=file("exact.rs",1304),base=file("B27.rs",112);
    ComPtr<IUnknown> a,b;
    HR(create(d.Get(),1,wire.data(),wire.size(),IID_ID3D12RootSignature,(void**)a.GetAddressOf()));
    HR(create(d.Get(),1,base.data(),base.size(),IID_ID3D12RootSignature,(void**)b.GetAddressOf()));
    CK(a.Get()!=b.Get() && !test_retain_im96(other.Get(),a.Get()) && !test_retain_im96(d.Get(),b.Get()) &&
       !xs_retain_colour_root(d.Get(),a.Get()) && xs_retain_colour_root(d.Get(),b.Get()));
    b->Release();
    auto original=xs_retain_colour_root(d.Get(),b.Get());CK(original);b->Release();
    ComPtr<ID3D12RootSignature> private_root;
    CK(xs_im96_private_layout(other.Get(),b.Get(),private_root.GetAddressOf())==E_INVALIDARG && !private_root);
    CK(xs_im96_private_layout(d.Get(),a.Get(),private_root.GetAddressOf())==E_INVALIDARG && !private_root);
    HR(xs_im96_private_layout(d.Get(),b.Get(),private_root.GetAddressOf()));
    CK(private_root.Get()!=original && xs_retain_colour_root(d.Get(),b.Get())==original && !test_retain_im96(d.Get(),b.Get()));
    b->Release();b->Release(); /* registry check and private-layout success pin */
    ComPtr<ID3D12RootSignature> host=test_retain_im96(d.Get(),a.Get());CK(host);a->Release();
    ComPtr<ID3D12VersionedRootSignatureDeserializer> de;
    HR(D3D12CreateVersionedRootSignatureDeserializer(wire.data(),wire.size(),IID_PPV_ARGS(&de)));
    auto desc=*de->GetUnconvertedRootSignatureDesc();auto &rs=desc.Desc_1_1;
    CK(desc.Version==D3D_ROOT_SIGNATURE_VERSION_1_1 && rs.NumStaticSamplers==22 && rs.NumParameters==1);
    auto p=rs.pParameters[0];CK(p.DescriptorTable.NumDescriptorRanges==3);rs.pParameters=&p;
    D3D12_DESCRIPTOR_RANGE1 ranges[3];memcpy(ranges,p.DescriptorTable.pDescriptorRanges,sizeof ranges);p.DescriptorTable.pDescriptorRanges=ranges;
    D3D12_STATIC_SAMPLER_DESC ss[22];memcpy(ss,rs.pStaticSamplers,sizeof ss);rs.pStaticSamplers=ss;
    auto reject=[&](){ComPtr<ID3DBlob> w;HR(D3D12SerializeVersionedRootSignature(&desc,&w,nullptr));
        void*out=a.Get();CK(create(d.Get(),1,w->GetBufferPointer(),w->GetBufferSize(),IID_ID3D12RootSignature,&out)==E_NOTIMPL && !out);};
    for(auto &s:ss){s.MipLODBias=1;reject();s.MipLODBias=0;}
    auto s14=ss[2];CK(s14.ShaderRegister==14 && s14.Filter==D3D12_FILTER_ANISOTROPIC && s14.MaxAnisotropy==4 &&
        s14.AddressU==D3D12_TEXTURE_ADDRESS_MODE_WRAP && s14.AddressV==s14.AddressU && s14.AddressW==s14.AddressU &&
        s14.ComparisonFunc==D3D12_COMPARISON_FUNC_NEVER && s14.MaxLOD==12 && !s14.MinLOD && !s14.MipLODBias);
    ss[2].AddressU=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;reject();ss[2]=s14;
    for(auto &r:ranges){++r.RegisterSpace;reject();--r.RegisterSpace;}
    std::swap(ranges[0],ranges[1]);reject();std::swap(ranges[0],ranges[1]);
    rs.Flags=D3D12_ROOT_SIGNATURE_FLAG_NONE;reject();
    void*out=a.Get();CK(create(d.Get(),0,wire.data(),wire.size(),IID_ID3D12RootSignature,&out)==E_NOTIMPL && !out);
    auto stale=a.Get();a.Reset();CK(!test_retain_im96(d.Get(),stale));
    puts("PASS im96 private layout: wrong owner/kind rejected; original b27 identity, host root and kind unchanged");
    puts("PASS +80 im96: distinct b27/im96 kinds and owners, exact table, all 22 near-miss samplers rejected, release unlinks");return private_root;
}
