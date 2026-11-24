#include "queue/queue.c"
ID3D12CommandQueue *test_host_queue(IUnknown *q){return as_queue(q);}
/* Test-only samples inside the native recording, without exposing or splitting it. */
HRESULT test_post_try(IUnknown *i,UINT64 addr,UINT value,UINT64 c,UINT flags){return record_native_post(sl(i),addr,value,c,flags);}
void test_post_snapshot(IUnknown *i,ID3D12Resource *rb,UINT dst,UINT stamp_offset)
{
    struct startup_list *s=sl(i);
    ID3D12Resource *r[2]={s->stamp->dst,s->marker->buffer};D3D12_RESOURCE_BARRIER b[2]={0};
    for(UINT j=0;j<2;j++) b[j].Transition=(D3D12_RESOURCE_TRANSITION_BARRIER){r[j],0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE};
    ID3D12GraphicsCommandList7_ResourceBarrier(s->host,2,b);
    ID3D12GraphicsCommandList7_CopyBufferRegion(s->host,rb,dst,r[0],stamp_offset,8);
    ID3D12GraphicsCommandList7_CopyBufferRegion(s->host,rb,dst+8,r[1],8,8);
    for(UINT j=0;j<2;j++){b[j].Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;b[j].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;}
    ID3D12GraphicsCommandList7_ResourceBarrier(s->host,2,b);
}

HRESULT test_primary_try(IUnknown *i,UINT n,const D3D12_BARRIER_GROUP *g){return record_primary_barrier(sl(i),n,g);}
