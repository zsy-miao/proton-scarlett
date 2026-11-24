/* Fixture only: sample the same image after each consuming draw. Restore the
 * attachment layout, and do not touch any shader-visible bindings. */
static void sample_quad(ID3D12GraphicsCommandList7 *l,ID3D12Resource *image,ID3D12Resource *read,UINT phase)
{
    D3D12_RESOURCE_BARRIER b={.Transition={image,0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE}};
    ID3D12GraphicsCommandList7_ResourceBarrier(l,1,&b);
    D3D12_TEXTURE_COPY_LOCATION src={.pResource=image},dst={.pResource=read,.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
            .PlacedFootprint={phase*512,{DXGI_FORMAT_R8G8B8A8_UNORM,2,2,1,256}}};
    D3D12_BOX box={10,8,0,12,10,1};
    ID3D12GraphicsCommandList7_CopyTextureRegion(l,&dst,0,0,0,&src,&box);
    b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;b.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
    ID3D12GraphicsCommandList7_ResourceBarrier(l,1,&b);
}
static BOOL execute_quad_fixture(ID3D12Device *hd,struct startup_list *s,ID3D12Resource *read)
{
    /* The fixture's split marker is synthetic. Execute exactly its two closed
     * native segments with everything pinned; no guest queue admission added. */
    D3D12_COMMAND_QUEUE_DESC qd={0};ID3D12CommandQueue *q;ID3D12Fence *f;
    REQUIRE(SUCCEEDED(ID3D12Device_CreateCommandQueue(hd,&qd,&IID_ID3D12CommandQueue,(void**)&q)) &&
            SUCCEEDED(ID3D12Device_CreateFence(hd,0,0,&IID_ID3D12Fence,(void**)&f)));
    ID3D12CommandList *ls[]={(ID3D12CommandList*)s->head,(ID3D12CommandList*)s->host};
    ID3D12CommandQueue_ExecuteCommandLists(q,2,ls);
    HANDLE e=CreateEventW(NULL,FALSE,FALSE,NULL);
    REQUIRE(e && SUCCEEDED(ID3D12CommandQueue_Signal(q,f,1)) && SUCCEEDED(ID3D12Fence_SetEventOnCompletion(f,1,e)) &&
            WaitForSingleObject(e,15000)==WAIT_OBJECT_0 && ID3D12Fence_GetCompletedValue(f)==1 && SUCCEEDED(ID3D12Device_GetDeviceRemovedReason(hd)));
    BYTE *p;REQUIRE(SUCCEEDED(ID3D12Resource_Map(read,0,NULL,(void**)&p)));
    const DWORD want[3][4]={{0xdf9f6020,0xffff00ff,0xffff00ff,0xdf9f6020},
                           {0xdf9f6020,0xffff00ff,0xffff00ff,0xdf9f6020},
                           {0xdf9f6020,0xdf9f6020,0xdf9f6020,0xdf9f6020}};
    for(UINT i=0;i<3;i++)for(UINT y=0;y<2;y++)for(UINT x=0;x<2;x++){
        DWORD got;memcpy(&got,p+512*i+y*256+x*4,4);
        if(got!=want[i][2*y+x])fprintf(stderr,"phase %u (%u,%u) got %08lx want %08lx\n",i,x,y,got,want[i][2*y+x]);
        REQUIRE(got==want[i][2*y+x]);
    }
    ID3D12Resource_Unmap(read,0,NULL);
    CloseHandle(e);ID3D12Fence_Release(f);ID3D12CommandQueue_Release(q);
    puts("PASS original #4 recorder +470/+258/+60: diagonal before/after split, real all-ones restore, three fenced 2x2 readbacks");return TRUE;
}
