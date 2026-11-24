/* Fixture-only inspection of the real open recorder. No exported production escape. */
#include "queue/queue.c"
ID3D12GraphicsCommandList *test_list_recorder(IUnknown *list){return (ID3D12GraphicsCommandList*)sl(list)->host;}
ID3D12CommandQueue *test_host_queue(IUnknown *q){return as_queue(q);}
HRESULT test_list_root_checked(IUnknown *list,IUnknown *root){return record_graphics_root(sl(list),root);}
