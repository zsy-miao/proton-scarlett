#include <windows.h>
#include <stdint.h>

#if !defined(__x86_64__) || !defined(_WIN64)
#error Windows x64 ABI required
#endif

typedef struct XApuConnectInputParameters XApuConnectInputParameters;
typedef struct XApuResult XApuResult;
typedef struct XApuCommandId XApuCommandId;
typedef struct XApuClient *XApuHandle;
typedef struct {
    void *baseData;
    HANDLE resultReadyEvent;
} XApuConnectOutputParameters;

__declspec(dllexport) HRESULT WINAPI XApuConnect(XApuConnectInputParameters *params,
        XApuConnectOutputParameters *data, XApuHandle *handle)
{
    if (data) *data = (XApuConnectOutputParameters){0};
    if (handle) *handle = NULL;
    if (!params || !data || !handle) return E_INVALIDARG;
    return (HRESULT)0x8ac81004; /* XAPU_E_NO_DEVICE_RESOURCES */
}

__declspec(dllexport) HRESULT WINAPI XApuDequeueResult(XApuHandle handle, XApuResult *result)
{
    (void)handle;
    return result ? E_HANDLE : E_INVALIDARG;
}

__declspec(dllexport) HRESULT WINAPI XApuDisconnect(XApuHandle handle)
{
    (void)handle;
    return E_HANDLE;
}

__declspec(dllexport) HRESULT WINAPI XApuEnqueueCommand(XApuHandle handle,
        XApuCommandId *command, uint32_t *total)
{
    (void)handle;
    if (total) *total = 0;
    return command ? E_HANDLE : E_INVALIDARG;
}

__declspec(dllexport) uint32_t WINAPI XApuGetResultCount(XApuHandle handle, uint32_t *total)
{
    (void)handle;
    if (total) *total = 0;
    return 0;
}
