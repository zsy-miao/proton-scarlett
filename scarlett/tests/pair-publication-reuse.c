/* CPU-only: exercise current publication checks with two recorded pairs
 * sharing one successor face. All readable addresses are fixture storage. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char BYTE;
typedef uint64_t UINT64;
typedef unsigned UINT;
typedef uintptr_t UINT_PTR;
typedef size_t SIZE_T;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define GetCurrentProcess() 0
static BOOL ReadProcessMemory(int self,const void *addr,void *out,size_t n,size_t *got)
{(void)self; memcpy(out,addr,n);*got=n;return TRUE;}
#include "../native/queue/pair-publication.h"

struct node { BYTE pad[0x18]; UINT64 stamp; UINT holds,gen; BYTE tail[0x30]; };
struct pair {
    UINT_PTR ids[2],lists[514];
    struct pub_wire records[2];struct pub_seal seals[2];
    struct node node;
};
static void close_pair(struct pair *x, UINT_PTR first,UINT_PTR last)
{
    _Alignas(8) BYTE ctx[0x12800]={0};
    UINT_PTR node=(UINT_PTR)&x->node, pool=7;
    x->ids[0]=first;x->ids[1]=last;x->node.gen=1;x->node.holds=5;
    for(unsigned j=0;j<2;j++){
        struct pub_wire *w=x->records+j;
        *w=(struct pub_wire){.list=x->ids[j],.arg=(UINT_PTR)&x->ids[j],.pool=pool,.acq=j?0:node};
        UINT_PTR active[]={w->list,w->arg,(UINT_PTR)w+0x70};
        memcpy(ctx+0x127c8,active,sizeof active);
        assert(pub_close((UINT_PTR)ctx,w->list,!j,node,1,x->seals+j));
        x->lists[j]=w->list;x->lists[256+j]=pool;x->lists[512+j]=w->arg;
    }
    x->records[0].next=(UINT_PTR)&x->records[1];
}
static void publish_pair(struct pair *x,UINT64 stamp)
{x->records[1].rel=(UINT_PTR)&x->node;x->node.stamp=stamp;x->node.holds=0;}
int main(void)
{
    struct pair older={0},fresh={0};
    close_pair(&older,0xa1,0xc1);
    publish_pair(&older,3);
    assert(pub_pair(older.seals,older.seals+1,(UINT_PTR)older.lists,3));
    close_pair(&fresh,0xd1,0xc1); /* Same successor interface, distinct recording and node. */
    assert(!pub_pair(fresh.seals,fresh.seals+1,(UINT_PTR)fresh.lists,43));
    publish_pair(&fresh,43);
    assert(pub_pair(fresh.seals,fresh.seals+1,(UINT_PTR)fresh.lists,43));
    fresh.node.holds=1;
    assert(!pub_pair(fresh.seals,fresh.seals+1,(UINT_PTR)fresh.lists,43));
    fresh.node.holds=0;
    fresh.records[1].rel=(UINT_PTR)&older.node;
    assert(!pub_pair(fresh.seals,fresh.seals+1,(UINT_PTR)fresh.lists,43));
    fresh.records[1].rel=(UINT_PTR)&fresh.node;
    assert(!pub_pair(older.seals,older.seals+1,(UINT_PTR)fresh.lists,43));
    assert(!pub_pair(fresh.seals,older.seals+1,(UINT_PTR)fresh.lists,43));
    assert(!pub_pair(fresh.seals,fresh.seals+1,(UINT_PTR)fresh.lists,3));
    fresh.node.gen++;
    assert(!pub_pair(fresh.seals,fresh.seals+1,(UINT_PTR)fresh.lists,43));
    puts("PASS current pub_close/pub_pair: later pair matches its own release after publication.");
    puts("PASS same successor face: old pair, mixed generations, old publication stamp, and recycled node rejected.");
}
