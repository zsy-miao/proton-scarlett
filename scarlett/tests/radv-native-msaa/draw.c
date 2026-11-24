#define _GNU_SOURCE
#include <sys/mman.h>
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#ifdef NDEBUG
#error The qualification checks require assertions.
#endif
#define V(x) do{VkResult _r=(x);if(_r!=VK_SUCCESS){fprintf(stderr,"%d %s -> %d\n",__LINE__,#x,_r);exit(1);}}while(0)
#define S(x) .sType=VK_STRUCTURE_TYPE_##x
static VkShaderModule shader(VkDevice d,const char *path){uint32_t words[4096];FILE *f=fopen(path,"rb");assert(f);size_t n=fread(words,1,sizeof words,f);assert(feof(f)&&n%4==0&&n>0);fclose(f);VkShaderModuleCreateInfo c={S(SHADER_MODULE_CREATE_INFO),.codeSize=n,.pCode=words};VkShaderModule m;V(vkCreateShaderModule(d,&c,NULL,&m));return m;}
int main(void){
    const size_t span=0x89700000,slot=0xe800000,guard=65536,slot_size=4u<<20;
    VkInstance in;VkApplicationInfo app={S(APPLICATION_INFO),.apiVersion=VK_API_VERSION_1_2};VkInstanceCreateInfo ic={S(INSTANCE_CREATE_INFO),.pApplicationInfo=&app};V(vkCreateInstance(&ic,NULL,&in));
    uint32_t n=1;VkPhysicalDevice pd;V(vkEnumeratePhysicalDevices(in,&n,&pd));VkPhysicalDeviceProperties pp;vkGetPhysicalDeviceProperties(pd,&pp);assert(pp.vendorID==0x1002 && pp.deviceID==0x164e && pp.driverVersion==0x06803000);
    VkPhysicalDeviceMemoryProperties mp;vkGetPhysicalDeviceMemoryProperties(pd,&mp);assert(mp.memoryTypeCount>2 && mp.memoryTypes[2].propertyFlags==6);
    VkPhysicalDeviceFeatures feat;vkGetPhysicalDeviceFeatures(pd,&feat);assert(feat.sampleRateShading);feat=(VkPhysicalDeviceFeatures){.sampleRateShading=VK_TRUE};
    float pr=1;VkDeviceQueueCreateInfo qc={S(DEVICE_QUEUE_CREATE_INFO),.queueCount=1,.pQueuePriorities=&pr};VkPhysicalDeviceMapMemoryPlacedFeaturesEXT pf={S(PHYSICAL_DEVICE_MAP_MEMORY_PLACED_FEATURES_EXT)};VkPhysicalDeviceFeatures2 f2={S(PHYSICAL_DEVICE_FEATURES_2),.pNext=&pf};vkGetPhysicalDeviceFeatures2(pd,&f2);assert(pf.memoryMapPlaced && pf.memoryUnmapReserve);
    VkPhysicalDeviceMapMemoryPlacedPropertiesEXT props={S(PHYSICAL_DEVICE_MAP_MEMORY_PLACED_PROPERTIES_EXT)};VkPhysicalDeviceProperties2 p2={S(PHYSICAL_DEVICE_PROPERTIES_2),.pNext=&props};vkGetPhysicalDeviceProperties2(pd,&p2);assert(props.minPlacedMemoryMapAlignment && !(span%props.minPlacedMemoryMapAlignment));
    const char *ext[]={"VK_KHR_map_memory2","VK_EXT_map_memory_placed"};VkDeviceCreateInfo dc={S(DEVICE_CREATE_INFO),.pNext=&pf,.enabledExtensionCount=2,.ppEnabledExtensionNames=ext,.pEnabledFeatures=&feat,.queueCreateInfoCount=1,.pQueueCreateInfos=&qc};VkDevice d;V(vkCreateDevice(pd,&dc,NULL,&d));VkQueue q;vkGetDeviceQueue(d,0,0,&q);
    VkBufferCreateInfo bc={S(BUFFER_CREATE_INFO),.size=span,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};VkBuffer pool;V(vkCreateBuffer(d,&bc,NULL,&pool));
    VkImageCreateInfo ci={S(IMAGE_CREATE_INFO),.flags=VK_IMAGE_CREATE_ALIAS_BIT,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.extent={1,1,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_4_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};VkImage im;V(vkCreateImage(d,&ci,NULL,&im));
    VkMemoryRequirements br;vkGetBufferMemoryRequirements(d,pool,&br);VkImageMemoryRequirementsInfo2 ri={S(IMAGE_MEMORY_REQUIREMENTS_INFO_2),.image=im};VkMemoryDedicatedRequirements dd={S(MEMORY_DEDICATED_REQUIREMENTS)};VkMemoryRequirements2 mr={S(MEMORY_REQUIREMENTS_2),.pNext=&dd};vkGetImageMemoryRequirements2(d,&ri,&mr);
    printf("native buffer+image: span=%#zx off=%#zx type=2 flags=%#x buffer-bits=%#x image-bits=%#x size=%#lx align=%#lx dedicated=%u\n",span,slot,mp.memoryTypes[2].propertyFlags,br.memoryTypeBits,mr.memoryRequirements.memoryTypeBits,mr.memoryRequirements.size,mr.memoryRequirements.alignment,dd.requiresDedicatedAllocation);
    assert(br.size==span && (br.memoryTypeBits & mr.memoryRequirements.memoryTypeBits & 4) && !dd.requiresDedicatedAllocation && !(slot%mr.memoryRequirements.alignment) && mr.memoryRequirements.size<=slot_size);
    VkMemoryAllocateInfo ma={S(MEMORY_ALLOCATE_INFO),.allocationSize=span,.memoryTypeIndex=2};VkDeviceMemory mem;V(vkAllocateMemory(d,&ma,NULL,&mem));V(vkBindBufferMemory(d,pool,mem,0));V(vkBindImageMemory(d,im,mem,slot));
    const size_t align=2ul<<20;assert(!(align%props.minPlacedMemoryMapAlignment));
    void *reservation=mmap(NULL,span+align,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(reservation!=MAP_FAILED);
    uintptr_t title=((uintptr_t)reservation+align-1)&~(align-1);size_t prefix=title-(uintptr_t)reservation;
    if(prefix)assert(!munmap(reservation,prefix));
    if(prefix!=align)assert(!munmap((void*)(title+span),align-prefix));
    VkMemoryMapPlacedInfoEXT placed={S(MEMORY_MAP_PLACED_INFO_EXT),.pPlacedAddress=(void*)title};
    VkMemoryMapInfoKHR mi={S(MEMORY_MAP_INFO_KHR),.pNext=&placed,.flags=VK_MEMORY_MAP_PLACED_BIT_EXT,.memory=mem,.size=span};
    PFN_vkMapMemory2KHR map2=(PFN_vkMapMemory2KHR)vkGetDeviceProcAddr(d,"vkMapMemory2KHR");PFN_vkUnmapMemory2KHR unmap2=(PFN_vkUnmapMemory2KHR)vkGetDeviceProcAddr(d,"vkUnmapMemory2KHR");assert(map2&&unmap2);
    void *ptr;V(map2(d,&mi,&ptr));assert((uintptr_t)ptr==title);uint8_t *map=ptr;memset(map+slot-guard,0xa7,guard+slot_size+guard);
    VkImageViewCreateInfo vc={S(IMAGE_VIEW_CREATE_INFO),.image=im,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=ci.format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};VkImageView view;V(vkCreateImageView(d,&vc,NULL,&view));
    VkAttachmentDescription at={.format=ci.format,.samples=ci.samples,.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_GENERAL};VkAttachmentReference ref={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&ref};VkRenderPassCreateInfo rc={S(RENDER_PASS_CREATE_INFO),.attachmentCount=1,.pAttachments=&at,.subpassCount=1,.pSubpasses=&sub};VkRenderPass rp;V(vkCreateRenderPass(d,&rc,NULL,&rp));
    VkFramebufferCreateInfo fc={S(FRAMEBUFFER_CREATE_INFO),.renderPass=rp,.attachmentCount=1,.pAttachments=&view,.width=1,.height=1,.layers=1};VkFramebuffer fb;V(vkCreateFramebuffer(d,&fc,NULL,&fb));
    VkPipelineLayoutCreateInfo lc={S(PIPELINE_LAYOUT_CREATE_INFO)};VkPipelineLayout pl;V(vkCreatePipelineLayout(d,&lc,NULL,&pl));VkShaderModule vs=shader(d,"vs.spv"),ps=shader(d,"ps.spv");
    VkPipelineShaderStageCreateInfo st[2]={{S(PIPELINE_SHADER_STAGE_CREATE_INFO),.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vs,.pName="vs"},{S(PIPELINE_SHADER_STAGE_CREATE_INFO),.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=ps,.pName="ps"}};
    VkPipelineVertexInputStateCreateInfo vi={S(PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO)};VkPipelineInputAssemblyStateCreateInfo ia={S(PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO),.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};VkViewport vp={0,0,1,1,0,1};VkRect2D rect={{0,0},{1,1}};VkPipelineViewportStateCreateInfo viewport={S(PIPELINE_VIEWPORT_STATE_CREATE_INFO),.viewportCount=1,.pViewports=&vp,.scissorCount=1,.pScissors=&rect};
    VkPipelineRasterizationStateCreateInfo rs={S(PIPELINE_RASTERIZATION_STATE_CREATE_INFO),.polygonMode=VK_POLYGON_MODE_FILL,.lineWidth=1};VkPipelineMultisampleStateCreateInfo ms={S(PIPELINE_MULTISAMPLE_STATE_CREATE_INFO),.rasterizationSamples=ci.samples,.sampleShadingEnable=VK_TRUE,.minSampleShading=1};VkPipelineColorBlendAttachmentState ca={.colorWriteMask=15};VkPipelineColorBlendStateCreateInfo blend={S(PIPELINE_COLOR_BLEND_STATE_CREATE_INFO),.attachmentCount=1,.pAttachments=&ca};
    VkGraphicsPipelineCreateInfo pc={S(GRAPHICS_PIPELINE_CREATE_INFO),.stageCount=2,.pStages=st,.pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&viewport,.pRasterizationState=&rs,.pMultisampleState=&ms,.pColorBlendState=&blend,.layout=pl,.renderPass=rp};VkPipeline pipe;V(vkCreateGraphicsPipelines(d,VK_NULL_HANDLE,1,&pc,NULL,&pipe));
    VkDescriptorSetLayoutBinding bindings[2]={{.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},{.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT}};
    VkDescriptorSetLayoutCreateInfo dlc={S(DESCRIPTOR_SET_LAYOUT_CREATE_INFO),.bindingCount=2,.pBindings=bindings};VkDescriptorSetLayout dl;V(vkCreateDescriptorSetLayout(d,&dlc,NULL,&dl));VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};VkDescriptorPoolCreateInfo dpc={S(DESCRIPTOR_POOL_CREATE_INFO),.maxSets=1,.poolSizeCount=2,.pPoolSizes=sizes};VkDescriptorPool dp;V(vkCreateDescriptorPool(d,&dpc,NULL,&dp));VkDescriptorSetAllocateInfo dsa={S(DESCRIPTOR_SET_ALLOCATE_INFO),.descriptorPool=dp,.descriptorSetCount=1,.pSetLayouts=&dl};VkDescriptorSet ds;V(vkAllocateDescriptorSets(d,&dsa,&ds));
    VkDescriptorImageInfo di={.imageView=view,.imageLayout=VK_IMAGE_LAYOUT_GENERAL};VkDescriptorBufferInfo db={pool,0,16};VkWriteDescriptorSet wr[2]={{S(WRITE_DESCRIPTOR_SET),.dstSet=ds,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,.pImageInfo=&di},{S(WRITE_DESCRIPTOR_SET),.dstSet=ds,.dstBinding=1,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&db}};vkUpdateDescriptorSets(d,2,wr,0,NULL);
    lc.setLayoutCount=1;lc.pSetLayouts=&dl;VkPipelineLayout cpl;V(vkCreatePipelineLayout(d,&lc,NULL,&cpl));VkShaderModule cs=shader(d,"cs.spv");VkComputePipelineCreateInfo cpc={S(COMPUTE_PIPELINE_CREATE_INFO),.stage={S(PIPELINE_SHADER_STAGE_CREATE_INFO),.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=cs,.pName="cs"},.layout=cpl};VkPipeline cp;V(vkCreateComputePipelines(d,VK_NULL_HANDLE,1,&cpc,NULL,&cp));
    VkCommandPoolCreateInfo poolc={S(COMMAND_POOL_CREATE_INFO),.queueFamilyIndex=0};VkCommandPool commands;V(vkCreateCommandPool(d,&poolc,NULL,&commands));VkCommandBufferAllocateInfo ba={S(COMMAND_BUFFER_ALLOCATE_INFO),.commandPool=commands,.commandBufferCount=1};VkCommandBuffer b;V(vkAllocateCommandBuffers(d,&ba,&b));VkCommandBufferBeginInfo bi={S(COMMAND_BUFFER_BEGIN_INFO)};V(vkBeginCommandBuffer(b,&bi));
    /* Disjoint buffer word proves that both kinds of GPU resource reach this allocation. */
    vkCmdFillBuffer(b,pool,slot-guard-4,4,0xab341278);VkRenderPassBeginInfo rb={S(RENDER_PASS_BEGIN_INFO),.renderPass=rp,.framebuffer=fb,.renderArea=rect};vkCmdBeginRenderPass(b,&rb,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(b,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe);vkCmdDraw(b,3,1,0,0);vkCmdEndRenderPass(b);
    VkMemoryBarrier sample={S(MEMORY_BARRIER),.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT};vkCmdPipelineBarrier(b,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&sample,0,NULL,0,NULL);
    vkCmdBindPipeline(b,VK_PIPELINE_BIND_POINT_COMPUTE,cp);vkCmdBindDescriptorSets(b,VK_PIPELINE_BIND_POINT_COMPUTE,cpl,0,1,&ds,0,NULL);vkCmdDispatch(b,1,1,1);
    VkMemoryUnmapInfoKHR unmap={S(MEMORY_UNMAP_INFO_KHR),.memory=mem,.flags=VK_MEMORY_UNMAP_RESERVE_BIT_EXT};V(unmap2(d,&unmap));uint8_t *replacement=mmap((void*)title,span,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);assert((uintptr_t)replacement==title);memset(replacement+slot-guard,0xab,guard+slot_size+guard);
    VkMemoryBarrier barrier={S(MEMORY_BARRIER),.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(b,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);V(vkEndCommandBuffer(b));VkSubmitInfo si={S(SUBMIT_INFO),.commandBufferCount=1,.pCommandBuffers=&b};VkFenceCreateInfo gc={S(FENCE_CREATE_INFO)};VkFence fence;V(vkCreateFence(d,&gc,NULL,&fence));V(vkQueueSubmit(q,1,&si,fence));V(vkWaitForFences(d,1,&fence,VK_TRUE,10000000000ull));
    V(vkMapMemory(d,mem,0,span,0,&ptr));map=ptr;assert((uintptr_t)map!=title);for(size_t i=slot-guard;i<slot+slot_size+guard;i++)assert(replacement[i]==0xab);
    for(unsigned i=0;i<4;i++){uint32_t value;memcpy(&value,map+i*4,4);assert(value==((4+4*i)|((12+8*i)<<8)|((128+12*i)<<16)|((240u-16*i)<<24)));printf("typed sample %u = %08x\n",i,value);}
    assert(*(uint32_t*)(map+slot-guard-4)==0xab341278);for(size_t i=0;i<guard;i++)assert(map[slot-guard+i]==0xa7 && map[slot+slot_size+i]==0xa7);
    unsigned found[4]={0};unsigned changed=0;for(size_t i=0;i<slot_size;i++){changed+=map[slot+i]!=0xa7;if(!(i%4)){uint32_t w;memcpy(&w,map+slot+i,4);for(unsigned s=0;s<4;s++)if(w==((4+4*s)|((12+8*s)<<8)|((128+12*s)<<16)|((240u-16*s)<<24))){found[s]++;printf("sample=%u offset=%#zx word=%08x\n",s,i,w);}}}
    assert(found[2]==1 && found[3]==1 && changed>=16);
    if(getenv("RADV_DEBUG") && !strcmp(getenv("RADV_DEBUG"),"nodcc")) {
        for(unsigned s=0;s<4;s++)
            assert(found[s]==1 && !memcmp(map+slot+s*0x4000,map+s*4,4));
    }
    printf("placed CPU map=%p (align=%#lx) remap=%p; replacement unchanged\n",(void*)title,props.minPlacedMemoryMapAlignment,ptr);
    printf("PASS changed=%u bytes, both 64 KiB guards intact, GPU buffer word=ab341278\n",changed);
    vkDestroyFence(d,fence,NULL);vkDestroyCommandPool(d,commands,NULL);vkDestroyDescriptorPool(d,dp,NULL);vkDestroyPipeline(d,cp,NULL);vkDestroyShaderModule(d,cs,NULL);vkDestroyPipelineLayout(d,cpl,NULL);vkDestroyDescriptorSetLayout(d,dl,NULL);vkDestroyPipeline(d,pipe,NULL);vkDestroyShaderModule(d,ps,NULL);vkDestroyShaderModule(d,vs,NULL);vkDestroyPipelineLayout(d,pl,NULL);vkDestroyFramebuffer(d,fb,NULL);vkDestroyRenderPass(d,rp,NULL);vkDestroyImageView(d,view,NULL);vkUnmapMemory(d,mem);vkDestroyImage(d,im,NULL);vkDestroyBuffer(d,pool,NULL);vkFreeMemory(d,mem,NULL);vkDestroyDevice(d,NULL);vkDestroyInstance(in,NULL);assert(!munmap(replacement,span));
}
