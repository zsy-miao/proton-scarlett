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
int main(int argc,char **argv)
{
    BOOL stale=argc==2 && !strcmp(argv[1],"--stale-gap");
    CK(argc==1 || stale);
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
    BYTE *other=at+9*P,*src2=src+4*P;
    void *va2[]={other,other+P};
    void *overlap[]={at+2*P,at+3*P};
    CK(!XMemMapPhysicalPagesScatter(overlap,2,id+start+4) && reserved(at+3*P));
    CK(!XMemMapPhysicalPagesScatter(va2,2,id+start+2) && reserved(other)); /* overlapping source */
    CK(!XMemMapPhysicalPagesScatter(va2,2,id) && reserved(other)); /* other section */
    CK(XMemMapPhysicalPagesScatter(va2,2,id+start+4));
    if (stale) {
        /* Deliberately invalidate the live arena. Process exit owns cleanup. */
        BYTE *gap=at+3*P;
        CK(VirtualFree(gap,0,MEM_RELEASE));
        CK(!XMemVirtualAlloc(gap,6*P,0x20402000,0x21,PAGE_READWRITE));
        CK(VirtualAlloc(gap,6*P,MEM_RESERVE,PAGE_NOACCESS)==gap);
        CK(!XMemMapPhysicalPagesScatter(va,3,NULL) && !XMemMapPhysicalPagesScatter(va2,2,NULL));
        other[0]=0x6f;
        CK(src2[0]==0x6f && at[0]==0x34 && reserved(gap) && VirtualFree(gap,0,MEM_RELEASE));
        puts("PASS stale gap: refused replacement arena and preserved both live aliases");
        return 0;
    }
    struct scarlett_section_info second;
    CK(SUCCEEDED(scarlett_query_section(0x5343000c,other,2*P,&second)) &&
       same(second.section,x.section) && second.base==other && second.offset==13*P &&
       second.view_size==x.view_size && CloseHandle(second.section));
    CK(!XMemMapPhysicalPagesScatter(va2+1,1,NULL) && !XMemMapPhysicalPagesScatter(va2,2,id+start+4));
    void *gap[]={at+5*P};
    CK(!XMemMapPhysicalPagesScatter(gap,1,id+start+5) && GetLastError()==ERROR_NOT_SUPPORTED); /* overlaps second backing only */
    for(SIZE_T i=0;i<2*P;++i)other[i]=(BYTE)(17+i);
    CK(!memcmp(src2,other,2*P));memset(src2,0x71,2*P);
    for(SIZE_T i=0;i<2*P;i++)CK(other[i]==0x71);
    for(SIZE_T i=0;i<3*P;i++)CK(at[i]==0x34);
    for(unsigned j=3;j<9;j++) CK(reserved(at+j*P));
    /* Remove the higher span first; reinstall in reverse VA order. */
    CK(XMemMapPhysicalPagesScatter(va2,2,NULL) && reserved(other) && at[0]==0x34);
    CK(XMemMapPhysicalPagesScatter(va,3,NULL) && reserved(at));
    /* Measured VAs 0x54b070000, 0x56b070000, 0x58b070000; IDs 0x3800, 0x1dc00, 0x2de80. */
    enum { N1=6720,N2=4136,N12=N1+N2,STEP=8192 };
    static void *span[N12+1];
    for(unsigned j=0;j<N1;j++)span[j]=at+(SIZE_T)j*P;
    for(unsigned j=0;j<N2;j++)span[N1+j]=at+(SIZE_T)(STEP+j)*P;
    BYTE *last_va=span[N12]=at+2*STEP*P;
    CK(XMemMapPhysicalPagesScatter(span,N1,id));
#ifdef SCATTER_TEST_ROLLBACK
    /* Fixture DLL fails the first N2 bind after both flanks were marked. */
    CK(!XMemMapPhysicalPagesScatter(span+N1,N2,id+N1) && GetLastError()==ERROR_GEN_FAILURE);
    at[0]=0x47;
    CK(base[0]==0x47 && VirtualQuery(at+N1*P,&m,sizeof m)==sizeof m &&
       m.AllocationBase==at+N1*P && m.RegionSize==size-(at-dst)-N1*P && m.State==MEM_RESERVE);
#endif
    CK(XMemMapPhysicalPagesScatter(span+N1,N2,id+N1));
    CK(!XMemMapPhysicalPagesScatter(span+N12-1,1,id+N12) && GetLastError()==ERROR_INVALID_ADDRESS);
    CK(XMemMapPhysicalPagesScatter(span+N12,1,id+N12));
    void *fourth[]={last_va+P};
    CK(!XMemMapPhysicalPagesScatter(fourth,1,id+N12+1) && GetLastError()==ERROR_INVALID_ADDRESS && reserved(fourth[0]));
    struct scarlett_section_info original;
    CK(SUCCEEDED(scarlett_query_section(0x5343000c,base,(N12+1)*P,&original)) &&
       original.base==base && !original.offset && original.view_size==tile_bytes(0));
    const unsigned starts[]={0,N1,N12,N12+1};
    for(unsigned j=0;j<3;j++) {
        SIZE_T offset=starts[j]*P,bytes=(starts[j+1]-starts[j])*P;
        BYTE *alias=span[starts[j]];
        CK(SUCCEEDED(scarlett_query_section(0x5343000c,alias,bytes,&second)) &&
           same(second.section,original.section) && second.base==alias && second.offset==offset &&
           second.view_size==original.view_size && CloseHandle(second.section));
        alias[0]=(BYTE)(0x51+j);base[offset+bytes-1]=(BYTE)(0x74+j);
        CK(base[offset]==0x51+j && alias[bytes-1]==0x74+j);
    }
    CK(CloseHandle(original.section));
    CK(VirtualQuery(at+N1*P,&m,sizeof m)==sizeof m && m.AllocationBase==at+N1*P &&
       m.RegionSize==1472*P && m.State==MEM_RESERVE);
#ifdef SCATTER_TEST_ROLLBACK
    /* Fixture DLL fails the first N1 unbind after both flanks were released. */
    CK(!XMemMapPhysicalPagesScatter(span,N1,NULL) && GetLastError()==ERROR_GEN_FAILURE);
    CK(at[0]==0x51 && ((BYTE *)span[N1])[0]==0x52 && last_va[0]==0x53 && reserved(at+N1*P));
#endif
    CK(XMemMapPhysicalPagesScatter(span+N1,N2,NULL));
    CK(VirtualQuery(at+N1*P,&m,sizeof m)==sizeof m && m.AllocationBase==at+N1*P &&
       m.RegionSize==(2*STEP-N1)*P && m.State==MEM_RESERVE);
    base[N1*P-1]=0x68; last_va[0]=0x69;
    CK(at[N1*P-1]==0x68 && base[N12*P]==0x69);
    /* Reinsert in the middle, then shift both survivors out and back at the front. */
    CK(XMemMapPhysicalPagesScatter(span+N1,N2,id+N1) && ((BYTE *)span[N1])[0]==0x52);
    CK(XMemMapPhysicalPagesScatter(span,N1,NULL) && XMemMapPhysicalPagesScatter(span,N1,id));
    CK(XMemMapPhysicalPagesScatter(span,N1,NULL) && XMemMapPhysicalPagesScatter(span+N1,N2,NULL) &&
       last_va[0]==0x69 && XMemMapPhysicalPagesScatter(span+N12,1,NULL));
    CK(XMemMapPhysicalPagesScatter(va2,2,id+start+4) && XMemMapPhysicalPagesScatter(va,3,id+start));
    CK(XMemMapPhysicalPages(base,n,NULL)==base && !XMemFreePhysicalPages(n,id) && GetLastError()==ERROR_BUSY);
    CK(XMemMapPhysicalPagesScatter(va,3,NULL) && reserved(at));
    CK(!XMemMapPhysicalPagesScatter(va,3,NULL));
    ULONG_PTR one=1,ordinary;
    CK(XMemAllocatePhysicalPages(0,&one,&ordinary));
    CK(!XMemMapPhysicalPages(dst,1,&ordinary) && !XMemFreePhysicalPages(n,id) && GetLastError()==ERROR_BUSY);
    for(SIZE_T i=0;i<2*P;i++)CK(other[i]==0x71);
    CK(XMemMapPhysicalPagesScatter(va2,2,NULL) && reserved(other));
    CK(VirtualQuery(dst,&m,sizeof m)==sizeof m && m.AllocationBase==dst && m.RegionSize==size && m.State==MEM_RESERVE);
    zero[1]=0;
    CK(XMemMapPhysicalPagesScatter(va,3,id+start) && XMemMapPhysicalPagesScatter(va,3,zero));
    CK(VirtualFree(dst,0,MEM_RELEASE));
    CK(VirtualAlloc(dst,size,MEM_RESERVE,PAGE_NOACCESS)==dst && !XMemMapPhysicalPagesScatter(va,3,id+start));
    CK(VirtualFree(dst,0,MEM_RELEASE));
    dst=XMemVirtualAlloc(NULL,size,0x20402000,0x21,PAGE_READWRITE);
    CK(dst); for (unsigned j=0;j<3;j++) va[j]=dst+j*P;
    CK(XMemMapPhysicalPages(dst+4*P,1,&ordinary)==dst+4*P && !XMemMapPhysicalPagesScatter(va,3,id+start));
    CK(XMemMapPhysicalPages(dst+4*P,1,NULL)==dst+4*P && XMemFreePhysicalPages(1,&ordinary));
    CK(VirtualFree(dst,0,MEM_RELEASE));
    dst=XMemVirtualAlloc(NULL,size,0x20402000,0x21,PAGE_READWRITE);
    CK(dst); for (unsigned j=0;j<3;j++) va[j]=dst+j*P;
    CK(XMemFreePhysicalPages(n,id) && !XMemMapPhysicalPagesScatter(va,3,id+start));
    CK(VirtualFree(dst,0,MEM_RELEASE) && VirtualFree(base,0,MEM_RELEASE) && CloseHandle(x.section) && CloseHandle(y.section));
    puts("PASS scatter: three measured original-section aliases, atomic refusal, middle teardown and surviving neighbors");
    return 0;
}
