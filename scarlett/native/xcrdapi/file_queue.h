/* Console +50 only. The public Windows factory and console +18 stay separate. */
#include <stdio.h>
#include <stdint.h>
#include <zlib.h>
static const IID queue_iid={0x3c02cb0c,0xc050,0x4195,{0x99,0xa1,0xa1,0xce,0x94,0xba,0x33,0xc5}};
typedef struct { UINT64 source; UINT16 capacity; INT8 priority; const char *name; UINT64 options; } QueueDesc1;
typedef struct {
    UINT64 options; void *dst; UINT64 page_tail; UINT32 dst_size;
    File *file; UINT64 offset; UINT32 size,intermediate; UINT64 tag; const char *name;
} FileRequest;
_Static_assert(sizeof(QueueDesc1)==32 && offsetof(QueueDesc1,priority)==10 &&
    offsetof(QueueDesc1,options)==24,"queue descriptor");
_Static_assert(offsetof(FileRequest,dst_size)==0x18 && offsetof(FileRequest,file)==0x20 &&
    offsetof(FileRequest,size)==0x30 && sizeof(FileRequest)==0x48,"queue request");

enum { FILE_CAPACITY=0x103f, MAX_READ_BYTES=32<<20 };
typedef struct {
    HRESULT error;
    HANDLE file; void *dst; LARGE_INTEGER offset; DWORD bytes,inflate_bytes;
    Status *marker; UINT index;
} FileEntry;
typedef struct {
    const void *vt; LONG refs; SRWLOCK lock; CONDITION_VARIABLE cv;
    HANDLE worker; BOOL closed;
    UINT first,count,submitted;
    FileEntry ring[FILE_CAPACITY];
} FileQueue;

