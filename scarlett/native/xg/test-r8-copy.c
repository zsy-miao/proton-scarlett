#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

__declspec(dllimport) HRESULT WINAPI XGCreateTextureComputer(const void *,void **);
__declspec(dllimport) HRESULT WINAPI XGSuggestSwizzleMode(const void *,uintptr_t,DWORD *);
typedef HRESULT (WINAPI *copy_fn)(void *,void *,UINT,UINT,const void *,UINT,UINT);
typedef ULONG (WINAPI *ref_fn)(void *);
#define CK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)

int main(void)
{
    DWORD d[]={3,0,4096,0,256,0,128,0x10001,61,1,0,0x105,0x300000,0};
    void **c=NULL;
    for (UINT i=0;i<13;i++) if (i!=1) {
        d[i]^=1; CK(FAILED(XGCreateTextureComputer(d,(void **)&c)) && !c); d[i]^=1;
    }
    d[11]=0; CK(FAILED(XGCreateTextureComputer(d,(void **)&c)) && !c);
    d[2]=0; CK(FAILED(XGSuggestSwizzleMode(d,0,&d[11])) && !d[11]); d[11]=0x105; d[2]=4096;
    CK(SUCCEEDED(XGCreateTextureComputer(d,(void **)&c)) && c);
    void **vt=* (void ***)c;
    CK(((ref_fn)vt[0])(c)==2 && ((ref_fn)vt[1])(c)==1);
    HMODULE xm=LoadLibraryW(L"xmem.dll"); CK(xm);
    typedef void *(WINAPI *alloc_fn)(void *,SIZE_T,DWORD,UINT64,DWORD);
    alloc_fn alloc=(alloc_fn)(void *)GetProcAddress(xm,"XMemVirtualAlloc"); CK(alloc);
    BYTE *src=alloc(NULL,0x49400000,0x20003000,0x183,0x22004);
    BYTE *dst=alloc(NULL,0x89700000,0x20003000,0x283,0x2404); CK(src && dst);
    BYTE *s=src+0x2390,*t=dst+8192;
    copy_fn copy=(copy_fn)vt[8];
    memset(dst,0xb6,65536);
    CK(FAILED(copy(c,t,1,0,s,256,32768)) && FAILED(copy(c,t,0,1,s,256,32768)));
    CK(FAILED(copy(c,t,0,0,s,255,32768)) && FAILED(copy(c,t,0,0,s,256,32767)));
    CK(FAILED(copy(c,t+1,0,0,s,256,32768)) && FAILED(copy(c,s,0,0,t,256,32768)));
    CK(FAILED(copy(c,t,0,0,src+0x49400000-32767,256,32768)));
    CK(FAILED(copy(c,dst+0x89700000-28672,0,0,s,256,32768)));
    for (UINT i=0;i<65536;i++) CK(dst[i]==0xb6);
    for (UINT pass=0;pass<2;pass++) {
        for (UINT i=0;i<32768;i++) s[i]=(BYTE)(i>>(8*pass));
        CK(copy(c,t,0,0,s,256,32768)==S_OK);
        for (UINT y=0;y<128;y++) for (UINT x=0;x<256;x++) {
            UINT a=(y/64*4+x/64)*4096+(x&15)+(y%32)*16+(x&16)*32+(y&32)*32+(x&32)*64;
            CK(t[a]==(pass?y:x));
        }
        for (UINT i=0;i<65536;i++) if(i<8192 || i>=40960) CK(dst[i]==0xb6);
    }
    CK(((ref_fn)vt[1])(c)==0);
    CK(VirtualFree(dst,0,MEM_RELEASE) && VirtualFree(src,0,MEM_RELEASE));
    puts("PASS explicit 0x105: two complete coordinate copies into original WC section; guards, ranges, pitches, policy, descriptor and ref controls");
    return 0;
}
