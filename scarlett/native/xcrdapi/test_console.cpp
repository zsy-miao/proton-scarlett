/* Raw x64 virtual calls use only the byte offsets recorded in ABI.md. */
#include <windows.h>
#include <unknwn.h>
#include <cstdio>
#include <cstdint>
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
    CHECK(slot<Open>(f, 0x20)(f, L"C:\\fixture", file, &out) == E_NOTIMPL && !out);
    using Status = HRESULT (WINAPI *)(void *, UINT32, PCSTR, REFIID, void **);
    out = f;
    CHECK(slot<Status>(f, 0x28)(f, 1, "", status, &out) == E_NOTIMPL && !out);
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
    std::puts("PASS console factory: observed IID/slots, 32 MiB committed and freed, output/error contracts");
}
