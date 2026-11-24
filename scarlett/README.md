# Native Scarlett runtime

Proton launches the title directly in Wine. Only this title's launch adds the tool's `scarlett/` DLL directory to `WINEPATH`. Wine supplies Win32, and vkd3d-proton supplies desktop D3D12. The fork implements the Scarlett-facing interfaces. No game files are used to build the runtime.

```bash
./scarlett/build-runtime.sh "$PWD/scarlett/runtime"  # choose a new directory
steam="$HOME/.local/share/Steam"
./scarlett/install.sh "$steam/steamapps/common/Proton 11.0" "$PWD/scarlett/runtime" \
    "$steam/compatibilitytools.d/scarlett-proton"
```

Requires x86-64 MinGW GCC and the matching Proton 11.0-2e depot. Move any previous installation aside before installing. For a full SDK build, stage the DLLs in `scarlett/runtime/`, initialize Valve's submodules and use the [upstream build instructions](../README.md). In Steam, select **scarlett-proton** in the shortcut's compatibility properties.

## What the first graphics test covers

The runtime also includes an original [xmem.dll](native/xmem/README.md) supporting ordinary
CPU pages and title allocation hooks. The other providers cover [DSP registration](native/chad/README.md),
[audio device discovery](native/xapu/README.md), [APU memory interfaces](native/acphal/README.md),
[texture layout suggestions](native/xg/README.md), and [read-only storage files](native/xcrdapi/README.md).
Each provider lists the operations it supports and how unsupported requests fail.
Run `./scarlett/tests/native-providers.sh` to exercise the installed DLLs through Proton
with synthetic callers (including an EXE exporting its own allocation hooks).

`./scarlett/tests/queue-fence.sh` builds a synthetic PE importing `d3d12_xs.dll`, then exercises a desktop-compatible D3D12 device, command queue, CPU/GPU signaling and completion. It runs in an isolated Wine prefix on the NixOS/NVIDIA test host. `D3D12XboxCreateDevice`, console-specific COM methods and Xbox shaders are separate work; this test does not boot a retail game. `SCARLETT_RECORD=1` enables recording of the private test display.
