#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstring>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12PipelineState *test_decomp_host(IUnknown *,ID3D12RootSignature **);
extern "C" ID3D12RootSignature *xs_bvh_root_host(IUnknown *,IUnknown *);
#define SLOT(o,n,T) ((T)((*reinterpret_cast<void***>((o).Get()))[n/8]))
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))

static UINT bits(float f){UINT u;memcpy(&u,&f,4);return u;}
static int dispatch(IUnknown *wrapped,UINT stage,UINT test)
{
    ID3D12RootSignature *root=nullptr;auto ps=test_decomp_host(wrapped,&root);CK(ps&&root);
    ComPtr<ID3D12Device>d;HR(ps->GetDevice(IID_PPV_ARGS(&d)));
    constexpr UINT section=12288,total=3*section,source=256,dest=section+256;
    std::vector<UINT> seed(total/4),aux(1024);for(UINT i=0;i<seed.size();i++)seed[i]=0x58117e61+13*i;
    auto in=&seed[source/4],out=&seed[dest/4];UINT k[19]={},inBytes=section-512,outBytes=inBytes,groups=1;
    constexpr UINT64 guest=0x83ffffff80ull;UINT window[]={UINT(guest),UINT(guest>>32),256};
    std::vector<UINT> expected;
    if(stage==0){
        k[0]=test==2?0:37;groups=2;in[0]=0xa7456273;in[12]=9*8+5;in[13]=128;in[14]=768;
        if(test==1)outBytes=512;
        expected=seed;auto e=&expected[dest/4];memcpy(e,in,std::min(outBytes,k[0]*16u));
        if(k[0]){e[0]&=~(1u<<24);if(outBytes>=800)e[199]=~0u;}
    }else if(stage==1 || stage==2){
        bool half=stage==1;const UINT stride=half?64:128; k[0]=128;k[1]=10240;k[2]=9472;k[3]=1;
        // Three live nodes among 64: a triangle, a half-box child and a float-box child.
        // Middle lane also references the second triangle in the triangle-pair leaf.
        memset(in+32,0,256);const UINT lane[]={1,33,63},tag[]={0,4,5},axis[]={1,3,5};
        UINT packed[]={0x01000002,0x02000008,0x01000020};
        UINT cid[3];for(UINT j=0;j<3;j++){
            cid[j]=((k[2]+128*j)/64)*8+tag[j];in[32+lane[j]]=packed[j];in[96+j+(j==2)]=cid[j];
        }
        in[98]=cid[0]|1;
        const float overrideVal[]={-4,8,16};const unsigned short halfVal[]={0xc400,0x4800,0x4c00};
        if(half)memcpy(in+100,halfVal,6);else memcpy(in+100,overrideVal,12);
        float tri[]={1,2,3,-1.0001f,-2,-3,4,5,6,8.001f,9,10};memcpy((BYTE*)out+k[2],tri,48);out[(k[2]+48)/4]=128;
        const unsigned short halfBox[]={0xbc00,0xc000,0xc200,0x4400,0x4500,0x4600};
        for(UINT j=0;j<4;j++){memcpy((BYTE*)out+k[2]+128+16+12*j,halfBox,12);float box[]={-1,-2,-3,4,5,6};memcpy((BYTE*)out+k[2]+256+16+24*j,box,24);}
        if(test==1)outBytes=k[1];
        expected=seed;auto e=&expected[dest/4];
        for(UINT i=0;i<64;i++){
            UINT off=k[0]+stride*i;auto node=e+off/4;memset(node,0xff,16);if(!half)memset(node+28,0,16);
            for(UINT c=0;c<4;c++)for(UINT j=0;j<6;j++){
                float v=j<3?INFINITY:-INFINITY;unsigned short h=j<3?0x7c00:0xfc00;
                for(UINT a=0;a<3;a++)if(i==lane[a]&&!c){node[0]=cid[a];float xyz[]={a?-1.f:-1.0001f,-2,-3,a?4.f:8.001f,a?5.f:9.f,a?6.f:10.f};
                    unsigned short hs[]={0xbc01,0xc000,0xc200,0x4801,0x4880,0x4900};v=xyz[j];h=a?halfBox[j]:hs[j];
                    if(axis[a]==j){v=overrideVal[a];h=halfVal[a];}
                    if(test==0)e[k[1]/4+2*a]=((off>>6)*8)+(half?4:5);
                }
                if(i==lane[1]&&c==1){node[1]=cid[0]|1;float xyz[]={-1.0001f,-2,-3,8.001f,9,10};
                    unsigned short hs[]={0xbc01,0xc000,0xc200,0x4801,0x4880,0x4900};v=xyz[j];h=hs[j];}
                if(half)memcpy((BYTE*)node+16+2*(6*c+j),&h,2);else node[4+6*c+j]=bits(v);
            }
        }
    }else{
        // Five columns: geometry/primitive/second primitive are 24-bit, metadata and swizzle are bytes.
        constexpr UINT num=3;k[3]=64;k[11]=num;BYTE*b=(BYTE*)in+k[3];
        for(UINT i=0;i<num;i++){UINT geom=1,prim=i,next=3+i;memcpy(b+3*i,&geom,3);b[3*num+i]=0x80+i;
            memcpy(b+4*num+3*i,&prim,3);memcpy(b+7*num+3*i,&next,3);b[10*num+i]=0x29;}
        // t0[1], at a nonzero suballocation offset; R32_UINT indices, XYZ32 vertices.
        constexpr UINT descBase=96;UINT *rec=&aux[descBase/4+12];rec[2]=42;rec[3]=6;
        UINT64 indexVA=guest+16,vertexVA=guest+128;memcpy(rec+6,&indexVA,8);memcpy(rec+8,&vertexVA,8);rec[10]=12;
        auto data=&aux[256];for(UINT i=0;i<18;i++)data[4+i]=(7+i*3)%10;
        for(UINT i=0;i<10;i++)for(UINT a=0;a<3;a++)data[32+3*i+a]=bits(float(int(i)*4-int(a)*7));
        if(test==1)window[2]=128+9*12+11;
        if(test==2)outBytes=64+2*64;
        if(test==3||test==4){
            rec[2]=57;unsigned short indices[18];for(UINT i=0;i<18;i++)indices[i]=(7+i*3)%10;
            memcpy(data+4,indices,sizeof indices);
            if(test==3){UINT64 transform=guest+64;memcpy(rec,&transform,8);
                float rows[]={1,0,0,3,0,-1,0,4,0,0,2,-1};memcpy(data+16,rows,sizeof rows);}
            else {indexVA=guest+256;memcpy(rec+6,&indexVA,8);memcpy(data+64,indices,sizeof indices);
                window[2]=256+7;} // Indices are short by one byte; vertex zero still fits.
        }
        expected=seed;auto e=&expected[dest/4+k[3]/4];
        for(UINT i=0;i<(test==2?2:num);i++,e+=16){for(UINT j=0;j<4;j++){
                UINT vertex=test==4?0:(7+3*(j==3?(3+i)*3:3*i+j))%10;
                for(UINT a=0;a<3;a++){float v=float(int(vertex)*4-int(a)*7);if(test==3)v=a==0?v+3:a==1?4-v:2*v-1;
                    e[3*j+a]=test==1&&vertex==9?0:bits(v);}}
            e[12]=0x180+i;e[13]=i;e[14]=3+i;e[15]=0x209;}
    }
    using R=ComPtr<ID3D12Resource>;
    auto buffer=[&](R&r,D3D12_HEAP_TYPE type){
        D3D12_HEAP_PROPERTIES hp={};hp.Type=type;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=total;
        rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(type==D3D12_HEAP_TYPE_DEFAULT)rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,type==D3D12_HEAP_TYPE_UPLOAD?
            D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r));};
    R initial,input,arena,rb;HR(buffer(initial,D3D12_HEAP_TYPE_UPLOAD));HR(buffer(input,D3D12_HEAP_TYPE_UPLOAD));
    HR(buffer(arena,D3D12_HEAP_TYPE_DEFAULT));HR(buffer(rb,D3D12_HEAP_TYPE_READBACK));void*m;
    HR(initial->Map(0,nullptr,&m));memcpy(m,seed.data(),total);initial->Unmap(0,nullptr);
    HR(input->Map(0,nullptr,&m));memcpy(m,aux.data(),aux.size()*4);input->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,1};
    ComPtr<ID3D12DescriptorHeap>heap;HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
    for(UINT i=0;i<2;i++){D3D12_UNORDERED_ACCESS_VIEW_DESC v={};v.Format=DXGI_FORMAT_R32_TYPELESS;v.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
        v.Buffer.FirstElement=(i?dest:source)/4;v.Buffer.NumElements=(i?outBytes:inBytes)/4;v.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
        d->CreateUnorderedAccessView(arena.Get(),nullptr,&v,cpu);cpu.ptr+=d->GetDescriptorHandleIncrementSize(hd.Type);}
    D3D12_SHADER_RESOURCE_VIEW_DESC t={};t.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;t.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    t.Buffer.FirstElement=2;t.Buffer.NumElements=2;t.Buffer.StructureByteStride=48;d->CreateShaderResourceView(input.Get(),&t,cpu);
    D3D12_COMMAND_QUEUE_DESC qd={};qd.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;ComPtr<ID3D12CommandQueue>q;
    HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;
    HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(1,qd.Type,a.Get(),ps,IID_PPV_ARGS(&l)));
    auto trans=[&](D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b={};b.Transition={arena.Get(),0,from,to};l->ResourceBarrier(1,&b);};
    l->CopyResource(arena.Get(),initial.Get());trans(D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    l->SetDescriptorHeaps(1,heap.GetAddressOf());l->SetComputeRootSignature(root);
    l->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());l->SetComputeRoot32BitConstants(1,19,k,0);
    if(stage==3){l->SetComputeRoot32BitConstants(2,3,window,0);l->SetComputeRootShaderResourceView(3,input->GetGPUVirtualAddress()+1024);}
    l->Dispatch(groups,1,1);
    trans(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyResource(rb.Get(),arena.Get());HR(l->Close());
    ID3D12CommandList*lists[]={l.Get()};q->ExecuteCommandLists(1,lists);ComPtr<ID3D12Fence>f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));
    HR(q->Signal(f.Get(),1));HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(e);HR(f->SetEventOnCompletion(1,e));
    CK(WaitForSingleObject(e,15000)==WAIT_OBJECT_0&&f->GetCompletedValue()==1);CloseHandle(e);
    HR(rb->Map(0,nullptr,&m));for(UINT i=0;i<expected.size();i++)if(((UINT*)m)[i]!=expected[i]){
        fprintf(stderr,"FAIL stage=%u test=%u word=%d gpu=%08x expected=%08x\n",stage,test,int(i)-int(dest/4),((UINT*)m)[i],expected[i]);return 1;}
    rb->Unmap(0,nullptr);printf("PASS decompress stage=%u case=%u fenced complete arena\n",stage,test);return 0;
}
using Create=decltype(&D3D12XboxCreateDevice);
static int verify(Create create,bool staged)
{
    const IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    ComPtr<IUnknown>dev,other,root,wrongroot,ps;HR(create(nullptr,params,boot,(void**)dev.GetAddressOf()));HR(create(nullptr,params,boot,(void**)other.GetAddressOf()));
    D3D12_ROOT_PARAMETER1 p[4]={};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[0].Constants.Num32BitValues=19;
    p[1].ParameterType=p[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[2].Descriptor.ShaderRegister=1;p[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC sig={};sig.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;sig.Desc_1_1={4,p,0,nullptr,(D3D12_ROOT_SIGNATURE_FLAGS)0x03000000};
    using Root=HRESULT(WINAPI *)(IUnknown*,UINT,const void*,SIZE_T,REFIID,void**);using Name=HRESULT(WINAPI *)(IUnknown*,const WCHAR*);
    auto makeRoot=[&](ID3DBlob*b,ComPtr<IUnknown>&r){return SLOT(dev,0x80,Root)(dev.Get(),1,b->GetBufferPointer(),b->GetBufferSize(),IID_ID3D12RootSignature,(void**)r.GetAddressOf());};
    ComPtr<ID3DBlob>b;HR(D3D12SerializeVersionedRootSignature(&sig,&b,nullptr));CK(b->GetBufferSize()==164);HR(makeRoot(b.Get(),root));
    HR(SLOT(root,0x30,Name)(root.Get(),L"XDXR BVHDecompressor"));
    ComPtr<ID3DBlob>wrong;p[0].Constants.Num32BitValues--;HR(D3D12SerializeVersionedRootSignature(&sig,&wrong,nullptr));CK(makeRoot(wrong.Get(),wrongroot)==E_NOTIMPL&&!wrongroot);
    p[0].Constants.Num32BitValues=4;p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;p[0].Descriptor={0,0,D3D12_ROOT_DESCRIPTOR_FLAG_NONE};p[1].Descriptor.ShaderRegister=1;p[2].Descriptor.ShaderRegister=2;
    p[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[3].Constants={0,0,4};wrong.Reset();HR(D3D12SerializeVersionedRootSignature(&sig,&wrong,nullptr));HR(makeRoot(wrong.Get(),wrongroot));
    struct Kernel {const char *file;const WCHAR *name;UINT size,root;};const Kernel ks[]={
        {"header.bin",L"XDXR DecompressHeader",0x1194,0x114},{"box16.bin",L"XDXR Box16Rehydration",0x2e40,0x114},
        {"box32.bin",L"XDXR Box32Rehydration",0x2b2c,0x114},{"leaf.bin",L"XDXR LeafRehydration",0x9568,0x12c}};
    for(UINT j=0;j<4;j++){const auto &kernel=ks[j];std::vector<BYTE>code(kernel.size);FILE*file=fopen(kernel.file,"rb");
    CK(file&&fread(code.data(),1,code.size(),file)==code.size()&&fgetc(file)==EOF&&!ferror(file));fclose(file);
    CK(!memcmp(code.data()+kernel.root,(BYTE*)b->GetBufferPointer()+36,128));
    struct Wire{IUnknown *root;const void*cs;SIZE_T size;BYTE tail[32];}w={wrongroot.Get(),code.data(),code.size(),{}};
    using Make=HRESULT(WINAPI *)(IUnknown*,const Wire*,UINT64,UINT64,REFIID,void**);
    auto make=[&](ComPtr<IUnknown>&o){return SLOT(o,0x1f0,Make)(o.Get(),&w,0,0,IID_ID3D12PipelineState,(void**)ps.GetAddressOf());};
    CK(make(dev)==E_INVALIDARG&&!ps);w.root=root.Get();CK(make(other)==E_INVALIDARG&&!ps);
    w.size--;CK(make(dev)==E_NOTIMPL&&!ps);w.size++;w.tail[31]=1;CK(make(dev)==E_NOTIMPL&&!ps);w.tail[31]=0;
    code[256]^=1;CK(make(dev)==E_NOTIMPL&&!ps);code[256]^=1;
    if(j==3&&!GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettBoundedLeafRehydrationV1")){
        CK(make(dev)==E_NOTIMPL&&!ps);puts("PASS leaf declined missing backend");continue;}
    HRESULT hr=make(dev);if(FAILED(hr))fprintf(stderr,"stage=%u factory hr=%08lx\n",j,(unsigned long)hr);HR(hr);
    WCHAR bad[32]={};CK(SLOT(ps,0x30,Name)(ps.Get(),bad)==E_NOTIMPL);HR(SLOT(ps,0x30,Name)(ps.Get(),kernel.name));
    printf("PASS %s decompress factory %s digest/schema/owner/name\n",staged?"staged":"linked",kernel.file);
    if(!staged)for(UINT test=0;test<(j==3?5u:j==0?3u:2u);test++)CK(!dispatch(ps.Get(),j,test));
    ps.Reset();}
    return 0;
}
int main()
{
    HMODULE dll=LoadLibraryW(L"d3d12_xs.dll");CK(dll);auto fn=GetProcAddress(dll,"D3D12XboxCreateDevice");
    Create create;CK(fn);memcpy(&create,&fn,sizeof create);CK(!verify(create,true));return verify(D3D12XboxCreateDevice,false);
}
