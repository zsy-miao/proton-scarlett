# AcpHal.dll (x64, no ACP or APU physical memory)

The nine named exports follow the [AcpHal](https://docs.xbox.com/reference/audio/acphal/acphal_members)
and [APU](https://docs.xbox.com/reference/audio/apu/apu_members) references.
The caller stores `ApuMapVirtualAddress`'s return as a 32-bit field and passes
that field to `ApuMapApuAddress`; the validity test consumes a one-byte bool.

`ApuAlloc` requires device-mappable memory even when the caller omits the
physical address output. A host allocation cannot satisfy that contract.
Heap creation, allocation, SHAPE context allocation/release and ACP creation
return `HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)` for the missing backend.
This is a compatibility diagnostic, not a claimed console-specific HRESULT.
Allocation clears both supplied outputs; interface creation clears its output.
Mapping returns zero/null, validation returns false, and `ApuFree` never frees
a foreign block. There is no ACP interface or audio processing implementation.

Build and run the synthetic importer from this directory:

```sh
mkdir -p build
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -shared acphal.c \
  -Wl,--out-implib,build/libacphal.a -o build/AcpHal.dll
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror test.c -Lbuild -lacphal -o build/test.exe
WINEPREFIX="$PWD/build/prefix" WINEDLLOVERRIDES=acphal=n wine build/test.exe
```
