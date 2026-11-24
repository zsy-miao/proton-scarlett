/* Only the window owner or a window-manager lease may authorize access. */
static HRESULT worker_present(struct adapter *a,UINT64 token,IUnknown *key)
{
    HRESULT hr=DXGI_ERROR_INVALID_CALL;
    ID3D12Fence *end=NULL;
    AcquireSRWLockShared(&a->submit_lock);
    if(a->external_submissions)hr=E_ACCESSDENIED;
    else if(a->display.released && a->display.released_token==token) {
        ID3D12Resource *named=xs_retain_primary(a->display.owner,key);
        if(named==a->display.released) {
            end=a->display.released_end;
            ID3D12Fence_AddRef(end);
        }
        if(named)ID3D12Resource_Release(named);
    }
    ReleaseSRWLockShared(&a->submit_lock);
    if(!end)return hr;

    /* Wait on the caller, before taking a window lease or submitting any WSI work. */
    LARGE_INTEGER start,now,freq;
    QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&start);now=start;
    UINT64 first=ID3D12Fence_GetCompletedValue(end),done=first;
#ifdef TEST_PRESENT_BRIDGE
    extern HANDLE test_pending_present;
    if(!done && test_pending_present)SetEvent(test_pending_present);
#endif
    while(!done && now.QuadPart-start.QuadPart<freq.QuadPart/4) {
        Sleep(1);done=ID3D12Fence_GetCompletedValue(end);QueryPerformanceCounter(&now);
    }
    fprintf(stderr,"WSI producer token=%llu fence=%p entry=%llu done=%llu wait_us=%lld budget_ms=250 tick=%llu\n",
            token,end,first,done,(now.QuadPart-start.QuadPart)*1000000/freq.QuadPart,GetTickCount64());
    if(done!=1) {
        ID3D12Fence_Release(end);
        return done==0?DXGI_ERROR_WAS_STILL_DRAWING:DXGI_ERROR_DEVICE_REMOVED;
    }
    AcquireSRWLockShared(&escape_lock);
    if(bridge_escaped)hr=E_ACCESSDENIED;
    else if(a->display.wsi && TryAcquireSRWLockExclusive(&a->display.lock)) {
        struct xs_title_window *w=&a->display.captured;
        typedef ULONG_PTR(WINAPI *query_fn)(HWND,ULONG_PTR,DWORD);
        query_fn fn=(query_fn)(void*)GetProcAddress(GetModuleHandleW(L"win32u.dll"),"NtUserCallHwndParam");
        if(fn && fn(w->hwnd,w->generation,0x53435703)==1) {
            hr=present_released(a,token,key,end);
            if(fn(w->hwnd,w->generation,0x53435704)!=1)xs_stop();
        } else fprintf(stderr,"WSI rejected: no lifetime lease hwnd=%p generation=%zu\n",w->hwnd,w->generation);
        ReleaseSRWLockExclusive(&a->display.lock);
    }
    ReleaseSRWLockShared(&escape_lock);ID3D12Fence_Release(end);return hr;
}
