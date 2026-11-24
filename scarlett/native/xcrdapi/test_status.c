/* A private producer fixture: no DLL export and no synthetic queue object. */
#include "xcrdapi.c"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void)
{
    void *out;
    CHECK(console_create_status(&console, 2, NULL, &status_iid, &out) == S_OK);
    Status *s = out;
    CHECK(status_begin(s, 1) == S_OK);
    HRESULT reads[] = { S_OK, E_ACCESSDENIED, E_PENDING, E_FAIL };
    CHECK(status_finish(s, 1, reads, 4) == E_PENDING);
    CHECK(!s->vt->is_complete(s, 1) && s->vt->get_hresult(s, 1) == E_PENDING);
    reads[2] = S_OK;
    CHECK(status_finish(s, 1, reads, 4) == S_OK);
    CHECK(s->vt->is_complete(s, 1) && s->vt->get_hresult(s, 1) == E_ACCESSDENIED);
    CHECK(status_begin(s, 1) == S_OK && !s->vt->is_complete(s, 1));
    CHECK(status_finish(s, 1, reads, 1) == S_OK);
    CHECK(s->vt->is_complete(s, 1) && s->vt->get_hresult(s, 1) == S_OK);
    CHECK(!s->vt->is_complete(s, 0) && s->vt->get_hresult(s, 0) == E_PENDING);
    CHECK(status_begin(s, 2) == E_INVALIDARG && status_finish(s, 2, NULL, 0) == E_INVALIDARG);
    CHECK(s->vt->release(s) == 0);
    puts("PASS private status writer: pending, first failure, reuse, success, independent entries");
    return 0;
}
