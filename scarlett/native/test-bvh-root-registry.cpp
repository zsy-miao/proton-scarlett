/* Independent oracle: shader RTS0 chunks and constructor name literals in the pinned DLL. */
#include <d3d12.h>
#include <cstdio>
#include <vector>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12RootSignature *xs_bvh_root_host(IUnknown *,IUnknown *);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>(o))[n/8]))
#define CK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
using Create=HRESULT(WINAPI *)(IUnknown *,UINT,const void *,SIZE_T,REFIID,void **);
using Name=HRESULT(WINAPI *)(IUnknown *,const WCHAR *);
static UINT word(const BYTE *p) { UINT v; memcpy(&v,p,sizeof v); return v; }

int main()
{
    FILE *f=fopen("private-builder-image.bin","rb"); CK(f);
    std::vector<BYTE> image(884224);
    CK(fread(image.data(),1,image.size(),f)==image.size() && fgetc(f)==EOF && !ferror(f));
    fclose(f);
    /* In file order: number of consecutive shaders using this root, file offset of
       its UTF-16 name (fourth argument at the DLL's calls to helper +c2d0). */
    const struct { UINT shaders, name; } families[]={
        {1,0x248c8},{4,0xcaf78},{3,0xcc508},{6,0xcc5b0},{3,0xcc6f8},{2,0xcc788},
        {1,0xcc810},{4,0xcc898},{2,0xccb68},{1,0xccc10},{3,0xccc78},{7,0xcccb0},
        {2,0xcced8},{1,0xccf68},{1,0xccf98},{11,0xccfe8}
    };
    DWORD params[20]={}; params[0]=0x20004; params[2]=0x100000; params[10]=1; params[15]=0x20;
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev,other;
    CK(D3D12XboxCreateDevice(nullptr,params,boot,(void **)dev.GetAddressOf())==S_OK);
    CK(D3D12XboxCreateDevice(nullptr,params,boot,(void **)other.GetAddressOf())==S_OK);
    auto make=SLOT(dev.Get(),0x80,Create);
    auto rejected=[&](const void *p,SIZE_T n,HRESULT expected=E_NOTIMPL){
        void *out=dev.Get();
        return make(dev.Get(),1,p,n,IID_ID3D12RootSignature,&out)==expected && !out;
    };
    SYSTEM_INFO info; GetSystemInfo(&info);
    BYTE *pages=(BYTE *)VirtualAlloc(nullptr,2*info.dwPageSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    DWORD old; CK(pages && VirtualProtect(pages+info.dwPageSize,info.dwPageSize,PAGE_NOACCESS,&old));
    UINT family=0,in_family=0,total=0;
    for (size_t i=0;i+32<=image.size();i++) {
        BYTE *shader=image.data()+i;
        if (memcmp(shader,"DXBC",4)) continue;
        UINT size=word(shader+24),parts=word(shader+28);
        CK(size>=32 && size<=image.size()-i && parts<=(size-32)/4);
        const BYTE *rts=nullptr; UINT bytes=0;
        for (UINT j=0;j<parts;j++) {
            UINT off=word(shader+32+4*j); CK(off<=size-8);
            if (memcmp(shader+off,"RTS0",4)) continue;
            CK(!rts); rts=shader+off; bytes=8+word(rts+4); CK(bytes>=8 && bytes<=size-off);
        }
        CK(rts && family<ARRAYSIZE(families));
        ComPtr<ID3D12VersionedRootSignatureDeserializer> parser;
        CK(SUCCEEDED(D3D12CreateVersionedRootSignatureDeserializer(shader,size,IID_PPV_ARGS(&parser))));
        auto desc=*parser->GetUnconvertedRootSignatureDesc();
        CK(desc.Version==D3D_ROOT_SIGNATURE_VERSION_1_1 && desc.Desc_1_1.Flags==0x03000000);
        ComPtr<ID3DBlob> blob;
        CK(SUCCEEDED(D3D12SerializeVersionedRootSignature(&desc,&blob,nullptr)));
        BYTE *wire=(BYTE *)blob->GetBufferPointer(); size_t n=blob->GetBufferSize();
        CK(n>=36 && word(wire+28)==1 && word(wire+32)==36 && n==36+bytes && !memcmp(wire+36,rts,bytes));
        if (!total) {
            void *out=dev.Get();
            CK(make(dev.Get(),0,wire,n,IID_ID3D12RootSignature,&out)==E_NOTIMPL && !out);
            out=dev.Get(); CK(make(dev.Get(),1,wire,n,IID_IUnknown,&out)==E_NOINTERFACE && !out);
            CK(make(dev.Get(),1,wire,n,IID_ID3D12RootSignature,nullptr)==E_POINTER);
            CK(rejected(nullptr,n,E_INVALIDARG));
        }
        ComPtr<IUnknown> root;
        CK(make(dev.Get(),1,wire,n,IID_ID3D12RootSignature,(void **)root.GetAddressOf())==S_OK && root);
        auto set=SLOT(root.Get(),0x30,Name);
        WCHAR name[32]; memcpy(name,image.data()+families[family].name,sizeof name);
        size_t len=wcsnlen(name,ARRAYSIZE(name)); CK(len && len+1<ARRAYSIZE(name));
        WCHAR *edge=(WCHAR *)(pages+info.dwPageSize)-(len+1);
        memcpy(edge,name,(len+1)*sizeof(WCHAR));
        CK(set(root.Get(),edge)==S_OK && set(root.Get(),edge+1)==E_POINTER);
        name[len]=L'!'; name[len+1]=0; CK(set(root.Get(),name)==E_NOTIMPL);
        CK(xs_bvh_root_host(dev.Get(),root.Get()) && !xs_bvh_root_host(other.Get(),root.Get()));
        if (!total) {
            IUnknown *owner=dev.Get();
            CK(dev.Reset()==1); // only the live root retains the device
            CK(xs_bvh_root_host(owner,root.Get()) && owner->AddRef()==2);
            dev.Attach(owner);
        }
        IUnknown *stale=root.Get(); CK(!root.Reset() && !xs_bvh_root_host(dev.Get(),stale));
        wire[4]^=1; CK(rejected(wire,n)); wire[4]^=1; // checksum only
        CK(rejected(wire,n-1) && rejected(pages+info.dwPageSize,n,E_POINTER));
        if (families[family].name==0xcc788) {
            D3D12_ROOT_PARAMETER1 ordered[4]; CK(desc.Desc_1_1.NumParameters==4);
            memcpy(ordered,desc.Desc_1_1.pParameters,sizeof ordered);
            for (UINT j=0;j<3;j++) {
                CK(ordered[j].ParameterType==D3D12_ROOT_PARAMETER_TYPE_UAV && ordered[j].Descriptor.ShaderRegister==(j+1)%3);
                ordered[j].Descriptor.ShaderRegister=j;
            }
            CK(ordered[3].ParameterType==D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS && ordered[3].Constants.Num32BitValues==3);
            auto sorted=desc; sorted.Desc_1_1.pParameters=ordered;
            blob.Reset(); CK(SUCCEEDED(D3D12SerializeVersionedRootSignature(&sorted,&blob,nullptr)));
            CK(rejected(blob->GetBufferPointer(),blob->GetBufferSize())); // canonical, same bindings, wrong order
        }
        desc.Desc_1_1.Flags=D3D12_ROOT_SIGNATURE_FLAG_NONE;
        blob.Reset(); CK(SUCCEEDED(D3D12SerializeVersionedRootSignature(&desc,&blob,nullptr)));
        CK(rejected(blob->GetBufferPointer(),blob->GetBufferSize()));
        ++total;
        if (++in_family==families[family].shaders) { ++family; in_family=0; }
        i+=size-1;
    }
    CK(total==52 && family==ARRAYSIZE(families) && !in_family);
    CK(VirtualFree(pages,0,MEM_RELEASE) && !dev.Reset() && !other.Reset());
    puts("PASS root registry: 52 shader RTS0 byte comparisons, 16 named families; owner, bounds and near-matches");
    return 0;
}
