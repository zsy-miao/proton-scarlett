/* Raw x64 virtual calls use only the byte offsets recorded in ABI.md. */
#include <windows.h>
#include <unknwn.h>
#include <dstorage_win32.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
extern "C" HRESULT WINAPI DStorageGetFactory(REFIID, void **);

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
template<typename Fn> static Fn slot(void *object, size_t offset)
{
    return reinterpret_cast<Fn>((*static_cast<void ***>(object))[offset / 8]);
}
constexpr DWORD bytes = 0x02000000;
static unsigned staging_sized_allocations()
{
    unsigned count = 0;
    MEMORY_BASIC_INFORMATION m{};
    for (uintptr_t address = 0; VirtualQuery(reinterpret_cast<void *>(address), &m, sizeof(m));) {
        if (m.State == MEM_COMMIT && m.Type == MEM_PRIVATE && m.Protect == PAGE_READWRITE &&
            m.AllocationBase == m.BaseAddress && m.RegionSize == bytes) ++count;
        address = reinterpret_cast<uintptr_t>(m.BaseAddress) + m.RegionSize;
    }
    return count;
}
int main()
{
    IID factory{}, file{}, status{}, queue{};
    CHECK(SUCCEEDED(CLSIDFromString(L"{62959d11-d45f-4884-a0a3-f7862ea243ae}", &factory)));
    CHECK(SUCCEEDED(CLSIDFromString(L"{43704f9f-27c6-4a9f-8815-d16527894df4}", &file)));
    CHECK(SUCCEEDED(CLSIDFromString(L"{cd66d752-2786-44f7-9070-e2a6fdad3f1b}", &status)));
    CHECK(SUCCEEDED(CLSIDFromString(L"{3c02cb0c-c050-4195-99a1-a1ce94ba33c5}", &queue)));
    void *f = nullptr, *out = nullptr;
    CHECK(DStorageGetFactory(factory, &f) == S_OK && f);
    using SetSize = HRESULT (WINAPI *)(void *, UINT32);
    auto set_size = slot<SetSize>(f, 0x40);
    auto before = staging_sized_allocations();
    CHECK(set_size(f, bytes) == S_OK && staging_sized_allocations() == before + 1);
    CHECK(set_size(f, bytes) == S_OK && staging_sized_allocations() == before + 1);
    CHECK(set_size(f, 1) == E_NOTIMPL);
    using GetStats = void (WINAPI *)(void *, void *);
    alignas(8) UINT32 stats[65] = {};
    stats[64] = 0xdecafbad;
    auto get_stats = slot<GetStats>(f, 0x48);
    get_stats(f, stats);
    CHECK(stats[61] == bytes && stats[62] == bytes && stats[64] == 0xdecafbad);
    CHECK(set_size(f, 0) == S_OK && staging_sized_allocations() == before);
    get_stats(f, stats);
    CHECK(!stats[61] && stats[62] == bytes && stats[64] == 0xdecafbad);
    using Open = HRESULT (WINAPI *)(void *, PCWSTR, REFIID, void **);
    out = f;
    const wchar_t path[] = L"C:\\xcrdapi-console-\u2603.bin";
    auto open = slot<Open>(f, 0x20);
    CHECK(open(f, path, file, &out) == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && !out);
    HANDLE writer = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    CHECK(writer != INVALID_HANDLE_VALUE);
    const char data[] = "console file payload";
    DWORD n;
    CHECK(WriteFile(writer, data, sizeof(data), &n, nullptr) && n == sizeof(data));
    CHECK(open(f, path, file, &out) == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) && !out);
    CHECK(CloseHandle(writer));
    CHECK(open(f, path, factory, &out) == E_NOINTERFACE && !out);
    CHECK(open(f, path, file, &out) == S_OK && out);
    auto *object = static_cast<IDStorageFileWin32 *>(out); // published declaration, matching IID
    CHECK(object->QueryInterface(file, &out) == S_OK && out == object);
    CHECK(static_cast<IUnknown *>(out)->Release() == 1);
    CHECK(object->QueryInterface(IID_IUnknown, &out) == S_OK && out == object);
    CHECK(static_cast<IUnknown *>(out)->Release() == 1);
    CHECK(object->QueryInterface(factory, &out) == E_NOINTERFACE && !out);
    CHECK(set_size(f, bytes) == HRESULT_FROM_WIN32(ERROR_BUSY));
    get_stats(f, stats);
    CHECK(stats[58] == 1 && stats[59] == 1 && stats[60] == 1);
    HANDLE h1 = object->GetHandle(), h2 = object->GetHandle();
    CHECK(h1 != INVALID_HANDLE_VALUE && h2 != INVALID_HANDLE_VALUE && h1 != h2);
    CHECK(!WriteFile(h1, data, sizeof(data), &n, nullptr) && GetLastError() == ERROR_ACCESS_DENIED);
    CHECK(CloseHandle(h1));
    object->Close();
    object->Close();
    CHECK(object->GetHandle() == INVALID_HANDLE_VALUE && GetLastError() == ERROR_INVALID_HANDLE);
    CHECK(set_size(f, 0) == HRESULT_FROM_WIN32(ERROR_BUSY));
    CHECK(object->Release() == 0);
    CHECK(set_size(f, 0) == S_OK);
    char buf[sizeof(data)] = {};
    CHECK(ReadFile(h2, buf, sizeof(buf), &n, nullptr) && n == sizeof(buf) && !std::memcmp(buf, data, n));
    CHECK(!DeleteFileW(path) && GetLastError() == ERROR_SHARING_VIOLATION);
    CHECK(CloseHandle(h2));
    CHECK(open(f, path, IID_IUnknown, &out) == S_OK && out);
    CHECK(static_cast<IUnknown *>(out)->Release() == 0);
    CHECK(DeleteFileW(path));
    get_stats(f, stats);
    CHECK(stats[58] == 0 && stats[59] == 1 && stats[60] == 2);
    using Status = HRESULT (WINAPI *)(void *, UINT32, PCSTR, REFIID, void **);
    auto make_status = slot<Status>(f, 0x28);
    out = f;
    CHECK(make_status(f, 1, "", file, &out) == E_NOINTERFACE && !out);
    CHECK(make_status(f, 0, "", status, &out) == E_INVALIDARG && !out);
    CHECK(make_status(f, UINT32_MAX, "", status, &out) == E_NOTIMPL && !out);
    CHECK(make_status(f, 1, "", status, nullptr) == E_POINTER);
    CHECK(make_status(f, 1, "", status, &out) == S_OK && out);
    auto *array = static_cast<IUnknown *>(out);
    CHECK(array->QueryInterface(IID_IUnknown, &out) == S_OK && out == array);
    CHECK(static_cast<IUnknown *>(out)->Release() == 1);
    CHECK(array->QueryInterface(status, &out) == S_OK && out == array);
    CHECK(static_cast<IUnknown *>(out)->Release() == 1);
    CHECK(array->QueryInterface(queue, &out) == E_NOINTERFACE && !out);
    using Complete = bool (WINAPI *)(void *, UINT32);
    using Result = HRESULT (WINAPI *)(void *, UINT32);
    CHECK(!slot<Complete>(array, 0x18)(array, 0) && slot<Result>(array, 0x20)(array, 0) == E_PENDING);
    CHECK(!slot<Complete>(array, 0x18)(array, 1) && slot<Result>(array, 0x20)(array, 1) == E_INVALIDARG);
    CHECK(array->Release() == 0);
    using Queue = HRESULT (WINAPI *)(void *, const void *, REFIID, void **);
    UINT64 desc[4] = {};
    out = f;
    CHECK(slot<Queue>(f, 0x50)(f, desc, queue, &out) == E_NOTIMPL && !out);
    auto *fac = static_cast<IUnknown *>(f);
    CHECK(fac->QueryInterface(IID_IUnknown, &out) == S_OK && out != f);
    auto *identity = static_cast<IUnknown *>(out);
    CHECK(identity->QueryInterface(factory, &out) == S_OK && out == f);
    static_cast<IUnknown *>(out)->Release();
    identity->Release();
    fac->Release();
    std::puts("PASS console factory: staging, file ABI/read-only handle lifetime, status array, queue refusal");
}
