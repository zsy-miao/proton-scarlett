#include <vulkan/vulkan.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#ifdef NDEBUG
#error This qualification test needs assertions enabled
#endif
#include <stdlib.h>
#include <assert.h>
#define V(x) do{VkResult r=(x);if(r){fprintf(stderr,"%d %s => %d\n",__LINE__,#x,r);exit(1);}}while(0)
#define ST(x) .sType=VK_STRUCTURE_TYPE_##x

/* Even a valid imported buffer is insufficient: retail requests both MSAA SRV and RTV. */
static void check_image(VkPhysicalDevice pd,VkDevice d,VkExternalMemoryHandleTypeFlagBits handle,
        VkImageTiling tiling,const uint64_t *modifier,VkImageUsageFlags usage)
{
    VkPhysicalDeviceImageDrmFormatModifierInfoEXT dm={ST(PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT),
        .drmFormatModifier=modifier?*modifier:0,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VkPhysicalDeviceExternalImageFormatInfo ex={ST(PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO),
        .pNext=modifier?&dm:NULL,.handleType=handle};
    VkPhysicalDeviceImageFormatInfo2 info={ST(PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2),.pNext=&ex,
        .format=VK_FORMAT_R8G8B8A8_UNORM,.type=VK_IMAGE_TYPE_2D,.tiling=tiling,.usage=usage};
    VkExternalImageFormatProperties xp={ST(EXTERNAL_IMAGE_FORMAT_PROPERTIES)};
    VkImageFormatProperties2 fp={ST(IMAGE_FORMAT_PROPERTIES_2),.pNext=&xp};
    VkResult result=vkGetPhysicalDeviceImageFormatProperties2(pd,&info,&fp);
    printf("usage=%#x handle=%#x tiling=%u modifier=%#llx result=%d samples=%#x external=%#x",
        usage,handle,tiling,(unsigned long long)dm.drmFormatModifier,result,
        fp.imageFormatProperties.sampleCounts,xp.externalMemoryProperties.externalMemoryFeatures);
    if (result==VK_ERROR_FORMAT_NOT_SUPPORTED || (result==VK_SUCCESS && !(fp.imageFormatProperties.sampleCounts&VK_SAMPLE_COUNT_4_BIT))) {
        puts("");return;
    }
    assert(result==VK_SUCCESS && !modifier && tiling==VK_IMAGE_TILING_OPTIMAL &&
        (handle==VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT || handle==VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT));
    VkExternalMemoryImageCreateInfo ext={ST(EXTERNAL_MEMORY_IMAGE_CREATE_INFO),.handleTypes=handle};
    VkImageCreateInfo ci={ST(IMAGE_CREATE_INFO),.pNext=&ext,.imageType=info.type,.format=info.format,.extent={1,1,1},
        .mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_4_BIT,.tiling=tiling,.usage=usage};
    VkImage image;V(vkCreateImage(d,&ci,NULL,&image));
    VkImageMemoryRequirementsInfo2 im={ST(IMAGE_MEMORY_REQUIREMENTS_INFO_2),.image=image};
    VkMemoryDedicatedRequirements dedicated={ST(MEMORY_DEDICATED_REQUIREMENTS)};
    VkMemoryRequirements2 req={ST(MEMORY_REQUIREMENTS_2),.pNext=&dedicated};vkGetImageMemoryRequirements2(d,&im,&req);
    printf(" bytes=%#lx align=%#lx bits=%#x dedicated=%u\n",(unsigned long)req.memoryRequirements.size,
        (unsigned long)req.memoryRequirements.alignment,req.memoryRequirements.memoryTypeBits,dedicated.requiresDedicatedAllocation);
    assert(dedicated.requiresDedicatedAllocation);vkDestroyImage(d,image,NULL);
}

int main(void){
    const VkDeviceSize span=0x89700000;const unsigned off=0xe800000;
    VkApplicationInfo app={ST(APPLICATION_INFO),.apiVersion=VK_API_VERSION_1_2};VkInstanceCreateInfo ic={ST(INSTANCE_CREATE_INFO),.pApplicationInfo=&app};
    VkInstance in;V(vkCreateInstance(&ic,0,&in));uint32_t count=1;VkPhysicalDevice pd;V(vkEnumeratePhysicalDevices(in,&count,&pd));
    VkPhysicalDeviceProperties pp;vkGetPhysicalDeviceProperties(pd,&pp);assert(pp.vendorID==0x1002&&pp.deviceID==0x164e && pp.driverVersion==0x06803000);puts(pp.deviceName);
    float p=1;VkDeviceQueueCreateInfo qc={ST(DEVICE_QUEUE_CREATE_INFO),.queueCount=1,.pQueuePriorities=&p};
    const char *exts[]={"VK_KHR_external_memory_fd","VK_EXT_external_memory_dma_buf","VK_EXT_external_memory_host","VK_EXT_image_drm_format_modifier"};
    VkDeviceCreateInfo dc={ST(DEVICE_CREATE_INFO),.queueCreateInfoCount=1,.pQueueCreateInfos=&qc,.enabledExtensionCount=4,.ppEnabledExtensionNames=exts};
    VkDevice d;V(vkCreateDevice(pd,&dc,0,&d));VkQueue q;vkGetDeviceQueue(d,0,0,&q);
    VkDrmFormatModifierPropertiesListEXT mods={ST(DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT)};
    VkFormatProperties2 fmt={ST(FORMAT_PROPERTIES_2),.pNext=&mods};vkGetPhysicalDeviceFormatProperties2(pd,VK_FORMAT_R8G8B8A8_UNORM,&fmt);
    assert(mods.drmFormatModifierCount);mods.pDrmFormatModifierProperties=calloc(mods.drmFormatModifierCount,sizeof(*mods.pDrmFormatModifierProperties));assert(mods.pDrmFormatModifierProperties);
    vkGetPhysicalDeviceFormatProperties2(pd,VK_FORMAT_R8G8B8A8_UNORM,&fmt);
    VkExternalMemoryHandleTypeFlagBits types[]={VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_MAPPED_FOREIGN_MEMORY_BIT_EXT,VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
    VkImageUsageFlags actual=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    for(unsigned i=0;i<2;i++) {
        VkImageUsageFlags use=actual|(i?VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT:0);
        for(unsigned h=0;h<sizeof(types)/sizeof(*types);h++){
            check_image(pd,d,types[h],VK_IMAGE_TILING_LINEAR,NULL,use);
            check_image(pd,d,types[h],VK_IMAGE_TILING_OPTIMAL,NULL,use);
        }
        for(unsigned j=0;j<mods.drmFormatModifierCount;j++)check_image(pd,d,VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
            VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,&mods.pDrmFormatModifierProperties[j].drmFormatModifier,use);
    }
    free(mods.pDrmFormatModifierProperties);
    unsigned ht=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    VkExternalMemoryBufferCreateInfo eb={ST(EXTERNAL_MEMORY_BUFFER_CREATE_INFO),.handleTypes=ht};
    VkBufferCreateInfo bc={ST(BUFFER_CREATE_INFO),.pNext=&eb,.size=span,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VkBuffer pool;V(vkCreateBuffer(d,&bc,0,&pool));VkMemoryRequirements req;vkGetBufferMemoryRequirements(d,pool,&req);assert(req.size==span && (req.memoryTypeBits&4));
    VkExportMemoryAllocateInfo ex={ST(EXPORT_MEMORY_ALLOCATE_INFO),.handleTypes=ht};
    VkMemoryAllocateInfo ma={ST(MEMORY_ALLOCATE_INFO),.pNext=&ex,.allocationSize=span,.memoryTypeIndex=2};
    VkDeviceMemory owner;V(vkAllocateMemory(d,&ma,0,&owner));V(vkBindBufferMemory(d,pool,owner,0));
    VkMemoryGetFdInfoKHR fi={ST(MEMORY_GET_FD_INFO_KHR),.memory=owner,.handleType=ht};int fd=-1;V(((PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(d,"vkGetMemoryFdKHR"))(d,&fi,&fd));
    VkMemoryFdPropertiesKHR fdprops={ST(MEMORY_FD_PROPERTIES_KHR)};V(((PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(d,"vkGetMemoryFdPropertiesKHR"))(d,ht,fd,&fdprops));
    printf("exported WC span=%#lx type=2 fd bits=%#x buffer bits=%#x\n",(unsigned long)span,fdprops.memoryTypeBits,req.memoryTypeBits);
    assert(fdprops.memoryTypeBits & req.memoryTypeBits & 4);
    uint32_t *mapping=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_SHARED,fd,off-4096);assert(mapping!=MAP_FAILED);
    struct dma_buf_sync sync={DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE};assert(!ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync));
    uint32_t *slice=mapping+1024;slice[-1]=0x11223344;slice[0]=0;slice[1]=0x77665544;
    sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_WRITE;assert(!ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync));
    VkBuffer dst;V(vkCreateBuffer(d,&bc,NULL,&dst));VkImportMemoryFdInfoKHR imp={ST(IMPORT_MEMORY_FD_INFO_KHR),.handleType=ht,.fd=dup(fd)};
    ma.pNext=&imp;VkDeviceMemory payload;V(vkAllocateMemory(d,&ma,NULL,&payload));V(vkBindBufferMemory(d,dst,payload,0));
    vkDestroyBuffer(d,pool,NULL);vkFreeMemory(d,owner,NULL);
    VkCommandPoolCreateInfo pc={ST(COMMAND_POOL_CREATE_INFO),.queueFamilyIndex=0};VkCommandPool cp;V(vkCreateCommandPool(d,&pc,NULL,&cp));
    VkCommandBufferAllocateInfo ca={ST(COMMAND_BUFFER_ALLOCATE_INFO),.commandPool=cp,.commandBufferCount=1};VkCommandBuffer b;V(vkAllocateCommandBuffers(d,&ca,&b));
    VkCommandBufferBeginInfo bi={ST(COMMAND_BUFFER_BEGIN_INFO)};V(vkBeginCommandBuffer(b,&bi));
    vkCmdFillBuffer(b,dst,off,4,0xace0d00d);VkMemoryBarrier barrier={ST(MEMORY_BARRIER),.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(b,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);V(vkEndCommandBuffer(b));
    VkSubmitInfo si={ST(SUBMIT_INFO),.commandBufferCount=1,.pCommandBuffers=&b};V(vkQueueSubmit(q,1,&si,VK_NULL_HANDLE));V(vkQueueWaitIdle(q));
    sync.flags=DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ;assert(!ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync));
    printf("same DMA-BUF GPU fill after original buffer/memory release: guards=%08x/%08x slot=%08x\n",slice[-1],slice[1],slice[0]);
    assert(slice[-1]==0x11223344 && slice[0]==0xace0d00d && slice[1]==0x77665544);
    sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ;assert(!ioctl(fd,DMA_BUF_IOCTL_SYNC,&sync));
    munmap(mapping,65536);close(fd);vkDestroyCommandPool(d,cp,NULL);vkDestroyBuffer(d,dst,NULL);vkFreeMemory(d,payload,NULL);vkDestroyDevice(d,NULL);vkDestroyInstance(in,NULL);
}
