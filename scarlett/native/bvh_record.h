#pragma once
/* Internal only. All COM objects and the resolved mapping come from the trusted
 * host caller. Keep the buffer in UAV state; release the hold after retirement. */
struct xs_bvh_range {
    ID3D12Resource *resource;
    UINT64 guest_base, resource_offset, bytes;
};
struct xs_bvh_hold;
#ifdef __cplusplus
extern "C" {
#endif
HRESULT xs_record_bvh_init(IUnknown *pipeline,ID3D12GraphicsCommandList *list,
    const struct xs_bvh_range *range,const void *initializer,SIZE_T bytes,struct xs_bvh_hold **out);
/* dst is the qualified slice beginning at root u0; header80 is its coherent
 * first 80 bytes. commands is the b0.x-sized immutable prefix of root t0.
 * source, if used, is a synchronized UPLOAD buffer mapping of the node lists. */
HRESULT xs_record_bvh_offline(IUnknown *pipeline,ID3D12GraphicsCommandList *list,
    const struct xs_bvh_range *dst,const void *header80,const struct xs_bvh_range *source,
    const void *commands,SIZE_T command_bytes,UINT num_commands,struct xs_bvh_hold **out);
/* Same snapshot contract as offline, with source resolving instance index lists. */
HRESULT xs_record_bvh_partitions(IUnknown *pipeline,ID3D12GraphicsCommandList *list,
    const struct xs_bvh_range *dst,const void *header80,const struct xs_bvh_range *source,
    const void *commands,SIZE_T command_bytes,UINT num_commands,struct xs_bvh_hold **out);
HRESULT xs_record_bvh_reset(IUnknown *pipeline,ID3D12GraphicsCommandList *list,
    const struct xs_bvh_range *dst,UINT count,UINT value,struct xs_bvh_hold **out);
/* Distinct, non-aliasing buffer ranges beginning at u0/t0. Caller supplies
 * UAV / NON_PIXEL_SHADER_RESOURCE states and orders accesses until retirement. */
HRESULT xs_record_bvh_copy_counters(IUnknown *pipeline,ID3D12GraphicsCommandList *list,
    const struct xs_bvh_range *dst,const struct xs_bvh_range *src,UINT count,struct xs_bvh_hold **out);
/* Same coherent snapshot contract; source resolves the referenced BLAS headers. */
HRESULT xs_record_bvh_instances(IUnknown *pipeline,ID3D12GraphicsCommandList *list,
    const struct xs_bvh_range *dst,const void *header80,const struct xs_bvh_range *source,
    const void *commands,SIZE_T command_bytes,UINT num_commands,struct xs_bvh_hold **out);
/* source is the coherent CPU snapshot of the exact t0 BVH prefix, header included. */
HRESULT xs_record_bvh_copy_update(IUnknown *pipeline,ID3D12GraphicsCommandList *list,
    const struct xs_bvh_range *dst,const void *source,SIZE_T bytes,UINT reset_bounds,struct xs_bvh_hold **out);
void xs_drop_bvh_hold(struct xs_bvh_hold *hold);
#ifdef __cplusplus
}
#endif
