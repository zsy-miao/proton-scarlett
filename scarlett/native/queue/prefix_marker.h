/* A CPU-only marker is published between prior queue work and this list. */
#include "../xmem/marked_section.h"
struct prefix_marker {
    LONG refs;
    void *alias;
    volatile LONG *word;
    UINT value;
    SIZE_T generation;
    struct prefix_marker *parent; /* Post marker shares the prefix alias. */
    HANDLE control_section;
    UINT native_post; /* Value published at offset 12, or zero before the post. */
    const UINT64 *counter;
    UINT64 threshold;
    ID3D12Heap *heap;
    ID3D12Resource *buffer;
};
static void drop_marker(struct prefix_marker *m)
{
    if (m && !InterlockedDecrement(&m->refs)) {
        if (m->counter && m->parent->buffer) {
            D3D12_RANGE none={0,0};
            ID3D12Resource_Unmap(m->parent->buffer,0,&none);
        }
        drop_marker(m->parent);
        if (m->buffer) ID3D12Resource_Release(m->buffer);
        if (m->control_section) CloseHandle(m->control_section);
        if (m->heap) ID3D12Heap_Release(m->heap);
        if (m->alias && !VirtualFree(m->alias,65536,MEM_RELEASE)) xs_stop();
        HeapFree(GetProcessHeap(),0,m);
    }
}
struct marker_job {
    struct prefix_marker *m;
    ID3D12Device *device; /* Owned by the private fence until this job closes. */
    ID3D12Fence *fence;
    HANDLE event;
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
    unsigned polls;
#endif
};
static void CALLBACK publish_marker(PTP_CALLBACK_INSTANCE instance,void *ctx,PTP_WAIT wait,TP_WAIT_RESULT result)
{
    struct marker_job *j=ctx;
    (void)instance;
    if (ID3D12Fence_GetCompletedValue(j->fence)==1 &&
        (!j->m->counter || SUCCEEDED(ID3D12Device_GetDeviceRemovedReason(j->device)))) {
        if (result==WAIT_OBJECT_0) {
            if (j->m->word) InterlockedExchange(j->m->word,j->m->value);
        } else if (!j->m->counter || result!=WAIT_TIMEOUT) xs_stop();
        /* Experimental whole-direct-queue gate: a timer wake is only a request
         * to sample the original again. Never park a pool worker or queue lock. */
        if (j->m->counter && __atomic_load_n(j->m->counter,__ATOMIC_ACQUIRE)<j->m->threshold) {
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
            if (!j->polls++) fprintf(stderr,"WSI producer gate pending tick=%llu\n",GetTickCount64());
#endif
            FILETIME due={(DWORD)-10000,(DWORD)-1}; /* relative 1 ms */
            SetThreadpoolWait(wait,j->event,&due);
            return;
        }
#ifdef SCARLETT_EXPERIMENTAL_BRIDGE
        if (j->m->counter) fprintf(stderr,"WSI producer gate satisfied tick=%llu polls=%u threshold=%llu\n",GetTickCount64(),j->polls,j->m->threshold);
#endif
        if (FAILED(ID3D12Fence_Signal(j->fence,2)) &&
            SUCCEEDED(ID3D12Device_GetDeviceRemovedReason(j->device))) xs_stop();
    }
    CloseThreadpoolWait(wait);
    drop_marker(j->m); ID3D12Fence_Release(j->fence); CloseHandle(j->event); HeapFree(GetProcessHeap(),0,j);
}
static void enqueue_marker(ID3D12CommandQueue *q,ID3D12Device *d,struct prefix_marker *m,ID3D12Fence **retire_gate)
{
    struct marker_job *j=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *j);
    PTP_WAIT wait;
    if (!j || FAILED(ID3D12Device_CreateFence(d,0,0,&IID_ID3D12Fence,(void **)&j->fence)) ||
        !(j->event=CreateEventW(NULL,FALSE,FALSE,NULL)) || !(wait=CreateThreadpoolWait(publish_marker,j,NULL)) ||
        FAILED(ID3D12Fence_SetEventOnCompletion(j->fence,1,j->event))) xs_stop();
    j->m=m; j->device=d; InterlockedIncrement(&m->refs);
    /* Dropping the last external fence ref can unblock a pending vkd3d wait.
     * Cancelling the observer must not release the GPU tail. */
    if (retire_gate) {ID3D12Fence_AddRef(j->fence); *retire_gate=j->fence;}
    if (FAILED(ID3D12CommandQueue_Signal(q,j->fence,1)) || FAILED(ID3D12CommandQueue_Wait(q,j->fence,2))) xs_stop();
    SetThreadpoolWait(wait,j->event,NULL);
}
