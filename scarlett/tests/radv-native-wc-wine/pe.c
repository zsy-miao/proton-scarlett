#define VK_NO_PROTOTYPES
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d12.h>
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#ifdef NDEBUG
#error The GPU and mapping checks require assertions.
#endif
#define V(x) do{VkResult _r=(x);if(_r!=VK_SUCCESS){fprintf(stderr,"%d %s -> %d\n",__LINE__,#x,_r);exit(1);}}while(0)
#define S(x) .sType=VK_STRUCTURE_TYPE_##x
#define PROCS(X) X(vkAllocateCommandBuffers) X(vkAllocateDescriptorSets) X(vkAllocateMemory) X(vkBeginCommandBuffer) \
    X(vkBindBufferMemory) X(vkBindImageMemory) X(vkCmdBeginRenderPass) X(vkCmdBindDescriptorSets) \
    X(vkCmdBindPipeline) X(vkCmdDispatch) X(vkCmdDraw) X(vkCmdEndRenderPass) \
    X(vkCmdFillBuffer) X(vkCmdPipelineBarrier) X(vkCmdWaitEvents) X(vkCreateBuffer) \
    X(vkCreateCommandPool) X(vkCreateComputePipelines) X(vkCreateDescriptorPool) X(vkCreateDescriptorSetLayout) \
    X(vkCreateEvent) X(vkCreateFence) X(vkCreateFramebuffer) X(vkCreateGraphicsPipelines) \
    X(vkCreateImage) X(vkCreateImageView) X(vkCreatePipelineLayout) X(vkCreateRenderPass) \
    X(vkCreateShaderModule) X(vkDestroyBuffer) X(vkDestroyCommandPool) X(vkDestroyDescriptorPool) \
    X(vkDestroyDescriptorSetLayout) X(vkDestroyEvent) X(vkDestroyFence) X(vkDestroyFramebuffer) \
    X(vkDestroyImage) X(vkDestroyImageView) X(vkDestroyPipeline) X(vkDestroyPipelineLayout) \
    X(vkDestroyRenderPass) X(vkDestroyShaderModule) X(vkEndCommandBuffer) X(vkFreeMemory) \
    X(vkGetBufferMemoryRequirements) X(vkGetFenceStatus) X(vkGetImageMemoryRequirements2) X(vkMapMemory) \
    X(vkQueueSubmit) X(vkSetEvent) X(vkUnmapMemory) X(vkUpdateDescriptorSets) \
    X(vkWaitForFences)
