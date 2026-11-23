# TomBox Scarlett providers

Seven x64 Windows DLLs, built from [omonkeyz/xbox-emulator at f89e4106](https://github.com/omonkeyz/xbox-emulator/tree/f89e4106fe8a1728515afa34ceb5097132cadcda), a continuation of [drycheetah84/TomBox](https://github.com/drycheetah84/TomBox). Neither repository had a published GitHub release on 2026-11-23. This is a release-profile **source build**. No title, retail DLL, Windows SDK, or Xbox SDK is an input.

## Rebuild

On Linux x86-64, install rustup, a host C compiler, x86-64 MinGW-w64 GCC/binutils, bash, curl, GNU tar, xz and coreutils. Then run:

```sh
rustup toolchain install 1.95.0 --profile minimal --target x86_64-pc-windows-gnu
./acquire.sh /tmp/opencode/tombox-provider
```

The script prints its fresh `build.*/bin` directory. It verifies SHA-256 before unpacking the TomBox source and the [Linux DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.8.2505.1) used to compile TomBox's own query shader. On MinGW variants missing static `libpthread.a`, it also verifies a Debian MinGW archive and extracts only that library. Cargo uses the pinned upstream lockfile. The matching source and notices stay in the build directory.

`SHA256SUMS.tested` records the seven stripped provider DLLs from this host (Rust 1.95.0, MinGW-w64 GCC 16.2.0, binutils 2.46, Debian winpthreads fallback); two independent clean builds matched byte for byte. Other toolchains may produce different binaries. To check the tested set, run `sha256sum -c /absolute/path/to/SHA256SUMS.tested` in the output directory.

## Use with Wine

`BIN` below is the acquisition script's printed path. On the test host `/tmp/opencode/tombox-provider/bin` points to the verified build.

```sh
BIN=/tmp/opencode/tombox-provider/bin
export WINEPREFIX=/tmp/opencode/tombox-provider/prefix
export WINEPATH="$(winepath -w "$BIN")"
export WINEDLLOVERRIDES='xmem,acphal,chad,xapu,xcrdapi,xg_xs,d3d12_xs=n'
wine "$BIN/probe.exe"
wine "$BIN/probe.exe" --device
```

The same `WINEPATH` and overrides work for a separate test executable, without copying providers into `system32` or the test executable directory. Do not override desktop `d3d12.dll`: the Scarlett provider calls it. TomBox installs process-local hooks, including GDK and loader hooks. Use a dedicated prefix; normal Wine import resolution in another application or Proton version can differ.

## Measured results (Wine 11.0)

* All seven `pei-x86-64` modules loaded from the output directory. The smoke probe allocated, wrote, read and freed 4 KiB through XMem; DirectStorage and D3D12.X rejected invalid arguments with the expected `E_POINTER` and `E_INVALIDARG`. Exit 0.
* With non-null creation parameters and the `ID3D12Device` IID, the provider loaded desktop `d3d12.dll`, but host device creation returned `0x80070057` and a null object. `--device` exited 1. No rendering or presentation was demonstrated.
* For the supplied EXE, a standalone probe asked `LoadLibrary`/`GetProcAddress` for its static imports, including ordinals. The EXE was never loaded. **514/587** resolved across 68 requested DLL names; **36/37** resolved for the seven providers. These are name-resolution results, including Wine built-ins; they do not establish API/ABI behavior.

| Remaining direct dependency in this prefix | Unresolved imports |
|---|---:|
| `libHttpClient.GDK.dll` | 38 |
| `Bink2Scarlett.dll` | 23 |
| `XboxBvhBuilder.dll` | 7 |
| `amd_fidelityfx_loader_xbox.dll` | 4 |
| `xg_xs.dll!XGSuggestSwizzleMode` | 1 |

To repeat the inventory with GNU binutils 2.46, set `PE` to the input file. This outputs names/ordinals only; the probe receives no game data:

```sh
LC_ALL=C objdump -p "$PE" | awk '
    /DLL Name:/ {d=$3; next}
    $1~/^[[:xdigit:]]{8}$/ && $2=="<none>" && NF>=4 {print d,$4}
    $1~/^[[:xdigit:]]{8}$/ && $3=="<none>" {print d,"#"$2}
' | wine "$BIN/probe.exe" --imports
```

**Provider dependencies:** The union of their static PE imports is `advapi32`, `api-ms-win-core-synch-l1-2-0`, `bcrypt`, `bcryptprimitives`, `crypt32`, `dwmapi`, `gdi32`, `iphlpapi`, `kernel32`, `kernelbase`, `msvcrt`, `ntdll`, `ole32`, `shell32`, `shlwapi`, `user32`, `userenv`, `winhttp`, `winmm`, `ws2_32` (all `.dll`). The clean Wine 11.0 prefix supplied all of them. Runtime graphics additionally needs a working desktop D3D12 implementation and driver. TomBox's [runtime shader compiler](https://github.com/omonkeyz/xbox-emulator/blob/f89e4106fe8a1728515afa34ceb5097132cadcda/crates/tombox-xshader/src/dxc.rs) looks for **Windows x64** `dxcompiler.dll` and `dxil.dll`, e.g. in `%TOMBOX_DXC_DIR%`; the build-time Linux DXC cannot satisfy this. No runtime compiler, middleware, game assets, or title-dependent dynamic dependencies were tested.

## License and attribution

TomBox at this pin declares **GPL-3.0-or-later**. The acquisition script and probe use the same license; see `COPYING`. The output contains upstream `COPYING.TomBox`, `CREDITS.TomBox`, and available third-party license texts. See the [pinned upstream credits](https://github.com/omonkeyz/xbox-emulator/blob/f89e4106fe8a1728515afa34ceb5097132cadcda/THIRD_PARTY/CREDITS), including the provenance notes about older TomBox forks. If redistributing binaries, retain applicable notices and provide the corresponding source and build instructions under the license terms. No DLLs are vendored in this repository.
