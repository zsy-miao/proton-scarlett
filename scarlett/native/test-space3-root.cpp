#include <cstdio>
#include <cstdlib>
#include <d3d12.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
extern "C" HRESULT WINAPI D3D12XboxCreateDevice(void *,const void *,REFIID,void **);
extern "C" ID3D12RootSignature *xs_bvh_root_host(IUnknown *,IUnknown *);
#define CK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
#define HR(x) CK(SUCCEEDED(x))
#ifdef TEST_POOL_ROOT
extern "C" HRESULT xs_allocator(ID3D12Device*,D3D12_COMMAND_LIST_TYPE,IUnknown**);
extern "C" HRESULT xs_device_list(ID3D12Device*,D3D12_COMMAND_LIST_TYPE,IUnknown*,BOOL,IUnknown*,IUnknown**);
extern "C" HRESULT xs_direct_queue(ID3D12Device*,IUnknown**);
extern "C" ID3D12GraphicsCommandList *test_list_recorder(IUnknown*);
extern "C" HRESULT test_list_root_checked(IUnknown*,IUnknown*);
extern "C" ID3D12CommandQueue *test_host_queue(IUnknown*);
#endif
#ifdef TEST_BINK
#include "test-bink-bindings.hpp"
#endif
#ifdef TEST_BINK
static int draw(bool alpha) {
#else
int main() {
#endif
    DWORD params[20]={};params[0]=0x20004;params[2]=0x100000;params[10]=1;params[15]=0x20;
    IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    ComPtr<IUnknown> dev,root;
    HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)dev.GetAddressOf()));
    D3D12_ROOT_PARAMETER1 p={};p.ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p.Descriptor.RegisterSpace=3;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rs={};rs.Version=D3D_ROOT_SIGNATURE_VERSION_1_1;
    rs.Desc_1_1={1,&p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
#ifdef TEST_BINK
    const UINT opaque[]={2,6,5,7,10,0,3},with_alpha[]={2,6,5,8,7,10,0,3};
    UINT count=alpha?8:7;const UINT *regs=alpha?with_alpha:opaque;D3D12_DESCRIPTOR_RANGE1 ranges[8]={};
    for(UINT i=0;i<count;i++)ranges[i]={i<count-2?D3D12_DESCRIPTOR_RANGE_TYPE_SRV:D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,regs[i],1,D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
    p.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p.DescriptorTable={count,ranges};
    D3D12_STATIC_SAMPLER_DESC samp={D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,0,1,D3D12_COMPARISON_FUNC_NEVER,D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,0,12,3,1,D3D12_SHADER_VISIBILITY_ALL};
    rs.Desc_1_1={1,&p,1,&samp,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};const SIZE_T bytes=alpha?332:308;
#else
    constexpr SIZE_T bytes=92;
#endif
    ComPtr<ID3DBlob> wire;HR(D3D12SerializeVersionedRootSignature(&rs,&wire,nullptr));
    const void *capture=wire->GetBufferPointer();CK(wire->GetBufferSize()==bytes);
    using Create=HRESULT(WINAPI *)(IUnknown*,UINT,const void*,SIZE_T,REFIID,void**);
    Create create=(Create)(*(void***)(dev.Get()))[16];
    void* bad=dev.Get();
    CK(create(dev.Get(),0,capture,bytes,IID_ID3D12RootSignature,&bad)==E_NOTIMPL&&!bad);
    CK(create(dev.Get(),1,capture,bytes-1,IID_ID3D12RootSignature,&bad)==E_NOTIMPL&&!bad);
#ifdef TEST_BINK
    ranges[1].BaseShaderRegister=5;ranges[2].BaseShaderRegister=6;
#else
    p.Descriptor.RegisterSpace=2;
#endif
    ComPtr<ID3DBlob> other;HR(D3D12SerializeVersionedRootSignature(&rs,&other,nullptr));
    CK(create(dev.Get(),1,other->GetBufferPointer(),other->GetBufferSize(),IID_ID3D12RootSignature,&bad)==E_NOTIMPL&&!bad);
    HR(create(dev.Get(),1,capture,bytes,IID_ID3D12RootSignature,(void**)root.GetAddressOf()));
    auto host=xs_bvh_root_host(dev.Get(),root.Get());CK(host);ComPtr<ID3D12Device>d;HR(host->GetDevice(IID_PPV_ARGS(&d)));
#ifdef TEST_BINK
    using Warm=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**);auto warm=(Warm)(*(void***)dev.Get())[0x1c8/8];IUnknown *key=root.Get();
    bad=key;CK(warm(dev.Get(),&key,IID_ID3D12PipelineState,&bad)==E_NOTIMPL && !bad);
    CK(warm(dev.Get(),&key,IID_ID3D12RootSignature,&bad)==E_NOINTERFACE && !bad);key=dev.Get();CK(warm(dev.Get(),&key,IID_ID3D12PipelineState,&bad)==E_INVALIDARG && !bad);
    CK(warm(dev.Get(),nullptr,IID_ID3D12PipelineState,&bad)==E_POINTER && !bad);
#endif
    BYTE shaders[2][8192];D3D12_SHADER_BYTECODE code[2];
    for (int i=0;i<2;i++) {
        const char *file=i?"ps.dxil":"vs.dxil";
#ifdef TEST_BINK
        if(i && alpha)file="alpha-ps.dxil";
#endif
        FILE *f=fopen(file,"rb");CK(f);auto n=fread(shaders[i],1,sizeof shaders[i],f);CK(n&&feof(f));fclose(f);
        code[i]={shaders[i],n};
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd={};pd.pRootSignature=host;pd.VS=code[0];pd.PS=code[1];pd.SampleMask=~0u;
    pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=DXGI_FORMAT_R32_UINT;pd.SampleDesc.Count=1;
#ifdef TEST_BINK
    D3D12_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,20,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};pd.InputLayout={elements,3};
#endif
    ComPtr<ID3D12PipelineState>pipeline;HR(d->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&pipeline)));
    auto resource=[&](D3D12_HEAP_TYPE t,D3D12_RESOURCE_STATES st,ID3D12Resource**o){
        D3D12_HEAP_PROPERTIES hp={};hp.Type=t;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=256;
        rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(t==D3D12_HEAP_TYPE_DEFAULT){rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Format=DXGI_FORMAT_R32_UINT;rd.Width=4;
            rd.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;}
        return d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,st,nullptr,IID_PPV_ARGS(o));
    };
    ComPtr<ID3D12Resource> src,rt,rb;HR(resource(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,&src));
    HR(resource(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RENDER_TARGET,&rt));HR(resource(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,&rb));
    const UINT vals[]={19,37,0xfedcba98,0x7abc1234};void* m;HR(src->Map(0,nullptr,&m));memcpy(m,vals,sizeof vals);src->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC hd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};ComPtr<ID3D12DescriptorHeap>heap;
    HR(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));auto h=heap->GetCPUDescriptorHandleForHeapStart();d->CreateRenderTargetView(rt.Get(),nullptr,h);
