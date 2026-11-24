"""Compile the C membership loop and writer transaction from the scratch fixture under CPU shims."""
from pathlib import Path
import subprocess
import tempfile

s = Path(__file__).resolve().parents[1] / "native"
q = (s/"test-ordered-queue.h").read_text().split("HRESULT xs_ordered_submit(", 1)[1]
loop = q[q.index("    for (UINT i=0;i<count;i++)"):q.index("    ID3D12Device *d=")]
d = (s/"d3d12_device.c").read_text()
writer = d[d.index("HRESULT xs_pair_writer"):].split("\n#endif", 1)[0]
code = r"""
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
typedef unsigned UINT; typedef int BOOL,HRESULT; typedef uintptr_t UINT_PTR,SIZE_T,UINT64;
typedef struct obj{unsigned refs;} IUnknown,ID3D12CommandQueue,ID3D12Resource,ID3D12CommandList;
#define CONTAINING_RECORD(p,T,m) ((T*)((char*)(p)-offsetof(T,m)))
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define E_ACCESSDENIED -1
#define FAILED(x) ((x)<0)
#define RTV 4
#define AcquireSRWLockExclusive(x) assert(++*(x)==1)
#define ReleaseSRWLockExclusive(x) assert(--*(x)==0)
#define ID3D12CommandQueue_AddRef(p) ((p)->refs++)
typedef SIZE_T D3D12_CPU_DESCRIPTOR_HANDLE;
struct descriptor_record{UINT_PTR rtv_key;UINT64 serial;};
struct desc_heap{IUnknown iface;struct desc_heap *next;SIZE_T start,stride;ID3D12Resource *target;struct descriptor_record records[1];};
struct color_resource{IUnknown iface;int slice,msaa,flipping,multi_writer,claimed;ID3D12Resource *host;ID3D12CommandQueue *writer;
    UINT64 writer_epoch;struct color_resource *next;};
struct xbox_device{IUnknown iface;struct color_resource *colors;struct desc_heap *rtv_heaps;int descriptor_lock;};
static HRESULT descriptor_slot(IUnknown*i,SIZE_T slot,SIZE_T*p,void*unused){
 (void)unused;*p=slot;return slot==CONTAINING_RECORD(i,struct desc_heap,iface)->start?0:-1;}
static IUnknown*attachment_locked(struct desc_heap*h,SIZE_T slot,UINT kind,SIZE_T*p){
 (void)slot;(void)kind;(void)p;return h->target;}
""" + writer + r"""
struct startup_list{IUnknown *ordered_id,*pool_owner,*requires_rt,*primary_pin,*host,*primary;
 struct startup_list *ordered_next;UINT64 clear_slot,clear_serial;int consumed,exports_rt,err;};
struct adapter{struct startup_list*ordered;struct {IUnknown*owner;}display;};
struct work{int pair;struct startup_list*gen[4];IUnknown*host[4];};
static HRESULT ordered_ready(struct startup_list*s){return s->err;}
static int admit(struct adapter*a,IUnknown**ids,UINT count){
 struct work work={0},*w=&work;int hr=-1;
""" + loop + r"""
 return 0;
out:return hr;}
int main(void){
 IUnknown id[7]={0};struct adapter a={0};struct startup_list first={0},last={0},wrong={0};struct xbox_device d={0};
 struct color_resource r={.slice=1,.host=id+2};struct desc_heap heap={.start=32,.stride=32,.target=id+2};
 heap.records[0]=(struct descriptor_record){(UINT_PTR)&r.iface,17};d.colors=&r;d.rtv_heaps=&heap;
 a.display.owner=&d.iface;a.ordered=&first;first=(struct startup_list){.ordered_id=id,.ordered_next=&last,.exports_rt=1,
 .pool_owner=&d.iface,.primary_pin=&r.iface,.primary=id+2,.clear_slot=32,.clear_serial=17};
 last=first;last.exports_rt=0;last.ordered_id=id+1;last.requires_rt=id;last.ordered_next=&wrong;
 wrong=first;wrong.ordered_id=id+6;wrong.ordered_next=0;
 IUnknown*ids[]={id,id+1,id+1};assert(!admit(&a,ids,2));ids[0]=id+6;assert(admit(&a,ids,2));
 ids[0]=id+1;ids[1]=id;assert(admit(&a,ids,2));ids[0]=id;assert(admit(&a,ids,2));ids[1]=id+1;
 assert(admit(&a,ids,3));last.err=-2;assert(admit(&a,ids,2)==-2);last.err=0;
 last.clear_serial++;assert(admit(&a,ids,2));last.clear_serial--;last.consumed=1;assert(admit(&a,ids,2));last.consumed=0;
 UINT64 epoch=99;
 #define CHECK(commit) xs_pair_writer(&d.iface,&r.iface,r.host,32,17,id+3,&epoch,commit)
 assert(!CHECK(0)&&epoch==0&&!r.writer&&!r.claimed);
 r.writer_epoch++;assert(CHECK(1)&&!r.claimed);assert(!CHECK(0)&&epoch==1);
 heap.records[0].serial++;assert(CHECK(1));heap.records[0].serial--;
 heap.records[0].rtv_key++;assert(CHECK(1));heap.records[0].rtv_key--;
 heap.target=id;assert(CHECK(1));heap.target=r.host;
 struct desc_heap alias=heap;heap.next=&alias;assert(CHECK(1));heap.next=0;
 r.multi_writer=1;assert(CHECK(1));r.multi_writer=0;r.flipping=1;assert(CHECK(1));r.flipping=0;
 r.writer=id+4;assert(CHECK(1));r.writer=0;assert(!r.claimed&&!id[3].refs);
 assert(!CHECK(1)&&r.claimed&&r.writer==id+3&&id[3].refs==1&&r.writer_epoch==2);
 assert(CHECK(1)&&r.writer_epoch==2);
 puts("PASS actual C ordered membership + locked writer/descriptor commit (CPU registry shims)");
}
"""
with tempfile.TemporaryDirectory() as tmp:
    c=Path(tmp)/"admit.c"; c.write_text(code)
    exe=Path(tmp)/"admit"
    subprocess.run(["cc","-std=c11","-DTEST_SAME_BATCH_RELEASE","-O2","-Wall","-Wextra","-Werror",str(c),"-o",str(exe)],check=True)
    subprocess.run([exe],check=True)
