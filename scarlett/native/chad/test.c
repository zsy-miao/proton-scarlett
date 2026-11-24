#include "chad.h"
#include <stdio.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void)
{
    const uint32_t bytes = 65536;
    float *arena = VirtualAlloc(NULL, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(arena);
    XDspClientHandle client = (void *)1, ids[8];
    #define connect(b,n,s,m,c,h) XDspConnectWithMaximumStreamLimit(b,n,s,m,c,h)
    CHECK(connect(arena, bytes - 1, 0, 128, 1, &client) == E_INVALIDARG && !client);
    CHECK(connect(arena + 4, bytes, 0, 128, 1, &client) == E_INVALIDARG);
    CHECK(connect(arena, bytes + 4096, 0, 128, 1, &client) == E_INVALIDARG);
    CHECK(connect(arena, bytes, 0, 257, 1, &client) == E_INVALIDARG);
    CHECK(connect(arena, bytes, 0, 128, 3, &client) == E_INVALIDARG);
    CHECK(connect(arena, bytes, 0, 128, 2, &client) == E_NOTIMPL);
    CHECK(connect(arena, bytes, 0, 128, 1, NULL) == E_POINTER);
    DWORD old;
    CHECK(VirtualProtect((char *)arena + bytes - 4096, 4096, PAGE_READONLY, &old));
    CHECK(connect(arena, bytes, 0, 128, 1, &client) == E_INVALIDARG);
    CHECK(VirtualProtect((char *)arena + bytes - 4096, 4096, old, &old));

    CHECK(connect(arena, bytes, 128, 128, 1, &ids[0]) == S_OK && ids[0]);
    CHECK(connect(arena, bytes, 1, 1, 1, &client) == (HRESULT)0x8ac810a6 && !client);
    for (unsigned i = 1; i < 8; ++i)
        CHECK(connect(arena, bytes, 0, 1, 1, &ids[i]) == S_OK && ids[i]);
    CHECK(connect(arena, bytes, 0, 1, 1, &client) == (HRESULT)0x8ac810a5 && !client);
    CHECK(XDspDisconnect(ids[0]) == S_OK);
    CHECK(connect(arena, bytes, 128, 128, 1, &client) == S_OK && client != ids[0]);
    CHECK(XDspDisconnect(ids[0]) == E_INVALIDARG);

    /* Title's mono forward FFT, followed by two zero IR fields. */
    _Alignas(16) uint32_t params[8] = {2, 0, 1024, 1};
    XDspStreamHandle stream = (void *)1;
    XDspStatus *status = (void *)1;
    CHECK(XDspActivate(client, (void *)params, &status, &stream) == E_NOTIMPL && !stream && !status);
    CHECK(XDspActivate(ids[0], (void *)params, &status, &stream) == E_INVALIDARG);
    CHECK(XDspActivate(client, NULL, &status, &stream) == E_POINTER);
    uint32_t sequence = 123;
    CHECK(XDspSubmitCommand((void *)client, (void *)params, &sequence) == E_INVALIDARG && sequence == 0);
    CHECK(XDspSubmitCommand(NULL, NULL, NULL) == E_POINTER);
    CHECK(XDspDeactivate((void *)client) == E_INVALIDARG);
    CHECK(XDspDisconnect(client) == S_OK);
    for (unsigned i = 1; i < 8; ++i) CHECK(XDspDisconnect(ids[i]) == S_OK);
    CHECK(VirtualFree(arena, 0, MEM_RELEASE));
    puts("PASS chad: registration, quotas, handle lifetime, unsupported streams");
    return 0;
}
