static BOOL atlas_desc(const D3D12_RESOURCE_DESC *rd)
{
    return rd->Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D && !rd->Alignment && rd->Width==rd->Height &&
        (rd->Width==2048 || rd->Width==1024) && rd->MipLevels==1 && rd->DepthOrArraySize==1 &&
        rd->Format==DXGI_FORMAT_A8_UNORM && rd->SampleDesc.Count==1 && !rd->SampleDesc.Quality &&
        (UINT)rd->Layout==0x100 && (UINT)rd->Flags==0x300000;
}
