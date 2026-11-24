/* No resource refs: the barrier pair outlives every recording-time replay. */
struct om_views {ID3D12DescriptorHeap *heap[2];D3D12_CPU_DESCRIPTOR_HANDLE cpu[2];};
static inline void drop_om(struct om_views *v)
{
    for(UINT i=0;i<2;i++)if(v->heap[i]) ID3D12DescriptorHeap_Release(v->heap[i]);
    *v=(struct om_views){0};
}
static inline void replay_om(const struct om_views *v,ID3D12GraphicsCommandList7 *l)
{
    if(v->heap[0]) ID3D12GraphicsCommandList7_OMSetRenderTargets(l,1,v->cpu,FALSE,v->cpu+1);
}
