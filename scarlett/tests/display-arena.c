#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define V(x) do { VkResult r_=(x); if(r_){fprintf(stderr,"line %d: %d\n",__LINE__,r_);return 1;} }while(0)
#define CK(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;} }while(0)
int main(void){
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo ic={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    VkInstance in;V(vkCreateInstance(&ic,0,&in));
    uint32_t n=1; VkPhysicalDevice p;V(vkEnumeratePhysicalDevices(in,&n,&p));
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT hp={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 pp={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&hp}; vkGetPhysicalDeviceProperties2(p,&pp);
    printf("%s; imported host pointer alignment %#lx\n",pp.properties.deviceName,(unsigned long)hp.minImportedHostPointerAlignment);
    const char *ext=VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME;
    const VkExternalMemoryHandleTypeFlagBits type=VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    VkPhysicalDeviceExternalImageFormatInfo ei={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,.handleType=type};
    const VkImageUsageFlags usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkPhysicalDeviceImageFormatInfo2 fi={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,.pNext=&ei,.format=VK_FORMAT_R8G8B8A8_UNORM,.type=VK_IMAGE_TYPE_2D,.tiling=VK_IMAGE_TILING_LINEAR,.usage=usage};
    VkExternalImageFormatProperties ep={.sType=VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
    VkImageFormatProperties2 fp={.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,.pNext=&ep};
    V(vkGetPhysicalDeviceImageFormatProperties2(p,&fi,&fp));
    VkExternalMemoryFeatureFlags bits=ep.externalMemoryProperties.externalMemoryFeatures;
    printf("linear external features=%#x\n",bits);CK((bits&VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)&&!(bits&VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT));
    float priority=1;
    VkDeviceQueueCreateInfo qc={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=0,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo dc={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qc,.enabledExtensionCount=1,.ppEnabledExtensionNames=&ext};
    VkDevice d;V(vkCreateDevice(p,&dc,0,&d));
    VkExternalMemoryImageCreateInfo ec={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,.handleTypes=type};
    VkImageCreateInfo c={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.pNext=&ec,.imageType=VK_IMAGE_TYPE_2D,.format=fi.format,.extent={3840,2160,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=fi.tiling,.usage=usage};
    VkImage im[2];for(unsigned i=0;i<2;i++)V(vkCreateImage(d,&c,0,im+i));
    VkMemoryRequirements mr;vkGetImageMemoryRequirements(d,im[0],&mr);
    size_t cap=0x4000000,unit=mr.alignment>65536?mr.alignment:65536,slice=(mr.size+unit-1)&~(unit-1);
    CK(slice*2<=cap&&cap-2*slice<0x200000);
    uint8_t *raw=aligned_alloc(hp.minImportedHostPointerAlignment>65536?hp.minImportedHostPointerAlignment:65536,cap);CK(raw);memset(raw,0xa5,cap);
    PFN_vkGetMemoryHostPointerPropertiesEXT get=(PFN_vkGetMemoryHostPointerPropertiesEXT)vkGetDeviceProcAddr(d,"vkGetMemoryHostPointerPropertiesEXT");CK(get);
    VkMemoryHostPointerPropertiesEXT mp={.sType=VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};V(get(d,type,raw,&mp));
    VkPhysicalDeviceMemoryProperties m;vkGetPhysicalDeviceMemoryProperties(p,&m);
    unsigned mt=0;
    for(;mt<m.memoryTypeCount;mt++) if(((mp.memoryTypeBits&mr.memoryTypeBits)&(1u<<mt)) &&
            (m.memoryTypes[mt].propertyFlags&(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))==(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) break;
    CK(mt<m.memoryTypeCount);
    VkMemoryAllocateFlagsInfo unused={0};(void)unused;
    VkImportMemoryHostPointerInfoEXT imp={.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,.handleType=type,.pHostPointer=raw};
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&imp,.allocationSize=cap,.memoryTypeIndex=mt};
    VkDeviceMemory mem;V(vkAllocateMemory(d,&ai,0,&mem));
    for(unsigned i=0;i<2;i++)V(vkBindImageMemory(d,im[i],mem,i*slice));
    VkCommandPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=0};VkCommandPool pool;V(vkCreateCommandPool(d,&pc,0,&pool));
    VkCommandBufferAllocateInfo ac={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer cb;V(vkAllocateCommandBuffers(d,&ac,&cb));
    VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};V(vkBeginCommandBuffer(cb,&bi));
    VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    for(unsigned i=0;i<2;i++){
        VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.image=im[i],.subresourceRange=range};
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,0,0,0,1,&b);
        VkClearColorValue cl={.float32={i==0,i==1,0,1}};vkCmdClearColorImage(cb,im[i],VK_IMAGE_LAYOUT_GENERAL,&cl,1,&range);
        b.oldLayout=b.newLayout;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,0,0,0,1,&b);
    }
    V(vkEndCommandBuffer(cb));VkQueue q;vkGetDeviceQueue(d,0,0,&q);VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};
    VkFenceCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;V(vkCreateFence(d,&fc,0,&fence));V(vkQueueSubmit(q,1,&si,fence));V(vkWaitForFences(d,1,&fence,1,10000000000ull));
    VkImageSubresource sub={VK_IMAGE_ASPECT_COLOR_BIT,0,0};
    for(unsigned i=0;i<2;i++){VkSubresourceLayout l;vkGetImageSubresourceLayout(d,im[i],&sub,&l);
        uint32_t want=i?0xff00ff00:0xff0000ff;
        for(unsigned y=0;y<2160;y++){uint32_t *row=(uint32_t *)(raw+i*slice+l.offset+y*l.rowPitch);for(unsigned x=0;x<3840;x++)CK(row[x]==want);}
        printf("image%u verified 3840x2160 at guest-arena offset %#zx, pitch %#lx, color %08x\n",i,i*slice,(unsigned long)l.rowPitch,want);
    }
    for(size_t i=2*slice;i<cap;i++) CK(raw[i]==0xa5);
    printf("PASS fenced clears through imported host pointer; req=%#lx slice=%#zx remaining=%#zx untouched\n",(unsigned long)mr.size,slice,cap-2*slice);
    vkDestroyFence(d,fence,0);vkDestroyCommandPool(d,pool,0);for(unsigned i=0;i<2;i++)vkDestroyImage(d,im[i],0);vkFreeMemory(d,mem,0);free(raw);vkDestroyDevice(d,0);vkDestroyInstance(in,0);
}
