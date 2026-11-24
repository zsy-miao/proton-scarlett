/* The same two marker banks use native GPU writes or ordered CPU aliases. */
#include "queue.c"
#define CK(x) do { if (!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;} } while(0)
#define HR(x) do { HRESULT h_=(x);if(FAILED(h_)){fprintf(stderr,"FAIL %d: %s hr=%lx\n",__LINE__,#x,h_);return 1;} } while(0)
extern HRESULT WINAPI D3D12XboxCreateDevice(void*,const void*,REFIID,void**);
static int check(UINT offset,UINT value,BOOL native)
{
    const UINT bytes=65536,post=0x29a5ee15,span=0x49400000,head=value?0x419aa38:0x4196698;
    const UINT64 target=0x8000000100000001ull,low=target-(1ull<<32);
    UINT params[20]={0x20004,0,0x100000};params[10]=1;params[15]=0x20;
    IID boot={0x5405c344,0xd457,0x444e,{0xb4,0xdd,0x23,0x66,0xe4,0x5a,0xee,0x39}};
    IUnknown *owner,*pair[2],*a,*b,*list,*q,*gate,*done;ID3D12Device *d;
    HR(D3D12XboxCreateDevice(NULL,params,&boot,(void**)&owner));HR(D3D12CreateDevice(NULL,D3D_FEATURE_LEVEL_12_0,&IID_ID3D12Device,(void**)&d));
    HMODULE xm=LoadLibraryW(L"xmem.dll");void *(WINAPI *alloc)(void*,SIZE_T,DWORD,UINT64,DWORD)=(void*)GetProcAddress(xm,"XMemVirtualAlloc");CK(alloc);
    typedef HRESULT(WINAPI *make_heap)(IUnknown*,const void*,REFIID,void**);
    make_heap make=((make_heap*)((void***)owner)[0])[0x70/8];
    UINT shapes[][4]={{0,0x130000,1,1},{1,128,1,1}};
    for(UINT j=0;j<2;j++)HR(make(owner,shapes[j],&IID_ID3D12DescriptorHeap,(void**)&pair[j]));
    BYTE *page=NULL;struct scarlett_section_info id,alias;
    if(native){page=alloc(NULL,bytes,0x20401000,0x283,0x42404);CK(page);HR(scarlett_query_section(0x5343000a,page,4,&id));HR(scarlett_query_section_handle(0x53430008,NULL,bytes,&alias,id.section));CloseHandle(id.section);}
    else {HRESULT(WINAPI *cpu)(ID3D12Device*,SIZE_T,void**)=(void*)GetProcAddress(GetModuleHandleW(L"d3d12core.dll"),"ScarlettCpuControlViewV1");CK(cpu);HR(cpu(d,bytes,(void**)&page));HR(scarlett_query_section(0x53430120,page+offset,4,&alias));}
    CK(alias.base!=page && alias.view_size==bytes);memset(page,0xa7,bytes);
    BYTE *sys=alloc(NULL,span,0x20003000,0x183,0x22004);CK(sys);UINT64 addr=(UINT_PTR)(page+offset),ts=(UINT_PTR)(sys+head);
    if(value){
        /* Relocate the owner and its qword array inside a real pool; only slot 1 belongs to this caller. */
        UINT_PTR records[4]={0};DWORD *r=(DWORD*)(sys+0x1000);records[2]=(UINT_PTR)r;
        struct scarlett_section_info pool={.base=sys,.view_size=span};r[2]=0x800;r[3]=8;r[14]=0x20001;
        for(UINT move=0;move<2;move++){
            UINT64 array=(UINT_PTR)(sys+0x419aa30+move*0x480),used=array+0x4010;
            memcpy(r+18,&used,8);memcpy(r+20,&array,8);memcpy(r+22,&array,8);
            CK(indexed_stamp(array+8,&pool,2,1,records) && !indexed_stamp(array,&pool,2,1,records) && !indexed_stamp(array+8,&pool,1,1,records));
            CK(!indexed_stamp(array+16,&pool,2,2,records));r[3]^=1;CK(!indexed_stamp(array+8,&pool,2,1,records));r[3]^=1;
        }
    }
    volatile UINT64 *tick=(volatile UINT64*)(sys+head);*tick=0;MemoryBarrier();
    HR(xs_allocator(d,0,&a));HR(xs_allocator(d,0,&b));HR(xs_device_list(d,0,a,TRUE,owner,&list));HR(xs_direct_queue(d,&q));HR(xs_shared_fence(d,&gate));HR(xs_shared_fence(d,&done));struct startup_list *s=sl(list);
    for(UINT pass=0;pass<2;pass++){
        HR(xs_bind_pool_heaps(list,2,pair));if(native)CK(FAILED(prefix_write(s,addr,value^1,0,0)));
        pool_vtbl.marker(list,addr,value,0,0);CK(s->marker && FAILED(prefix_write(s,addr,value,0,0)));
        HR(xs_bind_pool_heaps(list,2,pair));if(native && value)CK(FAILED(pool_timestamp(s,ts+8,0,0,1)));
        pool_vtbl.timestamp(list,ts,0,0,1);
        HRESULT (*write_post)(struct startup_list*,UINT64,UINT,UINT64,UINT)=native?record_native_post:split_after_timestamp;
        CK(FAILED(write_post(s,addr+8,post,0,0)) && FAILED(write_post(s,addr+4,post,1,0)));
        if(value)CK(FAILED(write_post(s,addr+4,post^1,0,0)));
        pool_vtbl.marker(list,addr+4,post,0,0);CK(FAILED(write_post(s,addr+4,post,0,0)));
        CK(FAILED(record_counter_wait(s,addr,7,target,1)) && FAILED(record_counter_wait(s,(UINT_PTR)page,6,target,1)) &&
                FAILED(record_counter_wait(s,(UINT_PTR)page,7,target,0)));
        __atomic_store_n((UINT64*)alias.base,target,__ATOMIC_RELEASE);
        pool_vtbl.wait64(list,(UINT_PTR)page,7,target,1);HR(sl_close(list));
        if(!pass)HR(sl_reset(list,b,NULL,0));
    }
    __atomic_store_n((UINT64*)alias.base,low,__ATOMIC_RELEASE);
    HR(xs_queue_wait(q,gate,1));startup_submit(q,1,&list,0,0);HR(queue_signal(q,done,1));HR(sl_reset(list,a,NULL,0));IUnknown_Release(list);
    CK(VirtualFree(page,bytes,MEM_RELEASE));CK(VirtualAlloc(page,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==page);memset(page,0xbd,bytes);
    HANDLE end=CreateEventW(NULL,FALSE,FALSE,NULL);CK(end);HR(xs_fence_event(done,1,end));CK(WaitForSingleObject(end,50)==WAIT_TIMEOUT);
    volatile UINT *orig=alias.base;for(UINT j=2;j<bytes/4;j++)CK(orig[j]==0xa7a7a7a7);CK(!*tick);
    HR(xs_cpu_signal(gate,1));ULONGLONG until=GetTickCount64()+10000;
    while(orig[offset/4+1]!=post && GetTickCount64()<until)Sleep(1);
    CK(*tick && orig[offset/4]==value && orig[offset/4+1]==post && WaitForSingleObject(end,150)==WAIT_TIMEOUT);
    // Unsigned compare: low has the same low DWORD; the high-bit-clear value is also insufficient.
    __atomic_store_n((UINT64*)alias.base,0x7fffffffffffffffull,__ATOMIC_RELEASE);CK(WaitForSingleObject(end,150)==WAIT_TIMEOUT);
    __atomic_store_n((UINT64*)alias.base,target+value,__ATOMIC_RELEASE);
    CK(WaitForSingleObject(end,10000)==WAIT_OBJECT_0);HR(ID3D12Device_GetDeviceRemovedReason(d));
    CK(__atomic_load_n((UINT64*)orig,__ATOMIC_ACQUIRE)==target+value);
    for(UINT j=0;j<bytes/4;j++){if(j>=2)CK(orig[j]==(4*j==offset?value:4*j==offset+4?post:0xa7a7a7a7));CK(((UINT*)page)[j]==0xbdbdbdbd);}
    CK(VirtualFree(alias.base,bytes,MEM_RELEASE) && VirtualFree(page,0,MEM_RELEASE) && VirtualFree(sys,0,MEM_RELEASE));CloseHandle(end);
    IUnknown_Release(gate);IUnknown_Release(done);IUnknown_Release(q);IUnknown_Release(a);IUnknown_Release(b);IUnknown_Release(pair[0]);IUnknown_Release(pair[1]);IUnknown_Release(owner);ID3D12Device_Release(d);FreeLibrary(xm);
    printf("PASS %s control +%x=%x/+%x=%x timestamp +%x: original unsigned wait held suffix fence after reset/reuse, released at target+%u\n",native?"native":"alias",offset,value,offset+4,post,head,value);return 0;
}
int main(void){ID3D12Device *d;HR(D3D12CreateDevice(NULL,D3D_FEATURE_LEVEL_12_0,&IID_ID3D12Device,(void**)&d));BOOL native=!xs_is_rtx3070(d);ID3D12Device_Release(d);return check(8,0,native)||check(0x50,1,native);}
