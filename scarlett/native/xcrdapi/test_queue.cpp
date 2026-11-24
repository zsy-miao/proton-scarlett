/* Public factory/file/status and queue calls through their x64 title slots. */
#include <windows.h>
#include <unknwn.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <cstdint>
extern "C" HRESULT WINAPI DStorageGetFactory(REFIID,void **);
#define CK(e) do { if (!(e)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#e); ExitProcess(1); } } while(0)
template<class F> F method(void *p,unsigned at) { return (F)((*(void ***)p)[at/8]); }
using Make=HRESULT(WINAPI*)(void *,const void *,REFIID,void **);
using Status=HRESULT(WINAPI*)(void *,UINT,const char *,REFIID,void **);
using Request=void(WINAPI*)(void *,const void *);
using Mark=void(WINAPI*)(void *,void *,UINT);
using Call=void(WINAPI*)(void *);
using Result=HRESULT(WINAPI*)(void *,UINT);
using Complete=bool(WINAPI*)(void *,UINT);
static const IID factory={0x62959d11,0xd45f,0x4884,{0xa0,0xa3,0xf7,0x86,0x2e,0xa2,0x43,0xae}},
    queue={0x3c02cb0c,0xc050,0x4195,{0x99,0xa1,0xa1,0xce,0x94,0xba,0x33,0xc5}},
    status={0xcd66d752,0x2786,0x44f7,{0x90,0x70,0xe2,0xa6,0xfd,0xad,0x3f,0x1b}},
    file={0x43704f9f,0x27c6,0x4a9f,{0x88,0x15,0xd1,0x65,0x27,0x89,0x4d,0xf4}};
