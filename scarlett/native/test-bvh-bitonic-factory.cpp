#include <vector>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
using Microsoft::WRL::ComPtr;
#define CK(x) do{if(!(x)){printf("FAIL line %u: %s\n",__LINE__,#x); return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>((o).Get()))[n/8]))
int main(){
    // Exercise the adapter DLL staged in the tool, not an in-process copy of the factory.
    HMODULE lib=LoadLibraryW(L"d3d12_xs.dll");CK(lib);
    using Create=HRESULT(WINAPI *)(void *,const void *,REFIID,void **);
    FARPROC sym=GetProcAddress(lib,"D3D12XboxCreateDevice");Create create;memcpy(&create,&sym,sizeof create);CK(create);
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    ComPtr<IUnknown> dev,other,root,ps,utility,morton;
    HR(create(nullptr,params,boot,(void **)dev.GetAddressOf()));HR(create(nullptr,params,boot,(void **)other.GetAddressOf()));
    for(UINT z=0;z<3;z++) {
        D3D12_ROOT_PARAMETER1 rp[6]={};
        if(!z){rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;rp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
          rp[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;rp[2].Constants.Num32BitValues=16;}
        else if(z==2) {rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;rp[0].Constants={0,0,8};
          for(UINT i=1;i<=5;i++){rp[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;rp[i].Descriptor.ShaderRegister=i-1;}}
        else for(UINT i=0;i<2;i++){rp[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;rp[i].Constants={1-i,0,2};
          rp[2+i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;rp[2+i].Descriptor.ShaderRegister=i;}
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC sig={};sig.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
    sig.Desc_1_1={z==2?6u:z?4u:3u,rp,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
    ComPtr<ID3DBlob> blob;HR(D3D12SerializeVersionedRootSignature(&sig,&blob,nullptr));
    using Root=HRESULT(WINAPI *)(IUnknown *,UINT,const void *,SIZE_T,REFIID,void **);
    HR(SLOT(dev,0x80,Root)(dev.Get(),1,blob->GetBufferPointer(),blob->GetBufferSize(),IID_ID3D12RootSignature,(void **)(z==2?morton:z?root:utility).GetAddressOf()));
    }
    const struct {const char *filename; UINT size; const WCHAR *name;} inputs[]={
        {"pre.dxbc",0x20c8,L"XDXR BitonicPreSort 0"},
        {"outer.dxbc",0x1468,L"XDXR BitonicOuterSort 0"},
        {"inner.dxbc",0x1f5c,L"XDXR BitonicInnerSort 0"},
        {"pre64.dxbc",0x2204,L"XDXR BitonicPreSort 1"},
        {"outer64.dxbc",0x1554,L"XDXR BitonicOuterSort 1"},
        {"inner64.dxbc",0x2094,L"XDXR BitonicInnerSort 1"},
    };
    for(auto &in:inputs){
    std::vector<BYTE> code(in.size); FILE *file=fopen(in.filename,"rb");CK(file&&fread(code.data(),1,code.size(),file)==code.size()&&fgetc(file)==EOF&&!ferror(file)); fclose(file);
    struct Wire{IUnknown *root;const void *code;SIZE_T size;BYTE tail[32];}w={root.Get(),code.data(),code.size(),{}};
    using Make=HRESULT(WINAPI *)(IUnknown *,const Wire *,UINT64,UINT64,REFIID,void **);
    auto make=[&](const ComPtr<IUnknown>&owner){return SLOT(owner,0x1f0,Make)(owner.Get(),&w,0,0,IID_ID3D12PipelineState,(void **)ps.GetAddressOf());};
    CK(make(other)==E_INVALIDARG&&!ps);w.root=utility.Get(); CK(make(dev)==E_INVALIDARG&&!ps); w.root=morton.Get(); CK(make(dev)==E_INVALIDARG&&!ps); w.root=root.Get();
    code.back()^=1;CK(make(dev)==E_NOTIMPL&&!ps);code.back()^=1;
    w.size--;CK(make(dev)==E_NOTIMPL&&!ps);w.size++;w.tail[31]=1;CK(make(dev)==E_NOTIMPL&&!ps);w.tail[31]=0;
    HR(make(dev));using Name=HRESULT(WINAPI *)(IUnknown *,const WCHAR *);
    WCHAR wrong[32]={};CK(SLOT(ps,0x30,Name)(ps.Get(),wrong)==E_NOTIMPL);HR(SLOT(ps,0x30,Name)(ps.Get(),in.name));
    ps.Reset();printf("PASS exact public +1f0 %s\n",in.filename);
    }
}
