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
void xs_drop_bvh_hold(struct xs_bvh_hold *hold);
#ifdef __cplusplus
}
#endif