#ifdef TEST_POOL_ROOT
    ComPtr<IUnknown> queue,list,alloc;HR(xs_direct_queue(d.Get(),queue.GetAddressOf()));
    HR(xs_allocator(d.Get(),D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.GetAddressOf()));
    HR(xs_device_list(d.Get(),D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),TRUE,dev.Get(),list.GetAddressOf()));
    auto q=test_host_queue(queue.Get()); auto l=test_list_recorder(list.Get());l->SetPipelineState(pipeline.Get());
    auto slots=*(void***)list.Get();using Close=HRESULT(WINAPI*)(IUnknown*);using Reset=HRESULT(WINAPI*)(IUnknown*,IUnknown*,IUnknown*,UINT);
    CK(test_list_root_checked(list.Get(),nullptr)==E_INVALIDARG && test_list_root_checked(list.Get(),dev.Get())==E_INVALIDARG);
    ComPtr<IUnknown> otherDev,foreign;HR(D3D12XboxCreateDevice(nullptr,params,boot,(void**)otherDev.GetAddressOf()));
    HR(create(otherDev.Get(),1,capture,bytes,IID_ID3D12RootSignature,(void**)foreign.GetAddressOf()));CK(test_list_root_checked(list.Get(),foreign.Get())==E_INVALIDARG);
