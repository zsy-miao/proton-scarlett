/* Two 4x3 views and two different samplers make descriptor selection observable. */
struct sources {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view[2];
    VkSampler sampler[2];
    VkDescriptorSetLayout layout;
    VkDescriptorPool pool;
    VkDescriptorSet set;
};

static void create_sources(struct sources *s, VkDeviceMemory staging) {
    VkImageCreateInfo ic={INFO(IMAGE_CREATE_INFO),.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R32G32B32A32_SFLOAT,
        .extent={4,3,1},.arrayLayers=2,.mipLevels=1,.samples=VK_SAMPLE_COUNT_1_BIT,
        .usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT};
    VK(vkCreateImage(device,&ic,NULL,&s->image)); VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device,s->image,&req); s->memory=allocate(req,0);
    VK(vkBindImageMemory(device,s->image,s->memory,0));
    float *p; VK(vkMapMemory(device,staging,0,2*3*4*16,0,(void**)&p));
    for (int k=0;k<2;k++) for (int y=0;y<3;y++) for (int x=0;x<4;x++) {
        *p++=(x+4*y)/16.f; *p++=(3-x+k)/8.f; *p++=(2*y+k)/8.f;
        *p++=((x^y)+k)/8.f;
    }
    vkUnmapMemory(device,staging);
    VkDescriptorSetLayoutBinding binds[4]; VkDescriptorImageInfo info[4]={0};
    VkWriteDescriptorSet writes[4];
    for (unsigned i=0;i<2;i++) {
        VkImageViewCreateInfo vi={INFO(IMAGE_VIEW_CREATE_INFO),.image=s->image,.format=ic.format,
            .viewType=VK_IMAGE_VIEW_TYPE_2D,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,i,1}};
        VK(vkCreateImageView(device,&vi,NULL,&s->view[i]));
        VkFilter filter=i?VK_FILTER_NEAREST:VK_FILTER_LINEAR;
        VkSamplerCreateInfo si={INFO(SAMPLER_CREATE_INFO),.magFilter=filter,.minFilter=filter,
            .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
        VK(vkCreateSampler(device,&si,NULL,&s->sampler[i]));
        binds[i]=(VkDescriptorSetLayoutBinding){72+i,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL};
        binds[2+i]=(VkDescriptorSetLayoutBinding){3+i,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL};
        info[i].imageView=s->view[i]; info[i].imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        info[2+i].sampler=s->sampler[i];
    }
    VkDescriptorSetLayoutCreateInfo lc={INFO(DESCRIPTOR_SET_LAYOUT_CREATE_INFO),.bindingCount=4,.pBindings=binds};
    VK(vkCreateDescriptorSetLayout(device,&lc,NULL,&s->layout));
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,2},{VK_DESCRIPTOR_TYPE_SAMPLER,2}};
    VkDescriptorPoolCreateInfo pc={INFO(DESCRIPTOR_POOL_CREATE_INFO),.maxSets=1,.poolSizeCount=2,.pPoolSizes=sizes};
    VK(vkCreateDescriptorPool(device,&pc,NULL,&s->pool));
    VkDescriptorSetAllocateInfo sa={INFO(DESCRIPTOR_SET_ALLOCATE_INFO),.descriptorPool=s->pool,
        .descriptorSetCount=1,.pSetLayouts=&s->layout};
    VK(vkAllocateDescriptorSets(device,&sa,&s->set));
    for (int i=0;i<4;i++)
        writes[i]=(VkWriteDescriptorSet){INFO(WRITE_DESCRIPTOR_SET),.dstSet=s->set,.dstBinding=binds[i].binding,
            .descriptorCount=1,.descriptorType=binds[i].descriptorType,.pImageInfo=&info[i]};
    vkUpdateDescriptorSets(device,4,writes,0,NULL);
}

static void upload_sources(const struct sources *s,VkCommandBuffer cmd,VkBuffer buffer) {
    VkImageMemoryBarrier b={INFO(IMAGE_MEMORY_BARRIER),.image=s->image,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2},.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&b);
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,2},.imageExtent={4,3,1}};
    vkCmdCopyBufferToImage(cmd,buffer,s->image,b.newLayout,1,&copy);
    b.oldLayout=b.newLayout; b.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask=b.dstAccessMask; b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&b);
}

static void destroy_sources(struct sources *s) {
    vkDestroyDescriptorPool(device,s->pool,NULL); vkDestroyDescriptorSetLayout(device,s->layout,NULL);
    for (int i=0;i<2;i++) {
        vkDestroyImageView(device,s->view[i],NULL); vkDestroySampler(device,s->sampler[i],NULL);
    }
    vkDestroyImage(device,s->image,NULL); vkFreeMemory(device,s->memory,NULL);
}
