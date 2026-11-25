/* Shared by the C recorder and the C++ fixture; the native and carried modes
 * differ in setup and checks, never in argument layout. */
#if defined(TEST_NATIVE160) && defined(TEST_CARRIED_60)
#error choose one restored-clear fixture mode
#endif
#ifdef __cplusplus
extern "C"
#endif
BOOL test_restored_clear(ID3D12Device *,IUnknown *,IUnknown *,IUnknown *,IUnknown **,
        ID3D12Resource **,SIZE_T *,IUnknown **,UINT64,IUnknown *,IUnknown *,void (*)(void *),void *);
