#include <amdgpu_drm.h>
#include <linux/dma-buf.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define OK(x) do { if(!(x)){perror(#x);return 1;} }while(0)
int main(int argc,char **argv)
{
    if(argc!=3)return 1;
    int fd=open(argv[1],O_RDWR|O_CLOEXEC);OK(fd>=0);
    union drm_amdgpu_gem_create gc={.in={.bo_size=65536,.alignment=65536,.domains=AMDGPU_GEM_DOMAIN_GTT,
        .domain_flags=AMDGPU_GEM_CREATE_CPU_GTT_USWC|AMDGPU_GEM_CREATE_UNCACHED|AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED}};
    OK(!ioctl(fd,DRM_IOCTL_AMDGPU_GEM_CREATE,&gc));
    struct drm_amdgpu_gem_create_in actual={0};
    struct drm_amdgpu_gem_op g={.handle=gc.out.handle,.op=AMDGPU_GEM_OP_GET_GEM_CREATE_INFO,.value=(uintptr_t)&actual};
    OK(!ioctl(fd,DRM_IOCTL_AMDGPU_GEM_OP,&g));
    printf("driver GET_GEM_CREATE_INFO size=%#llx domains=%#llx flags=%#llx\n",actual.bo_size,actual.domains,actual.domain_flags);
    OK(actual.bo_size==65536 && (actual.domain_flags & 0x4004)==0x4004);
    struct drm_prime_handle exp={.handle=gc.out.handle,.flags=DRM_RDWR,.fd=-1};OK(!ioctl(fd,DRM_IOCTL_PRIME_HANDLE_TO_FD,&exp));close(fd);
    volatile uint32_t *cpu=mmap(NULL,65536,PROT_READ|PROT_WRITE,MAP_SHARED,exp.fd,0);OK(cpu!=MAP_FAILED);
    struct dma_buf_sync sync={DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE};OK(!ioctl(exp.fd,DMA_BUF_IOCTL_SYNC,&sync));
    cpu[0]=0x13a4c95d;__asm__ volatile("sfence":::"memory");sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_WRITE;OK(!ioctl(exp.fd,DMA_BUF_IOCTL_SYNC,&sync));
    OK(!munmap((void*)cpu,65536));char num[32];sprintf(num,"%d",exp.fd);fflush(stdout);
    execl(argv[2],argv[2],num,(char*)NULL);return 1;
}
