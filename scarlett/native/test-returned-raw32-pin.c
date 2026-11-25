#include "d3d12_device.c"
/* Same synthetic arena layout as the returned +160 fixture, in the test PE only. */
static BYTE nodes[216*0x58] __attribute__((aligned(8)));
void test_lease(UINT_PTR base,UINT gen,UINT holds,UINT64 stamp)
{
    BYTE *page=(BYTE*)GetModuleHandleW(NULL)+0x75d0000;
    static BOOL allocated;
    if(!allocated && !(allocated=VirtualAlloc(page,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==page))abort();
    *(struct lease_arena*)(page+0xf1b8)=(struct lease_arena){base,(UINT_PTR)nodes,216,0x20000,27ull<<20};
    *(struct lease_node*)(nodes+2*0x58)=(struct lease_node){.data=base+0x40000,.gen=gen,.holds=holds,.stamp=stamp};
}

/* Fixture's host descriptor copy does not retain resources. Pin the registered record it copied. */
IUnknown *test_pin_raw(IUnknown *src,SIZE_T slot,UINT64 serial)
{
    struct desc_heap *h=CONTAINING_RECORD(src,struct desc_heap,iface);
    IUnknown *id=NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    AcquireSRWLockShared(&h->owner->descriptor_lock);
    if(src->lpVtbl==&desc_vtbl.unknown && !h->visible && h->records &&
       SUCCEEDED(descriptor_slot(src,slot,&cpu,NULL))){
        struct descriptor_record *r=&h->records[(slot-h->start)/h->stride];
        UINT64 tag[2];
        if(r->kind==PLACED_RAW32_VIEW && r->serial==serial && r->placed &&
           copy_in(tag,(void*)slot,sizeof tag) && tag[0]==serial && tag[1]==(0x5853444553430000ull|r->kind)){
            id=r->placed;IUnknown_AddRef(id);
        }
    }
    ReleaseSRWLockShared(&h->owner->descriptor_lock);
    return id;
}
