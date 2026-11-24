#include <windows.h>
#include <d3d12.h>
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <cstdio>

#define CK(e) do{auto c=(e);if(c){printf("FAIL %s = %#lx\n",#e,(unsigned long)c);return 2;}}while(0)
/* Public vkd3d-proton ID3D12DXVKInteropDevice3 IDL order; use only named methods. */
struct Interop : IUnknown {
    virtual void a()=0; virtual void b()=0; virtual void c()=0; virtual void d()=0;
    virtual HRESULT GetVulkanHandles(VkInstance*,VkPhysicalDevice*,VkDevice*)=0;
    virtual void e()=0;virtual void f()=0;virtual void g()=0;virtual void h()=0;virtual void i()=0;
    virtual void j()=0;virtual void k()=0;virtual void l()=0;virtual void m()=0;virtual void n()=0;
    virtual void o()=0;virtual void p()=0;
    virtual HRESULT GetVulkanHeapInfo(ID3D12Heap*,UINT64*,UINT64*,UINT*)=0;
};
const GUID iid3={0x22a70184,0xa6a4,0x4c24,{0xbf,0x97,0x7d,0x6d,0xf9,0xf1,0x2d,0x8a}};
int main(){
    setbuf(stdout,nullptr);
    constexpr UINT64 size=0x89700000;
    ID3D12Device *d;
    CK(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)));
    Interop *ix=nullptr;
    CK(d->QueryInterface(iid3,(void**)&ix));
    VkInstance vi;VkPhysicalDevice pd;VkDevice vd;
    CK(ix->GetVulkanHandles(&vi,&pd,&vd));
    HMODULE vulkan=LoadLibraryW(L"vulkan-1.dll");
    auto proc=(PFN_vkGetDeviceProcAddr)(void*)GetProcAddress(vulkan,"vkGetDeviceProcAddr");
    auto iproc=(PFN_vkGetInstanceProcAddr)(void*)GetProcAddress(vulkan,"vkGetInstanceProcAddr");
    if (!proc || !iproc) return 2;
    auto props=(PFN_vkGetPhysicalDeviceMemoryProperties)iproc(vi,"vkGetPhysicalDeviceMemoryProperties");
    VkPhysicalDeviceMemoryProperties mp;props(pd,&mp);
    D3D12_HEAP_DESC hd={};hd.SizeInBytes=size;hd.Properties.Type=D3D12_HEAP_TYPE_CUSTOM;
    hd.Properties.CPUPageProperty=D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
    hd.Properties.MemoryPoolPreference=D3D12_MEMORY_POOL_L0;hd.Properties.CreationNodeMask=hd.Properties.VisibleNodeMask=1;
    hd.Flags=(D3D12_HEAP_FLAGS)(D3D12_HEAP_FLAG_SHARED|D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS);
    ID3D12Heap*heap;CK(d->CreateHeap(&hd,IID_PPV_ARGS(&heap)));
    D3D12_RESOURCE_DESC rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=size;rd.Height=rd.SampleDesc.Count=rd.MipLevels=rd.DepthOrArraySize=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource *r;CK(d->CreatePlacedResource(heap,0,&rd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&r)));
    UINT64 raw,off;UINT type;CK(ix->GetVulkanHeapInfo(heap,&raw,&off,&type));
    printf("heap VkDeviceMemory=%#llx type=%u flags=%#x offset=%#llx\n",raw,type,mp.memoryTypes[type].propertyFlags,off);
    if (off || (mp.memoryTypes[type].propertyFlags&14)!=6) return 3;
    auto get=(PFN_vkGetMemoryWin32HandleKHR)proc(vd,"vkGetMemoryWin32HandleKHR");
    auto al=(PFN_vkAllocateMemory)proc(vd,"vkAllocateMemory");
    auto map=(PFN_vkMapMemory)proc(vd,"vkMapMemory");
    auto unmap=(PFN_vkUnmapMemory)proc(vd,"vkUnmapMemory");
    auto fre=(PFN_vkFreeMemory)proc(vd,"vkFreeMemory");
    if(!get||!al||!map||!unmap||!fre) return 4;
    VkMemoryGetWin32HandleInfoKHR gi={};gi.sType=VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;gi.memory=(VkDeviceMemory)raw;gi.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    HANDLE handle=nullptr;CK(get(vd,&gi,&handle));
    printf("heap Vulkan export handle=%p\n",handle);
    if(!handle){
        r->Release(); heap->Release();heap=nullptr;
        CK(d->CreateCommittedResource(&hd.Properties,D3D12_HEAP_FLAG_SHARED,&rd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&r)));
        CK(d->CreateSharedHandle(r,nullptr,GENERIC_ALL,nullptr,&handle));
        printf("committed resource export handle=%p\n",handle);
        if (!handle) return 5;
    }
    VkImportMemoryWin32HandleInfoKHR im={};im.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;im.handleType=gi.handleType;im.handle=handle;
    VkMemoryAllocateFlagsInfo afi={}; afi.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    afi.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT; afi.pNext=&im;
    VkMemoryAllocateInfo ai={};ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ai.pNext=&afi;ai.allocationSize=size;ai.memoryTypeIndex=type;
    VkDeviceMemory backing;CK(al(vd,&ai,nullptr,&backing));
    void *alias;CK(map(vd,backing,0,size,0,&alias));
    void *cpu;CK(r->Map(0,nullptr,&cpu));
    volatile DWORD *a=(volatile DWORD*)((BYTE*)alias+size-4),*b=(volatile DWORD*)((BYTE*)cpu+size-4);
    *a=0x465a9598;MemoryBarrier();if(cpu==alias||*b!=0x465a9598)return 5;
    D3D12_HEAP_PROPERTIES report;D3D12_HEAP_FLAGS flags;CK(r->GetHeapProperties(&report,&flags));
    if (report.CPUPageProperty!=D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE || !r->GetGPUVirtualAddress())return 6;
    D3D12_HEAP_PROPERTIES rb={};rb.Type=D3D12_HEAP_TYPE_READBACK;rb.CreationNodeMask=rb.VisibleNodeMask=1;
    rd.Width=65536;
    ID3D12Resource *out;
    CK(d->CreateCommittedResource(&rb,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out)));
    ID3D12CommandQueue *queue;ID3D12CommandAllocator *alloc;ID3D12GraphicsCommandList *list;
    ID3D12Fence *gate,*done;
    D3D12_COMMAND_QUEUE_DESC qd={};qd.Type=D3D12_COMMAND_LIST_TYPE_COPY;
    CK(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));CK(d->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&alloc)));
    CK(d->CreateCommandList(0,qd.Type,alloc,nullptr,IID_PPV_ARGS(&list)));
    CK(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    CK(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));
    list->CopyBufferRegion(out,0,r,size-4,4);CK(list->Close());ID3D12CommandList *batch[]={list};
    CK(queue->Wait(gate,1));queue->ExecuteCommandLists(1,batch);CK(queue->Signal(done,1));
    unmap(vd,backing);fre(vd,backing,nullptr);CloseHandle(handle);
    CK(gate->Signal(1));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return 7;
    CK(done->SetEventOnCompletion(1,event));if(WaitForSingleObject(event,15000)!=WAIT_OBJECT_0)return 7;
    CK(d->GetDeviceRemovedReason());void *result;CK(out->Map(0,nullptr,&result));if(*(DWORD*)result!=0x465a9598)return 7;
    out->Unmap(0,nullptr);CloseHandle(event);done->Release();gate->Release();list->Release();alloc->Release();queue->Release();out->Release();
    printf("PASS D3D12 heap cpuPage=%u, Vulkan independent aliases %p %p, GPU read exact last word after Vulkan alias release\n",report.CPUPageProperty,cpu,alias);
    r->Unmap(0,nullptr);
    r->Release();if(heap) heap->Release();ix->Release();d->Release();return 0;
}
