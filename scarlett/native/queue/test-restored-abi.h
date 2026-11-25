/* Both TUs use this signature, including the native source + carried draw fixture. */
#ifdef __cplusplus
extern "C"
#endif
BOOL test_restored_clear(ID3D12Device *,IUnknown *,IUnknown *,IUnknown *,IUnknown **,
        ID3D12Resource **,SIZE_T *,IUnknown **,UINT64,IUnknown *,IUnknown *,void (*)(void *),void *);
