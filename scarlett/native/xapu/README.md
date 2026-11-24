# xapu.dll (x64, no XAPU device)

The five named exports match the executable's import table. Signatures and
connect output layout come from the [XAPU reference](https://docs.xbox.com/reference/audio/xapu/xapu_members).
The caller loads its session handle as a pointer-sized value.

`XApuConnect` clears the buffer, event and handle outputs and returns
[`XAPU_E_NO_DEVICE_RESOURCES`](https://docs.xbox.com/reference/audio/xapu/enums/xapuerrors)
when the required pointers are present. No stream can be created. Enqueue,
dequeue and disconnect reject handles; the count function returns zero and
clears its optional total. No codecs or event queues run.

Build and run the synthetic importer from this directory:

```sh
mkdir -p build
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -shared xapu.c \
  -Wl,--out-implib,build/libxapu.a -o build/xapu.dll
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror test.c -Lbuild -lxapu -o build/test.exe
WINEPREFIX="$PWD/build/prefix" WINEDLLOVERRIDES=xapu=n wine build/test.exe
```
