#include <windows.h>
#include <cstdio>
#include <d3d12.h>
#include <vulkan/vulkan.h>
extern "C" __declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *,SIZE_T,DWORD,ULONGLONG,DWORD);
#define CK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d %s\n",__LINE__,#x);return 1;}}while(0)
int main()
{
    constexpr SIZE_T n=65536;
    auto lib=LoadLibraryW(L"vulkan-1.dll"); CK(lib);
#define FN(v) auto v=(PFN_##v)(void*)GetProcAddress(lib,#v);CK(v)
    FN(vkCreateInstance);FN(vkEnumeratePhysicalDevices);FN(vkGetPhysicalDeviceProperties);FN(vkCreateDevice);
    FN(vkGetDeviceProcAddr);FN(vkAllocateMemory);FN(vkFreeMemory);FN(vkDestroyDevice);FN(vkDestroyInstance);
    VkApplicationInfo app={}; app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=VK_API_VERSION_1_2;
    VkInstanceCreateInfo ic={};ic.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;ic.pApplicationInfo=&app;
    VkInstance in; CK(vkCreateInstance(&ic,nullptr,&in)==VK_SUCCESS);
    VkPhysicalDevice pd;uint32_t count=1;CK(vkEnumeratePhysicalDevices(in,&count,&pd)==VK_SUCCESS);
    VkPhysicalDeviceProperties pp;vkGetPhysicalDeviceProperties(pd,&pp);CK(pp.vendorID==0x10de && pp.deviceID==0x2484);
    float pri=1;VkDeviceQueueCreateInfo q={};q.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;q.queueCount=1;q.pQueuePriorities=&pri;
    const char *ext="VK_EXT_external_memory_host";VkDeviceCreateInfo dc={};dc.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dc.pQueueCreateInfos=&q;dc.queueCreateInfoCount=1;dc.ppEnabledExtensionNames=&ext;dc.enabledExtensionCount=1;
    VkDevice dev;CK(vkCreateDevice(pd,&dc,nullptr,&dev)==VK_SUCCESS);
    auto props=(PFN_vkGetMemoryHostPointerPropertiesEXT)vkGetDeviceProcAddr(dev,"vkGetMemoryHostPointerPropertiesEXT");CK(props);
    CK(!XMemVirtualAlloc(nullptr,n,0x20401000,0x283,0x42404));
    ID3D12Device3 *d=nullptr;CK(SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d))));
    using Maker=HRESULT(WINAPI*)(ID3D12Device3*,SIZE_T,void**);
    auto make=(Maker)(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettCpuControlViewV1");
    void *view=nullptr;CK(make && SUCCEEDED(make(d,n,&view))); BYTE *p=(BYTE*)view;CK(p);
    void *plain=VirtualAlloc(nullptr,n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);CK(plain);
    for(unsigned k=0;k<3;k++){
        void *host=k==0?plain:k==1?p:p+32768; SIZE_T len=k==2?32768:n;
        VkImportMemoryHostPointerInfoEXT hp={};hp.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT;
        hp.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;hp.pHostPointer=host;
        VkMemoryHostPointerPropertiesEXT bits={};bits.sType=VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT;
        CK(props(dev,hp.handleType,host,&bits)==VK_SUCCESS && bits.memoryTypeBits);
        VkMemoryAllocateInfo ma={};ma.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;ma.pNext=&hp;ma.allocationSize=len;
        ma.memoryTypeIndex=__builtin_ctz(bits.memoryTypeBits);
        VkDeviceMemory m=VK_NULL_HANDLE;VkResult r=vkAllocateMemory(dev,&ma,nullptr,&m);
        printf("RTX host import k=%u result=%d\n",k,r);
        if(k==0){CK(r==VK_SUCCESS && m);vkFreeMemory(dev,m,nullptr);}
        else CK(r==VK_ERROR_INVALID_EXTERNAL_HANDLE);
    }
    CK(VirtualFree(p,n,MEM_RELEASE) && VirtualFree(plain,0,MEM_RELEASE));
    vkDestroyDevice(dev,nullptr);vkDestroyInstance(in,nullptr);FreeLibrary(lib);d->Release();
    puts("PASS actual Vulkan host-pointer import: ordinary accepted, tagged base and interior rejected");
}
