/* In the device TU after descriptor_writer.h. Inputs are adapter-owned pins
 * and two already-copied slot values, never a guest pointer to a heap. */
HRESULT xs_freeze_om(IUnknown *owner,const struct xs_attachment_pin pins[2],
        const SIZE_T slots[2],struct om_views *out)
{
    struct xbox_device *d=CONTAINING_RECORD(owner,struct xbox_device,iface);
    struct om_views next={0};
    D3D12_CPU_DESCRIPTOR_HANDLE src[2];
    const D3D12_DESCRIPTOR_HEAP_TYPE types[]={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,D3D12_DESCRIPTOR_HEAP_TYPE_DSV};
    HRESULT hr;
    for (UINT i=0;i<2;i++) {
        D3D12_DESCRIPTOR_HEAP_DESC desc={types[i],1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,1};
        hr=ID3D12Device_CreateDescriptorHeap(d->host,&desc,&IID_ID3D12DescriptorHeap,(void **)&next.heap[i]);
        if (FAILED(hr)) goto done;
        next.heap[i]->lpVtbl->GetCPUDescriptorHandleForHeapStart(next.heap[i],&next.cpu[i]);
    }
    hr=E_INVALIDARG;
    AcquireSRWLockShared(&d->descriptor_lock);
    for (UINT i=0;i<2;i++) {
        const struct xs_attachment_pin *p=pins+i;
        if (!p->identity || !p->host || slots[i]!=p->slot) goto unlock;
        struct color_resource *c=d->colors;
        while(c && &c->iface!=p->identity)c=c->next;
        if (!c || c->owner!=d || c->host!=p->host || !!c->depth!=i) goto unlock;
        struct desc_heap *found=NULL;
        for(struct desc_heap *h=i?d->dsv_heaps:d->rtv_heaps;h;h=h->next) {
            if (FAILED(descriptor_slot(&h->iface,slots[i],&src[i],NULL))) continue;
            if (found) goto unlock;
            found=h;
        }
        if (!found || attachment_locked(found,slots[i],i?DSV:RTV,&src[i])!=p->host ||
            found->records[(slots[i]-found->start)/found->stride].serial!=p->serial) goto unlock;
    }
    for(UINT i=0;i<2;i++) ID3D12Device_CopyDescriptorsSimple(d->host,1,next.cpu[i],src[i],types[i]);
    hr=S_OK;
unlock:
    ReleaseSRWLockShared(&d->descriptor_lock);
done:
    if (SUCCEEDED(hr)) {struct om_views old=*out;*out=next;next=old;}
    drop_om(&next);
    return hr;
}
