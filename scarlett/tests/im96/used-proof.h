/* Fixture contract. The allocator supplies cb_allocation independently from raw_allocation.
 * They may pin the same generation; adjacency alone does not establish this fact. */
struct used_proof {
    IUnknown *owner,*src_heap,*visible,*raw_allocation,*cb_allocation;
    ID3D12Resource *image;
    SIZE_T base,raw_slot,tex_slot,cb_slot,table;
    UINT64 raw_serial,tex_serial,cb_generation;
    D3D12_SHADER_RESOURCE_VIEW_DESC view;
};
