/* Called by the ordered fixture only; the event publishes a real +2b0 result. */
static ComPtr<IUnknown> late_root,late_wrong,late_pso;
static IUnknown *late_owner;static HANDLE worker,gate;
extern "C" IUnknown *late_root_id(BOOL wrong){return wrong?late_wrong.Get():late_root.Get();}
static DWORD WINAPI make_late(void*){
    CK(WaitForSingleObject(gate,10000)==WAIT_OBJECT_0);
    UINT_PTR q[86];BYTE vs[1100],ps[1180];load("colour-stream.bin",q,sizeof q);load("colour-VS.bin",vs,sizeof vs);load("colour-PS.bin",ps,sizeof ps);
    D3D12_INPUT_ELEMENT_DESC ia[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    q[1]=(SIZE_T)late_root.Get();q[3]=(SIZE_T)vs;q[6]=(SIZE_T)ps;q[0x268/8]=(SIZE_T)ia;
    D3D12_PIPELINE_STATE_STREAM_DESC sd={sizeof q,q};using Make=HRESULT(WINAPI*)(IUnknown*,const void*,REFIID,void**);
    HR(((Make)(*(void***)late_owner)[0x2b0/8])(late_owner,&sd,IID_ID3D12PipelineState,(void**)late_pso.GetAddressOf()));return 0;
}
extern "C" IUnknown *publish_late(){CK(WaitForSingleObject(worker,0)==WAIT_TIMEOUT && SetEvent(gate));CK(WaitForSingleObject(worker,10000)==WAIT_OBJECT_0);return late_pso.Get();}
