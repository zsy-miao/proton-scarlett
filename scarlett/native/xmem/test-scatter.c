#include <windows.h>
#include <stdio.h>
#include "marked_section.h"
#include "tile_sections.h"
__declspec(dllimport) void *WINAPI XMemVirtualAlloc(void *,SIZE_T,DWORD,UINT64,DWORD);
__declspec(dllimport) BOOL WINAPI XMemAllocatePhysicalPages(UINT64,ULONG_PTR *,ULONG_PTR *);
__declspec(dllimport) BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR,ULONG_PTR *);
__declspec(dllimport) void *WINAPI XMemMapPhysicalPages(void *,ULONG_PTR,ULONG_PTR *);
__declspec(dllimport) BOOL WINAPI XMemMapPhysicalPagesScatter(void **,ULONG_PTR,ULONG_PTR *);
#define CK(x) do { if (!(x)) {fprintf(stderr,"FAIL %d %s error=%lu\n",__LINE__,#x,GetLastError());return 1;} } while(0)
enum { P=65536 };
static BOOL reserved(void *p)
{
    MEMORY_BASIC_INFORMATION m;
    return VirtualQuery(p,&m,sizeof m)==sizeof m && m.State==MEM_RESERVE;
}
int main(void)
{
    ULONG_PTR n=TILE_PAGES;
    static ULONG_PTR id[TILE_PAGES];
    SIZE_T size=24ull<<30;
    BYTE *dst=XMemVirtualAlloc(NULL,size,0x20402000,0x21,PAGE_READWRITE);
    BYTE *base=XMemVirtualAlloc(NULL,n*P,0x20402000,0x23,0x2004);
    CK(dst && base && XMemAllocatePhysicalPages(0x380,&n,id));
    CK(XMemMapPhysicalPages(base,n,id)==base);
    BYTE *at=dst+(16ull<<30);
    void *va[]={at,at+P,at+2*P};
    ULONG_PTR start=(1u<<14)+9; /* non-zero offset into the second original DMA section */
    BYTE *src=base+start*P;
    memset(src-P,0x9a,5*P);
    ULONG_PTR bad[]={id[start],id[start+1],id[start+2]+16};
    CK(!XMemMapPhysicalPagesScatter(va,3,bad) && reserved(at));
    bad[2]=id[start+2]; ++bad[0]; CK(!XMemMapPhysicalPagesScatter(va,3,bad) && reserved(at+2*P));
    CK(!XMemMapPhysicalPagesScatter(va,3,id+(1u<<14)-1) && reserved(at));
    ULONG_PTR clipped[]={id[n-2],id[n-1],id[n-1]+16};
    CK(!XMemMapPhysicalPagesScatter(va,3,clipped) && reserved(at));
    va[2]=va[1]; CK(!XMemMapPhysicalPagesScatter(va,3,id+start) && reserved(at)); va[2]=at+2*P;
    CK(!XMemMapPhysicalPagesScatter(va,0,id) && !XMemMapPhysicalPagesScatter(va,3,(void *)1));
    BYTE *guards=VirtualAlloc(NULL,2*P,MEM_RESERVE,PAGE_NOACCESS);
    CK(guards && VirtualAlloc(guards,P,MEM_COMMIT,PAGE_READWRITE)==guards);
    ULONG_PTR *last=(ULONG_PTR *)(guards+P)-2;
    memcpy(last,id+start,2*sizeof *last);
    CK(!XMemMapPhysicalPagesScatter(va,3,last) && reserved(at));
    memcpy(last,va,2*sizeof *last);
    CK(!XMemMapPhysicalPagesScatter((void **)last,3,id+start) && reserved(at));
    CK(VirtualFree(guards,0,MEM_RELEASE));
    CK(XMemMapPhysicalPagesScatter(va,3,id+start));
    MEMORY_BASIC_INFORMATION m;
    CK(VirtualQuery(at,&m,sizeof m) && m.State==MEM_COMMIT && m.Protect==PAGE_READWRITE &&
        m.AllocationBase==at && m.RegionSize==3*P && reserved(at-P) && reserved(at+3*P));
    struct scarlett_section_info x,y;
    CK(SUCCEEDED(scarlett_query_section(0x5343000c,at,3*P,&x)) &&
        SUCCEEDED(scarlett_query_section(0x5343000c,src,3*P,&y)));
    BOOL (WINAPI *same)(HANDLE,HANDLE)=(void*)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles");
    CK(same && same(x.section,y.section) && x.offset==9*P && x.offset==y.offset && x.view_size==(1u<<30));
    for (SIZE_T i=0;i<3*P;i++) at[i]=(BYTE)(i/P*29+i);
    CK(!memcmp(at,src,3*P));
    memset(src,0x34,3*P);
    CK(!XMemMapPhysicalPagesScatter(va,3,id+start) && !XMemMapPhysicalPagesScatter(va+1,2,NULL));
    ULONG_PTR zero[]={0,id[start+1],0};
    CK(!XMemMapPhysicalPagesScatter(va,3,zero));
    CK(!XMemMapPhysicalPages(at,3,NULL) && !XMemMapPhysicalPages(dst,1,id));
    for (SIZE_T i=0;i<3*P;i++) CK(at[i]==0x34);
    for (SIZE_T i=0;i<P;i++) CK((src-P)[i]==0x9a && src[3*P+i]==0x9a);
    CK(XMemMapPhysicalPages(base,n,NULL)==base && !XMemFreePhysicalPages(n,id) && GetLastError()==ERROR_BUSY);
    CK(XMemMapPhysicalPagesScatter(va,3,NULL) && reserved(at));
    CK(!XMemMapPhysicalPagesScatter(va,3,NULL));
    zero[1]=0;
    CK(XMemMapPhysicalPagesScatter(va,3,id+start) && XMemMapPhysicalPagesScatter(va,3,zero));
    CK(VirtualFree(dst,0,MEM_RELEASE));
    CK(VirtualAlloc(dst,size,MEM_RESERVE,PAGE_NOACCESS)==dst && !XMemMapPhysicalPagesScatter(va,3,id+start));
    CK(VirtualFree(dst,0,MEM_RELEASE));
    dst=XMemVirtualAlloc(NULL,size,0x20402000,0x21,PAGE_READWRITE);
    CK(dst); for (unsigned j=0;j<3;j++) va[j]=dst+j*P;
    CK(XMemFreePhysicalPages(n,id) && !XMemMapPhysicalPagesScatter(va,3,id+start));
    CK(VirtualFree(dst,0,MEM_RELEASE) && VirtualFree(base,0,MEM_RELEASE) && CloseHandle(x.section) && CloseHandle(y.section));
    puts("PASS scatter: original WB section and offset, all 196608 alias bytes, neighboring guards, atomic refusal, run busy, unmap and teardown");
    return 0;
}
