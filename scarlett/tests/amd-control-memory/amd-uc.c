#include <vulkan/vulkan.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define V(x) do { VkResult r_=(x); if(r_!=VK_SUCCESS){fprintf(stderr,"%d: %s = %d\n",__LINE__,#x,r_);exit(1);} } while(0)
#define CK(x) do {if(!(x)){fprintf(stderr,"%d: %s failed\n",__LINE__,#x);exit(2);} }while(0)
enum { SIZE=65536 };
static int has(const VkExtensionProperties *p,unsigned n,const char *name)
{for(unsigned i=0;i<n;i++)if(!strcmp(p[i].extensionName,name))return 1;return 0;}

int main(int argc,char **argv)
{
    setbuf(stdout,NULL);
    const VkExternalMemoryHandleTypeFlagBits ht=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_2};
    VkInstanceCreateInfo ic={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    VkInstance instance; V(vkCreateInstance(&ic,NULL,&instance));
    uint32_t n=1; VkPhysicalDevice pd;V(vkEnumeratePhysicalDevices(instance,&n,&pd));
    VkPhysicalDeviceProperties prop;vkGetPhysicalDeviceProperties(pd,&prop);
    VkPhysicalDeviceMemoryProperties mp;vkGetPhysicalDeviceMemoryProperties(pd,&mp);
    V(vkEnumerateDeviceExtensionProperties(pd,NULL,&n,NULL));
    VkExtensionProperties *ep=calloc(n,sizeof *ep);CK(ep);V(vkEnumerateDeviceExtensionProperties(pd,NULL,&n,ep));
    const char *exts[]={VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,VK_AMD_DEVICE_COHERENT_MEMORY_EXTENSION_NAME};
    printf("%s pci %04x:%04x driver %#x AMD coherent memory ext=%d\n",prop.deviceName,prop.vendorID,prop.deviceID,prop.driverVersion,has(ep,n,exts[2]));
    int cross=getenv("PROBE_NV_IMPORT")!=NULL && argc==2;
    if(!has(ep,n,exts[2]) && !cross){for(uint32_t i=0;i<mp.memoryTypeCount;i++)printf("type %u flags %#x\n",i,mp.memoryTypes[i].propertyFlags);puts("SKIP: no explicit device-uncached capability");return 77;}
    CK(has(ep,n,exts[0])&&has(ep,n,exts[1]));free(ep);
    VkPhysicalDeviceCoherentMemoryFeaturesAMD coh={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COHERENT_MEMORY_FEATURES_AMD};
    VkPhysicalDeviceFeatures2 f={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=cross?NULL:&coh};vkGetPhysicalDeviceFeatures2(pd,&f);CK(cross||coh.deviceCoherentMemory);
    VkPhysicalDeviceExternalBufferInfo eb={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,.handleType=ht};
    VkExternalBufferProperties eprop={.sType=VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};vkGetPhysicalDeviceExternalBufferProperties(pd,&eb,&eprop);
    CK((eprop.externalMemoryProperties.externalMemoryFeatures&6)==6);
    float priority=1;
    VkDeviceQueueCreateInfo qc={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo dc={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=cross?NULL:&coh,.queueCreateInfoCount=1,.pQueueCreateInfos=&qc,.enabledExtensionCount=cross?2:3,.ppEnabledExtensionNames=exts};
    VkDevice d[2];VkBuffer b[2];VkDeviceMemory m[2];void *cpu[2];int fd=-1,fd2=-1;
    size_t size=SIZE;
    int child=argc==2;
    if(child){fd=atoi(argv[1]);fd2=dup(fd);CK(fd2>=0);}
    for(unsigned j=child?1:0;j<2;j++) {
        V(vkCreateDevice(pd,&dc,NULL,&d[j]));
        VkExternalMemoryBufferCreateInfo ex={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,.handleTypes=ht};
        VkBufferCreateInfo bc={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.pNext=&ex,.size=size,.usage=eb.usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
        V(vkCreateBuffer(d[j],&bc,NULL,&b[j]));
        VkMemoryRequirements req;vkGetBufferMemoryRequirements(d[j],b[j],&req);CK(req.size==size);
        uint32_t bits=req.memoryTypeBits;
        if(j) {
            VkMemoryFdPropertiesKHR fp={.sType=VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
            PFN_vkGetMemoryFdPropertiesKHR get=(void*)vkGetDeviceProcAddr(d[j],"vkGetMemoryFdPropertiesKHR");CK(get);
            V(get(d[j],ht,fd,&fp));bits &= fp.memoryTypeBits;
        }
        printf("compatible buffer + fd types mask %#x\n",bits);
        uint32_t t=0;for(;t<mp.memoryTypeCount;t++) if((bits&(1u<<t)) && (cross || mp.memoryTypes[t].propertyFlags==0xc6u))break;
        CK(t<mp.memoryTypeCount);printf("%s 64KiB type=%u flags=%#x reqBits=%#x\n",j?"IMPORT":"ALLOC",t,mp.memoryTypes[t].propertyFlags,bits);
        VkMemoryDedicatedAllocateInfo ded={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,.buffer=b[j]};
        VkExportMemoryAllocateInfo exp={.sType=VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,.pNext=&ded,.handleTypes=ht};
        VkImportMemoryFdInfoKHR imp={.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,.pNext=&ded,.handleType=ht,.fd=fd};
        VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=j?(void*)&imp:(void*)&exp,.allocationSize=size,.memoryTypeIndex=t};
        V(vkAllocateMemory(d[j],&ma,NULL,&m[j]));V(vkBindBufferMemory(d[j],b[j],m[j],0));
        cpu[j]=NULL;if(mp.memoryTypes[t].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) V(vkMapMemory(d[j],m[j],0,size,0,&cpu[j]));
        if(!j) {
            PFN_vkGetMemoryFdKHR get=(void*)vkGetDeviceProcAddr(d[j],"vkGetMemoryFdKHR");CK(get);
            VkMemoryGetFdInfoKHR fi={.sType=VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,.memory=m[j],.handleType=ht};
            V(get(d[j],&fi,&fd));fd2=dup(fd);CK(fd2>=0);
            if(getenv("FRESH_VM")){
                *(volatile unsigned*)cpu[j]=0x13a4c95d; __asm__ volatile("sfence":::"memory");
                char num[32];sprintf(num,"%d",fd2);
                vkUnmapMemory(d[j],m[j]);vkDestroyBuffer(d[j],b[j],NULL);vkFreeMemory(d[j],m[j],NULL);
                vkDestroyDevice(d[j],NULL);vkDestroyInstance(instance,NULL);close(fd);
                execl("/proc/self/exe",argv[0],num,(char*)NULL);return 3;
            }
        }
    }
    volatile uint32_t *title=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd2,0);CK((void*)title!=MAP_FAILED);
    struct dma_buf_sync sync={DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE};CK(!ioctl(fd2,DMA_BUF_IOCTL_SYNC,&sync));
    if(!child) title[0]=0x13a4c95d;
    __asm__ volatile("sfence":::"memory");sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_WRITE;CK(!ioctl(fd2,DMA_BUF_IOCTL_SYNC,&sync));
    if(cpu[1]) CK(*(volatile uint32_t*)cpu[1]==0x13a4c95d);
    if(!child){vkUnmapMemory(d[0],m[0]);vkDestroyBuffer(d[0],b[0],NULL);vkFreeMemory(d[0],m[0],NULL);vkDestroyDevice(d[0],NULL);}
    VkCommandPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    VkCommandPool pool;V(vkCreateCommandPool(d[1],&pc,NULL,&pool));
    VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.commandBufferCount=1};
    VkCommandBuffer cb;V(vkAllocateCommandBuffers(d[1],&ca,&cb));
    VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};V(vkBeginCommandBuffer(cb,&bi));
    VkBufferCopy copy={0,size-4,4};vkCmdCopyBuffer(cb,b[1],b[1],1,&copy);
    VkMemoryBarrier bar={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&bar,0,NULL,0,NULL);
    V(vkEndCommandBuffer(cb));VkQueue q;vkGetDeviceQueue(d[1],0,0,&q);
    VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};
    V(vkQueueSubmit(q,1,&si,VK_NULL_HANDLE));V(vkQueueWaitIdle(q));
    sync.flags=DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ;CK(!ioctl(fd2,DMA_BUF_IOCTL_SYNC,&sync));
    CK(title[size/4-1]==0x13a4c95d);sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ;CK(!ioctl(fd2,DMA_BUF_IOCTL_SYNC,&sync));
    puts("PASS: second logical device GPU copy reached last word of independent dma-buf CPU alias after exporter release");
    munmap((void*)title,size);close(fd2);vkDestroyCommandPool(d[1],pool,NULL);if(cpu[1])vkUnmapMemory(d[1],m[1]);vkDestroyBuffer(d[1],b[1],NULL);vkFreeMemory(d[1],m[1],NULL);vkDestroyDevice(d[1],NULL);vkDestroyInstance(instance,NULL);
}
