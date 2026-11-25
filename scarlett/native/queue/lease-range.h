/* Read-only snapshots of the measured recycler arena; no COM call through
 * title memory. A placed capture owns the source mapping, not the payload. */
#include <stdint.h>
struct lease_arena {UINT_PTR base,table;UINT count,stride;UINT64 size;};
struct lease_node {UINT_PTR prev,next,data;UINT64 stamp;UINT holds,gen;};
struct lease_span {
    struct lease_arena arena;
    UINT_PTR node,data,va;
    UINT gen,bytes,holds;
    UINT64 stamp;
};
static inline BOOL lease_read(UINT_PTR addr,void *out,SIZE_T size)
{
    SIZE_T n;return addr && addr<=UINTPTR_MAX-size && ReadProcessMemory(GetCurrentProcess(),(void*)addr,out,size,&n) && n==size;
}
static inline BOOL lease_span_read(UINT_PTR base,UINT_PTR va,UINT bytes,struct lease_span *out)
{
    struct lease_arena a;struct lease_node n;
    if(!lease_read((UINT_PTR)GetModuleHandleW(NULL)+0x75df1b8,&a,sizeof a) || a.base!=base || !base || !a.table ||
       !a.count || !a.stride || (UINT64)a.count*a.stride!=a.size || a.size>UINTPTR_MAX-base ||
       a.count>(UINTPTR_MAX-a.table)/0x58 || va<base || va-base>=a.size || !bytes)return FALSE;
    UINT ix=(va-base)/a.stride;UINT_PTR k=a.table+(SIZE_T)ix*0x58;
    if(!lease_read(k,&n,sizeof n) || n.data!=base+(SIZE_T)ix*a.stride || va-n.data>=a.stride ||
       bytes>a.stride-(va-n.data) || !n.gen)return FALSE;
    *out=(struct lease_span){a,k,n.data,va,n.gen,bytes,n.holds,n.stamp};return TRUE;
}
