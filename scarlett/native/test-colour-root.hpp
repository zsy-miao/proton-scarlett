static ComPtr<IUnknown> colour_root(IUnknown *d){
    D3D12_DESCRIPTOR_RANGE1 range={D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,27,1,D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE,UINT_MAX};
    D3D12_ROOT_PARAMETER1 p={};p.DescriptorTable={1,&range};
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc={};desc.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1={1,&p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ComPtr<ID3DBlob> blob;HR(D3D12SerializeVersionedRootSignature(&desc,&blob,nullptr));
    using Make=HRESULT(WINAPI*)(IUnknown*,UINT,const void*,SIZE_T,REFIID,void**);
    ComPtr<IUnknown> r;HR(((Make)(*(void***)d)[16])(d,1,blob->GetBufferPointer(),blob->GetBufferSize(),IID_ID3D12RootSignature,(void**)r.GetAddressOf()));return r;
}
