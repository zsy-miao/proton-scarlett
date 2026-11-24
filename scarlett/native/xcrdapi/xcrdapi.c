/* Windows x64 DirectStorage factory/file ABI; see README for the header and test. */
#include <windows.h>
#include <unknwn.h>

static const IID factory_iid = {0x6924ea0c,0xc3cd,0x4826,{0xb1,0x0a,0xf6,0x4f,0x4e,0xd9,0x27,0xc1}};
static const IID file_iid = {0x5de95e7b,0x955a,0x4868,{0xa7,0x3c,0x24,0x3b,0x29,0xf4,0xb8,0xda}};

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
struct Factory { const FactoryVtable *vt; LONG refs; };
struct File { const FileVtable *vt; LONG refs; SRWLOCK lock; HANDLE handle; };

static HRESULT check_interface(REFIID requested, REFIID supported, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!requested) return E_POINTER;
    return IsEqualIID(requested, supported) || IsEqualIID(requested, &IID_IUnknown)
        ? S_OK : E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_add_ref(Factory *f) { return InterlockedIncrement(&f->refs); }
static ULONG STDMETHODCALLTYPE factory_release(Factory *f) { return InterlockedDecrement(&f->refs); }
static HRESULT STDMETHODCALLTYPE factory_query(Factory *f, REFIID iid, void **out)
{
    HRESULT hr = check_interface(iid, &factory_iid, out);
    if (FAILED(hr)) return hr;
    factory_add_ref(f);
    *out = f;
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
static Factory singleton = {&factory_vt, 1}; /* permanent factory reference */

__declspec(dllexport) HRESULT WINAPI DStorageGetFactory(REFIID iid, void **out)
{
    return factory_query(&singleton, iid, out);
}