static void queue_stop(void)
{
    OutputDebugStringA("scarlett: unsupported console DirectStorage queue operation\n");
    RaiseFailFastException(NULL,NULL,0);
    __builtin_trap();
}
static HRESULT read_exact(FileEntry *e,void *dst)
{
    DWORD n;
    if (!ReadFile(e->file,dst,e->bytes,&n,NULL)) return HRESULT_FROM_WIN32(GetLastError());
    return n==e->bytes ? S_OK : HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
}
static voidpf zalloc_heap(voidpf unused,uInt n,uInt size)
{
    (void)unused;
    return HeapAlloc(GetProcessHeap(),0,(SIZE_T)n*size); /* x64, two 32-bit factors */
}
static void zfree_heap(voidpf unused,voidpf p)
{
    (void)unused;
    HeapFree(GetProcessHeap(),0,p);
}
static BOOL writable_range(void *dst,SIZE_T bytes)
{
    UINT_PTR cur=(UINT_PTR)dst;
    if (bytes>UINTPTR_MAX-cur) return FALSE;
    while (bytes) {
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery((void *)cur,&m,sizeof m) || m.State!=MEM_COMMIT || (m.Protect&PAGE_GUARD) ||
            !(m.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return FALSE;
        SIZE_T left=m.RegionSize-(cur-(UINT_PTR)m.BaseAddress);
        if (left>=bytes) break;
        bytes-=left; cur+=left;
    }
    return TRUE;
}
static HRESULT inflate_entry(FileEntry *e)
{
    BYTE *buf=HeapAlloc(GetProcessHeap(),0,(SIZE_T)e->bytes+e->inflate_bytes);
    if (!buf) return E_OUTOFMEMORY;
    BYTE *out=buf+e->bytes;
    HRESULT hr=read_exact(e,buf);
    if (SUCCEEDED(hr)) {
        z_stream s={.zalloc=zalloc_heap,.zfree=zfree_heap,.next_in=buf,.avail_in=e->bytes,
            .next_out=out,.avail_out=e->inflate_bytes};
        int rc=inflateInit(&s);
        if (rc==Z_OK) {
            rc=inflate(&s,Z_FINISH);
            if (inflateEnd(&s)!=Z_OK) rc=Z_STREAM_ERROR;
        }
        if (rc==Z_MEM_ERROR) hr=E_OUTOFMEMORY;
        else if (rc!=Z_STREAM_END || s.total_in!=e->bytes || s.total_out!=e->inflate_bytes)
            hr=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        else {
            SIZE_T n=0;
            /* Caller owns this mapping until completion; recheck at publication, not enqueue. */
            if (!writable_range(e->dst,e->inflate_bytes) ||
                !ReadProcessMemory(GetCurrentProcess(),out,e->dst,e->inflate_bytes,&n) || n!=e->inflate_bytes)
                hr=E_POINTER;
        }
    }
    HeapFree(GetProcessHeap(),0,buf);
    return hr;
}
static HRESULT read_entry(FileEntry *e)
{
    if (FAILED(e->error)) return e->error;
    LARGE_INTEGER length;
    if (!GetFileSizeEx(e->file,&length)) return HRESULT_FROM_WIN32(GetLastError());
    if (length.QuadPart<e->offset.QuadPart+e->bytes) return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
    if (!SetFilePointerEx(e->file,e->offset,NULL,FILE_BEGIN)) return HRESULT_FROM_WIN32(GetLastError());
    return e->inflate_bytes ? inflate_entry(e) : read_exact(e,e->dst);
}
static DWORD WINAPI file_worker(void *arg)
{
    FileQueue *q=arg;
    HRESULT first_error=S_OK;
    for (;;) {
        AcquireSRWLockExclusive(&q->lock);
        while (!q->submitted && !q->closed) SleepConditionVariableSRW(&q->cv,&q->lock,INFINITE,0);
        if (!q->submitted) { ReleaseSRWLockExclusive(&q->lock); return 0; }
        FileEntry e=q->ring[q->first];
        q->first=(q->first+1)%FILE_CAPACITY; --q->count; --q->submitted;
        WakeAllConditionVariable(&q->cv);
        ReleaseSRWLockExclusive(&q->lock);
        if (e.marker) {
            AcquireSRWLockExclusive(&staging_lock);
            ++queue_stats.priority[3].counts[3];
            ReleaseSRWLockExclusive(&staging_lock);
            status_finish(e.marker,e.index,&first_error,1);
            status_release(e.marker);
            first_error=S_OK;
        } else {
            AcquireSRWLockExclusive(&staging_lock);
            ++queue_stats.priority[3].counts[5];
            ++queue_stats.priority[3].counts[6];
            if (e.file) { ++queue_stats.counters[1]; ++queue_stats.counters[2]; }
            ReleaseSRWLockExclusive(&staging_lock);
            HRESULT hr=read_entry(&e);
            if (e.file) CloseHandle(e.file);
            AcquireSRWLockExclusive(&staging_lock);
            --queue_stats.priority[3].counts[5];
            if (e.file) {
                --queue_stats.counters[1];
                if (FAILED(hr)) ++queue_stats.counters[4];
                queue_stats.priority[3].bytes[0]+=e.bytes;
                queue_stats.priority[3].bytes[1]+=e.bytes;
            }
            if (FAILED(hr)) ++queue_stats.error_count;
            ReleaseSRWLockExclusive(&staging_lock);
            if (FAILED(hr) && SUCCEEDED(first_error)) first_error=hr;
        }
    }
}
static void push_entry(FileQueue *q,FileEntry e)
{
    AcquireSRWLockExclusive(&q->lock);
    while (q->count==FILE_CAPACITY && !q->closed) SleepConditionVariableSRW(&q->cv,&q->lock,INFINITE,0);
    if (q->closed) {
        ReleaseSRWLockExclusive(&q->lock);
        if (e.file) CloseHandle(e.file);
        if (e.marker) status_release(e.marker);
        queue_stop();
    }
    if (e.marker) status_begin(e.marker,e.index);
    q->ring[(q->first+q->count)%FILE_CAPACITY]=e; ++q->count;
    ReleaseSRWLockExclusive(&q->lock);
}
static void WINAPI file_enqueue(FileQueue *q,const FileRequest *input)
{
    FileEntry e={0};
    FileRequest r;
    SIZE_T got;
    if (!input || !ReadProcessMemory(GetCurrentProcess(),input,&r,sizeof r,&got) || got!=sizeof r)
        e.error=E_POINTER;
    else if (r.options>1 || r.intermediate || (r.options==1 && r.page_tail)) e.error=E_NOTIMPL;
    else if (!r.dst || !r.file) e.error=E_POINTER;
    else if (!r.size || !r.dst_size || r.size>MAX_READ_BYTES || r.dst_size>MAX_READ_BYTES ||
             (!r.options && r.size!=r.dst_size) || r.offset>INT64_MAX-(UINT64)r.size)
        e.error=E_INVALIDARG;
    else {
        /* The caller may recycle its stack request and Close/Release the file.
         * An independent read handle also isolates seek offsets across queues. */
        AcquireSRWLockShared(&staging_lock);
        File *p=files;
        while (p && p!=r.file) p=p->next;
        if (p) file_add_ref(p);
        ReleaseSRWLockShared(&staging_lock);
        if (!p) e.error=E_NOINTERFACE;
        else {
            e.file=file_get_handle(p);
            if (e.file==INVALID_HANDLE_VALUE) { e.error=HRESULT_FROM_WIN32(GetLastError()); e.file=NULL; }
            file_release(p);
        }
        e.offset.QuadPart=r.offset; e.dst=r.dst; e.bytes=r.size;
        if (r.options==1) e.inflate_bytes=r.dst_size;
    }
    push_entry(q,e);
}
static void WINAPI file_marker(FileQueue *q,Status *s,UINT i)
{
    AcquireSRWLockShared(&staging_lock);
    Status *p=statuses;
    while (p && p!=s) p=p->next;
    if (p && i<p->capacity) status_add_ref(p); else p=NULL;
    ReleaseSRWLockShared(&staging_lock);
    if (!p) queue_stop(); /* no valid status destination or HRESULT channel */
    push_entry(q,(FileEntry){.marker=p,.index=i});
}
static void WINAPI file_submit(FileQueue *q)
{
    AcquireSRWLockExclusive(&q->lock);
    q->submitted=q->count;
    WakeAllConditionVariable(&q->cv);
    ReleaseSRWLockExclusive(&q->lock);
}
static void WINAPI queue_close(FileQueue *q)
{
    AcquireSRWLockExclusive(&q->lock);
    if (!q->closed) {
        AcquireSRWLockExclusive(&staging_lock);
        --queue_stats.priority[3].counts[0];
        ReleaseSRWLockExclusive(&staging_lock);
    }
    q->closed=TRUE; q->submitted=q->count;
    WakeAllConditionVariable(&q->cv);
    ReleaseSRWLockExclusive(&q->lock);
    WaitForSingleObject(q->worker,INFINITE);
}
static ULONG WINAPI queue_ref(FileQueue *q) { return InterlockedIncrement(&q->refs); }
static ULONG WINAPI queue_release(FileQueue *q)
{
    ULONG n=InterlockedDecrement(&q->refs);
    if (!n) {
        queue_close(q); CloseHandle(q->worker);
        AcquireSRWLockExclusive(&staging_lock);
        queue_bytes-=sizeof *q;
        ReleaseSRWLockExclusive(&staging_lock);
        HeapFree(GetProcessHeap(),0,q);
    }
    return n;
}
static HRESULT WINAPI queue_query(FileQueue *q,REFIID iid,void **out)
{
    HRESULT hr=check_interface(iid,&queue_iid,out);
    if (SUCCEEDED(hr)) { queue_ref(q); *out=q; }
    return hr;
}
static const struct {
    __typeof__(&queue_query) query; __typeof__(&queue_ref) ref,release;
    __typeof__(&file_enqueue) request; __typeof__(&file_marker) marker;
    __typeof__(&queue_stop) signal; __typeof__(&file_submit) submit;
    __typeof__(&queue_stop) cancel; __typeof__(&queue_close) close;
    __typeof__(&queue_stop) other[5];
} file_queue_vt={queue_query,queue_ref,queue_release,file_enqueue,file_marker,queue_stop,file_submit,
    queue_stop,queue_close,{[0 ... 4]=queue_stop}};
static HRESULT WINAPI console_create_queue1(Factory *f,const QueueDesc *wire,REFIID iid,void **out)
{
    (void)f;
    if (!out) return E_POINTER;
    *out=NULL;
    QueueDesc1 d;
    SIZE_T got;
    if (!iid || !wire || !ReadProcessMemory(GetCurrentProcess(),wire,&d,sizeof d,&got) || got!=sizeof d) return E_POINTER;
    if (!IsEqualIID(iid,&queue_iid)) return E_NOINTERFACE;
    if (d.source || d.capacity!=FILE_CAPACITY || d.priority!=2 || d.options!=1) {
        char msg[160];
        snprintf(msg,sizeof msg,"scarlett: CreateQueue1 unsupported source=%llu capacity=%u priority=%d options=%#llx\n",
                d.source,d.capacity,d.priority,d.options);
        OutputDebugStringA(msg);
        return E_NOTIMPL;
    }
    FileQueue *q=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *q);
    if (!q) return E_OUTOFMEMORY;
    q->vt=&file_queue_vt; q->refs=1;
    q->worker=CreateThread(NULL,0,file_worker,q,0,NULL);
    if (!q->worker) { HRESULT hr=HRESULT_FROM_WIN32(GetLastError()); HeapFree(GetProcessHeap(),0,q); return hr; }
    AcquireSRWLockExclusive(&staging_lock);
    UINT *counts=queue_stats.priority[3].counts;
    if (++counts[0]>counts[1]) counts[1]=counts[0];
    ++counts[2]; queue_bytes+=sizeof *q; track_peak();
    ReleaseSRWLockExclusive(&staging_lock);
    *out=q;
    return S_OK;
}