static void *q,*s;
struct Blocked { HANDLE entered; };
static DWORD WINAPI blocked(void *arg)
{
    SetEvent(((Blocked *)arg)->entered);
    method<Mark>(q,0x20)(q,s,1);
    return 0;
}
static void test_zlib(void *f)
{
    // zlib.compress(bytes(range(16))*4); a second stream has a damaged Adler trailer.
    BYTE stream[]={0x78,0x9c,0x63,0x60,0x64,0x62,0x66,0x61,0x65,0x63,0xe7,0xe0,0xe4,0xe2,
        0xe6,0xe1,0xe5,0xe3,0x67,0x20,0x91,0x0f,0x00,0x37,0xe0,0x01,0xe1};
    WCHAR dir[MAX_PATH],path[MAX_PATH];
    CK(GetTempPathW(MAX_PATH,dir) && GetTempFileNameW(dir,L"zds",0,path));
    HANDLE h=CreateFileW(path,GENERIC_WRITE,0,nullptr,TRUNCATE_EXISTING,0,nullptr);
    DWORD n;CK(h!=INVALID_HANDLE_VALUE && WriteFile(h,stream,1,&n,nullptr) && n==1);
    CK(WriteFile(h,stream,sizeof stream,&n,nullptr) && n==sizeof stream);
    stream[sizeof stream-1]^=1;
    CK(WriteFile(h,stream,sizeof stream,&n,nullptr) && n==sizeof stream && CloseHandle(h));
    void *src; CK(method<Make>(f,0x20)(f,path,file,&src)==S_OK);
    UINT64 d[]={0,0x2103f,0,1}; CK(method<Make>(f,0x50)(f,d,queue,&q)==S_OK);
    CK(method<Status>(f,0x28)(f,1,nullptr,status,&s)==S_OK);
    BYTE dst[80];BYTE *pages=(BYTE*)VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CK(pages);memset(pages,0xab,8192);
    auto run=[&](UINT64 off,UINT64 size,UINT64 len,HRESULT expect,BYTE *target=nullptr,DWORD protect=0,bool close=false) {
        memset(dst,0xda,sizeof dst);
        UINT64 r[]={1,(UINT_PTR)(target?target:dst+8),0,len,(UINT_PTR)src,off,size,0,0};
        method<Request>(q,0x18)(q,r); memset(r,0,sizeof r);
        method<Mark>(q,0x20)(q,s,0);
        CK(method<Result>(s,0x20)(s,0)==E_PENDING);
        DWORD old; if(protect) CK(VirtualProtect(pages+4096,4096,protect,&old));
        if(close) { method<Call>(src,0x18)(src); CK(((IUnknown*)src)->Release()==0); }
        method<Call>(q,0x30)(q);
        auto until=GetTickCount64()+5000;
        while(!method<Complete>(s,0x18)(s,0) && GetTickCount64()<until) Sleep(1);
        CK(method<Result>(s,0x20)(s,0)==expect);
        if(protect) CK(VirtualProtect(pages+4096,4096,old,&old));
        for(unsigned i=0;i<sizeof dst;i++) CK(dst[i]==(expect==S_OK && i>=8 && i<72 ? ((i-8)&15) : 0xda));
        for(unsigned i=0;i<8192;i++) CK(pages[i]==0xab);
    };
    constexpr UINT64 z=sizeof stream;
    const HRESULT bad=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    run(1,z,64,S_OK); run(1,z-1,64,bad); run(1,z+1,64,bad);
    run(1+z,z,64,bad); run(1,z,63,bad); run(1,z,65,bad);
    run(1+z,z+1,64,HRESULT_FROM_WIN32(ERROR_HANDLE_EOF));
    run(INT64_MAX,z,64,E_INVALIDARG); run(1,z,0,E_INVALIDARG); run(1,0,64,E_INVALIDARG);
    run(1,z,(32u<<20)+1,E_INVALIDARG); run(1,(32u<<20)+1,64,E_INVALIDARG);
    run(1,z,64,E_POINTER,pages+4096-16,PAGE_READONLY);
    run(1,z,64,E_POINTER,pages+4096,PAGE_READWRITE|PAGE_GUARD);
    run(1,z,64,S_OK,nullptr,0,true);
    CK(VirtualFree(pages,0,MEM_RELEASE) && DeleteFileW(path));
    ((IUnknown*)q)->Release();((IUnknown*)s)->Release();
    puts("PASS zlib queue: exact offset/output, pending and terminal status, snapshot and file lifetime; corrupt, truncated, trailing, length, range and changed protection preserve destination");
}
int main()
{
    void *f,*p;
    CK(DStorageGetFactory(factory,&f)==S_OK);
    UINT64 d[4]={0,0x2103f,0,1}; auto create=method<Make>(f,0x50);
    for (UINT i:{0u,1u,3u}) { d[i]^=1; p=f; CK(create(f,d,queue,&p)==E_NOTIMPL && !p); d[i]^=1; }
    d[1]^=1u<<16; CK(create(f,d,queue,&p)==E_NOTIMPL && !p); d[1]^=1u<<16;
    CK(create(f,d,file,&p)==E_NOINTERFACE && !p);
    CK(method<Make>(f,0x18)(f,d,queue,&p)==E_NOTIMPL && !p);
    CK(create(f,d,queue,&q)==S_OK && q);
    CK(((IUnknown *)q)->QueryInterface(queue,&p)==S_OK && p==q); CK(((IUnknown *)p)->Release()==1);
    CK(((IUnknown *)q)->QueryInterface(status,&p)==E_NOINTERFACE && !p);
    CK(method<Status>(f,0x28)(f,5,nullptr,status,&s)==S_OK);
    WCHAR dir[MAX_PATH],path[MAX_PATH]; CK(GetTempPathW(MAX_PATH,dir) && GetTempFileNameW(dir,L"qio",0,path));
    HANDLE h=CreateFileW(path,GENERIC_WRITE,0,nullptr,TRUNCATE_EXISTING,0,nullptr);CK(h!=INVALID_HANDLE_VALUE);
    BYTE pattern[128]; for (unsigned i=0;i<128;i++) pattern[i]=(BYTE)(i*37+13);
    DWORD n; CK(WriteFile(h,pattern,sizeof pattern,&n,nullptr)&&n==sizeof pattern&&CloseHandle(h));
    void *source=nullptr;CK(method<Make>(f,0x20)(f,path,file,&source)==S_OK);
    BYTE dst[80];memset(dst,0xda,sizeof dst);
    UINT64 r[9]={0,(UINT_PTR)(dst+8),0,32,(UINT_PTR)source,33,32,0,0};
    auto request=method<Request>(q,0x18); auto mark=method<Mark>(q,0x20); auto submit=method<Call>(q,0x30);
    auto result=method<Result>(s,0x20);auto complete=method<Complete>(s,0x18);
    request(q,r); mark(q,s,0);
    r[1]=(UINT_PTR)(dst+40);
    r[0]=2; request(q,r); // unsupported options, followed by a distinct invalid length
    r[0]=0; r[3]=31; request(q,r); r[3]=32; mark(q,s,1);
    r[5]=100;request(q,r); mark(q,s,2); // past EOF
    r[5]=1; r[4]=1;request(q,r); mark(q,s,3); // foreign file, never invoke it
    r[4]=(UINT_PTR)source; r[5]=3; request(q,r); mark(q,s,4);
    memset(r,0,sizeof r); // no request pointer retained, file may be closed before submission
    method<Call>(source,0x18)(source); CK(((IUnknown*)source)->Release()==0);
    for (UINT i=0;i<5;i++) CK(!complete(s,i) && result(s,i)==E_PENDING);
    for (BYTE b:dst) CK(b==0xda);
    submit(q);
    ULONGLONG deadline=GetTickCount64()+10000;
    while (!complete(s,4) && GetTickCount64()<deadline) Sleep(1);
    CK(result(s,0)==S_OK && result(s,1)==E_NOTIMPL && result(s,2)==HRESULT_FROM_WIN32(ERROR_HANDLE_EOF) &&
        result(s,3)==E_NOINTERFACE && result(s,4)==S_OK);
    CK(!memcmp(dst+8,pattern+33,32) && !memcmp(dst+40,pattern+3,32));
    for (unsigned i=0;i<8;i++) CK(dst[i]==0xda && dst[72+i]==0xda);
    CK(DeleteFileW(path)); // all independent handles released
    puts("PASS offset file reads, snapshot, file Close/Release before Submit, guards, first error and status reset");

    for (unsigned i=0;i<0x103f;i++) request(q,nullptr);
    Blocked args={CreateEventW(nullptr,TRUE,FALSE,nullptr)};CK(args.entered);
    HANDLE thread=CreateThread(nullptr,0,blocked,&args,0,nullptr);CK(thread);
    CK(WaitForSingleObject(args.entered,5000)==WAIT_OBJECT_0 && WaitForSingleObject(thread,50)==WAIT_TIMEOUT);
    submit(q); CK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
    CK(!complete(s,1)); // enqueue behind the captured submission boundary
    CK(((IUnknown*)q)->Release()==0); // final release drains the remaining marker and joins
    CK(result(s,1)==E_POINTER);
    using Stats=void(WINAPI*)(void *,void *);
    alignas(8) DWORD stats[64]={};
    method<Stats>(f,0x48)(f,stats);
    CK(!stats[36] && stats[37]==1 && stats[38]==1 && stats[39]==6 && !stats[41] && stats[42]==0x103f+6);
    CK(!stats[61] && stats[62]>0 && stats[63]==0x103f+4);
    CK(CloseHandle(thread)&&CloseHandle(args.entered));
    CK(((IUnknown*)s)->Release()==0);test_zlib(f);((IUnknown*)f)->Release();
    puts("PASS exact capacity stalls enqueue; explicit Submit wakes producer; final release drains and joins");
}
