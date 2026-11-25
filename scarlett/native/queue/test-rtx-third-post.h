/* Two fresh recordings capture the same original before the guest VA is replaced. */
static int third_post(ID3D12Device *d,IUnknown *owner,IUnknown **heaps,BYTE *page,BYTE *alias,BYTE *ts)
{
    const UINT post=0x29a5ee15;UINT_PTR addr=(UINT_PTR)page+0x9c;
    IUnknown *q,*end,*gate,*a[3],*l[3];
    HR(xs_direct_queue(d,&q));HR(xs_shared_fence(d,&end));HR(xs_shared_fence(d,&gate));
    *(volatile UINT64*)ts=0;
    for(UINT k=0;k<3;k++){
        HR(xs_allocator(d,0,&a[k]));HR(xs_device_list(d,0,a[k],TRUE,owner,&l[k]));
        HR(xs_bind_pool_heaps(l[k],2,heaps));pool_vtbl.marker(l[k],addr-(k==2?8:4),2,0,0);
        HR(xs_bind_pool_heaps(l[k],2,heaps));pool_vtbl.timestamp(l[k],(UINT_PTR)ts,0,0,1);
        pair_op(sl(l[k]),0x238);
    }
    struct startup_list *s=sl(l[0]);
    CK(FAILED(split_after_timestamp(sl(l[2]),addr,post,0,0))); /* prior word +94 */
    CK(FAILED(split_after_timestamp(s,(UINT_PTR)alias+0x9c,post,0,0))); /* same bytes, different view generation */
    CK(FAILED(split_after_timestamp(s,addr+4,post,0,0)) && FAILED(split_after_timestamp(s,addr,post^1,0,0)) &&
        FAILED(split_after_timestamp(s,addr,post,1,0)) && FAILED(split_after_timestamp(s,addr,post,0,1)) && !s->post);
    s->nops--;pool_vtbl.marker(l[0],addr,post,0,0);
    CK(s->post && s->post->parent==s->marker && s->post->word==s->marker->word+1 && !s->post->counter &&
        FAILED(record_counter_wait(s,(UINT_PTR)page,7,1,1)) && !s->post->counter && s->post_boundary);
    HR(sl_close(l[0]));HR(xs_queue_wait(q,gate,1));HR(checked_startup_submit(q,1,l,0,0));HR(queue_signal(q,end,1));
    CK(VirtualFree(page,65536,MEM_RELEASE) && VirtualAlloc(page,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==page);
    memset(page,0xbd,65536);
    CK(FAILED(split_after_timestamp(sl(l[1]),addr,post,0,0)) && !sl(l[1])->post);
    for(UINT k=0;k<3;k++){if(k)HR(sl_close(l[k]));IUnknown_Release(l[k]);IUnknown_Release(a[k]);}
    HANDLE done=CreateEventW(NULL,FALSE,FALSE,NULL);CK(done);HR(xs_fence_event(end,1,done));
    CK(WaitForSingleObject(done,50)==WAIT_TIMEOUT && !*(volatile UINT64*)ts &&
        ((volatile UINT*)alias)[0x98/4]==0xa7a7a7a7 && ((volatile UINT*)alias)[0x9c/4]==0xa7a7a7a7);
    HR(xs_cpu_signal(gate,1));CK(WaitForSingleObject(done,10000)==WAIT_OBJECT_0);HR(ID3D12Device_GetDeviceRemovedReason(d));
    CK(*(volatile UINT64*)ts);
    for(UINT j=0;j<65536/4;j++)CK(((volatile UINT*)alias)[j]==(4*j==0x98?2:4*j==0x9c?post:0xa7a7a7a7) &&
        ((UINT*)page)[j]==0xbdbdbdbd);
    CloseHandle(done);IUnknown_Release(q);IUnknown_Release(gate);IUnknown_Release(end);
    CK(VirtualFree(page,0,MEM_RELEASE) && VirtualFree(alias,65536,MEM_RELEASE));
    puts("PASS RTX third post +98=2/+9c=29a5ee15: original after gate, replacement and neighbors intact, later wait refused");
    return 0;
}
