#include <stdint.h>
#include <windows.h>

/* Private software page table; an entry of zero means unmapped. */
enum { VM_PAGE_BYTES = 65536, VM_MAX_TABLE_BYTES = 3 * 1024 * 1024 };
struct vm_table {
    uint64_t *entries;
    SIZE_T count;
};
static struct vm_table graphics;
static SRWLOCK vm_lock = SRWLOCK_INIT;

/* Called with exclusive ownership. Never resize a table with live mappings. */
static HRESULT vm_init_table(struct vm_table *table, uint64_t span_bytes)
{
    uint64_t count = span_bytes / VM_PAGE_BYTES;
    if (!count || span_bytes % VM_PAGE_BYTES)
        return E_INVALIDARG;
    if (count > VM_MAX_TABLE_BYTES / sizeof(*table->entries))
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    if (table->entries)
        return count == table->count ? S_OK : HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    uint64_t *entries = VirtualAlloc(NULL, (SIZE_T)count * sizeof(*entries),
                                     MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!entries)
        return E_OUTOFMEMORY;
    table->entries = entries;
    table->count = (SIZE_T)count;
    return S_OK;
}

__declspec(dllexport) HRESULT WINAPI D3D12ConfigureVirtualMemoryX(const void *config)
{
    uint32_t words[8];
    SIZE_T copied;
    if (!config)
        return E_INVALIDARG;
    if (!ReadProcessMemory(GetCurrentProcess(), config, words, sizeof(words), &copied) ||
        copied != sizeof(words))
        return E_POINTER;
    if (words[1] != 3 && words[1] != 4)
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    for (unsigned i = 0; i < 8; ++i)
        if (i != 1 && words[i])
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);

    /* Only these two shapes have corresponding XMem arena sizes at the call site. */
    uint64_t arena_bytes = (words[1] == 3 ? UINT64_C(16) : UINT64_C(24)) << 30;
    AcquireSRWLockExclusive(&vm_lock);
    HRESULT hr = vm_init_table(&graphics, arena_bytes);
    ReleaseSRWLockExclusive(&vm_lock);
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    (void)instance;
    if (reason == DLL_PROCESS_DETACH && !reserved && graphics.entries)
        VirtualFree(graphics.entries, 0, MEM_RELEASE);
    return TRUE;
}
