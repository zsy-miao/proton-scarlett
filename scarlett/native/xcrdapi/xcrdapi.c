/* Windows x64 DirectStorage factory/file ABI; see README for the header and test. */
#include <windows.h>
#include <unknwn.h>
#include <stddef.h>

static const IID factory_iid = {0x6924ea0c,0xc3cd,0x4826,{0xb1,0x0a,0xf6,0x4f,0x4e,0xd9,0x27,0xc1}};
static const IID file_iid = {0x5de95e7b,0x955a,0x4868,{0xa7,0x3c,0x24,0x3b,0x29,0xf4,0xb8,0xda}};
static const IID console_factory_iid = {0x62959d11,0xd45f,0x4884,{0xa0,0xa3,0xf7,0x86,0x2e,0xa2,0x43,0xae}};

typedef struct Factory Factory;
typedef struct File File;
typedef struct QueueDesc QueueDesc;

typedef struct {
    HRESULT (STDMETHODCALLTYPE *query)(Factory *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *add_ref)(Factory *);
    ULONG (STDMETHODCALLTYPE *release)(Factory *);
    HRESULT (STDMETHODCALLTYPE *create_queue)(Factory *, const QueueDesc *, REFIID, void **);
    HRESULT (STDMETHODCALLTYPE *open_file)(Factory *, PCWSTR, REFIID, void **);
    HRESULT (STDMETHODCALLTYPE *create_status)(Factory *, UINT32, PCSTR, REFIID, void **);
    void (STDMETHODCALLTYPE *set_debug_flags)(Factory *, UINT32);
    HRESULT (STDMETHODCALLTYPE *set_staging_size)(Factory *, UINT32);
} FactoryVtable;
typedef struct {
    HRESULT (STDMETHODCALLTYPE *query)(File *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *add_ref)(File *);
    ULONG (STDMETHODCALLTYPE *release)(File *);
    void (STDMETHODCALLTYPE *close)(File *);
    HRESULT (STDMETHODCALLTYPE *information)(File *, BY_HANDLE_FILE_INFORMATION *);
} FileVtable;
struct Factory { const void *vt; };
struct File { const FileVtable *vt; LONG refs; SRWLOCK lock; HANDLE handle; };
static Factory singleton, console;
static LONG factory_refs = 1; /* permanent factory reference */

static HRESULT check_interface(REFIID requested, REFIID supported, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!requested) return E_POINTER;
    return IsEqualIID(requested, supported) || IsEqualIID(requested, &IID_IUnknown)
        ? S_OK : E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_add_ref(Factory *f) { (void)f; return InterlockedIncrement(&factory_refs); }
static ULONG STDMETHODCALLTYPE factory_release(Factory *f) { (void)f; return InterlockedDecrement(&factory_refs); }
static HRESULT STDMETHODCALLTYPE factory_query(Factory *f, REFIID iid, void **out)
{
    HRESULT hr = check_interface(iid, &factory_iid, out);
    if (hr == E_NOINTERFACE && IsEqualIID(iid, &console_factory_iid)) {
        *out = &console;
        factory_add_ref(f);
        return S_OK;
    }
    if (FAILED(hr)) return hr;
    factory_add_ref(f);
    *out = &singleton; /* canonical IUnknown for both interfaces */
    return S_OK;
}

static ULONG STDMETHODCALLTYPE file_add_ref(File *f) { return InterlockedIncrement(&f->refs); }
static HRESULT STDMETHODCALLTYPE file_query(File *f, REFIID iid, void **out)
{
    HRESULT hr = check_interface(iid, &file_iid, out);
    if (FAILED(hr)) return hr;
    file_add_ref(f);
    *out = f;
    return S_OK;
}
static void STDMETHODCALLTYPE file_close(File *f)
{
    AcquireSRWLockExclusive(&f->lock);
    if (f->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(f->handle);
        f->handle = INVALID_HANDLE_VALUE;
    }
    ReleaseSRWLockExclusive(&f->lock);
}
static ULONG STDMETHODCALLTYPE file_release(File *f)
{
    ULONG remaining = InterlockedDecrement(&f->refs);
    if (!remaining) {
        file_close(f);
        HeapFree(GetProcessHeap(), 0, f);
    }
    return remaining;
}
static HRESULT STDMETHODCALLTYPE file_information(File *f, BY_HANDLE_FILE_INFORMATION *info)
{
    HRESULT hr = S_OK;
    if (!info) return E_POINTER;
    AcquireSRWLockShared(&f->lock);
    if (f->handle == INVALID_HANDLE_VALUE) hr = HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
    else if (!GetFileInformationByHandle(f->handle, info)) hr = HRESULT_FROM_WIN32(GetLastError());
    ReleaseSRWLockShared(&f->lock);
    return hr;
}
static const FileVtable file_vt = {file_query, file_add_ref, file_release, file_close, file_information};

