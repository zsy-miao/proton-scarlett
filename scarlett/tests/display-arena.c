#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define V(x) do { VkResult r_=(x); if(r_){fprintf(stderr,"line %d: %d\n",__LINE__,r_);return 1;} }while(0)
#define CK(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;} }while(0)
static uint32_t swaprb(uint32_t x) { return (x&0xff00ff00)|((x&255)<<16)|((x>>16)&255); }
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
    size_t cap=0x4000000;
    uint8_t *raw=aligned_alloc(hp.minImportedHostPointerAlignment>65536?hp.minImportedHostPointerAlignment:65536,cap); CK(raw);
    for(unsigned generation=0;generation<2;generation++) {
    printf("ring generation %u\n",generation);
    memset(raw,0xa5,cap);
    VkImage im[2];for(unsigned i=0;i<2;i++)V(vkCreateImage(d,&c,0,im+i));
    VkMemoryRequirements mr={0};
    for(unsigned i=0;i<2;i++) { VkMemoryDedicatedRequirements dr={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
        VkMemoryRequirements2 r={.sType=VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,.pNext=&dr};
        VkImageMemoryRequirementsInfo2 q={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,.image=im[i]};
        vkGetImageMemoryRequirements2(d,&q,&r); CK(!dr.requiresDedicatedAllocation);
        if(!i) mr=r.memoryRequirements; else {CK(mr.size==r.memoryRequirements.size && mr.alignment==r.memoryRequirements.alignment); mr.memoryTypeBits&=r.memoryRequirements.memoryTypeBits;}
    }
    size_t unit=mr.alignment>65536?mr.alignment:65536,slice=(mr.size+unit-1)&~(unit-1);
    CK(slice*2<=cap&&cap-2*slice<0x200000);
    PFN_vkGetMemoryHostPointerPropertiesEXT get=(PFN_vkGetMemoryHostPointerPropertiesEXT)vkGetDeviceProcAddr(d,"vkGetMemoryHostPointerPropertiesEXT");CK(get);
    VkMemoryHostPointerPropertiesEXT mp={.sType=VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};V(get(d,type,raw,&mp));
    VkPhysicalDeviceMemoryProperties m;vkGetPhysicalDeviceMemoryProperties(p,&m);
    unsigned mt=0;
    for(;mt<m.memoryTypeCount;mt++) if(((mp.memoryTypeBits&mr.memoryTypeBits)&(1u<<mt)) &&
            (m.memoryTypes[mt].propertyFlags&(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))==(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) break;
    CK(mt<m.memoryTypeCount);
    printf("memoryType=%u flags=%#x hostBits=%#x imageBits=%#x\n",mt,m.memoryTypes[mt].propertyFlags,mp.memoryTypeBits,mr.memoryTypeBits);
    VkImportMemoryHostPointerInfoEXT imp={.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,.handleType=type,.pHostPointer=raw};
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&imp,.allocationSize=cap,.memoryTypeIndex=mt};
    VkDeviceMemory mem;V(vkAllocateMemory(d,&ai,0,&mem));
    for(unsigned i=0;i<2;i++)V(vkBindImageMemory(d,im[i],mem,i*slice));
    VkCommandPoolCreateInfo pc={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=0};VkCommandPool pool;V(vkCreateCommandPool(d,&pc,0,&pool));
    VkCommandBufferAllocateInfo ac={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer cb;V(vkAllocateCommandBuffers(d,&ac,&cb));
    VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};V(vkBeginCommandBuffer(cb,&bi));
    VkImageView view[2];
    VkAttachmentDescription att={.format=fi.format,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_GENERAL};
    VkAttachmentReference ref={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&ref};
    VkRenderPassCreateInfo rpc={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&att,.subpassCount=1,.pSubpasses=&sp};
    VkRenderPass rp;V(vkCreateRenderPass(d,&rpc,0,&rp));
    VkFramebuffer fb[2];
    // Different per-slot, per-half values. Each image is both attachment and sampled/storage target.
    uint32_t shades[2][2]={{0xff0703a1,0xff3105b2},{0xff998411,0xff8ab518}};
    shades[0][0]^=0x325710*generation; shades[1][1]^=0x6837b4*generation;
    for(unsigned i=0;i<2;i++) {
        VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=im[i],.viewType=VK_IMAGE_VIEW_TYPE_2D,
            .format=fi.format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        V(vkCreateImageView(d,&vi,0,view+i));
        VkFramebufferCreateInfo fc0={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=rp,.attachmentCount=1,.pAttachments=view+i,.width=3840,.height=2160,.layers=1};
        V(vkCreateFramebuffer(d,&fc0,0,fb+i));
        VkClearValue color[2];
        for(unsigned k=0;k<2;k++)for(unsigned ch=0;ch<4;ch++) color[k].color.float32[ch]=((shades[i][k]>>(8*ch))&255)/255.0f;
        VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=rp,.framebuffer=fb[i],.renderArea={{0,0},{3840,2160}},.clearValueCount=1,.pClearValues=color};
        vkCmdBeginRenderPass(cb,&begin,VK_SUBPASS_CONTENTS_INLINE);
        VkClearAttachment half={VK_IMAGE_ASPECT_COLOR_BIT,0,color[1]};
        VkClearRect rect={{{1920,0},{1920,2160}},0,1};
        vkCmdClearAttachments(cb,1,&half,1,&rect);
        vkCmdEndRenderPass(cb);
    }
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,0,0,0);
    VkDescriptorSetLayoutBinding binding[]={{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},{1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL}};
    VkDescriptorSetLayoutCreateInfo sc={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=binding};
    VkDescriptorSetLayout sl;V(vkCreateDescriptorSetLayout(d,&sc,0,&sl));
    VkPipelineLayoutCreateInfo lc={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&sl};
    VkPipelineLayout pl;V(vkCreatePipelineLayout(d,&lc,0,&pl));
    uint32_t code[2048]; FILE *file=fopen("display-arena.spv","rb"); CK(file);
    size_t len=fread(code,1,sizeof code,file); CK(len && feof(file) && !(len%4)); fclose(file);
    VkShaderModuleCreateInfo mc={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=len,.pCode=code};
    VkShaderModule module;V(vkCreateShaderModule(d,&mc,0,&module));
    VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=pl,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"}};
    VkPipeline pipe;V(vkCreateComputePipelines(d,VK_NULL_HANDLE,1,&ci,0,&pipe));
    VkSamplerCreateInfo sac={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    VkSampler sampler;V(vkCreateSampler(d,&sac,0,&sampler));
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,2}};
    VkDescriptorPoolCreateInfo dp={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=2,.poolSizeCount=2,.pPoolSizes=sizes};
    VkDescriptorPool pool2;V(vkCreateDescriptorPool(d,&dp,NULL,&pool2));
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipe);
    for(unsigned i=0;i<2;i++) {
        VkDescriptorSet set;VkDescriptorSetAllocateInfo salloc={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool2,.descriptorSetCount=1,.pSetLayouts=&sl};
        V(vkAllocateDescriptorSets(d,&salloc,&set));
        VkDescriptorImageInfo info[]={{sampler,view[i],VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,view[i^1],VK_IMAGE_LAYOUT_GENERAL}};
        VkWriteDescriptorSet wr[2];for(unsigned j=0;j<2;j++) wr[j]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=j,.descriptorCount=1,.descriptorType=binding[j].descriptorType,.pImageInfo=&info[j]};
        vkUpdateDescriptorSets(d,2,wr,0,0);
        vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pl,0,1,&set,0,NULL);vkCmdDispatch(cb,240,135,1);
        barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=i?VK_ACCESS_HOST_READ_BIT:VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,i?VK_PIPELINE_STAGE_HOST_BIT:VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,0,0,0);
    }
    V(vkEndCommandBuffer(cb));VkQueue q;vkGetDeviceQueue(d,0,0,&q);VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};
    VkFenceCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;V(vkCreateFence(d,&fc,0,&fence));V(vkQueueSubmit(q,1,&si,fence));V(vkWaitForFences(d,1,&fence,1,10000000000ull));
    VkImageSubresource sub={VK_IMAGE_ASPECT_COLOR_BIT,0,0};
    for(unsigned i=0;i<2;i++){VkSubresourceLayout l;vkGetImageSubresourceLayout(d,im[i],&sub,&l);
        CK(l.offset+2159*l.rowPitch+3840*4<=mr.size);
        for(unsigned y=0;y<2160;y++) { uint32_t *row=(uint32_t *)(raw+i*slice+l.offset+y*l.rowPitch);
            for(unsigned x=0;x<3840;x++) { unsigned k=x>=1920; uint32_t mask=(x&255)|((y&255)<<8)|(((x^y)&255)<<16);
                uint32_t y1=swaprb(shades[0][k])^shades[1][k]^mask;
                uint32_t want=i?y1:swaprb(y1)^shades[0][k]^mask;
                if(row[x]!=want) { fprintf(stderr,"image%u (%u,%u) = %08x expected %08x\n",i,x,y,row[x],want); return 1; }
            }
        }
        for(size_t j=mr.size;j<slice;j++)CK(raw[i*slice+j]==0xa5);
        printf("image%u verified 8294400 RGBA8 pixels, offset %#zx pitch %#lx\n",i,i*slice,(unsigned long)l.rowPitch);
    }
    for(size_t i=2*slice;i<cap;i++) CK(raw[i]==0xa5);
    printf("PASS both RTV attachment clears + sampled image fetch + storage image loads/stores, inspect original imported allocation; req=%#lx slice=%#zx remaining=%#zx untouched\n",(unsigned long)mr.size,slice,cap-2*slice);
    vkDestroyDescriptorPool(d,pool2,0);vkDestroySampler(d,sampler,0);vkDestroyPipeline(d,pipe,0);vkDestroyPipelineLayout(d,pl,0);vkDestroyDescriptorSetLayout(d,sl,0);vkDestroyShaderModule(d,module,0);for(unsigned i=0;i<2;i++){vkDestroyFramebuffer(d,fb[i],0);vkDestroyImageView(d,view[i],0);}vkDestroyRenderPass(d,rp,0);
    vkDestroyFence(d,fence,0);vkDestroyCommandPool(d,pool,0);for(unsigned i=2;i-->0;)vkDestroyImage(d,im[i],0);vkFreeMemory(d,mem,0);
    }
    free(raw);vkDestroyDevice(d,0);vkDestroyInstance(in,0);
}
