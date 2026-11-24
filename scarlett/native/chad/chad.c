#define CHAD_BUILD
#include "chad.h"

_Static_assert(sizeof(void *) == 8, "Scarlett requires Windows x64");

static SRWLOCK lock = SRWLOCK_INIT;
static struct registration {
    XDspClientHandle id;
    float *base;
    uint32_t bytes, seconds, streams;
} clients[8];
static uintptr_t next_id = 1;

/* A registered arena is a single live, writable allocation owned by the caller. */
static BOOL valid_arena(float *base, uint32_t bytes)
{
    uintptr_t addr = (uintptr_t)base;
    if (!addr || (addr & 65535) || bytes < 65536 || bytes > 500u * 1024 * 1024 ||
        addr > UINTPTR_MAX - bytes) return FALSE;
    uintptr_t end = addr + bytes;
    MEMORY_BASIC_INFORMATION mbi;
    while (addr < end) {
        if (!VirtualQuery((void *)addr, &mbi, sizeof(mbi)) || mbi.AllocationBase != base ||
            mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) ||
            !(mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE |
                             PAGE_EXECUTE_WRITECOPY))) return FALSE;
        addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
    return TRUE;
}

static struct registration *lookup(XDspClientHandle id)
{
    for (unsigned i = 0; i < ARRAYSIZE(clients); ++i)
        if (clients[i].id == id) return &clients[i];
    return NULL;
}

CHAD XDspConnectWithMaximumStreamLimit(float *base, uint32_t bytes, uint32_t seconds,
                                      uint32_t streams, uint32_t channels, XDspClientHandle *out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!streams || streams > 256 || (channels != 1 && channels != 2) ||
        !valid_arena(base, bytes)) return E_INVALIDARG;
    if (channels != 1) return E_NOTIMPL;

    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&lock);
    uint32_t available = 128;
    for (unsigned i = 0; i < ARRAYSIZE(clients); ++i) available -= clients[i].seconds;
    struct registration *slot = lookup(NULL);
    if (!slot) hr = (HRESULT)0x8ac810a5; /* MAX_CLIENTS_EXCEEDED */
    else if (seconds > available) hr = (HRESULT)0x8ac810a6; /* MAX_AGGREGATE_IMPULSE_RESPONSE_EXCEEDED */
    else if (!next_id) hr = E_OUTOFMEMORY;
    else {
        /* A released handle must not identify a new client occupying the same slot. */
        *out = (XDspClientHandle)next_id++;
        *slot = (struct registration){ *out, base, bytes, seconds, streams };
    }
    ReleaseSRWLockExclusive(&lock);
    return hr;
}

CHAD XDspDisconnect(XDspClientHandle id)
{
    if (!id) return E_INVALIDARG;
    AcquireSRWLockExclusive(&lock);
    struct registration *slot = lookup(id);
    if (slot) *slot = (struct registration){0};
    ReleaseSRWLockExclusive(&lock);
    return slot ? S_OK : E_INVALIDARG;
}

CHAD XDspActivate(XDspClientHandle id, XDspActivationParameters *params,
                  XDspStatus **status, XDspStreamHandle *stream)
{
    if (status) *status = NULL;
    if (stream) *stream = NULL;
    if (!status || !stream || !params) return E_POINTER;
    AcquireSRWLockShared(&lock);
    HRESULT hr = id && lookup(id) ? E_NOTIMPL : E_INVALIDARG;
    ReleaseSRWLockShared(&lock);
    return hr;
}

/* No valid stream exists until Activate has a processing backend. */
CHAD XDspDeactivate(XDspStreamHandle stream)
{
    (void)stream;
    return E_INVALIDARG;
}

CHAD XDspSubmitCommand(XDspStreamHandle stream, XDspCommand *cmd, uint32_t *sequence)
{
    (void)stream;
    if (sequence) *sequence = 0;
    return cmd && sequence ? E_INVALIDARG : E_POINTER;
}
