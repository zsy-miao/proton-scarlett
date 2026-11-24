/* Public factory/slot test. XS_TEST_LINKED additionally exercises the wrapped signature on a host list. */
#define INITGUID
#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#ifdef XS_TEST_LINKED
#include <d3dcompiler.h>
#include "queue/queue.h"
#else
__declspec(dllimport)
#endif
HRESULT WINAPI D3D12XboxCreateDevice(void *, const void *, REFIID, void **);

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); return 1; } } while (0)
#define OK(e) do { HRESULT hr_ = (e); if (hr_ != S_OK) { fprintf(stderr, "FAIL line %d: %s = %08lx\n", \
    __LINE__, #e, (unsigned long)hr_); return 1; } } while (0)
typedef HRESULT (WINAPI *CreateSig)(IUnknown *, const D3D12_COMMAND_SIGNATURE_DESC *, IUnknown *, REFIID, void **);
static const IID console_id = {0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};

#ifdef XS_TEST_LINKED
static HRESULT buffer(ID3D12Device *d, D3D12_HEAP_TYPE type, UINT64 bytes, ID3D12Resource **out)
{
    D3D12_HEAP_PROPERTIES heap = {.Type=type, .CreationNodeMask=1, .VisibleNodeMask=1};
    D3D12_RESOURCE_DESC desc = {.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER, .Width=bytes, .Height=1,
        .DepthOrArraySize=1, .MipLevels=1, .SampleDesc={1,0}, .Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    return ID3D12Device_CreateCommittedResource(d, &heap, 0, &desc, type == D3D12_HEAP_TYPE_UPLOAD ?
            D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)out);
}

