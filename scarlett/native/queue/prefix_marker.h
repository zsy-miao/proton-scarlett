/* A CPU-only marker is published between prior queue work and this list. */
#include "../xmem/marked_section.h"
struct prefix_marker {
    LONG refs;
    void *alias;
    volatile LONG *word;
    UINT value;
    ID3D12Heap *heap;
    ID3D12Resource *buffer;
};
static void drop_marker(struct prefix_marker *m)
{
    if (m && !InterlockedDecrement(&m->refs)) {
        if (m->buffer) ID3D12Resource_Release(m->buffer);
        if (m->heap) ID3D12Heap_Release(m->heap);
        if (m->alias && !VirtualFree(m->alias,65536,MEM_RELEASE)) xs_stop();
        HeapFree(GetProcessHeap(),0,m);
    }
}
struct marker_job {
    struct prefix_marker *m;
    ID3D12Fence *fence;
    HANDLE event;
};
static void CALLBACK publish_marker(PTP_CALLBACK_INSTANCE instance,void *ctx,PTP_WAIT wait,TP_WAIT_RESULT result)
{
    struct marker_job *j=ctx;
    (void)instance;(void)result;
    CloseThreadpoolWait(wait);
    if (ID3D12Fence_GetCompletedValue(j->fence)==1) {
        InterlockedExchange(j->m->word,j->m->value);
        if (FAILED(ID3D12Fence_Signal(j->fence,2))) xs_stop();
    }
    drop_marker(j->m); ID3D12Fence_Release(j->fence); CloseHandle(j->event); HeapFree(GetProcessHeap(),0,j);
}
static void enqueue_marker(ID3D12CommandQueue *q,ID3D12Device *d,struct prefix_marker *m)
{
    struct marker_job *j=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *j);
    PTP_WAIT wait;
    if (!j || FAILED(ID3D12Device_CreateFence(d,0,0,&IID_ID3D12Fence,(void **)&j->fence)) ||
        !(j->event=CreateEventW(NULL,FALSE,FALSE,NULL)) || !(wait=CreateThreadpoolWait(publish_marker,j,NULL)) ||
        FAILED(ID3D12Fence_SetEventOnCompletion(j->fence,1,j->event))) xs_stop();
    j->m=m; InterlockedIncrement(&m->refs);
    if (FAILED(ID3D12CommandQueue_Signal(q,j->fence,1)) || FAILED(ID3D12CommandQueue_Wait(q,j->fence,2))) xs_stop();
    SetThreadpoolWait(wait,j->event,NULL);
}
