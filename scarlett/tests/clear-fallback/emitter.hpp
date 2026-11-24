/* Test only. Execute the pinned writer under the Windows x64 ABI, with a
 * borrowed checked-out list, a mapped chunk, and an explicit ready PSO. */
#include <bcrypt.h>
#include <cstdint>
#include <vector>
struct Emitter {
    BYTE *image;
    Emitter() {
        FILE *f=fopen("emitter.bin","rb"); CK(f && !fseek(f,0,SEEK_END));
        long size=ftell(f); CK(size>0 && size<65536); rewind(f);
        std::vector<BYTE> data(size); CK(fread(data.data(),1,size,f)==(size_t)size); fclose(f);
        BYTE hash[32]; static const BYTE expected[]={0x01,0x23,0xf6,0x00,0x81,0xe0,0x06,0x08,0x2c,0xae,0xf7,0x4a,0x71,0x4c,0x47,0x9b,0xc8,0x01,0xfd,0xdb,0xbd,0x17,0xdc,0xbe,0xe7,0x08,0xc2,0x0a,0x18,0x75,0x99,0xbf};
        BCRYPT_ALG_HANDLE alg; CK(!BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0));
        CK(!BCryptHash(alg,nullptr,0,data.data(),size,hash,sizeof hash)); BCryptCloseAlgorithmProvider(alg,0);
        CK(!memcmp(hash,expected,32));
        image=(BYTE*)VirtualAlloc(nullptr,0xa000000,MEM_RESERVE,PAGE_NOACCESS); CK(image);
        for(size_t i=0;i<data.size();) {
            UINT a,n; memcpy(&a,&data[i],4);memcpy(&n,&data[i+4],4); i+=8;
            CK(VirtualAlloc(image+a,n,MEM_COMMIT,PAGE_READWRITE));memcpy(image+a,&data[i],n); i+=n;
            DWORD old; CK(VirtualProtect(image+a,n,a<0x6000000?PAGE_EXECUTE_READ:PAGE_READONLY,&old));
        }
        CK(FlushInstructionCache(GetCurrentProcess(),nullptr,0));
        for(UINT g:{0x75d59c0,0x75df1b8,0x85f9bf0,0x7bc65a2,0x9e1dac0,0x75a58c0})
            CK(VirtualAlloc(image+g,8,MEM_COMMIT,PAGE_READWRITE));
    }
    ~Emitter(){VirtualFree(image,0,MEM_RELEASE);}
    void run(void *list,IUnknown *pso,IUnknown *root,SIZE_T slot,BYTE *arena,UINT64 alias,const float *rgba) {
        alignas(32) static BYTE ctx[0x48000]; memset(ctx,0,sizeof ctx);
        alignas(16) BYTE view[0x30]={},req[0x50]={},shaders[0x100]={},chunk[0x58]={},ds[0x80]={};
        auto q=[](BYTE *p,size_t k,UINT64 v){memcpy(p+k,&v,8);};
        auto u=[](BYTE *p,size_t k,UINT v){memcpy(p+k,&v,4);};
        q(image,0x75d59c0,(SIZE_T)shaders);q(image,0x75df1b8,(SIZE_T)arena);q(image,0x75df1c0,(SIZE_T)chunk);
        u(image,0x75df1cc,256);u(chunk,0x20,1);q(image,0x75df138,0x24000000);q(image,0x9e1dac0,0x24000000);
        q(image,0x9e1dac8,256);q(image,0x9e1dad0,alias);q(ctx,0x12810,(SIZE_T)(arena+256));u(ctx,0x12818,256);
        q(ctx,0x127c8,(SIZE_T)list);q(ctx,0x12328,(SIZE_T)shaders);q(ctx,0x12330,(SIZE_T)req);q(ctx,0x12360,UINT64_MAX);
        q(ctx,0x12640,(SIZE_T)view);view[0x13]=0x1c;q(view,0x20,slot);u(ctx,0x1268c,1);
        unsigned state=0x04000000;q(ctx,0x12198,(SIZE_T)&state);q(ctx,0x12520,(1ULL<<22)|(1ULL<<27));q(image,0x75a58d0,(SIZE_T)ds);
        float vp[]={1,2,32,32,0,1};int box[]={3,4,16,16};memcpy(ctx+0x12338,vp,24);memcpy(ctx+0x12350,box,16);
        q(shaders,0x80,(SIZE_T)root);memset(shaders+0x50,0xff,4);shaders[0x59]=1;q(req,0x38,(SIZE_T)pso);
        ((void(*)(void*,UINT,const float*,float))(image+0x122630))(ctx,1,rgba,.25f);
        UINT refs;memcpy(&refs,chunk+0x20,4);CK(refs==1);
    }
};
