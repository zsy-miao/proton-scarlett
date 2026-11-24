/* White-box test: do not export inspection functions in the title-facing DLL. */
#include "d3d12_vm.c"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(int argc, char **argv)
{
    struct vm_table table = {0};
    uint32_t words[8] = {0};
    CHECK(argc == 2 && (!strcmp(argv[1], "3") || !strcmp(argv[1], "4")));
    uint32_t units = (uint32_t)(argv[1][0] - '0');
    CHECK(D3D12ConfigureVirtualMemoryX(NULL) == E_INVALIDARG);
    CHECK(D3D12ConfigureVirtualMemoryX((void *)1) == E_POINTER);
    CHECK(D3D12ConfigureVirtualMemoryX(words) == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
    words[1] = UINT32_MAX;
    CHECK(D3D12ConfigureVirtualMemoryX(words) == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
    words[1] = units;
    for (size_t i = 0; i < 8; ++i) {
        if (i == 1) continue;
        words[i] = 1;
        CHECK(D3D12ConfigureVirtualMemoryX(words) == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
        words[i] = 0;
    }
    CHECK(!graphics.entries && !graphics.count);

    CHECK(vm_init_table(&table, 0) == E_INVALIDARG);
    CHECK(vm_init_table(&table, VM_PAGE_BYTES + 1) == E_INVALIDARG);
    CHECK(vm_init_table(&table, UINT64_MAX - (VM_PAGE_BYTES - 1)) == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
    CHECK(!table.entries && !table.count);

    CHECK(D3D12ConfigureVirtualMemoryX(words) == S_OK);
    SIZE_T count = units == 3 ? 262144 : 393216;
    CHECK(graphics.entries && graphics.count == count);
    for (SIZE_T i = 0; i < count; ++i)
        CHECK(!graphics.entries[i]);
    graphics.entries[count - 1] = 42;
    CHECK(D3D12ConfigureVirtualMemoryX(words) == S_OK);
    words[1] = 7 - units;
    CHECK(D3D12ConfigureVirtualMemoryX(words) == HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED));
    CHECK(graphics.count == count && graphics.entries[count - 1] == 42);
    printf("PASS: word1=%u, %zu zero-initialized software VA entries; repeat preserved state\n", units, count);
    return 0;
}
