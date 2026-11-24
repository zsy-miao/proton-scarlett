/* Observe accepted DRM requests from the synthetic application's own Mesa driver. */
#define _GNU_SOURCE
#include <amdgpu_drm.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <dlfcn.h>
#include <errno.h>
#include <string.h>
int ioctl(int fd,unsigned long request,...)
{
    static int (*fn)(int,unsigned long,...);
    if(!fn)fn=dlsym(RTLD_NEXT,"ioctl");
    va_list vl;va_start(vl,request);void *p=va_arg(vl,void*);va_end(vl);
    struct drm_amdgpu_gem_create_in create={0};
    if(request==DRM_IOCTL_AMDGPU_GEM_CREATE)memcpy(&create,p,sizeof create);
    int r=fn(fd,request,p),err=errno;
    if(!r && request==DRM_IOCTL_AMDGPU_GEM_CREATE) {
        union drm_amdgpu_gem_create *a=p;
        fprintf(stderr,"GEM fd=%d bo=%u sz=%#llx domain=%#llx flags=%#llx HOST_USWC=%d BO_UNCACHED=%d\n",fd,a->out.handle,
                create.bo_size,create.domains,create.domain_flags,!!(create.domain_flags&AMDGPU_GEM_CREATE_CPU_GTT_USWC),!!(create.domain_flags&AMDGPU_GEM_CREATE_UNCACHED));
    }
    if(!r && _IOC_TYPE(request)=='d' && _IOC_NR(request)==_IOC_NR(DRM_IOCTL_AMDGPU_GEM_VA)) {
        struct drm_amdgpu_gem_va *v=p;
        fprintf(stderr,"VA fd=%d bo=%u op=%u sz=%#llx va=%#llx flags=%#x mtype=%#x\n",fd,v->handle,v->operation,v->map_size,v->va_address,v->flags,v->flags&AMDGPU_VM_MTYPE_MASK);
    }
    if(!r && (request==DRM_IOCTL_PRIME_FD_TO_HANDLE || request==DRM_IOCTL_PRIME_HANDLE_TO_FD)){
        struct drm_prime_handle *h=p;fprintf(stderr,"PRIME %s fd=%d bo=%u dmafd=%d\n",request==DRM_IOCTL_PRIME_FD_TO_HANDLE?"import":"export",fd,h->handle,h->fd);
        struct drm_amdgpu_gem_create_in ci={0};
        struct drm_amdgpu_gem_op g={.handle=h->handle,.op=AMDGPU_GEM_OP_GET_GEM_CREATE_INFO,.value=(unsigned long)&ci};
        if(!fn(fd,DRM_IOCTL_AMDGPU_GEM_OP,&g))
            fprintf(stderr,"GET fd=%d bo=%u flags=%#llx USWC=%d UNCACHED=%d\n",fd,h->handle,ci.domain_flags,
                    !!(ci.domain_flags&AMDGPU_GEM_CREATE_CPU_GTT_USWC),!!(ci.domain_flags&AMDGPU_GEM_CREATE_UNCACHED));
    }
    errno=err;return r;
}
