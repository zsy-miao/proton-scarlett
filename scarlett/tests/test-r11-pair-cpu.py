"""Run the actual range reservation on the CPU with synthetic backing identities."""
from pathlib import Path
import subprocess
import tempfile
import sys

s = Path(sys.argv[1]).read_text()
shared = s.split('static HRESULT scarlett_tiled_buffer(',1)[1]
bound = next(line for line in shared.splitlines() if 'off>size-span' in line)
wrapper = s[s.index('HRESULT WINAPI ScarlettTiledR11FiveV1('):]
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)/'test.c'
    p.write_text(r'''
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
typedef int HRESULT, BOOL, IUnknown;
typedef uintptr_t HANDLE, VkDeviceMemory;
#define WINAPI
#define S_OK 0
#define E_INVALIDARG -1
#define DXGI_ERROR_INVALID_CALL -2
#define E_NOTIMPL -3
#define SUCCEEDED(h) ((h)>=0)
static BOOL same_handle(HANDLE a,HANDLE b){return a/2==b/2;}
#define GetProcAddress(a,b) same_handle
''' + s[s.index('struct scarlett_slice\n'):s.index('static void scarlett_remove_slice')] + r'''
typedef int d3d12_device_iface, ID3D12Heap, ID3D12Resource;
typedef size_t SIZE_T;
#define E_POINTER -4
static HRESULT scarlett_tiled_buffer(d3d12_device_iface *iface, ID3D12Heap *native,HANDLE section,
        SIZE_T size,SIZE_T off,bool r11,ID3D12Resource **out){
    const SIZE_T span=r11?0x4b1400:0x384000; (void)out;
''' + bound + '\nreturn S_OK;\n}\n' + wrapper + r'''
static struct d3d12_device *dev=(void*)1;
static struct scarlett_slice a,b,c,r;
enum { off=0x13d08000, stride=0x384000 };
static void init(void){
    r=(struct scarlett_slice){.start=off,.end=off+0x4b1400,.wc_section=3};
    a=(struct scarlett_slice){.start=off-2*stride,.end=off-stride,.wc_section=2,.r32_device=dev};
    b=(struct scarlett_slice){.next=&a,.start=off-stride,.end=off,.wc_section=2,.r32_device=dev};
    c=(struct scarlett_slice){.next=&b,.start=r.end,.end=r.end+1,.wc_section=2};
    scarlett_slices=&c;
}
static unsigned n;
static void pre(int native,int section,size_t size,size_t start,int want){
    int iface=0,*out=&iface;
    if(ScarlettTiledR11FiveV1(&iface,native?&iface:NULL,section,size,start,&out)!=want || out)abort();
    ++n;
}
static void check(int hr,struct d3d12_device *d){
    struct scarlett_slice *old=scarlett_slices;
    if(scarlett_reserve_wc_slice(&r,d)!=hr || scarlett_slices!=(hr==S_OK?&r:old))abort();
    n++;
}
static pthread_barrier_t barrier;
struct job { struct scarlett_slice slot; HRESULT hr; };
static void *race(void *ptr){
    struct job *job=ptr;
    pthread_barrier_wait(&barrier);
    job->hr=scarlett_reserve_wc_slice(&job->slot,dev);
    return NULL;
}
int main(void){
    init();check(S_OK,dev);
    init();a.wc_section=4;check(E_INVALIDARG,dev);
    init();b.wc_section=4;check(E_INVALIDARG,dev);
    init();a.r32_device=NULL;check(E_INVALIDARG,dev);
    init();b.r32_device=NULL;check(E_INVALIDARG,dev);
    init();a.r32_device=(void*)2;check(E_INVALIDARG,dev);
    init();b.start--;b.end--;check(E_INVALIDARG,dev);
    init();c.start--;check(DXGI_ERROR_INVALID_CALL,dev);
    init();c.start--;c.wc_section=4;check(S_OK,dev);
    init();scarlett_slices=NULL;check(E_INVALIDARG,dev);
    init();scarlett_slices=NULL;check(S_OK,NULL);
    pre(0,1,0x89700000,off,S_OK);
    pre(0,1,0x89700000,0x13d14000,S_OK);
    pre(1,0,0x89700000,0x13c88000,S_OK);
    pre(1,0,0x89700000,off,E_INVALIDARG);
    pre(0,1,0x89700000,off+0x100,E_INVALIDARG);
    pre(0,1,0x89700000,SIZE_MAX-0x3fff,E_INVALIDARG);
    pre(0,1,0x89700000,0x89700000-0x4b0000,E_INVALIDARG);
    pre(0,1,1,off,E_INVALIDARG);
    pre(1,1,0x89700000,0x13c88000,E_INVALIDARG);
    init();
    struct job jobs[2]={{r,0},{r,0}};pthread_t threads[2];
    if(pthread_barrier_init(&barrier,NULL,2))abort();
    for(int i=0;i<2;i++)if(pthread_create(&threads[i],NULL,race,&jobs[i]))abort();
    for(int i=0;i<2;i++)if(pthread_join(threads[i],NULL))abort();
    if(!((jobs[0].hr==S_OK && jobs[1].hr==DXGI_ERROR_INVALID_CALL) ||
         (jobs[1].hr==S_OK && jobs[0].hr==DXGI_ERROR_INVALID_CALL)))abort();
    printf("PASS %u CPU cases: 11 reservation, 9 offset/argument, 1 concurrent double claim\n",n+1);
}
''')
    subprocess.run(['cc','-std=c11','-D_XOPEN_SOURCE=700','-Wall','-Wextra','-Werror','-pthread',str(p),'-o',str(p.with_suffix(''))],check=True)
    subprocess.run([str(p.with_suffix(''))],check=True)