#define DECLARE(name) static PFN_##name name;
PROCS(DECLARE)
static VkShaderModule shader(VkDevice d,const char *path){uint32_t words[4096];FILE *f=fopen(path,"rb");assert(f);size_t n=fread(words,1,sizeof words,f);assert(feof(f)&&n%4==0&&n>0);fclose(f);VkShaderModuleCreateInfo c={S(SHADER_MODULE_CREATE_INFO),.codeSize=n,.pCode=words};VkShaderModule m;V(vkCreateShaderModule(d,&c,NULL,&m));return m;}
int main(void){
    setbuf(stdout,NULL);
    const size_t span=0x89700000,slot=0xe800000,guard=65536,slot_size=4u<<20;
    typedef LONG (WINAPI *Q)(HANDLE,const void*,ULONG,void*,SIZE_T,SIZE_T*);
    Q cmd=(Q)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryVirtualMemory");assert(cmd);
    assert(!cmd(GetCurrentProcess(),NULL,0x53430101,NULL,0,NULL));
    ID3D12Device *dev=NULL;assert(SUCCEEDED(D3D12CreateDevice(NULL,D3D_FEATURE_LEVEL_12_0,&IID_ID3D12Device,(void**)&dev)));
    const IID iid={0x39da4e09,0xbd1c,0x4198,{0x9f,0xae,0x86,0xbb,0xe3,0xbe,0x41,0xfd}};
    void *interop=NULL; assert(SUCCEEDED(ID3D12Device_QueryInterface(dev,&iid,&interop)));void **table=*(void***)interop;
    VkInstance in=NULL;VkPhysicalDevice pd=NULL;VkDevice d=NULL;
    typedef HRESULT(WINAPI *Handles)(void*,VkInstance*,VkPhysicalDevice*,VkDevice*);assert(SUCCEEDED(((Handles)table[7])(interop,&in,&pd,&d)));
    ID3D12CommandQueue *queue=NULL;D3D12_COMMAND_QUEUE_DESC desc={0};assert(SUCCEEDED(ID3D12Device_CreateCommandQueue(dev,&desc,&IID_ID3D12CommandQueue,(void**)&queue)));
    typedef HRESULT(WINAPI *QI)(void*,ID3D12CommandQueue*,VkQueue*,uint32_t*);VkQueue q;uint32_t family;assert(SUCCEEDED(((QI)table[8])(interop,queue,&q,&family)));
    typedef HRESULT(WINAPI *Lock)(void*,ID3D12CommandQueue*);typedef HRESULT(WINAPI *Unlock)(void*,ID3D12CommandQueue*);
    ID3D12Device_Release(dev); // queue and queried interface retain the real vkd3d owner
    PFN_vkGetInstanceProcAddr gi=(void*)GetProcAddress(LoadLibraryW(L"vulkan-1.dll"),"vkGetInstanceProcAddr"); assert(gi);
    PFN_vkGetDeviceProcAddr gd=(PFN_vkGetDeviceProcAddr)gi(in,"vkGetDeviceProcAddr");assert(gd);
#define LOAD(name) name=(PFN_##name)gd(d,#name);assert(name);
    PROCS(LOAD)
    VkPhysicalDeviceProperties pp;((PFN_vkGetPhysicalDeviceProperties)gi(in,"vkGetPhysicalDeviceProperties"))(pd,&pp);assert(pp.vendorID==0x1002 && pp.deviceID==0x164e);
    VkPhysicalDeviceMemoryProperties mp;((PFN_vkGetPhysicalDeviceMemoryProperties)gi(in,"vkGetPhysicalDeviceMemoryProperties"))(pd,&mp);assert(mp.memoryTypeCount>2 && mp.memoryTypes[2].propertyFlags==6);
    VkBufferCreateInfo bc={S(BUFFER_CREATE_INFO),.size=span,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT};VkBuffer pool;V(vkCreateBuffer(d,&bc,NULL,&pool));
    VkImageCreateInfo ci={S(IMAGE_CREATE_INFO),.flags=VK_IMAGE_CREATE_ALIAS_BIT,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.extent={1,1,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_4_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};VkImage im;V(vkCreateImage(d,&ci,NULL,&im));
    VkMemoryRequirements br;vkGetBufferMemoryRequirements(d,pool,&br);VkImageMemoryRequirementsInfo2 ri={S(IMAGE_MEMORY_REQUIREMENTS_INFO_2),.image=im};VkMemoryDedicatedRequirements dd={S(MEMORY_DEDICATED_REQUIREMENTS)};VkMemoryRequirements2 mr={S(MEMORY_REQUIREMENTS_2),.pNext=&dd};vkGetImageMemoryRequirements2(d,&ri,&mr);
    printf("native buffer+image: span=%#zx off=%#zx type=2 flags=%#x buffer-bits=%#x image-bits=%#x size=%#lx align=%#lx dedicated=%u\n",span,slot,mp.memoryTypes[2].propertyFlags,br.memoryTypeBits,mr.memoryRequirements.memoryTypeBits,(unsigned long)mr.memoryRequirements.size,(unsigned long)mr.memoryRequirements.alignment,dd.requiresDedicatedAllocation);
    assert(br.size==span && (br.memoryTypeBits & mr.memoryRequirements.memoryTypeBits & 4) && !dd.requiresDedicatedAllocation && !(slot%mr.memoryRequirements.alignment) && mr.memoryRequirements.size<=slot_size);
    VkMemoryAllocateInfo ma={S(MEMORY_ALLOCATE_INFO),.allocationSize=span,.memoryTypeIndex=2};VkDeviceMemory mem;V(vkAllocateMemory(d,&ma,NULL,&mem));V(vkBindBufferMemory(d,pool,mem,0));V(vkBindImageMemory(d,im,mem,slot));
    assert(!cmd(GetCurrentProcess(),NULL,0x53430102,NULL,0,NULL));void *ptr=NULL;V(vkMapMemory(d,mem,0,span,0,&ptr));uintptr_t title=(uintptr_t)ptr;assert(!(title%0x200000));
    MEMORY_BASIC_INFORMATION info;assert(VirtualQuery(ptr,&info,sizeof info)==sizeof info);printf("view base=%p allocation=%p span=%zx state=%lx prot=%lx type=%lx vkd-device=%p\n",ptr,info.AllocationBase,info.RegionSize,info.State,info.Protect,info.Type,(void*)d);
    assert(info.BaseAddress==ptr && info.AllocationBase==ptr && info.RegionSize==span && info.State==MEM_COMMIT && info.Protect==(PAGE_READWRITE|PAGE_WRITECOMBINE));
    assert(!VirtualFree((char*)ptr+65536,0,MEM_RELEASE) && !VirtualFree(ptr,65536,MEM_DECOMMIT));DWORD old;assert(!VirtualProtect(ptr,4096,PAGE_READONLY,&old));
    struct {UINT64 id;BYTE bytes[32768];} stage;for(UINT i=0;i<32768;i++)stage.bytes[i]=i*13+7;
    assert(!cmd(GetCurrentProcess(),(char*)ptr+0x2000,0x53430103,&stage.id,8,NULL) && stage.id);
    assert(!cmd(GetCurrentProcess(),(char*)ptr+0x2000,0x53430104,&stage,32776,NULL));assert(!memcmp((char*)ptr+0x2000,stage.bytes,32768));
    assert(!cmd(GetCurrentProcess(),(char*)ptr+0xe5c7000,0x53430104,&stage,12,NULL));assert(!memcmp((char*)ptr+0xe5c7000,stage.bytes,4));
    stage.id++;assert(cmd(GetCurrentProcess(),(char*)ptr+0x2000,0x53430104,&stage,12,NULL)<0);stage.id--;
    puts("PASS generation-checked 32 KiB and four-byte WC writes, wrong incarnation refused");
    uint8_t *map=ptr;memset(map+slot-guard,0xa7,guard+slot_size+guard);
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
    VkCommandPoolCreateInfo poolc={S(COMMAND_POOL_CREATE_INFO),.queueFamilyIndex=family};VkCommandPool commands;V(vkCreateCommandPool(d,&poolc,NULL,&commands));VkCommandBufferAllocateInfo ba={S(COMMAND_BUFFER_ALLOCATE_INFO),.commandPool=commands,.commandBufferCount=1};VkCommandBuffer b;V(vkAllocateCommandBuffers(d,&ba,&b));VkCommandBufferBeginInfo bi={S(COMMAND_BUFFER_BEGIN_INFO)};V(vkBeginCommandBuffer(b,&bi));
    VkEventCreateInfo ec={S(EVENT_CREATE_INFO)};VkEvent gate;V(vkCreateEvent(d,&ec,NULL,&gate));vkCmdWaitEvents(b,1,&gate,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,NULL,0,NULL,0,NULL);
    /* Disjoint buffer word proves that both kinds of GPU resource reach this allocation. */
    vkCmdFillBuffer(b,pool,slot-guard-4,4,0xab341278);VkRenderPassBeginInfo rb={S(RENDER_PASS_BEGIN_INFO),.renderPass=rp,.framebuffer=fb,.renderArea=rect};vkCmdBeginRenderPass(b,&rb,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(b,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe);vkCmdDraw(b,3,1,0,0);vkCmdEndRenderPass(b);
    VkMemoryBarrier sample={S(MEMORY_BARRIER),.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT};vkCmdPipelineBarrier(b,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&sample,0,NULL,0,NULL);
    vkCmdBindPipeline(b,VK_PIPELINE_BIND_POINT_COMPUTE,cp);vkCmdBindDescriptorSets(b,VK_PIPELINE_BIND_POINT_COMPUTE,cpl,0,1,&ds,0,NULL);vkCmdDispatch(b,1,1,1);
    VkMemoryBarrier barrier={S(MEMORY_BARRIER),.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(b,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);V(vkEndCommandBuffer(b));VkSubmitInfo si={S(SUBMIT_INFO),.commandBufferCount=1,.pCommandBuffers=&b};VkFenceCreateInfo gc={S(FENCE_CREATE_INFO)};VkFence fence;V(vkCreateFence(d,&gc,NULL,&fence));assert(SUCCEEDED(((Lock)table[11])(interop,queue))); V(vkQueueSubmit(q,1,&si,fence));assert(SUCCEEDED(((Unlock)table[12])(interop,queue)));
    assert(vkGetFenceStatus(d,fence)==VK_NOT_READY);assert(VirtualFree(ptr,0,MEM_RELEASE));assert(VirtualQuery(ptr,&info,sizeof info)==sizeof info && info.State==MEM_FREE);
    uint8_t *replacement=VirtualAlloc(ptr,span,MEM_RESERVE,PAGE_NOACCESS);assert(replacement==ptr);
    assert(VirtualAlloc(replacement+slot-guard,guard+slot_size+guard,MEM_COMMIT,PAGE_READWRITE)==replacement+slot-guard);memset(replacement+slot-guard,0xab,guard+slot_size+guard);
    assert(cmd(GetCurrentProcess(),(char*)ptr+0x2000,0x53430104,&stage,12,NULL)<0);assert(vkGetFenceStatus(d,fence)==VK_NOT_READY);puts("PASS VK_NOT_READY after VirtualFree and exact VA reuse");V(vkSetEvent(d,gate));V(vkWaitForFences(d,1,&fence,VK_TRUE,10000000000ull));
    V(vkMapMemory(d,mem,0,span,0,&ptr));map=ptr;assert((uintptr_t)map!=title);for(size_t i=slot-guard;i<slot+slot_size+guard;i++)assert(replacement[i]==0xab);
    for(unsigned i=0;i<4;i++){uint32_t value;memcpy(&value,map+i*4,4);assert(value==((4+4*i)|((12+8*i)<<8)|((128+12*i)<<16)|((240u-16*i)<<24)));printf("typed sample %u = %08x\n",i,value);}
    assert(*(uint32_t*)(map+slot-guard-4)==0xab341278);for(size_t i=0;i<guard;i++)assert(map[slot-guard+i]==0xa7 && map[slot+slot_size+i]==0xa7);
    unsigned found[4]={0};unsigned changed=0;for(size_t i=0;i<slot_size;i++){changed+=map[slot+i]!=0xa7;if(!(i%4)){uint32_t w;memcpy(&w,map+slot+i,4);for(unsigned s=0;s<4;s++)if(w==((4+4*s)|((12+8*s)<<8)|((128+12*s)<<16)|((240u-16*s)<<24))){found[s]++;printf("sample=%u offset=%#zx word=%08x\n",s,i,w);}}}
    assert(found[2]==1 && found[3]==1 && changed>=16); if(getenv("RADV_DEBUG") && !strcmp(getenv("RADV_DEBUG"),"nodcc"))assert(found[0]==1 && found[1]==1);
    printf("PASS public free, stale write refused, pending fence, reused VA unchanged, original=%p remap=%p\n",(void*)title,ptr);
    printf("PASS changed=%u bytes, both 64 KiB guards intact, GPU buffer word=ab341278\n",changed);
    vkDestroyFence(d,fence,NULL);vkDestroyCommandPool(d,commands,NULL);vkDestroyDescriptorPool(d,dp,NULL);vkDestroyPipeline(d,cp,NULL);vkDestroyShaderModule(d,cs,NULL);vkDestroyPipelineLayout(d,cpl,NULL);vkDestroyDescriptorSetLayout(d,dl,NULL);vkDestroyPipeline(d,pipe,NULL);vkDestroyShaderModule(d,ps,NULL);vkDestroyShaderModule(d,vs,NULL);vkDestroyPipelineLayout(d,pl,NULL);vkDestroyFramebuffer(d,fb,NULL);vkDestroyRenderPass(d,rp,NULL);vkDestroyImageView(d,view,NULL);vkUnmapMemory(d,mem);vkDestroyImage(d,im,NULL);vkDestroyBuffer(d,pool,NULL);vkFreeMemory(d,mem,NULL);vkDestroyEvent(d,gate,NULL);printf("PASS retired pool; original typed words f0800c04 e08c1408 d0981c0c c0a42410\n");ID3D12CommandQueue_Release(queue);IUnknown_Release((IUnknown*)interop);assert(VirtualFree(replacement,0,MEM_RELEASE));
}
