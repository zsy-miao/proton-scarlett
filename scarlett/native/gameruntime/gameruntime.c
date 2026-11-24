#include "abi.h"
#include <stdio.h>
#include <winternl.h>
NTSYSAPI NTSTATUS NTAPI RtlGetVersion(PRTL_OSVERSIONINFOW);

static SRWLOCK lock = SRWLOCK_INIT;
static ULONG clients;
static bool terminal;
static HMODULE lifetime;
static Analytics analytics;
static ErrorCallback callback;
static void *context;
static uint32_t debug_options = 1, normal_options;

__declspec(dllexport) void WINAPI XErrorReport(HRESULT hr, const char *msg)
{
    BOOL debug = IsDebuggerPresent();
    AcquireSRWLockShared(&lock);
    ErrorCallback cb = callback;
    void *ctx = context;
    uint32_t options = debug ? debug_options : normal_options;
    ReleaseSRWLockShared(&lock);
    if (cb && !cb(hr, msg, ctx)) return;
    if (options & 3) {
        char prefix[80];
        snprintf(prefix, sizeof(prefix), "xgameruntime: hr=0x%08lx tid=%lu: ",
                 (unsigned long)hr, GetCurrentThreadId());
        OutputDebugStringA(prefix);
        OutputDebugStringA(msg ? msg : "");
        OutputDebugStringA("\n");
    }
    if (options & 2) DebugBreak();
    if (options & 4) RaiseFailFastException(NULL, NULL, 0);
}

static void WINAPI report(Api *a, HRESULT hr, const char *msg)
{
    (void)a;
    XErrorReport(hr, msg);
}
static void WINAPI set_callback(Api *a, ErrorCallback cb, void *ctx)
{
    (void)a;
    AcquireSRWLockExclusive(&lock);
    if (clients) { callback = cb; context = ctx; }
    ReleaseSRWLockExclusive(&lock);
}
static void WINAPI set_options(Api *a, uint32_t debug, uint32_t normal)
{
    (void)a;
    AcquireSRWLockExclusive(&lock);
    if (clients) { debug_options = debug; normal_options = normal; }
    ReleaseSRWLockExclusive(&lock);
}

static ULONG WINAPI add_ref(Api *a)
{
    AcquireSRWLockExclusive(&lock);
    ULONG n = ++a->refs;
    ReleaseSRWLockExclusive(&lock);
    return n;
}
static ULONG WINAPI release(Api *a)
{
    AcquireSRWLockExclusive(&lock);
    ULONG n = --a->refs;
    ReleaseSRWLockExclusive(&lock);
    return n;
}
static HRESULT WINAPI query(Api *a, const GUID *iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid) return E_POINTER;
    if (!IsEqualGUID(iid, a->id) && !IsEqualGUID(iid, &unknown_id)) return E_NOINTERFACE;
    add_ref(a);
    *out = a;
    return S_OK;
}
static bool WINAPI available(Api *a, uint32_t feature)
{
    (void)a;
    AcquireSRWLockShared(&lock);
    bool result = clients && feature == 19; /* XError; analytics alone is not XSystem. */
    ReleaseSRWLockShared(&lock);
    return result;
}
static Analytics *WINAPI get_analytics(Api *a, Analytics *out)
{
    (void)a;
    if (out) {
        AcquireSRWLockShared(&lock);
        *out = analytics;
        ReleaseSRWLockShared(&lock);
    }
    return out;
}

static const FeatureTable features = {{query, add_ref, release}, available};
static const ErrorTable errors = {{query, add_ref, release}, report, set_callback, set_options};
static const AnalyticsTable stats = {{query, add_ref, release}, get_analytics};
static Api apis[] = {{&features,&feature_id,0}, {&errors,&error_id,0}, {&stats,&analytics_id,0}};

