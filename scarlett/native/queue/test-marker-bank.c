/* The same two marker banks use native GPU writes or ordered CPU aliases. */
#ifdef TEST_RTX_THIRD_WAIT
#define TEST_RTX_THIRD_POST
#endif
#ifdef TEST_NATIVE_THIRD_WAIT
#define TEST_THIRD_PREFIX
#define TEST_BANK3_WB
static volatile unsigned *post_alias;
static unsigned release_sample;
#define XS_OBSERVE_COUNTER_RELEASE() __atomic_store_n(&release_sample,*post_alias,__ATOMIC_RELEASE)
static int ready_before_submit;
#endif
#include "queue.c"
#include "test-depth-slots.h"
#define CK(x) do { if (!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;} } while(0)
#define HR(x) do { HRESULT h_=(x);if(FAILED(h_)){fprintf(stderr,"FAIL %d: %s hr=%lx\n",__LINE__,#x,h_);return 1;} } while(0)
extern HRESULT WINAPI D3D12XboxCreateDevice(void*,const void*,REFIID,void**);
#ifdef TEST_THIRD_PREFIX
static int third(ID3D12Device *d,IUnknown *owner,IUnknown **heaps,BYTE *page,void *alias)
{
    IUnknown *a,*fresh,*list,*q,*gate,*end;
    HR(xs_allocator(d,0,&a));HR(xs_allocator(d,0,&fresh));HR(xs_direct_queue(d,&q));
    HR(xs_shared_fence(d,&gate));HR(xs_shared_fence(d,&end));HR(xs_device_list(d,0,a,TRUE,owner,&list));
    struct startup_list *s=sl(list);UINT_PTR addr=(UINT_PTR)page+0x98;
    HR(xs_bind_pool_heaps(list,2,heaps));pair_op(s,0x238);
    CK(FAILED(prefix_write(s,addr,1,0,0)) && FAILED(prefix_write(s,addr+4,2,0,0)) &&
        FAILED(prefix_write(s,addr,2,1,0)) && FAILED(prefix_write(s,addr,2,0,1)) && !s->marker);
    s->alloc_generation++;CK(FAILED(prefix_write(s,addr,2,0,0)));s->alloc_generation--;
    s->phase=B_REBOUND;CK(FAILED(prefix_write(s,addr,2,0,0)));s->phase=B_BOUND;
    s->nops--;pool_vtbl.marker(list,addr,2,0,0);
#ifndef TEST_BANK3_WB
    CK(s->marker && s->marker->value==2 && s->marker->control_section && s->marker->buffer && !s->marker->alias &&
        s->phase==B_PREFIX && FAILED(pool_timestamp(s,addr,0,0,1)) && !s->stamp);
#else /* TEST_BANK3_WB */
    CK(s->marker && s->marker->value==2 && s->marker->control_section && s->marker->buffer && !s->marker->alias);

    const SIZE_T span=0x49400000,win=0x4190000,off=0xf258;
    BYTE *exe=(BYTE*)GetModuleHandleW(NULL),*bp=exe+0xa940000,*ip=exe+0x75d0000;
    CK(VirtualAlloc(bp,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==bp &&
       VirtualAlloc(ip,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==ip);
    void *(WINAPI *alloc)(void*,SIZE_T,DWORD,UINT64,DWORD)=(void*)GetProcAddress(GetModuleHandleW(L"xmem.dll"),"XMemVirtualAlloc");
    CK(alloc);BYTE *sys=alloc(NULL,span,0x20003000,0x183,0x22004);CK(sys);
    UINT64 target=(UINT_PTR)(sys+win+off),array=target-8,used=array+0x4010;
    DWORD *rec=(DWORD*)(sys+0x3fb30c0),*bank=(DWORD*)(exe+0xa943a0c),*index=(DWORD*)(exe+0x75dedb0);
    UINT_PTR *slots=(UINT_PTR*)(exe+0xa943a18);
    slots[2]=slots[3]=(UINT_PTR)rec;*bank=3;*index=1;
    rec[2]=2048;rec[3]=8;rec[14]=2<<16;
    memcpy(rec+18,&used,8);memcpy(rec+20,&array,8);memcpy(rec+22,&array,8);
    struct scarlett_section_info dst,view;
    HR(scarlett_query_section(0x5343000c,(void*)target,8,&dst));
    HR(scarlett_query_section_handle(0x5343000b,NULL,span,&view,dst.section));
    CK(dst.base==sys && dst.offset==win+off && view.base!=sys && view.view_size==span);
    BYTE *window=(BYTE*)view.base+win;memset(window,0xad,65536);
    volatile UINT64 *tick=(volatile UINT64*)(window+off);
    CK(s->phase==B_PREFIX && FAILED(pool_timestamp(s,target,0,0,1)) && !s->stamp);
    HR(xs_bind_pool_heaps(list,2,heaps));CK(s->phase==B_REBOUND);
    struct scarlett_section_info wrong=dst;
    wrong.section=s->marker->control_section;CK(!third_wb_target(target,&wrong));
    wrong=dst;wrong.offset-=8;CK(!third_wb_target(target,&wrong));
    wrong=dst;wrong.view_size=wrong.offset+7;CK(!third_wb_target(target,&wrong));CloseHandle(dst.section);
    pair_op(s,0x240);
    *index=2;CK(FAILED(pool_timestamp(s,target,0,0,1)));*index=1;
    *bank=2;CK(FAILED(pool_timestamp(s,target,0,0,1)));*bank=3;
    /* A well-formed record at a different offset must not substitute for the measured owner. */
    memcpy(rec+32,rec,96);slots[3]=(UINT_PTR)(rec+32);
    CK(FAILED(pool_timestamp(s,target,0,0,1)));slots[3]=(UINT_PTR)rec;
    for(UINT j=0;j<6;j++){
        DWORD *field=rec+((UINT[]){2,3,14,18,20,22})[j];DWORD mask=j==2?1u<<16:1u;
        *field^=mask;CK(FAILED(pool_timestamp(s,target,0,0,1)));*field^=mask;
    }
    CK(FAILED(pool_timestamp(s,target-8,0,0,1)) && FAILED(pool_timestamp(s,target+8,0,0,1)) &&
       FAILED(pool_timestamp(s,(UINT_PTR)(sys+span),0,0,1)) && !s->stamp);
    HR(pool_timestamp(s,target,0,0,1));
    CK(s->stamp && s->stamp->retained && s->stamp->query && s->stamp->dst &&
       !s->stamp->second_bank && !s->stamp->successor_bank && FAILED(pool_timestamp(s,target,0,0,1)));
    batch_step(s,B_HEAD_STAMP);CK(s->phase==B_HEAD_STAMP);
#ifdef TEST_NATIVE_THIRD_WAIT
    pair_op(s,0x238);
    CK(FAILED(record_native_post(s,addr,0x29a5ee15,0,0)) && FAILED(record_native_post(s,addr+4,2,0,0)));
    CK(FAILED(record_native_post(s,addr+4,0x29a5ee15,1,0)) && !s->marker->native_post);
    const UINT64 ref=0x8000000100000000ull|(UINT)GetTickCount64(),below=ref-(1ull<<32);
    UINT64 cell=(UINT_PTR)page;
    CK(FAILED(record_counter_wait(s,cell,7,ref,1)) && !s->head && !s->post);
    s->alloc_generation++;CK(FAILED(native_post_at(s,addr+4,0x29a5ee15,0,0,0x4ed46aa)));s->alloc_generation--;
    CK(FAILED(native_post_at(s,addr+4,0x29a5ee15,0,0,0x4ed46ab)) && !s->marker->native_post);
    BYTE *foreign=alloc(NULL,65536,0x20401000,0x283,0x42404);CK(foreign && foreign!=page);
    CK(FAILED(native_post_at(s,(UINT_PTR)foreign+0x9c,0x29a5ee15,0,0,0x4ed46aa)));
    HR(native_post_at(s,addr+4,0x29a5ee15,0,0,0x4ed46aa));
    CK(FAILED(record_native_post(s,addr+4,0x29a5ee15,0,0)));batch_step(s,B_POST);
    pair_op(s,0x250);
    CK(record_counter_wait(s,(UINT_PTR)foreign,7,ref,1)==E_INVALIDARG && VirtualFree(foreign,65536,MEM_RELEASE));
    CK(FAILED(record_counter_wait(s,cell+8,7,ref,1)) && FAILED(record_counter_wait(s,cell+1,7,ref,1)) &&
        FAILED(record_counter_wait(s,cell,6,ref,1)) && FAILED(record_counter_wait(s,cell,7,ref,0)));
    s->ops[4]^=1;CK(FAILED(record_counter_wait(s,cell,7,ref,1)));s->ops[4]^=1;
    s->phase=B_HEAD_STAMP;CK(FAILED(record_counter_wait(s,cell,7,ref,1)));s->phase=B_POST;
    s->alloc_generation++;CK(FAILED(record_counter_wait(s,cell,7,ref,1)));s->alloc_generation--;
    s->stamp->third_wb=FALSE;CK(FAILED(record_counter_wait(s,cell,7,ref,1)));s->stamp->third_wb=TRUE;
    CK(!s->head && !s->post);
    s->nops--;CK(FAILED(wait64_at(s,cell,7,ref,1,0x2484b11)) && s->nops==5 && !s->post);
    HR(wait64_at(s,cell,7,ref,1,0x2484b12));
    CK(s->phase==B_WAIT && s->split_at==6 && s->head && s->host!=s->head && !s->post_boundary &&
       s->post && s->post->parent==s->marker && !s->post->word && s->post->counter && s->post->threshold==ref &&
       FAILED(record_counter_wait(s,cell,7,ref,1)));
    post_alias=(volatile UINT*)alias+0x9c/4;
    __atomic_store_n(&release_sample,0,__ATOMIC_RELEASE);
    __atomic_store_n((UINT64*)alias,ready_before_submit?ref+1:below,__ATOMIC_RELEASE);
#endif
#endif /* TEST_BANK3_WB */
    HR(sl_close(list));HR(xs_queue_wait(q,gate,1));HR(checked_startup_submit(q,1,&list,0,0));HR(queue_signal(q,end,1));
    HR(sl_reset(list,fresh,NULL,0));IUnknown_Release(list);
    HANDLE done=CreateEventW(NULL,FALSE,FALSE,NULL);CK(done);HR(xs_fence_event(end,1,done));
    CK(VirtualFree(page,65536,MEM_RELEASE) && VirtualAlloc(page,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==page);
    memset(page,0xbd,65536);MemoryBarrier();
#ifndef TEST_BANK3_WB
    CK(WaitForSingleObject(done,50)==WAIT_TIMEOUT && ((volatile UINT*)alias)[0x98/4]==0xa7a7a7a7);
#else /* TEST_BANK3_WB */
    CK(VirtualFree(sys,0,MEM_RELEASE));
    BYTE *reuse=VirtualAlloc(sys+win,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);CK(reuse==sys+win);
    memset(reuse,0x49,65536);MemoryBarrier();
    CK(WaitForSingleObject(done,50)==WAIT_TIMEOUT && ((volatile UINT*)alias)[0x98/4]==0xa7a7a7a7 &&
       *tick==0xadadadadadadadadull);
    UINT64 low,high,cpu;HR(ID3D12CommandQueue_GetClockCalibration(as_queue(q),&low,&cpu));
#endif /* TEST_BANK3_WB */
#ifdef TEST_NATIVE_THIRD_WAIT
    CK(*post_alias==0xa7a7a7a7 && !__atomic_load_n(&release_sample,__ATOMIC_ACQUIRE));
    HR(xs_cpu_signal(gate,1));
    if (!ready_before_submit) {
        ULONGLONG until=GetTickCount64()+10000;
        while(*post_alias!=0x29a5ee15 && GetTickCount64()<until)Sleep(1);
        CK(*post_alias==0x29a5ee15 && ((volatile UINT*)alias)[0x98/4]==2 && *tick!=0xadadadadadadadadull);
        CK(WaitForSingleObject(done,150)==WAIT_TIMEOUT && !__atomic_load_n(&release_sample,__ATOMIC_ACQUIRE));
        __atomic_store_n((UINT64*)alias,0x7fffffffffffffffull,__ATOMIC_RELEASE);
        CK(WaitForSingleObject(done,150)==WAIT_TIMEOUT && !__atomic_load_n(&release_sample,__ATOMIC_ACQUIRE));
        __atomic_store_n((UINT64*)alias,ref,__ATOMIC_RELEASE);
    }
    CK(WaitForSingleObject(done,10000)==WAIT_OBJECT_0 && __atomic_load_n(&release_sample,__ATOMIC_ACQUIRE)==0x29a5ee15);
    HR(ID3D12Device_GetDeviceRemovedReason(d));
#else
    HR(xs_cpu_signal(gate,1));CK(WaitForSingleObject(done,10000)==WAIT_OBJECT_0);HR(ID3D12Device_GetDeviceRemovedReason(d));
#endif
#ifdef TEST_BANK3_WB
    HR(ID3D12CommandQueue_GetClockCalibration(as_queue(q),&high,&cpu));
    CK(*tick && *tick!=0xadadadadadadadadull && *tick>=low && *tick<=high);
    for(UINT j=0;j<65536;j++)CK(reuse[j]==0x49 && ((j>=off && j<off+8) || window[j]==0xad));
    CK(VirtualFree(reuse,0,MEM_RELEASE) && VirtualFree(view.base,0,MEM_RELEASE) &&
       VirtualFree(bp,0,MEM_RELEASE) && VirtualFree(ip,0,MEM_RELEASE));
#endif /* TEST_BANK3_WB */
#ifdef TEST_NATIVE_THIRD_WAIT
    CK(__atomic_load_n((UINT64*)alias,__ATOMIC_ACQUIRE)==ref+(UINT64)ready_before_submit);
    for(UINT j=0;j<65536/4;j++)CK((j<2 || ((volatile UINT*)alias)[j]==(j==0x98/4?2:j==0x9c/4?0x29a5ee15u:0xa7a7a7a7u)) && ((UINT*)page)[j]==0xbdbdbdbd);
#else
    for(UINT j=0;j<65536/4;j++)CK(((volatile UINT*)alias)[j]==(j==0x98/4?2:0xa7a7a7a7) && ((UINT*)page)[j]==0xbdbdbdbd);
#endif
    CloseHandle(done);IUnknown_Release(end);IUnknown_Release(gate);IUnknown_Release(q);IUnknown_Release(a);IUnknown_Release(fresh);
    CK(VirtualFree(alias,65536,MEM_RELEASE) && VirtualFree(page,0,MEM_RELEASE));
#ifndef TEST_BANK3_WB
    puts("PASS third native +98=2: exact original dword after gate; replacement and neighbors intact; wrong phase/gen/offset/value/control/flags and timestamp refused");
#elif !defined(TEST_NATIVE_THIRD_WAIT)
    puts("PASS third native +98=2 and bank3/index1 +419f258: original WB qword in queue clock interval, replacements and neighbors intact");
#endif /* TEST_BANK3_WB */
    return 0;
}
#elif defined(TEST_RTX_THIRD_POST)
#include "test-rtx-third-post.h"
#else /* Preserve the original fixture's embedded diagnostic IDs. */
#line 6
#endif
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
#ifdef TEST_RTX_THIRD_POST
    if(value==2){
        CK(!native);
        BYTE *sys=alloc(NULL,span,0x20003000,0x183,0x22004);CK(sys);
        int rc=third_post(d,owner,pair,page,alias.base,sys+0x4196698);
        CK(VirtualFree(sys,0,MEM_RELEASE));IUnknown_Release(pair[0]);IUnknown_Release(pair[1]);
        IUnknown_Release(owner);ID3D12Device_Release(d);FreeLibrary(xm);return rc;
    }
#elif defined(TEST_THIRD_PREFIX)
    if(value==2){
        CK(native);
        int rc=third(d,owner,pair,page,alias.base);
        IUnknown_Release(pair[0]);IUnknown_Release(pair[1]);IUnknown_Release(owner);ID3D12Device_Release(d);FreeLibrary(xm);return rc;
    }
#else
#line 23
#endif
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
    const UINT64 range=0xffffffffff00ull;
    D3D12_HEAP_PROPERTIES hp={.Type=D3D12_HEAP_TYPE_READBACK,.CreationNodeMask=1,.VisibleNodeMask=1};
    D3D12_RESOURCE_DESC rd={.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER,.Width=16,.Height=1,.DepthOrArraySize=1,.MipLevels=1,.SampleDesc={1,0},.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    ID3D12Resource *read=NULL,*upload=NULL;UINT *result=NULL;
    if(value){
        HR(ID3D12Device_CreateCommittedResource(d,&hp,0,&rd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&read));
        HR(ID3D12Resource_Map(read,0,NULL,(void**)&result));memset(result,0xad,16);
        hp.Type=D3D12_HEAP_TYPE_UPLOAD;
        HR(ID3D12Device_CreateCommittedResource(d,&hp,0,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,NULL,&IID_ID3D12Resource,(void**)&upload));
        UINT *data;HR(ID3D12Resource_Map(upload,0,NULL,(void**)&data));
        for(UINT j=0;j<4;j++) data[j]=j+1;
        ID3D12Resource_Unmap(upload,0,NULL);
    }
    HR(xs_allocator(d,0,&a));HR(xs_allocator(d,0,&b));HR(xs_device_list(d,0,a,TRUE,owner,&list));HR(xs_direct_queue(d,&q));HR(xs_shared_fence(d,&gate));HR(xs_shared_fence(d,&done));struct startup_list *s=sl(list);
    CK(guarded_depth_slots(list));
    for(UINT pass=0;pass<2;pass++){
        CK(!s->depth_zero_pending && record_depth_order(s,1)==E_NOTIMPL);
        HR(xs_bind_pool_heaps(list,2,pair));if(native)CK(FAILED(prefix_write(s,addr,value^1,0,0)));
        pool_vtbl.marker(list,addr,value,0,0);CK(s->marker && FAILED(prefix_write(s,addr,value,0,0)));
        HR(xs_bind_pool_heaps(list,2,pair));if(native && value)CK(FAILED(pool_timestamp(s,ts+8,0,0,1)));
        pool_vtbl.timestamp(list,ts,0,0,1);
        pool_vtbl.depth_order(list,0);
        CK(unbind_index(s,(const D3D12_INDEX_BUFFER_VIEW *)1)==E_NOTIMPL);pool_vtbl.index(list,NULL);CK(s->depth_zero_pending && !xs_list_host(list));
        HRESULT (*write_post)(struct startup_list*,UINT64,UINT,UINT64,UINT)=native?record_native_post:split_after_timestamp;
        CK(FAILED(write_post(s,addr+8,post,0,0)) && FAILED(write_post(s,addr+4,post,1,0)));
        if(value)CK(FAILED(write_post(s,addr+4,post^1,0,0)));
        pool_vtbl.marker(list,addr+4,post,0,0);CK(FAILED(write_post(s,addr+4,post,0,0)));
        CK(FAILED(record_counter_wait(s,addr,7,target,1)) && FAILED(record_counter_wait(s,(UINT_PTR)page,6,target,1)) &&
                FAILED(record_counter_wait(s,(UINT_PTR)page,7,target,0)));
        __atomic_store_n((UINT64*)alias.base,target,__ATOMIC_RELEASE);
        CK(!atlas_sync_valid(s,0xc280,0x20000,0,range));
        pool_vtbl.wait64(list,(UINT_PTR)page,7,target,1);
        CK(s->head && s->depth_zero_pending && !xs_list_host(list));
        if(value){
            CK(atlas_sync_valid(s,0xc280,0x20000,0,range) && !atlas_sync_valid(s,0xc3c0,0x20000,0,range) &&
                !atlas_sync_valid(s,0xc280,0x80020000,0,range) && !atlas_sync_valid(s,0xc280,0x20000,4,range) &&
                !atlas_sync_valid(s,0xc280,0x20000,0,range-1));
            ID3D12GraphicsCommandList7_CopyBufferRegion(s->host,read,0,upload,0,16);
            pool_vtbl.sync418(list,0xc280,0x20000,0,range);
            ID3D12GraphicsCommandList7_CopyBufferRegion(s->host,read,4,upload,12,4);
        }else CK(!atlas_sync_valid(s,0xc280,0x20000,0,range));
        HR(sl_close(list));CK(!atlas_sync_valid(s,0xc280,0x20000,0,range));
        CK(FAILED(unbind_index(s,NULL)) && FAILED(record_depth_order(s,0)) && FAILED(sl_reset(list,b,NULL,1)) && s->depth_zero_pending);
        if(!pass)HR(sl_reset(list,b,NULL,0));
    }
    __atomic_store_n((UINT64*)alias.base,low,__ATOMIC_RELEASE);
    HR(xs_queue_wait(q,gate,1));startup_submit(q,1,&list,0,0);HR(queue_signal(q,done,1));HR(sl_reset(list,a,NULL,0));CK(!s->depth_zero_pending && xs_list_host(list) && FAILED(record_depth_order(s,0)));
    HR(sl_close(list));HR(sl_reset(list,b,NULL,0));CK(FAILED(record_depth_order(s,0)));IUnknown_Release(list);
    CK(VirtualFree(page,bytes,MEM_RELEASE));CK(VirtualAlloc(page,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==page);memset(page,0xbd,bytes);
    HANDLE end=CreateEventW(NULL,FALSE,FALSE,NULL);CK(end);HR(xs_fence_event(done,1,end));CK(WaitForSingleObject(end,50)==WAIT_TIMEOUT);
    volatile UINT *orig=alias.base;for(UINT j=2;j<bytes/4;j++)CK(orig[j]==0xa7a7a7a7);CK(!*tick);
    HR(xs_cpu_signal(gate,1));ULONGLONG until=GetTickCount64()+10000;
    while(orig[offset/4+1]!=post && GetTickCount64()<until)Sleep(1);
    CK(*tick && orig[offset/4]==value && orig[offset/4+1]==post && WaitForSingleObject(end,150)==WAIT_TIMEOUT);
    // Unsigned compare: low has the same low DWORD; the high-bit-clear value is also insufficient.
    __atomic_store_n((UINT64*)alias.base,0x7fffffffffffffffull,__ATOMIC_RELEASE);CK(WaitForSingleObject(end,150)==WAIT_TIMEOUT);
    if(value)for(UINT j=0;j<4;j++)CK(result[j]==0xadadadad);
    __atomic_store_n((UINT64*)alias.base,target+value,__ATOMIC_RELEASE);
    CK(WaitForSingleObject(end,10000)==WAIT_OBJECT_0);HR(ID3D12Device_GetDeviceRemovedReason(d));
    CK(__atomic_load_n((UINT64*)orig,__ATOMIC_ACQUIRE)==target+value);
    if(value){
        CK(result[0]==1 && result[1]==4 && result[2]==3 && result[3]==4);
        ID3D12Resource_Unmap(read,0,NULL);ID3D12Resource_Release(upload);ID3D12Resource_Release(read);
        puts("PASS pooled Direct +418: ordered overlapping GPU copies after original wait, suffix fence and guards");
    }
    for(UINT j=0;j<bytes/4;j++){if(j>=2)CK(orig[j]==(4*j==offset?value:4*j==offset+4?post:0xa7a7a7a7));CK(((UINT*)page)[j]==0xbdbdbdbd);}
    CK(VirtualFree(alias.base,bytes,MEM_RELEASE) && VirtualFree(page,0,MEM_RELEASE) && VirtualFree(sys,0,MEM_RELEASE));CloseHandle(end);
    IUnknown_Release(gate);IUnknown_Release(done);IUnknown_Release(q);IUnknown_Release(a);IUnknown_Release(b);IUnknown_Release(pair[0]);IUnknown_Release(pair[1]);IUnknown_Release(owner);ID3D12Device_Release(d);FreeLibrary(xm);
    printf("PASS %s control +%x=%x/+%x=%x timestamp +%x: original unsigned wait held suffix fence after reset/reuse, released at target+%u\n",native?"native":"alias",offset,value,offset+4,post,head,value);return 0;
}
int main(void){ID3D12Device *d;HR(D3D12CreateDevice(NULL,D3D_FEATURE_LEVEL_12_0,&IID_ID3D12Device,(void**)&d));BOOL native=!xs_is_rtx3070(d);ID3D12Device_Release(d);
#ifdef TEST_RTX_THIRD_POST
    CK(!native);return check(0x98,2,FALSE);
#elif defined(TEST_NATIVE_THIRD_WAIT)
    CK(native);CK(!check(0x98,2,TRUE));ready_before_submit=1;CK(!check(0x98,2,TRUE));
    puts("PASS guarded RADV third native +250: held and pre-satisfied unsigned waits, +9c visible before counter release, VA reuse intact");
    return 0;
#elif defined(TEST_THIRD_PREFIX)
    CK(native);return check(0x98,2,TRUE);
#else
    return check(8,0,native)||check(0x50,1,native);
#endif
}
