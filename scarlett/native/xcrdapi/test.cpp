/* Build the consumer against the published Windows interfaces, not the DLL's declarations. */
#include <dstorage.h>
#include <cstdio>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main()
{
    IID factory_id{}, file_id{};
    CHECK(CLSIDFromString(FACTORY_GUID, &factory_id) == S_OK);
    CHECK(CLSIDFromString(FILE_GUID, &file_id) == S_OK);
    void *out = reinterpret_cast<void *>(1);
    CHECK(DStorageGetFactory(file_id, &out) == E_NOINTERFACE && !out);
    CHECK(DStorageGetFactory(factory_id, nullptr) == E_POINTER);
    CHECK(DStorageGetFactory(factory_id, &out) == S_OK && out);
    auto *factory = static_cast<IDStorageFactory *>(out);
    CHECK(DStorageGetFactory(IID_IUnknown, &out) == S_OK && out == factory);
    static_cast<IUnknown *>(out)->Release();
    CHECK(factory->QueryInterface(factory_id, &out) == S_OK && out == factory);
    static_cast<IDStorageFactory *>(out)->Release();

    DSTORAGE_QUEUE_DESC desc{};
    CHECK(factory->CreateQueue(&desc, IID_IUnknown, &out) == E_NOTIMPL && !out);
    CHECK(factory->CreateStatusArray(1, nullptr, IID_IUnknown, &out) == E_NOTIMPL && !out);
    factory->SetDebugFlags(0);
    CHECK(factory->SetStagingBufferSize(0) == E_NOTIMPL);

    const wchar_t path[] = L"C:\\xcrdapi-fixture.bin";
    CHECK(factory->OpenFile(path, file_id, &out) == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && !out);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    const char payload[] = "synthetic DirectStorage file";
    DWORD written;
    CHECK(WriteFile(h, payload, sizeof(payload), &written, nullptr) && written == sizeof(payload));
    CHECK(CloseHandle(h));
    CHECK(factory->OpenFile(path, factory_id, &out) == E_NOINTERFACE && !out);
    CHECK(factory->OpenFile(path, file_id, &out) == S_OK && out);
    auto *file = static_cast<IDStorageFile *>(out);
    CHECK(file->QueryInterface(IID_IUnknown, &out) == S_OK && out == file);
    CHECK(static_cast<IUnknown *>(out)->Release() == 1);
    BY_HANDLE_FILE_INFORMATION info{};
    CHECK(file->GetFileInformation(&info) == S_OK);
    CHECK(info.nFileSizeHigh == 0 && info.nFileSizeLow == written);
    CHECK(!DeleteFileW(path) && GetLastError() == ERROR_SHARING_VIOLATION);
    file->Close();
    file->Close();
    CHECK(file->GetFileInformation(&info) == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE));
    CHECK(file->Release() == 0);
    CHECK(factory->OpenFile(path, file_id, &out) == S_OK && out);
    CHECK(static_cast<IDStorageFile *>(out)->Release() == 0);
    CHECK(DeleteFileW(path));
    factory->Release();
    std::puts("PASS XCRDAPI.dll: factory identity, read-only file metadata/lifetime, explicit HRESULTs");
}