static int dispatch(ID3D12Device *d, IUnknown *wrapped)
{
    static const char source[] = "[numthreads(4,1,1)] void main() {}";
    ID3D12CommandSignature *sig = xs_signature_host(wrapped);
    ID3D12CommandQueue *q;
    D3D12_COMMAND_QUEUE_DESC qd = {0};
    ID3D12CommandAllocator *a;
    ID3D12GraphicsCommandList *l;
    ID3D12PipelineState *pso;
    ID3D12RootSignature *root;
    ID3D12Resource *args, *readback;
    D3D12_ROOT_SIGNATURE_DESC rs = {0};
    D3D12_QUERY_HEAP_DESC qh = {D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS,1,0};
    ID3D12QueryHeap *query;
    ID3DBlob *blob, *cs;
    ID3D12Fence *fence;
    HANDLE event = CreateEventW(NULL, FALSE, FALSE, NULL);
    CHECK(event && sig && !xs_signature_host((IUnknown *)d));
    OK(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, NULL));
    OK(ID3D12Device_CreateRootSignature(d, 0, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
            &IID_ID3D12RootSignature, (void **)&root));
    ID3D10Blob_Release(blob);
    OK(D3DCompile(source, sizeof(source)-1, NULL, NULL, NULL, "main", "cs_5_0", 0, 0, &cs, NULL));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {.pRootSignature=root,
        .CS={ID3D10Blob_GetBufferPointer(cs), ID3D10Blob_GetBufferSize(cs)}};
    OK(ID3D12Device_CreateComputePipelineState(d, &pd, &IID_ID3D12PipelineState, (void **)&pso));
    ID3D10Blob_Release(cs);
    OK(ID3D12Device_CreateCommandQueue(d, &qd, &IID_ID3D12CommandQueue, (void **)&q));
    OK(ID3D12Device_CreateCommandAllocator(d, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&a));
    OK(ID3D12Device_CreateCommandList(d, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, a, pso,
            &IID_ID3D12GraphicsCommandList, (void **)&l));
    OK(ID3D12Device_CreateQueryHeap(d, &qh, &IID_ID3D12QueryHeap, (void **)&query));
    OK(ID3D12Device_CreateFence(d, 0, 0, &IID_ID3D12Fence, (void **)&fence));
    OK(buffer(d, D3D12_HEAP_TYPE_UPLOAD, 24, &args));
    OK(buffer(d, D3D12_HEAP_TYPE_READBACK, sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS), &readback));
    void *map;
    OK(ID3D12Resource_Map(args, 0, NULL, &map));
    const UINT data[] = {0,0,0,3,2,1};
    memcpy(map,data,sizeof(data));
    ID3D12Resource_Unmap(args, 0, NULL);
    ID3D12GraphicsCommandList_SetComputeRootSignature(l, root);
    ID3D12GraphicsCommandList_BeginQuery(l, query, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
    ID3D12GraphicsCommandList_ExecuteIndirect(l, sig, 1, args, 12, NULL, 0);
    ID3D12GraphicsCommandList_EndQuery(l, query, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
    ID3D12GraphicsCommandList_ResolveQueryData(l, query, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, 1, readback, 0);
    OK(ID3D12GraphicsCommandList_Close(l));
    ID3D12CommandList *submit = (ID3D12CommandList *)l;
    ID3D12CommandQueue_ExecuteCommandLists(q, 1, &submit);
    OK(ID3D12CommandQueue_Signal(q, fence, 1));
    OK(ID3D12Fence_SetEventOnCompletion(fence, 1, event));
    CHECK(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0);
    OK(ID3D12Resource_Map(readback, 0, NULL, &map));
    UINT64 count = ((D3D12_QUERY_DATA_PIPELINE_STATISTICS *)map)->CSInvocations;
    ID3D12Resource_Unmap(readback, 0, NULL);
    printf("ExecuteIndirect GPU CSInvocations: %llu (expected 24)\n", (unsigned long long)count);
    CHECK(count == 24);
    ID3D12GraphicsCommandList_Release(l); ID3D12CommandAllocator_Release(a);
    ID3D12Resource_Release(args); ID3D12Resource_Release(readback); ID3D12QueryHeap_Release(query);
    ID3D12PipelineState_Release(pso); ID3D12RootSignature_Release(root);
    ID3D12Fence_Release(fence); ID3D12CommandQueue_Release(q); CloseHandle(event);
    return 0;
}
#endif

int main(void)
{
    DWORD params[20] = {[0]=0x20004,[2]=0x100000,[10]=1,[15]=0x20};
    ID3D12Device *host;
    IUnknown *device;
    OK(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void **)&host));
    OK(D3D12XboxCreateDevice(NULL, params, &console_id, (void **)&device));
    CreateSig create = (CreateSig)((void *const *)device->lpVtbl)[0x148 / 8];
    UINT types[] = {0,1,0x69,2,10}, strides[] = {16,20,12,12,12};
    D3D12_INDIRECT_ARGUMENT_DESC arg = {0};
    D3D12_COMMAND_SIGNATURE_DESC desc = {.NumArgumentDescs=1,.pArgumentDescs=&arg};
    void *out;
    for (unsigned i = 0; i < ARRAYSIZE(types); ++i) {
        arg.Type = types[i]; desc.ByteStride = strides[i]; out = device;
        HRESULT hr = create(device, &desc, NULL, &IID_ID3D12CommandSignature, &out);
        if (arg.Type == 0x69) { CHECK(hr == E_NOTIMPL && !out); continue; }
        if (arg.Type == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS7 caps;
            HRESULT feature = ID3D12Device_CheckFeatureSupport(host, D3D12_FEATURE_D3D12_OPTIONS7, &caps, sizeof(caps));
            if (FAILED(feature) || !caps.MeshShaderTier) { CHECK(FAILED(hr) && !out); continue; }
        }
        CHECK(hr == S_OK && out);
        IUnknown *sig = out;
        OK(IUnknown_QueryInterface(sig, &IID_IUnknown, &out)); CHECK(out == sig); IUnknown_Release(sig);
        OK(IUnknown_QueryInterface(sig, &IID_ID3D12CommandSignature, &out)); CHECK(out == sig); IUnknown_Release(sig);
        CHECK(IUnknown_QueryInterface(sig, &IID_ID3D12Device, &out) == E_NOINTERFACE && !out);
#ifdef XS_TEST_LINKED
        if (arg.Type == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH) CHECK(!dispatch(host, sig));
#endif
        CHECK(!IUnknown_Release(sig));
    }
    arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH; desc.ByteStride = 12;
    CHECK(create(device, &desc, NULL, &IID_IUnknown, &out) == E_NOINTERFACE && !out);
    CHECK(create(device, &desc, device, &IID_ID3D12CommandSignature, &out) == E_NOTIMPL && !out);
    desc.NodeMask = 1;
    CHECK(create(device, &desc, NULL, &IID_ID3D12CommandSignature, &out) == E_NOTIMPL && !out);
    desc.NodeMask = 0; desc.ByteStride = 8;
    CHECK(create(device, &desc, NULL, &IID_ID3D12CommandSignature, &out) == E_NOTIMPL && !out);
    desc.pArgumentDescs = NULL;
    CHECK(create(device, &desc, NULL, &IID_ID3D12CommandSignature, &out) == E_POINTER && !out);
    CHECK(!IUnknown_Release(device));
    CHECK(!ID3D12Device_Release(host));
    puts("PASS five signature creation requests and COM lifetime");
    return 0;
}