static HRESULT STDMETHODCALLTYPE open_file(Factory *factory, PCWSTR path, REFIID iid, void **out)
{
    (void)factory;
    HRESULT hr = check_interface(iid, &file_iid, out);
    if (FAILED(hr)) return hr;
    if (!path) return E_POINTER;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    File *f = HeapAlloc(GetProcessHeap(), 0, sizeof(*f));
    if (!f) {
        CloseHandle(h);
        return E_OUTOFMEMORY;
    }
    f->vt = &file_vt;
    f->refs = 1;
    InitializeSRWLock(&f->lock);
    f->handle = h;
    *out = f;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE create_queue(Factory *f, const QueueDesc *desc, REFIID iid, void **out)
{
    (void)f;
    if (!out) return E_POINTER;
    *out = NULL;
    return iid && desc ? E_NOTIMPL : E_POINTER;
}
static HRESULT STDMETHODCALLTYPE create_status(Factory *f, UINT32 capacity, PCSTR name, REFIID iid, void **out)
{
    (void)f; (void)capacity; (void)name;
    if (!out) return E_POINTER;
    *out = NULL;
    return iid ? E_NOTIMPL : E_POINTER;
}
static void STDMETHODCALLTYPE set_debug_flags(Factory *f, UINT32 flags) { (void)f; (void)flags; }
static HRESULT STDMETHODCALLTYPE set_staging_size(Factory *f, UINT32 size) { (void)f; (void)size; return E_NOTIMPL; }

static const FactoryVtable factory_vt = {factory_query, factory_add_ref, factory_release,
    create_queue, open_file, create_status, set_debug_flags, set_staging_size};
static Factory singleton = {&factory_vt};

/* IDStorageFactoryX2 subset. Consumer offsets and unresolved slots: see ABI.md. */
enum { STAGING_BYTES = 32 * 1024 * 1024 };
static SRWLOCK staging_lock = SRWLOCK_INIT;
static void *staging;
static UINT32 peak_staging;

static HRESULT STDMETHODCALLTYPE console_staging(Factory *f, UINT32 size)
{
    HRESULT hr = S_OK;
    (void)f;
    if (size != 0 && size != STAGING_BYTES) return E_NOTIMPL;
    AcquireSRWLockExclusive(&staging_lock);
    if (size && !staging) {
        staging = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!staging) hr = HRESULT_FROM_WIN32(GetLastError());
        else peak_staging = size;
    } else if (!size && staging) {
        if (VirtualFree(staging, 0, MEM_RELEASE)) staging = NULL;
        else hr = HRESULT_FROM_WIN32(GetLastError());
    }
    ReleaseSRWLockExclusive(&staging_lock);
    return hr;
}

/* Published DSTORAGE_PRIORITY_STATS and DSTORAGE_STATS, GDK 2510. */
typedef struct { UINT32 counts[7]; UINT64 bytes[2]; } PriorityStats;
typedef struct {
    PriorityStats priority[4];
    UINT8 virtual_disk_counts[2];
    UINT32 counters[12], current_bytes, peak_bytes, error_count;
} ConsoleStats;
_Static_assert(offsetof(ConsoleStats, current_bytes) == 244 && sizeof(ConsoleStats) == 256, "stats layout");
static void STDMETHODCALLTYPE console_stats(Factory *f, ConsoleStats *stats)
{
    (void)f;
    if (!stats) return;
    ZeroMemory(stats, sizeof(*stats));
    AcquireSRWLockShared(&staging_lock);
    stats->current_bytes = staging ? STAGING_BYTES : 0;
    stats->peak_bytes = peak_staging;
    ReleaseSRWLockShared(&staging_lock);
}

static HRESULT STDMETHODCALLTYPE console_open(Factory *f, PCWSTR path, REFIID iid, void **out)
{
    (void)f;
    if (!out) return E_POINTER;
    *out = NULL;
    return iid && path ? E_NOTIMPL : E_POINTER;
}
/* Both unresolved scalar slots take this + one integer on Windows x64.
 * An HRESULT caller gets E_NOTIMPL; a void caller discards RAX. */
static HRESULT STDMETHODCALLTYPE unsupported_scalar(Factory *f, UINT64 arg)
{
    (void)f; (void)arg;
    return E_NOTIMPL;
}
typedef struct {
    HRESULT (STDMETHODCALLTYPE *query)(Factory *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *add_ref)(Factory *);
    ULONG (STDMETHODCALLTYPE *release)(Factory *);
    HRESULT (STDMETHODCALLTYPE *create_queue)(Factory *, const QueueDesc *, REFIID, void **);
    HRESULT (STDMETHODCALLTYPE *open_file)(Factory *, PCWSTR, REFIID, void **);
    HRESULT (STDMETHODCALLTYPE *create_status)(Factory *, UINT32, PCSTR, REFIID, void **);
    HRESULT (STDMETHODCALLTYPE *unsupported_scalar[2])(Factory *, UINT64);
    HRESULT (STDMETHODCALLTYPE *set_staging_size)(Factory *, UINT32);
    void (STDMETHODCALLTYPE *get_stats)(Factory *, ConsoleStats *);
    HRESULT (STDMETHODCALLTYPE *create_queue1)(Factory *, const QueueDesc *, REFIID, void **);
} ConsoleVtable;
_Static_assert(offsetof(ConsoleVtable, set_staging_size) == 0x40 &&
               offsetof(ConsoleVtable, create_queue1) == 0x50, "console x64 vtable");
static const ConsoleVtable console_vt = {factory_query, factory_add_ref, factory_release,
    create_queue, console_open, create_status, {unsupported_scalar, unsupported_scalar},
    console_staging, console_stats, create_queue};
static Factory console = {&console_vt};

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    if (reason == DLL_PROCESS_DETACH && !reserved && staging)
        VirtualFree(staging, 0, MEM_RELEASE);
    return TRUE;
}

__declspec(dllexport) HRESULT WINAPI DStorageGetFactory(REFIID iid, void **out)
{
    return factory_query(&singleton, iid, out);
}
