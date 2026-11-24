/* Included in the console device implementation. QPC paces origins; only DXGI
 * frame statistics for the recorded PresentCount can confirm presented frames. */
static LONG64 frame_serial;
static LONGLONG frame_now(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

static HRESULT WINAPI bootstrap_frame_interval(IUnknown *iface, IDXGIOutput *output, UINT us, UINT period, UINT flags)
{
    if (!us || !period) return E_INVALIDARG;
    if (output || flags || (us != 33333 && us != 16667) || period > 3) return E_NOTIMPL;
    struct frame_clock *f = &CONTAINING_RECORD(iface, struct xbox_device, iface)->clock;
    LARGE_INTEGER freq;
    if (!QueryPerformanceFrequency(&freq)) return E_FAIL;
    AcquireSRWLockExclusive(&f->lock);
    frame_clear(f);
    f->freq = freq.QuadPart;
    f->step = (freq.QuadPart * us + 999999) / 1000000;
    f->base = f->due = frame_now();
    f->period = period;
    f->registered = 0;
    f->epoch++;
    WakeAllConditionVariable(&f->change);
    ReleaseSRWLockExclusive(&f->lock);
    return S_OK;
}

static HRESULT WINAPI bootstrap_frame_event(IUnknown *iface, UINT type, UINT offset, const void *signals, UINT flags)
{
    if ((type != 0 && type != 2) || flags || (type == 2 && offset) || signals) return E_NOTIMPL;
    struct frame_clock *f = &CONTAINING_RECORD(iface, struct xbox_device, iface)->clock;
    AcquireSRWLockExclusive(&f->lock);
    HRESULT hr = DXGI_ERROR_INVALID_CALL;
    if (f->step && (UINT64)offset * f->freq < (UINT64)f->step * f->period * 1000000) {
        if (!type) {
            LONGLONG phase = f->base + ((UINT64)offset*f->freq+999999)/1000000;
            if (phase < f->due) phase += ((f->due-phase+f->step-1)/f->step)*f->step;
            f->due = phase;
        }
        f->registered |= 1u << type;
        WakeAllConditionVariable(&f->change);
        hr = S_OK;
    }
    ReleaseSRWLockExclusive(&f->lock);
    return hr;
}

static struct frame_record *frame_find(struct frame_clock *f, UINT64 token)
{
    for (unsigned i=0; token && i<ARRAYSIZE(f->history); ++i)
        if (f->history[i].token == token) return &f->history[i];
    return NULL;
}

#ifdef SCARLETT_EXPERIMENTAL_TAGGED_BATCH
/* Called with the queue and allocator locked. Reconfiguration/eviction cannot split
 * tag validation from submission. These fences never complete a frame event. */
HRESULT xs_frame_submit(IUnknown *owner,UINT64 token,ID3D12CommandQueue *q,BOOL batch,ID3D12Fence *end,
        void (*submit)(void *),void *context)
{
    struct frame_clock *f=&CONTAINING_RECORD(owner,struct xbox_device,iface)->clock;
    AcquireSRWLockExclusive(&f->lock);
    struct frame_record *r=frame_find(f,token);
    HRESULT hr=DXGI_ERROR_INVALID_CALL;
    if (r && r==&f->history[(f->cursor-1)%ARRAYSIZE(f->history)] && !r->presenting && r->status==S_FALSE &&
        (batch ? r->queue==q && r->sync_end && !r->batch_end && ID3D12Fence_GetCompletedValue(r->sync_end)!=UINT64_MAX : !r->queue)) {
        submit(context);
        if (batch) r->batch_end=end;
        else {r->sync_end=end;r->queue=q;ID3D12CommandQueue_AddRef(q);}
        ID3D12Fence_AddRef(end);
        fprintf(stderr,"scarlett: tagged singleton queued token=%llu queue=%p sync=%p batch=%p\n",
                token,q,r->sync_end,r->batch_end);
        hr=S_OK;
    }
    ReleaseSRWLockExclusive(&f->lock);
    return hr;
}
HRESULT xs_frame_submission_query(IUnknown *owner,UINT64 token,ID3D12CommandQueue *q,BOOL batch)
{
    struct frame_clock *f=&CONTAINING_RECORD(owner,struct xbox_device,iface)->clock;
    AcquireSRWLockShared(&f->lock);
    struct frame_record *r=frame_find(f,token);
    HRESULT hr=r?S_FALSE:DXGI_ERROR_INVALID_CALL;
    if (r && r->queue==q) {
        ID3D12Fence *end=batch?r->batch_end:r->sync_end;
        if (end) {
            UINT64 value=ID3D12Fence_GetCompletedValue(end);
            hr=value==UINT64_MAX?DXGI_ERROR_DEVICE_REMOVED:value==1?S_OK:E_PENDING;
        }
    }
    ReleaseSRWLockShared(&f->lock);
    return hr;
}
#endif
static void frame_refresh(struct frame_record *r)
{
    if (!r->chain) return;
    DXGI_FRAME_STATISTICS stats;
    HRESULT hr = IDXGISwapChain_GetFrameStatistics(r->chain, &stats);
    if (hr == S_OK) {
        LONG delta = (LONG)(stats.PresentCount-r->present_id);
        if (delta < 0) return;
        hr = delta == 0 ? S_OK : DXGI_ERROR_FRAME_STATISTICS_DISJOINT;
    }
    r->status = hr;
    IDXGISwapChain_Release(r->chain);
    r->chain = NULL;
}

HRESULT xs_frame_present_after(IUnknown *iface,UINT64 token,IDXGISwapChain *chain,
        HRESULT (*prepare)(void *),void *context)
{
    if (!iface || !chain || !token) return E_INVALIDARG;
    struct xbox_device *device = CONTAINING_RECORD(iface, struct xbox_device, iface);
    ID3D12Device *owner = NULL;
    HRESULT hr = IDXGISwapChain_GetDevice(chain, &IID_ID3D12Device, (void **)&owner);
    if (FAILED(hr)) return hr;
    BOOL match = owner == device->host;
    ID3D12Device_Release(owner);
    if (!match) return E_INVALIDARG;
    struct frame_clock *f = &device->clock;
    AcquireSRWLockExclusive(&f->lock);
    struct frame_record *r = frame_find(f, token);
    if (!r || r->status != S_FALSE || r->presenting) {
        ReleaseSRWLockExclusive(&f->lock);
        return DXGI_ERROR_INVALID_CALL;
    }
    r->presenting = TRUE;
    ReleaseSRWLockExclusive(&f->lock);
    UINT before=0, id=0;
    DXGI_FRAME_STATISTICS baseline;
    /* The first GetFrameStatistics on a new chain reports DISJOINT. Establish
     * that boundary before submitting this frame, not while waiting for it. */
    hr = IDXGISwapChain_GetFrameStatistics(chain, &baseline);
    if (hr == S_OK || hr == DXGI_ERROR_FRAME_STATISTICS_DISJOINT)
        hr = IDXGISwapChain_GetLastPresentCount(chain, &before);
    if (hr == S_OK && prepare) hr=prepare(context);
    if (hr == S_OK) hr = IDXGISwapChain_Present(chain, 1, 0);
    if (hr == S_OK) hr = IDXGISwapChain_GetLastPresentCount(chain, &id);
    if (hr == S_OK && (!id || id != before+1)) hr = DXGI_ERROR_FRAME_STATISTICS_DISJOINT;
    AcquireSRWLockExclusive(&f->lock);
    r = frame_find(f, token);
    if (!r) hr = DXGI_ERROR_INVALID_CALL; /* Reconfigured during Present. */
    else {
        r->presenting = FALSE;
        r->status = FAILED(hr) ? hr : DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
        if (hr == S_OK) {
            IDXGISwapChain_AddRef(chain);
            r->chain = chain;
            r->present_id = id;
            r->status = E_PENDING;
        }
    }
    WakeAllConditionVariable(&f->change);
    ReleaseSRWLockExclusive(&f->lock);
    return hr;
}

HRESULT xs_frame_present(IUnknown *iface, UINT64 token, IDXGISwapChain *chain)
{
    return xs_frame_present_after(iface,token,chain,NULL,NULL);
}

static HRESULT WINAPI bootstrap_frame_wait(IUnknown *iface, UINT type, DWORD timeout,
        const void *objects, UINT flags, UINT64 *token)
{
    if (!token) return E_POINTER;
    UINT64 wanted = type == 2 ? *token : 0;
    if (!type) *token = 0;
    if (objects || (type != 0 && type != 2) || (flags && !(type == 0 && flags == 2 && !timeout)))
        return E_NOTIMPL;
    struct frame_clock *f = &CONTAINING_RECORD(iface, struct xbox_device, iface)->clock;
    ULONGLONG start = GetTickCount64();
    AcquireSRWLockExclusive(&f->lock);
    UINT epoch = f->epoch;
    HRESULT hr = DXGI_ERROR_INVALID_CALL;
    while (epoch == f->epoch && f->step) {
        DWORD sleep_ms = INFINITE;
        if (type == 2) {
            struct frame_record *r = frame_find(f, wanted);
            if (!(f->registered & 4) || !r) break;
            frame_refresh(r);
            if (r->status != S_FALSE && r->status != E_PENDING) { hr = r->status; break; }
            if (r->chain) sleep_ms = 1; /* Poll the display's counter, never infer completion from time. */
        } else {
            if (!(f->registered & 1)) break;
            LONGLONG now = frame_now();
            if (now >= f->due) {
                struct frame_record *r = &f->history[f->cursor % ARRAYSIZE(f->history)];
                frame_refresh(r);
                if (r->presenting || r->chain) { hr = DXGI_ERROR_WAS_STILL_DRAWING; break; }
                LONGLONG origin = now - (now - f->due) % f->step;
                drop_submissions(r);
                *r = (struct frame_record){.token=InterlockedIncrement64(&frame_serial), .origin=origin,
                        .target=origin+f->period*f->step, .status=S_FALSE};
                f->due = origin+f->step;
                f->cursor++;
                WakeAllConditionVariable(&f->change); /* Evicted token waits must see invalidation. */
                *token = r->token;
                hr = S_OK;
                break;
            }
            sleep_ms = (DWORD)(((f->due-now)*1000 + f->freq-1)/f->freq);
        }
        if (timeout != INFINITE) {
            ULONGLONG elapsed = GetTickCount64()-start;
            if (elapsed >= timeout) { hr = DXGI_ERROR_WAIT_TIMEOUT; break; }
            if (sleep_ms > timeout-elapsed) sleep_ms = (DWORD)(timeout-elapsed);
        }
        if (!SleepConditionVariableSRW(&f->change,&f->lock,sleep_ms,0) && GetLastError()!=ERROR_TIMEOUT) {
            hr = HRESULT_FROM_WIN32(GetLastError()); break;
        }
    }
    ReleaseSRWLockExclusive(&f->lock);
    return hr;
}
