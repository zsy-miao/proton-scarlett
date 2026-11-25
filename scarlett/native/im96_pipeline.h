/* Only included by the opt-in factory. q is an owned 688-byte snapshot. */
#include "im96_host_dxil.h"
HRESULT xs_im96_private_layout(IUnknown*,IUnknown*,ID3D12RootSignature**);

static const BYTE im96_fixed[]={0x6a,0x1a,0x91,0xfc,0x96,0x6f,0x57,0x9f,0x3e,0x7e,0x5c,0xf9,0xf0,0xf4,0x05,0xcf,0xa3,0xee,0x4c,0x5b,0x9d,0x31,0x3c,0x28,0x77,0x8b,0xd0,0x9f,0xd9,0x5a,0x21,0xf4};
static HRESULT im96_prepare(IUnknown *device,UINT_PTR *q,IUnknown *alias,const void *vs,const void *ps)
{
    static const BYTE vh[]={0xc6,0x5e,0xb8,0xc9,0xaf,0x8f,0xcf,0x73,0x89,0xa2,0x08,0x72,0xba,0x42,0xeb,0x13,0x57,0xcc,0xc9,0x2a,0x5a,0xe5,0xbb,0x71,0x84,0x2c,0xed,0x43,0x57,0x41,0xfd,0xb4};
    static const BYTE ph[]={0x5c,0xd7,0x3c,0xc9,0x75,0xd0,0x97,0x6d,0xa6,0x4f,0x3b,0x1a,0x1e,0x88,0x2e,0x49,0xbc,0x99,0xc0,0x64,0xdb,0x54,0x0f,0xab,0xb4,0xeb,0x1b,0xb8,0x61,0x4c,0xfe,0x9f};
    static const D3D12_INPUT_ELEMENT_DESC ia[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    const void *layout=(void*)q[77];q[77]=0; /* root and both shader pointers already zeroed */
    if(q[0] || q[2]!=1 || q[4]!=1712 || q[5]!=2 || q[7]!=1588 ||
       q[8]!=3 || q[9] || q[10] || q[11]!=4 || q[12] || q[13] || q[14]!=5 || q[15] || q[16] ||
       q[76]!=12 || q[78]!=3 || q[79]!=(((UINT64)19<<32)|14) ||
       q[80]!=(((UINT64)DXGI_FORMAT_R8G8B8A8_UNORM<<32)|15) || q[84]>>32!=1 || q[85]!=16 ||
       !shader_digest_matches((BYTE*)q,688,im96_fixed))return E_NOTIMPL;
    BYTE v[1712],p[1588];D3D12_INPUT_ELEMENT_DESC elements[3];
    if(!copy_in(v,vs,sizeof v)||!copy_in(p,ps,sizeof p)||!copy_in(elements,layout,sizeof elements))return E_POINTER;
    if(!shader_digest_matches(v,sizeof v,vh)||!shader_digest_matches(p,sizeof p,ph))return E_NOTIMPL;
    for(UINT k=0;k<3;k++){
        char name[9];SIZE_T n=strlen(ia[k].SemanticName)+1;
        if(!copy_in(name,elements[k].SemanticName,n))return E_POINTER;
        if(memcmp(name,ia[k].SemanticName,n) || elements[k].SemanticIndex || elements[k].InputSlot ||
           elements[k].Format!=ia[k].Format || elements[k].AlignedByteOffset!=ia[k].AlignedByteOffset ||
           elements[k].InputSlotClass!=ia[k].InputSlotClass || elements[k].InstanceDataStepRate)return E_NOTIMPL;
    }
    ID3D12RootSignature *private_root=NULL;
    HRESULT hr=xs_im96_private_layout(device,alias,&private_root);
    if(FAILED(hr))return hr; /* success transfers an alias ref and a host root ref */
    q[1]=(UINT_PTR)private_root;q[3]=(UINT_PTR)im96_vs;q[4]=sizeof im96_vs;q[6]=(UINT_PTR)im96_ps;q[7]=sizeof im96_ps;
    q[15]=(UINT_PTR)im96_gs;q[16]=sizeof im96_gs;q[77]=(UINT_PTR)ia;
    q[79]=((UINT64)D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE<<32)|14;
    return S_OK;
}
