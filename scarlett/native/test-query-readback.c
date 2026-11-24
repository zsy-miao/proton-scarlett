/* White-box access only to record host commands; all guest calls dispatch by ABI slot. */
#include "d3d12_device.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while (0)
#define OK(x) CHECK((x)==S_OK)
#define SLOT(i,offset,type) ((type)((void *const *)(i)->lpVtbl)[(offset)/8])

int main(void)
{
    typedef HRESULT (WINAPI *Create)(IUnknown *,const void *,UINT,const void *,UINT,const void *,REFIID,void **);
    typedef HRESULT (WINAPI *Map)(IUnknown *,UINT,const D3D12_RANGE *,void **);
    typedef void (WINAPI *Unmap)(IUnknown *,UINT,const D3D12_RANGE *);
    const IID device_id={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    DWORD params[20]={[0]=0x20004,[2]=0x100000,[10]=1,[15]=0x20};
    IUnknown *dev,*obj,*alias;
    OK(D3D12XboxCreateDevice(NULL,params,&device_id,(void **)&dev));
    Create create=SLOT(dev,0xd8,Create);
    D3D12_HEAP_PROPERTIES hp={.Type=D3D12_HEAP_TYPE_READBACK,.CreationNodeMask=1,.VisibleNodeMask=1};
    enum { width=4*88 };
    D3D12_RESOURCE_DESC rd={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=width,.Height=1,
        .DepthOrArraySize=1,.MipLevels=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    void *out;
#define REJECT(props,flags,desc,state,clear,iid,hr) do { out=dev; CHECK( \
        create(dev,props,flags,desc,state,clear,iid,&out)==(hr) && !out); } while (0)
    CHECK(create(dev,&hp,0,&rd,0x400,NULL,&IID_ID3D12Resource,NULL)==E_POINTER);
    REJECT(NULL,0,&rd,0x400,NULL,&IID_ID3D12Resource,E_INVALIDARG);
    REJECT(&hp,0,NULL,0x400,NULL,&IID_ID3D12Resource,E_INVALIDARG);
    REJECT((void *)1,0,&rd,0x400,NULL,&IID_ID3D12Resource,E_POINTER);
    REJECT(&hp,0,(void *)1,0x400,NULL,&IID_ID3D12Resource,E_POINTER);
    REJECT(&hp,0,&rd,0x400,NULL,NULL,E_INVALIDARG);
    REJECT(&hp,0,&rd,0x400,NULL,(REFIID)1,E_POINTER);
    REJECT(&hp,0,&rd,0x400,NULL,&IID_IUnknown,E_NOINTERFACE);
    REJECT(&hp,1,&rd,0x400,NULL,&IID_ID3D12Resource,E_NOTIMPL);
    REJECT(&hp,0,&rd,0,NULL,&IID_ID3D12Resource,E_NOTIMPL);
    REJECT(&hp,0,&rd,0x400,&rd,&IID_ID3D12Resource,E_NOTIMPL);
#define CHANGE(field,value,hr) do { __typeof__(field) orig=field; field=value; \
        REJECT(&hp,0,&rd,0x400,NULL,&IID_ID3D12Resource,hr); field=orig; } while (0)
    CHANGE(hp.Type,D3D12_HEAP_TYPE_UPLOAD,E_NOTIMPL); CHANGE(hp.CPUPageProperty,1,E_NOTIMPL);
    CHANGE(hp.MemoryPoolPreference,1,E_NOTIMPL); CHANGE(hp.CreationNodeMask,0,E_NOTIMPL); CHANGE(hp.VisibleNodeMask,2,E_NOTIMPL);
    CHANGE(rd.Width,0,E_INVALIDARG); CHANGE(rd.Width,12,E_INVALIDARG); CHANGE(rd.Width,1ull<<32,E_INVALIDARG);
    CHANGE(rd.Dimension,D3D12_RESOURCE_DIMENSION_TEXTURE2D,E_NOTIMPL); CHANGE(rd.Alignment,65536,E_NOTIMPL);
    CHANGE(rd.Height,2,E_NOTIMPL); CHANGE(rd.DepthOrArraySize,2,E_NOTIMPL); CHANGE(rd.MipLevels,2,E_NOTIMPL);
    CHANGE(rd.Format,DXGI_FORMAT_R32_UINT,E_NOTIMPL); CHANGE(rd.SampleDesc.Count,2,E_NOTIMPL);
    CHANGE(rd.SampleDesc.Quality,1,E_NOTIMPL); CHANGE(rd.Layout,0,E_NOTIMPL); CHANGE(rd.Flags,1,E_NOTIMPL);
    /* The title did not write any trailing descriptor padding. */
    BYTE *pages=VirtualAlloc(NULL,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old;
    CHECK(pages && VirtualProtect(pages+4096,4096,PAGE_NOACCESS,&old));
    size_t n=offsetof(D3D12_RESOURCE_DESC,Flags)+sizeof rd.Flags;
    void *wire=memcpy(pages+4096-n,&rd,n);
    OK(create(dev,&hp,0,wire,0x400,NULL,&IID_ID3D12Resource,(void **)&obj));
    CHECK(VirtualFree(pages,0,MEM_RELEASE));
    OK(IUnknown_QueryInterface(obj,&IID_IUnknown,(void **)&alias)); CHECK(obj==alias && IUnknown_Release(alias)==1);
    OK(IUnknown_QueryInterface(obj,&IID_ID3D12Resource,(void **)&alias)); CHECK(obj==alias && IUnknown_Release(alias)==1);
    CHECK(IUnknown_QueryInterface(obj,&IID_ID3D12Device,&out)==E_NOINTERFACE && !out);
    ID3D12Resource *dst=CONTAINING_RECORD(obj,struct query_readback,iface)->host,*upload;
    ID3D12Device *host;
    OK(ID3D12Resource_GetDevice(dst,&IID_ID3D12Device,(void **)&host));
    CHECK(host==CONTAINING_RECORD(dev,struct xbox_device,iface)->host && (void *)dst!=(void *)obj);
    CHECK(IUnknown_Release(dev)==1);
    D3D12_HEAP_FLAGS heap_flags;
    OK(ID3D12Resource_GetHeapProperties(dst,&hp,&heap_flags));
    CHECK(hp.Type==D3D12_HEAP_TYPE_READBACK && !heap_flags);
    hp.Type=D3D12_HEAP_TYPE_UPLOAD;
    OK(ID3D12Device_CreateCommittedResource(host,&hp,0,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,NULL,
            &IID_ID3D12Resource,(void **)&upload));
    UINT64 expected[width/8],*words;
    for (unsigned i=0;i<ARRAYSIZE(expected);i++) expected[i]=0xc5a5000000000000ull+i;
    OK(ID3D12Resource_Map(upload,0,NULL,(void **)&words)); memcpy(words,expected,width); ID3D12Resource_Unmap(upload,0,NULL);
    ID3D12CommandQueue *q;
    ID3D12CommandAllocator *a;
    ID3D12GraphicsCommandList *l;
    ID3D12Fence *f;
    ID3D12QueryHeap *ts;
    D3D12_COMMAND_QUEUE_DESC qd={.Type=D3D12_COMMAND_LIST_TYPE_DIRECT};
    D3D12_QUERY_HEAP_DESC hd={D3D12_QUERY_HEAP_TYPE_TIMESTAMP,2,1};
    OK(ID3D12Device_CreateQueryHeap(host,&hd,&IID_ID3D12QueryHeap,(void **)&ts));
    OK(ID3D12Device_CreateCommandQueue(host,&qd,&IID_ID3D12CommandQueue,(void **)&q));
    OK(ID3D12Device_CreateCommandAllocator(host,qd.Type,&IID_ID3D12CommandAllocator,(void **)&a));
    OK(ID3D12Device_CreateCommandList(host,0,qd.Type,a,NULL,&IID_ID3D12GraphicsCommandList,(void **)&l));
    OK(ID3D12Device_CreateFence(host,0,0,&IID_ID3D12Fence,(void **)&f));
    ID3D12GraphicsCommandList_EndQuery(l,ts,D3D12_QUERY_TYPE_TIMESTAMP,0);
    ID3D12GraphicsCommandList_CopyBufferRegion(l,dst,0,upload,0,width);
    ID3D12GraphicsCommandList_EndQuery(l,ts,D3D12_QUERY_TYPE_TIMESTAMP,1);
    ID3D12GraphicsCommandList_ResolveQueryData(l,ts,D3D12_QUERY_TYPE_TIMESTAMP,0,2,dst,8);
    OK(ID3D12GraphicsCommandList_Close(l));
    ID3D12CommandList *submit=(ID3D12CommandList *)l;
    ID3D12CommandQueue_ExecuteCommandLists(q,1,&submit); OK(ID3D12CommandQueue_Signal(q,f,1));
    HANDLE e=CreateEventW(NULL,FALSE,FALSE,NULL); CHECK(e);
    OK(ID3D12Fence_SetEventOnCompletion(f,1,e)); CHECK(WaitForSingleObject(e,10000)==WAIT_OBJECT_0);
    Map map=SLOT(obj,0x40,Map);
    D3D12_RANGE read={0,width};
    CHECK(map(obj,0,&read,NULL)==E_POINTER);
#define BADMAP(sub,r,hr) do { out=obj; CHECK(map(obj,sub,r,&out)==(hr) && !out); } while (0)
    BADMAP(1,&read,E_INVALIDARG); BADMAP(0,NULL,E_INVALIDARG); BADMAP(0,(void *)1,E_POINTER);
    read.Begin=8; BADMAP(0,&read,E_INVALIDARG); read.Begin=0;
    read.End--; BADMAP(0,&read,E_INVALIDARG); read.End=width+1; BADMAP(0,&read,E_INVALIDARG); read.End=width;
    OK(map(obj,0,&read,(void **)&words));
    CHECK(words[1] && words[2]>=words[1] && words[1]!=expected[1] && words[2]!=expected[2] && words[2]!=UINT64_MAX);
    for (unsigned i=0;i<ARRAYSIZE(expected);i++) if (i!=1 && i!=2) CHECK(words[i]==expected[i]);
    printf("fenced readback: %llu..%llu, %u other qwords verified\n",words[1],words[2],(unsigned)ARRAYSIZE(expected)-2);
    SLOT(obj,0x48,Unmap)(obj,0,NULL);
    CloseHandle(e); ID3D12Fence_Release(f); ID3D12GraphicsCommandList_Release(l); ID3D12CommandAllocator_Release(a);
    ID3D12CommandQueue_Release(q); ID3D12QueryHeap_Release(ts); ID3D12Resource_Release(upload);
    CHECK(!IUnknown_Release(obj)); CHECK(!ID3D12Device_Release(host));
    puts("PASS device+0xd8: same-device READBACK, guarded creation and Map, fenced copy and query resolve");
    return 0;
}