__declspec(dllexport) HRESULT WINAPI QueryApiImpl(const GUID *provider, const GUID *iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!provider || !iid) return E_POINTER;
    AcquireSRWLockExclusive(&lock);
    HRESULT hr = clients ? REGDB_E_CLASSNOTREG : NOT_INITIALIZED;
    if (clients) for (size_t i = 0; i < ARRAYSIZE(apis); ++i) {
        Api *a = &apis[i];
        if (!IsEqualGUID(provider, a->id)) continue;
        if (IsEqualGUID(iid, a->id) || IsEqualGUID(iid, &unknown_id)) {
            ++a->refs;
            *out = a;
            hr = S_OK;
        } else hr = E_NOINTERFACE;
        break;
    }
    ReleaseSRWLockExclusive(&lock);
    return hr;
}

__declspec(dllexport) HRESULT WINAPI InitializeApiImplEx2(uint64_t sdk, uint64_t build,
                                                          uint64_t flags, const InitOptions *opt)
{
    if (sdk != SDK_VERSION || build != SDK_BUILD) return VERSION_MISMATCH;
    if ((flags & ~UINT64_C(15)) || !(flags & 2)) return E_INVALIDARG;
    if (opt && (opt->version != 1 || opt->flags || opt->game_config)) return UNSUPPORTED_OPTIONS;
    AcquireSRWLockExclusive(&lock);
    HRESULT hr = S_OK;
    if (terminal) hr = ACTIVE_OBJECTS;
    else if (flags & 5) hr = clients ? S_OK : NOT_INITIALIZED; /* implicit / validate only */
    else if (clients == MAXDWORD) hr = E_OUTOFMEMORY;
    else {
        if (!clients) {
            RTL_OSVERSIONINFOW os = { .dwOSVersionInfoSize = sizeof(os) };
            NTSTATUS status = RtlGetVersion(&os);
            if (status < 0) hr = HRESULT_FROM_NT(status);
            else if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                         (LPCWSTR)&lock, &lifetime))
                hr = HRESULT_FROM_WIN32(GetLastError());
            else {
                analytics.os = (Version){os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber, 0};
                analytics.hosting_os = analytics.os;
                strcpy(analytics.family, "Windows");
                strcpy(analytics.form, "PC");
            }
        }
        if (SUCCEEDED(hr)) ++clients;
    }
    ReleaseSRWLockExclusive(&lock);
    return hr;
}
__declspec(dllexport) HRESULT WINAPI InitializeApiImplEx(uint64_t sdk, uint64_t build, uint64_t flags)
{
    return InitializeApiImplEx2(sdk, build, flags, NULL);
}
__declspec(dllexport) HRESULT WINAPI InitializeApiImpl(uint64_t major, uint64_t minor)
{
    if (major != (SDK_VERSION >> 32) || minor != (uint32_t)SDK_VERSION) return VERSION_MISMATCH;
    return InitializeApiImplEx2(SDK_VERSION, SDK_BUILD, 2, NULL);
}
__declspec(dllexport) void WINAPI UninitializeApiImpl(void)
{
    HMODULE drop = NULL;
    bool leaked = false;
    AcquireSRWLockExclusive(&lock);
    if (clients && !--clients) {
        for (size_t i = 0; i < ARRAYSIZE(apis); ++i) leaked |= apis[i].refs != 0;
        terminal = leaked;
        if (!leaked) { drop = lifetime; lifetime = NULL; }
        analytics = (Analytics){0};
        if (!leaked) { callback = NULL; context = NULL; debug_options = 1; normal_options = 0; }
    }
    ReleaseSRWLockExclusive(&lock);
    if (leaked) {
        XErrorReport(ACTIVE_OBJECTS, "Interfaces still referenced at final uninitialize");
        AcquireSRWLockExclusive(&lock);
        callback = NULL; context = NULL; debug_options = 1; normal_options = 0;
        ReleaseSRWLockExclusive(&lock);
    }
    if (drop) FreeLibrary(drop);
}
