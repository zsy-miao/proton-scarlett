#pragma once
#include <windows.h>
#include <stdint.h>

typedef struct XDspClient *XDspClientHandle;
typedef struct XDspStream *XDspStreamHandle;
typedef struct XDspActivationParameters XDspActivationParameters;
typedef struct XDspCommand XDspCommand;
typedef struct XDspStatus XDspStatus;

#ifdef CHAD_BUILD
#define CHAD __declspec(dllexport) HRESULT WINAPI
#else
#define CHAD __declspec(dllimport) HRESULT WINAPI
#endif

CHAD XDspConnectWithMaximumStreamLimit(float *, uint32_t, uint32_t, uint32_t,
                                      uint32_t, XDspClientHandle *);
CHAD XDspDisconnect(XDspClientHandle);
CHAD XDspActivate(XDspClientHandle, XDspActivationParameters *, XDspStatus **, XDspStreamHandle *);
CHAD XDspDeactivate(XDspStreamHandle);
CHAD XDspSubmitCommand(XDspStreamHandle, XDspCommand *, uint32_t *);
