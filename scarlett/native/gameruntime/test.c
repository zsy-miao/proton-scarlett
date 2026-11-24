#include "abi.h"
#include <assert.h>
#include <stdio.h>

static unsigned reports;
#ifdef TEST_INSTALLED_RUNTIME
#define DLL_SEARCH LOAD_LIBRARY_SEARCH_SYSTEM32
#else
#define DLL_SEARCH LOAD_LIBRARY_SEARCH_APPLICATION_DIR
#endif
static bool WINAPI capture(HRESULT hr, const char *msg, void *ctx)
{
    assert(ctx == &reports && msg && (hr == E_FAIL || hr == ACTIVE_OBJECTS));
    ++reports;
    return false;
}

int main(void)
{
    HMODULE dll = LoadLibraryExW(L"xgameruntime.dll", NULL, DLL_SEARCH);
    assert(dll);
#define RESOLVE(name) (__typeof__(name))(void *)GetProcAddress(dll, #name)
    HRESULT (WINAPI *InitializeApiImpl)(uint64_t,uint64_t) = RESOLVE(InitializeApiImpl);
    HRESULT (WINAPI *InitializeApiImplEx)(uint64_t,uint64_t,uint64_t) = RESOLVE(InitializeApiImplEx);
    HRESULT (WINAPI *InitializeApiImplEx2)(uint64_t,uint64_t,uint64_t,const InitOptions *) = RESOLVE(InitializeApiImplEx2);
    HRESULT (WINAPI *QueryApiImpl)(const GUID *,const GUID *,void **) = RESOLVE(QueryApiImpl);
    void (WINAPI *UninitializeApiImpl)(void) = RESOLVE(UninitializeApiImpl);
    void (WINAPI *XErrorReport)(HRESULT,const char *) = RESOLVE(XErrorReport);
    assert(InitializeApiImpl && InitializeApiImplEx && InitializeApiImplEx2 &&
           QueryApiImpl && UninitializeApiImpl && XErrorReport);
    void *p = (void *)1;
    assert(QueryApiImpl(&feature_id,&feature_id,&p) == NOT_INITIALIZED && !p);
    assert(InitializeApiImplEx(SDK_VERSION,0,2) == VERSION_MISMATCH);
    assert(InitializeApiImplEx(SDK_VERSION,SDK_BUILD,3) == NOT_INITIALIZED);
    InitOptions custom = {1,1,"<Game/>"}, defaults = {1,0,NULL};
    assert(InitializeApiImplEx2(SDK_VERSION,SDK_BUILD,2,&custom) == UNSUPPORTED_OPTIONS);
    assert(InitializeApiImplEx2(SDK_VERSION,SDK_BUILD,2,NULL) == S_OK);
    assert(InitializeApiImplEx2(SDK_VERSION,SDK_BUILD,6,&defaults) == S_OK);
    assert(InitializeApiImpl(0x6336,0x2712) == S_OK);
    UninitializeApiImpl();
    assert(QueryApiImpl(&unknown_id,&error_id,&p) == REGDB_E_CLASSNOTREG && !p);
    assert(QueryApiImpl(&feature_id,&error_id,&p) == E_NOINTERFACE && !p);
    assert(QueryApiImpl(&feature_id,&feature_id,&p) == S_OK);
    Api *a = p;
    const FeatureTable *f = a->vtable;
    assert(f->base.query(a,&unknown_id,&p) == S_OK && p == a);
    assert(f->base.release(p) == 1);
    assert(f->available(a,19) && !f->available(a,10) && !f->available(a,UINT32_MAX));
    assert(!f->base.release(a));
    assert(QueryApiImpl(&analytics_id,&analytics_id,&p) == S_OK);
    a = p;
    const AnalyticsTable *stats = a->vtable;
    Analytics result;
    assert(stats->get(a,&result) == &result && result.os.build && result.os.major &&
           !strcmp(result.family,"Windows") && !memcmp(&result.os,&result.hosting_os,sizeof(Version)));
    stats->base.release(a);
    assert(QueryApiImpl(&error_id,&error_id,&p) == S_OK);
    a = p;
    const ErrorTable *errors = a->vtable;
    errors->set_options(a,4,4);
    errors->set_callback(a,capture,&reports);
    XErrorReport(E_FAIL,"Synthetic callback vetoes fail-fast");
    errors->report(a,E_FAIL,"Error vtable method");
    assert(reports == 2);
    errors->base.release(a);
    UninitializeApiImpl();
    assert(QueryApiImpl(&analytics_id,&analytics_id,&p) == NOT_INITIALIZED && !p);
    assert(FreeLibrary(dll));
    assert(!GetModuleHandleW(L"xgameruntime.dll"));

    dll = LoadLibraryExW(L"xgameruntime.dll", NULL, DLL_SEARCH);
    assert(dll);
    InitializeApiImplEx = RESOLVE(InitializeApiImplEx);
    UninitializeApiImpl = RESOLVE(UninitializeApiImpl);
    QueryApiImpl = RESOLVE(QueryApiImpl);
    XErrorReport = RESOLVE(XErrorReport);
    assert(InitializeApiImplEx(SDK_VERSION,SDK_BUILD,10) == S_OK);
    XErrorReport(E_FAIL,"Defaults restored");
    assert(reports == 2);
    assert(QueryApiImpl(&error_id,&error_id,&p) == S_OK);
    a = p;
    errors = a->vtable;
    errors->set_callback(a,capture,&reports);
    UninitializeApiImpl();
    assert(reports == 3 && InitializeApiImplEx(SDK_VERSION,SDK_BUILD,2) == ACTIVE_OBJECTS);
    assert(FreeLibrary(dll) && GetModuleHandleW(L"xgameruntime.dll"));
    assert(!errors->base.release(a));
    puts("xgameruntime: six exports, init, features, callbacks, unload and leak checks passed");
    return 0;
}
