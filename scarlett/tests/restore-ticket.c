/* Actual snapshot/consume predicate; stand in for the two locked descriptor lookups. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t UINT_PTR,SIZE_T;typedef uint64_t UINT64;typedef unsigned UINT;
typedef int HRESULT,ID3D12Resource,ID3D12CommandQueue,D3D12_CPU_DESCRIPTOR_HANDLE;
typedef struct {void *lpVtbl;} IUnknown;
typedef int BOOL,ID3D12Fence; enum {TRUE=1,FALSE=0,DXGI_FORMAT_R8G8B8A8_UNORM=28};
struct xs_attachment_pin {IUnknown *identity;ID3D12Resource *host;SIZE_T slot;UINT64 serial;};
struct xs_sr_ticket {struct xs_attachment_pin rt,ds;ID3D12Fence *end;};
struct color_resource {IUnknown iface;struct xs_attachment_pin sr_ds;int depth4k,depth,small_rt,single_format;void *pair_section,*pair_heap;ID3D12Fence *sr_end;SIZE_T sr_slot;UINT64 sr_serial;struct xbox_device *owner;ID3D12Resource *host;void *slice;int msaa,claimed,flipping,multi_writer;
    ID3D12CommandQueue *writer;UINT64 common_epoch,writer_epoch,common_slot,common_serial;struct color_resource *next;};
struct desc_heap {IUnknown iface;struct desc_heap *next;UINT count;SIZE_T start,stride;
    struct rec {ID3D12Resource *resource;UINT64 serial;UINT_PTR rtv_key;} *records;};
struct xbox_device {IUnknown iface;struct color_resource *colors;struct desc_heap *rtv_heaps,*dsv_heaps;};
static struct {int unknown;} bootstrap_vtbl;
#define CONTAINING_RECORD(ptr,type,field) ((type *)((char*)ptr - offsetof(type,field)))
#define AcquireSRWLockExclusive(...) ((void)0)
#define ReleaseSRWLockExclusive(...) ((void)0)
#define SUCCEEDED(hr) ((hr)>=0)
enum {S_OK=0,E_INVALIDARG=-1,E_ACCESSDENIED=-2,RTV=1,DSV=2};
#define IUnknown_AddRef(p) ((void)(p))
#define IUnknown_Release(p) ((void)(p))
static int descriptor_slot(IUnknown *heap,SIZE_T slot,int *at,void *n){
    struct desc_heap *h=(void*)heap;(void)at;(void)n;return slot>=h->start && slot<h->start+h->count*h->stride?0:-1;
}
static ID3D12Resource *attachment_locked(struct desc_heap *h,SIZE_T slot,int kind,int *at){
    (void)kind;*at=(int)slot;return h->records[(slot-h->start)/h->stride].resource;
}
#define ID3D12Fence_GetCompletedValue(f) ((UINT64)*(f))
#define ID3D12Fence_Release(f) ((void)(f))
#define ID3D12Fence_AddRef(f) ((void)(f))
#define ID3D12CommandQueue_AddRef(q) ((void)(q))
#include "reuse.inc"
int main(void){
    int img,q,foreign,arena;struct xbox_device d={.iface={&bootstrap_vtbl.unknown}};
    struct color_resource r={.owner=&d,.host=&img,.slice=&arena,.claimed=1,.writer=&q,.writer_epoch=5,.common_epoch=5,.common_slot=1024,.common_serial=14};
    struct rec recs[2]={{&img,14,(UINT_PTR)&r.iface}};struct desc_heap h={.count=2,.start=1024,.stride=32,.records=recs};d.rtv_heaps=&h;d.colors=&r;
    UINT64 e=0;
#define T(queue) xs_reuse_common(&d.iface,&r.iface,&img,queue,&e)
    assert(T(NULL)==0 && e==5);assert(T(&foreign)<0);
    r.writer_epoch++;assert(T(NULL)<0 && T(&q)<0);r.writer_epoch--;
    r.flipping=1;assert(T(NULL)<0);r.flipping=0;recs[0].serial++;assert(T(&q)<0);recs[0].serial--;
    recs[1]=recs[0];assert(T(NULL)<0);recs[1].resource=NULL;
    struct color_resource duplicate={.slice=&arena};r.next=&duplicate;assert(T(&q)<0);r.next=NULL;
    int tex, fence=0, other_end=0;struct color_resource rt={.owner=&d,.small_rt=1,.single_format=28,.pair_section=&tex,.host=&tex};
    d.colors=&rt;rt.next=&r;
    int z;struct color_resource ds={.owner=&d,.host=&z,.depth4k=1,.depth=1};r.next=&ds;
    struct rec zrec={&z,121,(UINT_PTR)&ds.iface};struct desc_heap zh={.count=1,.start=2048,.stride=32,.records=&zrec};d.dsv_heaps=&zh;
    recs[1]=(struct rec){&tex,119,(UINT_PTR)&rt.iface};struct xs_sr_ticket t={{&rt.iface,&tex,1056,119},{&ds.iface,&z,2048,121},&fence};
    assert(!sr_live(&d,&rt,&t,&q));assert(xs_sr_submitted(&d.iface,&t.rt,&t.ds,&q,&fence));
    assert(!xs_sr_submitted(&d.iface,&t.rt,&t.ds,&foreign,NULL));
    zrec.serial++;assert(xs_reuse_with_sr(&d.iface,&r.iface,&img,&q,&e,&t)<0);zrec.serial--;
    t.ds.identity=&rt.iface;assert(!sr_live(&d,&rt,&t,&q));t.ds.identity=&ds.iface;
    struct color_resource alias={.host=&z};ds.next=&alias;assert(!sr_live(&d,&rt,&t,&q));ds.next=NULL;
    assert(xs_reuse_with_sr(&d.iface,&r.iface,&img,&foreign,&e,&t)<0);
    t.end=&other_end;assert(xs_reuse_with_sr(&d.iface,&r.iface,&img,&q,&e,&t)<0);t.end=&fence;
    recs[1].serial++;assert(xs_reuse_with_sr(&d.iface,&r.iface,&img,&q,&e,&t)<0);recs[1].serial--;
    recs[1].rtv_key=0;assert(xs_reuse_with_sr(&d.iface,&r.iface,&img,&q,&e,&t)<0);recs[1].rtv_key=(UINT_PTR)&rt.iface;
    fence=-1;assert(xs_reuse_with_sr(&d.iface,&r.iface,&img,&q,&e,&t)<0);fence=0;
    struct xs_attachment_pin another=t.rt;another.host=&arena;another.identity=&r.iface;
    assert(xs_sr_submitted(&d.iface,&another,&t.ds,&q,NULL) && xs_reuse_with_sr(&d.iface,&r.iface,&img,&q,&e,&t)<0);
    assert(xs_sr_submitted(&d.iface,&t.rt,&t.ds,&q,&fence));
    assert(!xs_reuse_with_sr(&d.iface,&r.iface,&img,&q,&e,&t) && !rt.sr_end && r.writer_epoch==6 && !r.common_epoch && T(&q)<0);
    puts("PASS SR ticket: pending same-queue end; wrong fence/queue, bad key/serial, invalidating write, removed fence and replay refused");
}
