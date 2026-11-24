#include <windows.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>

typedef struct { uint32_t process, streams, queued, length, frames, options; } Input;
typedef struct { void *base; HANDLE event; } Output;
__declspec(dllimport) HRESULT WINAPI XApuConnect(Input *, Output *, HANDLE *);
__declspec(dllimport) HRESULT WINAPI XApuDequeueResult(HANDLE, void *);
__declspec(dllimport) HRESULT WINAPI XApuDisconnect(HANDLE);
__declspec(dllimport) HRESULT WINAPI XApuEnqueueCommand(HANDLE, void *, uint32_t *);
__declspec(dllimport) uint32_t WINAPI XApuGetResultCount(HANDLE, uint32_t *);

int main(void)
{
    Input params = {1, 1, 1, 16384, 0, 0};
    Output data = {&params, INVALID_HANDLE_VALUE};
    HANDLE handle = INVALID_HANDLE_VALUE;
    uint32_t count = UINT32_MAX, marker = 17;
    assert(XApuConnect(&params, &data, &handle) == (HRESULT)0x8ac81004);
    assert(!handle && !data.base && !data.event);
    handle = INVALID_HANDLE_VALUE;
    assert(XApuConnect(&params, NULL, &handle) == E_INVALIDARG && !handle);
    assert(XApuDisconnect(INVALID_HANDLE_VALUE) == E_HANDLE);
    assert(XApuEnqueueCommand(NULL, &marker, &count) == E_HANDLE && !count);
    assert(XApuEnqueueCommand(NULL, &marker, NULL) == E_HANDLE);
    assert(XApuDequeueResult(NULL, &marker) == E_HANDLE && marker == 17);
    count = UINT32_MAX;
    assert(!XApuGetResultCount(INVALID_HANDLE_VALUE, &count) && !count);
    assert(!XApuGetResultCount(NULL, NULL));
    puts("xapu: no session, command, or result exposed");
    return 0;
}
