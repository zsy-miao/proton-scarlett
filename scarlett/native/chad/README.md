# chad.dll — XDSP client registration

Original x64 Win32 implementation of the five XDSP names imported by title build
`1.0.2.1`. No Xbox SDK or third-party console runtime is needed.

| Export | Behavior |
| --- | --- |
| `XDspConnectWithMaximumStreamLimit` | Registers a caller-owned mono arena and reserves a client slot and impulse-response budget. Returns an opaque handle. Enforces eight clients and 128 aggregate seconds per process; accepts 1–256 maximum streams. |
| `XDspDisconnect` | Releases the registration and its quota. Rejects unknown or previously closed handles. Does not free caller memory. |
| `XDspActivate` | A live client with non-null arguments gets `E_NOTIMPL`. Returns no stream handle or status block. |
| `XDspSubmitCommand`, `XDspDeactivate` | Reject stream handles with `E_INVALIDARG`; no stream has been activated. Submit produces no completion and clears a supplied sequence number. |

The arena must start at the base of a 64 KiB-aligned committed writable Win32
allocation, 64 KiB to 500 MiB of accessible memory. The caller must keep it
live through disconnect. This is a CPU registration; it does not pin physical
pages. Stereo registration is unsupported. There is no convolution or FFT/IFFT
backend. In particular, the [FFT walkthrough](https://docs.xbox.com/build/console-features/audio/overviews/xdsp-overview-single-stream-fft-ifft)
describes N-float buffers, but does not specify DC/Nyquist packing or transform
normalization. Verify those against a reference before implementing the FFT.

**ABI evidence:** Microsoft's [function, struct and handle index](https://docs.xbox.com/reference/audio/xdspaudio/xdspaudio_members),
[connection requirements](https://docs.xbox.com/reference/audio/xdspaudio/functions/xdspconnectwithmaximumstreamlimit)
and [XDSP errors](https://docs.xbox.com/reference/audio/xdspaudio/enums/xdsperrors).
Static checks in build `1.0.2.1`: RVA `0x5b5262e` calls connect with a 128-stream
limit, one channel and a sixth handle-out argument; `0x43b3966` passes activation
data for a mono 1024-frame forward FFT; `0x4e5b20d` checks submit's HRESULT.
Imports are by name; no ordinal contract is assumed.

## Build and test

From this directory, using x86-64 MinGW-w64:

```sh
mkdir -p build
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -shared chad.c \
    -Wl,--out-implib,build/libchad.a -o build/chad.dll
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror test.c -Lbuild -lchad \
    -o build/game_scarlett_master.exe
```

The EXE above is the synthetic test, named for the project's isolated NixOS
Proton harness. Run `../../test-launch-nixos.sh "$PWD/build"` with the fork
installed, or `WINEPREFIX="$PWD/build/prefix" wine build/game_scarlett_master.exe`.
The test imports the adjacent DLL, exercises successful registration and
release, and checks bad memory, quota exhaustion, stale handles and stream
errors. It does not require a game installation.