#else
    ComPtr<ID3D12CommandQueue>q;D3D12_COMMAND_QUEUE_DESC qd={};HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ComPtr<ID3D12CommandAllocator>a;HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    ComPtr<ID3D12GraphicsCommandList>l;HR(d->CreateCommandList(1,qd.Type,a.Get(),pipeline.Get(),IID_PPV_ARGS(&l)));
#endif
    D3D12_VIEWPORT vp={0,0,4,1,0,1};D3D12_RECT sc={0,0,4,1};l->RSSetViewports(1,&vp);l->RSSetScissorRects(1,&sc);
    l->OMSetRenderTargets(1,&h,FALSE,nullptr);l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
#ifdef TEST_POOL_ROOT
    ((void(WINAPI*)(IUnknown*,IUnknown*))slots[0x110/8])(list.Get(),root.Get());
#else
    l->SetGraphicsRootSignature(host);
#endif
#ifdef TEST_BINK
    BinkInputs inputs;CK(!inputs.bind(d.Get(),l.Get(),alpha));
#else
    l->SetGraphicsRootShaderResourceView(0,src->GetGPUVirtualAddress());
#endif
    l->DrawInstanced(3,1,0,0);
    D3D12_RESOURCE_BARRIER b={};b.Transition={rt.Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};l->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION to={},from={};from.pResource=rt.Get();to.pResource=rb.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint.Footprint={DXGI_FORMAT_R32_UINT,4,1,1,256};l->CopyTextureRegion(&to,0,0,0,&from,nullptr);
#ifdef TEST_POOL_ROOT
    HR(((Close)slots[0x48/8])(list.Get()));CK(test_list_root_checked(list.Get(),root.Get())==E_INVALIDARG);
    ComPtr<ID3D12Fence> gate;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));HR(q->Wait(gate.Get(),1));
    using Submit=void(WINAPI*)(IUnknown*,UINT,IUnknown*const*,UINT64,UINT);IUnknown *entry=list.Get();
    ((Submit)(*(void***)queue.Get())[0x118/8])(queue.Get(),1,&entry,0,0);
    HR(((Reset)slots[0x458/8])(list.Get(),alloc.Get(),nullptr,0));
    IUnknown *old=root.Get();root.Reset();CK(!xs_bvh_root_host(dev.Get(),old) && test_list_root_checked(list.Get(),old)==E_INVALIDARG);list.Reset();
#else
    HR(l->Close());
    ID3D12CommandList*ls[]={l.Get()};q->ExecuteCommandLists(1,ls);
#endif
    ComPtr<ID3D12Fence>fence;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE evt=CreateEventW(nullptr,FALSE,FALSE,nullptr);CK(evt);HR(q->Signal(fence.Get(),1));HR(fence->SetEventOnCompletion(1,evt));
#ifdef TEST_POOL_ROOT
    CK(WaitForSingleObject(evt,50)==WAIT_TIMEOUT);HR(gate->Signal(1));
#endif
    CK(WaitForSingleObject(evt,10000)==WAIT_OBJECT_0);CloseHandle(evt);
    HR(rb->Map(0,nullptr,&m));
#ifdef TEST_BINK
    for(UINT i=0;i<4;i++){UINT dx=i==0?0:i==1?2:4;UINT want=11+3*13+12*19+17+(2+dx)+3*(10+dx)+7*(18+dx)+11*(28+dx)+(alpha?13*(34+dx):0);printf("RG pixel %u = %u want %u\n",i,((UINT*)m)[i],want);CK(((UINT*)m)[i]==want);}
    rb->Unmap(0,nullptr);printf("PASS Bink slot %u: IA, all %u table entries, static s3 linear/clamp, 2D/3D fenced readback and reject controls\n",alpha?0:1,count);return 0;
#else
    CK(!memcmp(m,vals,sizeof vals));rb->Unmap(0,nullptr);
    puts("PASS node/size/space negatives; t0 space3 fenced draw returned four exact R32_UINT pixels");return 0;
#endif
}

#ifdef TEST_BINK
int main(){return draw(false) || draw(true);}
#endif
