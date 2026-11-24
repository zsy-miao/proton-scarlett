// Host-only differential control for the clear lookahead; no guest list or title data.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#define CK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
#define HR(x) do { HRESULT hr_=(x); if (FAILED(hr_)) { std::fprintf(stderr,"FAIL line %d: %s = %08lx\n",__LINE__,#x,(unsigned long)hr_); std::exit(1); } } while (0)

template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Kind, class T> struct alignas(void*) Part {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE kind=Kind;
    T value{};
};
struct DepthPipeline {
    Part<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,ID3D12RootSignature*> root;
    Part<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS,D3D12_SHADER_BYTECODE> vs;
    Part<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,D3D12_RASTERIZER_DESC> raster;
    Part<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1,D3D12_DEPTH_STENCIL_DESC1> ds;
    Part<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT,DXGI_FORMAT> format;
    Part<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY,D3D12_PRIMITIVE_TOPOLOGY_TYPE> topology;
    Part<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC,DXGI_SAMPLE_DESC> samples;
};

int main()
{
    ComPtr<IDXGIFactory1> factory; ComPtr<IDXGIAdapter1> adapter;
    HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    HR(factory->EnumAdapters1(0,&adapter));
    DXGI_ADAPTER_DESC1 desc{}; HR(adapter->GetDesc1(&desc));
    std::printf("adapter 0: %04x:%04x\n",desc.VendorId,desc.DeviceId);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { std::puts("SKIP software adapter"); return 77; }
    ComPtr<ID3D12Device> device;
    HR(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    D3D12_FEATURE_DATA_D3D12_OPTIONS2 opts{};
    HRESULT support=device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2,&opts,sizeof opts);
    if (support==E_INVALIDARG || (SUCCEEDED(support) && !opts.DepthBoundsTestSupported)) {
        std::puts("SKIP depth bounds unsupported"); return 77;
    }
    HR(support);
    ComPtr<ID3D12Device2> d; HR(device.As(&d));

    D3D12_ROOT_SIGNATURE_DESC rd{}; ComPtr<ID3DBlob> blob,vs; ComPtr<ID3D12RootSignature> root;
    HR(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr));
    HR(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    const char shader[]="float4 main(uint id:SV_VertexID):SV_Position {"
        "return float4(id==1 ? 3.0 : -1.0, id==2 ? 3.0 : -1.0,0.5,1.0);}";
    HR(D3DCompile(shader,sizeof shader-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&vs,nullptr));
    DepthPipeline p; p.root.value=root.Get(); p.vs.value={vs->GetBufferPointer(),vs->GetBufferSize()};
    p.raster.value.FillMode=D3D12_FILL_MODE_SOLID; p.raster.value.CullMode=D3D12_CULL_MODE_NONE;
    p.raster.value.DepthClipEnable=TRUE;
    p.ds.value.DepthEnable=TRUE; p.ds.value.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    p.ds.value.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO; p.ds.value.DepthBoundsTestEnable=TRUE;
    p.ds.value.FrontFace=p.ds.value.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
    p.samples.value={1,0}; p.format.value=DXGI_FORMAT_D32_FLOAT;
    p.topology.value=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    D3D12_PIPELINE_STATE_STREAM_DESC stream={sizeof p,&p}; ComPtr<ID3D12PipelineState> pso;
    HR(d->CreatePipelineState(&stream,IID_PPV_ARGS(&pso)));

    D3D12_HEAP_PROPERTIES hp{}; hp.Type=D3D12_HEAP_TYPE_DEFAULT; hp.CreationNodeMask=hp.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC image{}; image.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; image.Width=image.Height=8;
    image.DepthOrArraySize=image.MipLevels=image.SampleDesc.Count=1; image.Format=p.format.value;
    image.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    ComPtr<ID3D12Resource> ds,rb;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_PPV_ARGS(&ds)));
    D3D12_DESCRIPTOR_HEAP_DESC dh={D3D12_DESCRIPTOR_HEAP_TYPE_DSV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};
    ComPtr<ID3D12DescriptorHeap> views; HR(d->CreateDescriptorHeap(&dh,IID_PPV_ARGS(&views)));
    auto dsv=views->GetCPUDescriptorHandleForHeapStart(); d->CreateDepthStencilView(ds.Get(),nullptr,dsv);
    D3D12_QUERY_HEAP_DESC qh={D3D12_QUERY_HEAP_TYPE_OCCLUSION,4,0}; ComPtr<ID3D12QueryHeap> queries;
    HR(d->CreateQueryHeap(&qh,IID_PPV_ARGS(&queries)));
    hp.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buf{}; buf.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buf.Width=6*sizeof(UINT64);
    buf.Height=buf.DepthOrArraySize=buf.MipLevels=buf.SampleDesc.Count=1; buf.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    HR(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buf,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rb)));
    UINT64 *words; D3D12_RANGE none{};
    HR(rb->Map(0,&none,(void**)&words)); for (unsigned i=0;i<6;++i) words[i]=UINT64_MAX; rb->Unmap(0,nullptr);

    D3D12_COMMAND_QUEUE_DESC qd{}; ComPtr<ID3D12CommandQueue> q; HR(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));
    ComPtr<ID3D12CommandAllocator> a; ComPtr<ID3D12GraphicsCommandList1> l;
    HR(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));
    HR(d->CreateCommandList(0,qd.Type,a.Get(),pso.Get(),IID_PPV_ARGS(&l)));
    D3D12_RECT halves[]={{0,0,4,8},{4,0,8,8}}; D3D12_VIEWPORT vp={0,0,8,8,0,1};
    l->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,.25f,0,0,nullptr);
    l->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,.75f,0,1,&halves[1]);
    l->OMSetRenderTargets(0,nullptr,FALSE,&dsv); l->SetGraphicsRootSignature(root.Get());
    l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); l->RSSetViewports(1,&vp);
    l->OMSetDepthBounds(.125f,.375f);
    for (UINT i=0;i<4;++i) {
        if (i==2) l->OMSetDepthBounds(0.f,1.f);
        l->RSSetScissorRects(1,&halves[i%2]);
        l->BeginQuery(queries.Get(),D3D12_QUERY_TYPE_OCCLUSION,i);
        l->DrawInstanced(3,1,0,0);
        l->EndQuery(queries.Get(),D3D12_QUERY_TYPE_OCCLUSION,i);
    }
    l->ResolveQueryData(queries.Get(),D3D12_QUERY_TYPE_OCCLUSION,0,4,rb.Get(),sizeof(UINT64));
    HR(l->Close()); ID3D12CommandList* submit[]={l.Get()};
    ComPtr<ID3D12Fence> fence; HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    q->ExecuteCommandLists(1,submit); HR(q->Signal(fence.Get(),1));
    HANDLE ev=CreateEventW(nullptr,FALSE,FALSE,nullptr); CK(ev);
    HR(fence->SetEventOnCompletion(1,ev)); CK(WaitForSingleObject(ev,10000)==WAIT_OBJECT_0); CloseHandle(ev);
    HR(d->GetDeviceRemovedReason()); HR(rb->Map(0,nullptr,(void**)&words));
    std::printf("samples L/R: narrow=%llu/%llu full=%llu/%llu\n",words[1],words[2],words[3],words[4]);
    CK(words[0]==UINT64_MAX && words[5]==UINT64_MAX);
    CK(words[1]==32 && !words[2] && words[3]==32 && words[4]==32);
    rb->Unmap(0,&none); std::puts("PASS host OMSetDepthBounds(0,1)"); return 0;
}
