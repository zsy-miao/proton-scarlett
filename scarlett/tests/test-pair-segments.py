"""Run the actual segment issuer with a recording CPU queue; no D3D device."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root/"native/queue/startup_list.h").read_text()
issuer = src[src.index("static void execute_recording("):src.index("\n}",src.index("static void execute_recording("))+2]
unit = r'''
#include <assert.h>
#include <string.h>
typedef int ID3D12CommandQueue, ID3D12Fence;
typedef char ID3D12CommandList;
#define SCARLETT_EXPERIMENTAL_TAGGED_BATCH
#define TEST_PAIR_SPLIT
#define FAILED(x) ((x)<0)
#define xs_stop() assert(0)
struct heap_retirement { ID3D12CommandList *tag_tail; ID3D12Fence *prefix_gate,*gate,*fence; };
struct marker {int alias,counter; char op;};
struct startup_list {struct marker *marker,*post;void *device; ID3D12CommandList *head,*host;};
static char trace[8];static int n;static ID3D12Fence prefix,post;
static void enqueue_marker(ID3D12CommandQueue *q,void *d,struct marker *m,ID3D12Fence **hold){
    assert(q && !d && hold && !*hold);
    trace[n++]=m->op;*hold=m->counter?&post:&prefix;
}
static void ID3D12CommandQueue_ExecuteCommandLists(ID3D12CommandQueue *q,int count,ID3D12CommandList **lists){
    assert(q && count==1);trace[n++]=**lists;
}
static int ID3D12CommandQueue_Signal(ID3D12CommandQueue *q,ID3D12Fence *f,int v){
    assert(q && f && v==1);trace[n++]='S';return 0;
}
''' + issuer + r'''
int main(void){
    int q=0;char h='H',t='T';struct marker m={1,0,'P'},p={0,1,'W'};
    struct startup_list s={&m,&p,0,&h,&t};struct heap_retirement r={.tag_tail=&t};
    execute_recording(&q,&s,&r);assert(n==4&&!memcmp(trace,"PHWT",4)&&r.prefix_gate==&prefix&&r.gate==&post);
    n=0;m.alias=0;r.prefix_gate=r.gate=0;execute_recording(&q,&s,&r);
    assert(n==3&&!memcmp(trace,"HWT",3)&&!r.prefix_gate&&r.gate==&post);
    n=0;r.fence=&q;r.gate=0;execute_recording(&q,&s,&r);
    assert(n==4&&!memcmp(trace,"HWTS",4)&&r.gate==&post);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp)/"check.c"
    c.write_text(unit)
    exe = c.with_suffix("")
    subprocess.run(["cc","-O2","-Wall","-Wextra","-Werror",str(c),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print("PASS actual segment issuer: CPU-prefix/head/post-wait/tail and native-head/post-wait/tail; gate refs returned")
