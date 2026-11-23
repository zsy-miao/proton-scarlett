/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Optional stdin mode: one DLL and one imported name (or #ordinal) per line.
 * Reads an import inventory only; never maps the application being assessed. */
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--imports")) {
        char dll[256], prev[256] = "", name[1024];
        unsigned total = 0, missing = 0;
        HMODULE mod = NULL;
        while (scanf("%255s %1023s", dll, name) == 2) {
            if (strcmp(prev, dll)) {
                strcpy(prev, dll);
                mod = LoadLibraryA(dll);
                if (!mod) printf("missing DLL %s (%lu)\n", dll, GetLastError());
            }
            ++total;
            if (!mod) ++missing;
            else if (!GetProcAddress(mod, name[0] == '#' ? MAKEINTRESOURCEA(atoi(name+1)) : name)) {
                printf("missing export %s!%s\n", dll, name);
                ++missing;
            }
        }
        printf("%u of %u imports unresolved\n", missing, total);
        return !total || missing != 0;
    }
    const char *names[] = {"xmem", "acphal", "chad", "xapu", "xcrdapi", "xg_xs", "d3d12_xs"};
    HMODULE mods[7];
    for (unsigned i = 0; i < 7; i++) {
        mods[i] = LoadLibraryA(names[i]);
        if (!mods[i]) {
            printf("load %s failed: %lu\n", names[i], GetLastError());
            return 1;
        }
        char path[MAX_PATH];
        if (!GetModuleFileNameA(mods[i], path, sizeof(path))) return 1;
        printf("loaded %s\n", path);
    }
    void *(WINAPI *alloc)(size_t, UINT64) = (void *)GetProcAddress(mods[0], "XMemAllocDefault");
    void (WINAPI *release)(void *, UINT64) = (void *)GetProcAddress(mods[0], "XMemFreeDefault");
    HRESULT (WINAPI *factory)(const GUID *, void **) = (void *)GetProcAddress(mods[4], "DStorageGetFactory");
    HRESULT (WINAPI *device)(void *, const void *, const GUID *, void **) =
        (void *)GetProcAddress(mods[6], "D3D12XboxCreateDevice");
    if (!alloc || !release || !factory || !device) return 1;
    volatile unsigned char *p = alloc(4096, 0);
    if (!p) return 1;
    p[4095] = 42;
    if (p[4095] != 42) return 1;
    release((void *)p, 0);
    HRESULT f = factory(NULL, NULL), d = device(NULL, NULL, NULL, NULL);
    printf("XMem write/read/free OK; factory(NULL)=%08lx device(NULL)=%08lx\n",
           (unsigned long)f, (unsigned long)d);
    if (f != E_POINTER || d != E_INVALIDARG) return 1;
    if (argc == 2 && !strcmp(argv[1], "--device")) {
        const GUID iid = {0x189819f1,0x1db6,0x4b57,{0xbe,0x54,0x18,0x21,0x33,0x9b,0x85,0xf7}};
        UINT params[18] = {0x20004, 0, ~0u, 0, 0, 0, ~0u, ~0u};
        void *obj = NULL;
        fflush(stdout);
        d = device(NULL, params, &iid, &obj);
        printf("D3D12XboxCreateDevice=%08lx object=%p\n", (unsigned long)d, obj);
        if (FAILED(d) || !obj) return 1;
        ULONG (WINAPI *drop)(void *) = (void *)((void ***)obj)[0][2];
        drop(obj);
    }
    return 0;
}
