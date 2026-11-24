#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <d3d12.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12RootSignature *xs_bvh_root_host(IUnknown *,IUnknown *);
extern "C" ID3D12PipelineState *clear_test_host(IUnknown *);
#define CK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define HR(x) do {HRESULT status=(x); if(FAILED(status)){fprintf(stderr,"HRESULT %08lx\n",(unsigned long)status);CK(false);}}while(0)
static void load(const char *path,void *p,size_t n) {
    FILE *f=fopen(path,"rb"); CK(f && fread(p,1,n,f)==n && fgetc(f)==EOF); fclose(f);
}
static ComPtr<IUnknown> device() {
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> d; HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)d.GetAddressOf())); return d;
}
static ComPtr<IUnknown> root(IUnknown *d) {
    D3D12_ROOT_PARAMETER1 p={};p.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p.Descriptor.RegisterSpace=3;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rs={};rs.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
    rs.Desc_1_1={1,&p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> w;HR(D3D12SerializeVersionedRootSignature(&rs,&w,nullptr));
    using Create=HRESULT(WINAPI *)(IUnknown*,UINT,const void*,SIZE_T,REFIID,void**);
    ComPtr<IUnknown> r;
    HR(((Create)(*(void***)d)[0x80/8])(d,1,w->GetBufferPointer(),w->GetBufferSize(),IID_ID3D12RootSignature,(void**)r.GetAddressOf()));
    return r;
}
