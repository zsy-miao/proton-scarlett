/* Fixture: draw inside a one-pixel guard border, fence and read back RGBA16F. */
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define VK(x) do { VkResult r_=(x); if (r_!=VK_SUCCESS) { fprintf(stderr,"%d: %s: %d\n",__LINE__,#x,r_); exit(1); } } while (0)
#define INFO(type) .sType=VK_STRUCTURE_TYPE_##type

static VkDevice device;
static VkPhysicalDeviceMemoryProperties mem;

static VkDeviceMemory allocate(VkMemoryRequirements req, VkMemoryPropertyFlags flags) {
    uint32_t i=0;
    while (i<mem.memoryTypeCount && (!(req.memoryTypeBits & (1u<<i)) ||
           (mem.memoryTypes[i].propertyFlags & flags)!=flags)) ++i;
    CHECK(i<mem.memoryTypeCount);
    VkMemoryAllocateInfo ai={INFO(MEMORY_ALLOCATE_INFO), .allocationSize=req.size,.memoryTypeIndex=i};
    VkDeviceMemory result; VK(vkAllocateMemory(device,&ai,NULL,&result)); return result;
}

static VkShaderModule module(const char *path) {
    uint32_t words[2048]; FILE *f=fopen(path,"rb"); CHECK(f);
    size_t n=fread(words,1,sizeof words,f); CHECK(feof(f) && !ferror(f) && n>=20 && n%4==0);
    fclose(f);
    VkShaderModuleCreateInfo mi={INFO(SHADER_MODULE_CREATE_INFO),.codeSize=n,.pCode=words};
    VkShaderModule m; VK(vkCreateShaderModule(device,&mi,NULL,&m)); return m;
}

#include "sample_draw.h"

int main(int argc,char **argv) {
    const int sample=argc==5 && !strcmp(argv[4],"sample");
    CHECK(argc==4 || sample || (argc==5 && !strcmp(argv[4],"lut")));
    const uint32_t W=sample?16:argc==5?256:8, H=argc==5?16:6, BYTES=W*H*8;
    VkInstanceCreateInfo ici={INFO(INSTANCE_CREATE_INFO)};
    VkInstance instance; VK(vkCreateInstance(&ici,NULL,&instance));
    uint32_t n=0; VK(vkEnumeratePhysicalDevices(instance,&n,NULL)); CHECK(n && n<=64);
    VkPhysicalDevice devices[64]; VK(vkEnumeratePhysicalDevices(instance,&n,devices));
    VkPhysicalDevice gpu=devices[0]; VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu,&props); fprintf(stderr,"%s\n",props.deviceName);
    vkGetPhysicalDeviceMemoryProperties(gpu,&mem);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,NULL); CHECK(n && n<=64);
    VkQueueFamilyProperties families[64]; vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,families);
    uint32_t family=0; while (family<n && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    CHECK(family<n);
    float priority=1; VkDeviceQueueCreateInfo qc={INFO(DEVICE_QUEUE_CREATE_INFO),
        .queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo dc={INFO(DEVICE_CREATE_INFO),.queueCreateInfoCount=1,.pQueueCreateInfos=&qc};
    VK(vkCreateDevice(gpu,&dc,NULL,&device)); VkQueue queue; vkGetDeviceQueue(device,family,0,&queue);
    VkFormat fmt=VK_FORMAT_R16G16B16A16_SFLOAT;
    VkImageCreateInfo ic={INFO(IMAGE_CREATE_INFO),.imageType=VK_IMAGE_TYPE_2D,.format=fmt,.extent={W,H,1},
        .mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
    VkImage image; VK(vkCreateImage(device,&ic,NULL,&image));
    VkMemoryRequirements req; vkGetImageMemoryRequirements(device,image,&req);
    VkDeviceMemory imem=allocate(req,0); VK(vkBindImageMemory(device,image,imem,0));
    VkBufferCreateInfo bc={INFO(BUFFER_CREATE_INFO),.size=BYTES,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    if (sample) bc.usage|=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer buffer; VK(vkCreateBuffer(device,&bc,NULL,&buffer)); vkGetBufferMemoryRequirements(device,buffer,&req);
    VkDeviceMemory bmem=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK(vkBindBufferMemory(device,buffer,bmem,0));
    VkImageViewCreateInfo vc={INFO(IMAGE_VIEW_CREATE_INFO),.image=image,.viewType=VK_IMAGE_VIEW_TYPE_2D,
        .format=fmt,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    VkImageView view; VK(vkCreateImageView(device,&vc,NULL,&view));
    VkAttachmentDescription att={.format=fmt,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    VkAttachmentReference ref={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&ref};
    VkSubpassDependency dep={.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,
        .srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT,
        .srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
    VkRenderPassCreateInfo rc={INFO(RENDER_PASS_CREATE_INFO),.attachmentCount=1,.pAttachments=&att,
        .subpassCount=1,.pSubpasses=&sub,.dependencyCount=1,.pDependencies=&dep};
    VkRenderPass render; VK(vkCreateRenderPass(device,&rc,NULL,&render));
    VkFramebufferCreateInfo fc={INFO(FRAMEBUFFER_CREATE_INFO),.renderPass=render,
        .attachmentCount=1,.pAttachments=&view,.width=W,.height=H,.layers=1};
    VkFramebuffer fb; VK(vkCreateFramebuffer(device,&fc,NULL,&fb));

    VkPipelineShaderStageCreateInfo stages[]={
        {INFO(PIPELINE_SHADER_STAGE_CREATE_INFO),.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=module(argv[1]),.pName="main"},
        {INFO(PIPELINE_SHADER_STAGE_CREATE_INFO),.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=module(argv[2]),.pName="main"}};
    VkPipelineLayoutCreateInfo lc={INFO(PIPELINE_LAYOUT_CREATE_INFO)};
    struct sources src={0};
    if (sample) {
        VkFormatProperties fp; vkGetPhysicalDeviceFormatProperties(gpu,VK_FORMAT_R32G32B32A32_SFLOAT,&fp);
        CHECK(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
        create_sources(&src,bmem);
        lc.setLayoutCount=1; lc.pSetLayouts=&src.layout;
    }
    VkPipelineLayout layout; VK(vkCreatePipelineLayout(device,&lc,NULL,&layout));
    VkViewport viewport={0,0,W,H,0,1}; VkRect2D scissor={{1,1},{W-2,H-2}};
    VkPipelineViewportStateCreateInfo vp={INFO(PIPELINE_VIEWPORT_STATE_CREATE_INFO),.viewportCount=1,
        .pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
    VkPipelineVertexInputStateCreateInfo vi={INFO(PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO)};
    VkPipelineInputAssemblyStateCreateInfo ia={INFO(PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO),
        .topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineRasterizationStateCreateInfo rs={INFO(PIPELINE_RASTERIZATION_STATE_CREATE_INFO),.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo ms={INFO(PIPELINE_MULTISAMPLE_STATE_CREATE_INFO),.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ca={.colorWriteMask=15};
    VkPipelineColorBlendStateCreateInfo cb={INFO(PIPELINE_COLOR_BLEND_STATE_CREATE_INFO),.attachmentCount=1,.pAttachments=&ca};
    VkGraphicsPipelineCreateInfo pc={INFO(GRAPHICS_PIPELINE_CREATE_INFO),.stageCount=2,.pStages=stages,
        .pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,.pRasterizationState=&rs,
        .pMultisampleState=&ms,.pColorBlendState=&cb,.layout=layout,.renderPass=render};
    VkPipeline pipeline; VK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pc,NULL,&pipeline));
    VkCommandPoolCreateInfo poolinfo={INFO(COMMAND_POOL_CREATE_INFO),.queueFamilyIndex=family};
    VkCommandPool pool; VK(vkCreateCommandPool(device,&poolinfo,NULL,&pool));
    VkCommandBufferAllocateInfo alloc={INFO(COMMAND_BUFFER_ALLOCATE_INFO),.commandPool=pool,.commandBufferCount=1};
    VkCommandBuffer cmd; VK(vkAllocateCommandBuffers(device,&alloc,&cmd));
    VkCommandBufferBeginInfo begin={INFO(COMMAND_BUFFER_BEGIN_INFO)}; VK(vkBeginCommandBuffer(cmd,&begin));
    if (sample) {
        upload_sources(&src,cmd,buffer);
        vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&src.set,0,NULL);
    }
    VkClearValue clear={.color.float32={1,0,1,1}};
    VkRenderPassBeginInfo rp={INFO(RENDER_PASS_BEGIN_INFO),.renderPass=render,.framebuffer=fb,
        .renderArea={{0,0},{W,H}},.clearValueCount=1,.pClearValues=&clear};
    vkCmdBeginRenderPass(cmd,&rp,VK_SUBPASS_CONTENTS_INLINE); vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
    vkCmdDraw(cmd,3,1,0,0); vkCmdEndRenderPass(cmd);
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={W,H,1}};
    vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
    VkMemoryBarrier mb={INFO(MEMORY_BARRIER),.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,NULL,0,NULL);
    VK(vkEndCommandBuffer(cmd));
    VkFenceCreateInfo fenceinfo={INFO(FENCE_CREATE_INFO)}; VkFence fence; VK(vkCreateFence(device,&fenceinfo,NULL,&fence));
    VkSubmitInfo submit={INFO(SUBMIT_INFO),.commandBufferCount=1,.pCommandBuffers=&cmd};
    VK(vkQueueSubmit(queue,1,&submit,fence)); VK(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000ULL));
    void *pixels; VK(vkMapMemory(device,bmem,0,BYTES,0,&pixels));
    FILE *out=fopen(argv[3],"wb"); CHECK(out && fwrite(pixels,1,BYTES,out)==BYTES && fclose(out)==0);
    vkUnmapMemory(device,bmem); vkDestroyFence(device,fence,NULL); vkDestroyCommandPool(device,pool,NULL);
    vkDestroyPipeline(device,pipeline,NULL); vkDestroyPipelineLayout(device,layout,NULL);
    if (sample) destroy_sources(&src);
    for (int i=0;i<2;i++) vkDestroyShaderModule(device,stages[i].module,NULL);
    vkDestroyFramebuffer(device,fb,NULL); vkDestroyRenderPass(device,render,NULL);
    vkDestroyImageView(device,view,NULL); vkDestroyImage(device,image,NULL); vkDestroyBuffer(device,buffer,NULL);
    vkFreeMemory(device,imem,NULL); vkFreeMemory(device,bmem,NULL);
    vkDestroyDevice(device,NULL); vkDestroyInstance(instance,NULL);
}
