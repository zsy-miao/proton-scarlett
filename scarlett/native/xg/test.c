#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Independent call-site fixture: descriptor words as written by the title. */
__declspec(dllimport) HRESULT WINAPI XGSuggestSwizzleMode(const void *, uintptr_t, uint32_t *);
__declspec(dllimport) HRESULT WINAPI XGCreateTextureComputer(const void *, void **);

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

int main(void)
{
    _Alignas(8) uint32_t d[14] = {3, 0, 0, 0, 4096, 0, 4096, 0x10001, 71, 1};
    const unsigned formats[] = {71, 77, 80, 83};
    uint32_t mode = 0xdeadbeef, before[14];
    void *obj = d;
    for (unsigned i = 0; i < sizeof(formats)/sizeof(*formats); ++i) {
        d[8] = formats[i];
        d[2] = i % 2 ? 65536 : 0;
        d[11] = 0;
        memcpy(before, d, sizeof(d));
        CHECK(XGSuggestSwizzleMode(d, 0, &d[11]) == S_OK);
        before[11] = 0x109;
        CHECK(!memcmp(before, d, sizeof(d)));
    }
    d[11] = 0;
    CHECK(XGSuggestSwizzleMode(d, 0, &mode) == S_OK && mode == 0x109);
    mode = 0xdeadbeef;
    CHECK(XGSuggestSwizzleMode(d, 1, &mode) == E_NOTIMPL);
    /* Each mutation invalidates one field; refusal must not touch aliased output. */
    const unsigned fields[] = {0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    for (unsigned i = 0; i < sizeof(fields)/sizeof(*fields); ++i) {
        unsigned idx = fields[i];
        d[idx] ^= 1;
        memcpy(before, d, sizeof(d));
        CHECK(XGSuggestSwizzleMode(d, 0, &d[11]) == E_NOTIMPL);
        CHECK(!memcmp(before, d, sizeof(d)));
        d[idx] ^= 1;
    }
    CHECK(XGSuggestSwizzleMode(NULL, 0, &mode) == E_POINTER && mode == 0xdeadbeef);
    CHECK(XGSuggestSwizzleMode(d, 0, NULL) == E_POINTER);
    CHECK(XGCreateTextureComputer(d, &obj) == E_NOTIMPL && !obj);
    obj = d;
    CHECK(XGCreateTextureComputer(NULL, &obj) == E_POINTER && !obj);
    CHECK(XGCreateTextureComputer(d, NULL) == E_POINTER);
    puts("xg_xs synthetic import tests passed");
    return 0;
}
