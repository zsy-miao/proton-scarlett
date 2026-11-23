// SPDX-License-Identifier: MIT
#include <httpClient/httpClient.h>
#include <cstdio>
#include <cstring>

static_assert(sizeof(XAsyncBlock) == 56 && offsetof(XAsyncBlock, internal) == 24);

#define CHECK(expr) do { HRESULT hr = (expr); if (FAILED(hr)) { \
    std::fprintf(stderr, "%s: HRESULT %08lx\n", #expr, (unsigned long)hr); return 1; } } while (0)

int main(int argc, char** argv)
{
    if (argc < 3) return 2;
    HMODULE dll = GetModuleHandleW(L"libHttpClient.GDK.dll");
    for (int i = 2; i < argc; ++i) {
        if (!GetProcAddress(dll, argv[i])) {
            std::fprintf(stderr, "missing export: %s\n", argv[i]);
            return 1;
        }
    }
    CHECK(HCInitialize(nullptr));
    HCTraceSetEtwEnabled(true);
    XTaskQueueHandle queue{};
    CHECK(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue));
    XAsyncBlock perform{queue}, cleanup{queue};
    HCCallHandle call{};
    CHECK(HCHttpCallCreate(&call));
    CHECK(HCHttpCallRequestSetRetryAllowed(call, false));
    CHECK(HCHttpCallRequestSetUrl(call, "POST", argv[1]));
    CHECK(HCHttpCallRequestSetHeader(call, "X-Probe", "request", false));
    CHECK(HCHttpCallRequestSetRequestBodyString(call, "hello"));
    CHECK(HCHttpCallPerformAsync(call, &perform));
    CHECK(XAsyncGetStatus(&perform, true));

    HRESULT network{};
    uint32_t platform{}, status{};
    const char *header{}, *body{};
    CHECK(HCHttpCallResponseGetNetworkErrorCode(call, &network, &platform));
    CHECK(network);
    CHECK(HCHttpCallResponseGetStatusCode(call, &status));
    CHECK(HCHttpCallResponseGetHeader(call, "X-Probe", &header));
    CHECK(HCHttpCallResponseGetResponseString(call, &body));
    if (status != 201 || !header || std::strcmp(header, "response") || !body || std::strcmp(body, "echo:hello")) {
        std::fprintf(stderr, "unexpected HTTP response: %u, %s, %s\n", status, header ? header : "-", body ? body : "-");
        return 1;
    }
    CHECK(HCHttpCallCloseHandle(call));
    CHECK(HCCleanupAsync(&cleanup));
    CHECK(XAsyncGetStatus(&cleanup, true));
    XTaskQueueCloseHandle(queue);
    std::printf("%d exports; async POST, response and cleanup passed\n", argc - 2);
}
