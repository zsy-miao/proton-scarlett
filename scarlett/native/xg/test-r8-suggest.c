/* Measure the layout recommendation without loading a game or copying a texture. */
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>

int wmain(int argc, wchar_t **argv)
{
    if (argc != 2) {
        fputs("usage: test-r8-suggest FULL_PATH_TO_XG_DLL\n", stderr);
        return 1;
    }
    HMODULE dll = LoadLibraryExW(argv[1], NULL,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) { fprintf(stderr, "LoadLibraryExW: %lu\n", GetLastError()); return 1; }
    typedef HRESULT (WINAPI *suggest_fn)(const D3D12_RESOURCE_DESC *, UINT, D3D12_TEXTURE_LAYOUT *);
    suggest_fn suggest = (suggest_fn)(void *)GetProcAddress(dll, "XGSuggestSwizzleMode");
    if (!suggest) { fputs("missing XGSuggestSwizzleMode\n", stderr); return 1; }
    D3D12_RESOURCE_DESC d = {.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Width=256, .Height=128, .DepthOrArraySize=1, .MipLevels=1,
        .Format=DXGI_FORMAT_R8_UNORM, .SampleDesc={1,0}, .Flags=0x300000};
    HRESULT hr = suggest(&d, 0, &d.Layout);
    printf("R8 256x128 flags=0x300000 option=0: hr=0x%08lx layout=0x%x\n",
            (unsigned long)hr, (unsigned)d.Layout);
    FreeLibrary(dll);
    return FAILED(hr) ? 2 : 0;
}
