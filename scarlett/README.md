# Native Scarlett runtime

Proton launches the title directly in Wine. Only this title's launch adds the tool's `scarlett/` DLL directory to `WINEPATH`. Wine supplies Win32, and vkd3d-proton supplies desktop D3D12. The fork implements the Scarlett-facing interfaces. No game files are used to build the runtime.

The title uses `scarlett-title/pfx` under its Steam compatibility-data directory. Its system32 links to the tool's GameInput provider. Existing regular prefixes are preserved; saves or settings in them are not automatically migrated.

```bash
export SCARLETT_GAMEINPUT_HEADER="/path/to/Windows Kits/10/Include/10.0.26100/um/GameInput.h"
./scarlett/build-runtime.sh "$PWD/scarlett/runtime"  # choose a new directory
# For retail graphics commits, stage the patched ntdll.so here before installing (link below).
steam="$HOME/.local/share/Steam"
./scarlett/install.sh "$steam/steamapps/common/Proton 11.0" "$PWD/scarlett/runtime" \
    "$steam/compatibilitytools.d/scarlett-proton"
```

Requires x86-64 MinGW GCC/G++, the [Windows SDK v0 input header](native/gameinput/README.md), and the matching Proton 11.0-2e depot. Move any previous installation aside before installing. For a full SDK build, stage the DLLs in `scarlett/runtime/`, initialize Valve's submodules and use the [upstream build instructions](../README.md). In Steam, select **scarlett-proton** in the shortcut's compatibility properties.

The first committed retail graphics pool requires the fork's [x64 Wine memory patch and build steps](wine-patches/README.md). Installing the PE DLLs alone leaves this call unsupported.
The 2,199 MiB pool additionally requires the [paired Wine and vkd3d-proton patches](vkd3d-patches/README.md).
Retail `G:` maps to the game directory. `T:` maps to `scarlett-title/scratch` under the shortcut's compatibility-data directory, where the game can save its crash reports. In the test harness both the reports and the prefix stay in the individual run directory.
Run `./scarlett/tests/graphics-pools.sh` on the qualified RTX 3070 host to exercise all three pool sizes through the installed tool, including GPU completion after the title address is reused.

The NixOS harness defaults to NVIDIA. To run the queue and fence test on the host's AMD adapter instead, use `SCARLETT_TEST_GPU=radv ./scarlett/tests/queue-fence.sh`. The Proton log identifies the selected adapter and driver.

`tests/present-capture.cpp` (under `scarlett/`) presents red and green DXGI back buffers. The private Xvfb recorder captured both colors on NVIDIA. With RADV 26.3.0, creating the swapchain on Xvfb failed because the surface did not support presentation. Offscreen GPU tests do not check this display path.

For RADV presentation, set `SCARLETT_TEST_GPU=radv SCARLETT_TEST_DISPLAY=sway` when invoking `test-launch-nixos.sh`. This starts headless Sway and Xwayland in the same isolated sandbox with only the AMD render node. Requires NixOS `sway` and `Xwayland` on PATH and exactly one AMD render node. `SCARLETT_RECORD=1` samples the compositor with `grim` and saves `display.mp4` and `display.png` using `ffmpeg`; the MP4 is a color/ordering check, not a timing trace. Run `./scarlett/tests/radv-present.sh` to build the synthetic fixture and check the decoded colors.

For external input on private Sway, set `SCARLETT_INPUT_TITLE` to the exact foreground window title at launch.
After the target is ready, send e.g. `timeout 2 sh -c 'printf "space 700\n" >"$1/input.fifo"' _ "$run"`
where `run` is the printed output directory. `space` maps to A; `w a s d` and `Up Down Left Right` map to sticks.
Holds are serialized and limited to 1–2000 ms. The owner-only FIFO accepts data, not shell commands.
A separate helper uses Wine `SendInput` in that run's prefix, checking the foreground title and executable;
the target does not need hooks or changes. See `logs/input.log` for accepted/refused commands.
Run `SCARLETT_GAMEINPUT_HEADER=/path/to/10.0.26100/um/GameInput.h ./scarlett/tests/radv-gameinput.sh`
to exercise the installed DLL and verify neutral/held/released in a **synthetic** GPU recording.

## What the first graphics test covers

The runtime also includes an original [xmem.dll](native/xmem/README.md) supporting ordinary
CPU pages and title allocation hooks. The other providers cover [DSP registration](native/chad/README.md),
[audio device discovery](native/xapu/README.md), [APU memory interfaces](native/acphal/README.md),
[texture layout suggestions](native/xg/README.md), and [read-only storage files](native/xcrdapi/README.md).
The console storage test opens a file through the title's interface and keeps reading from its handle after the COM object closes. Storage queue submission remains unsupported.
The [Direct queue adapter test](native/queue/README.md) verifies that a GPU wait stays blocked until the full 64-bit fence value matches, and that the CPU word returned by fence `+0x60` changes after GPU completion.
The [device bootstrap test](native/d3d12_device.md) verifies that a default Compute queue waits for a Direct queue's signal. The four observed indexed Compute descriptors each receive a distinct logical queue; tests verify fence ordering and independent progress, not physical engine affinity.
On the RTX 3070 and RADV 26.3.0, the test also submits Direct and Compute lists, waits for their fences, then resets and reuses each allocator. The four [synchronization requests observed in the startup lists](native/d3d12_device.md#what-the-two-pre-close-calls-consume) record conservative global GPU barriers. Other masks are rejected at Close.
Each provider lists the operations it supports and how unsupported requests fail.
Run `./scarlett/tests/native-providers.sh` to exercise the installed DLLs through Proton
with synthetic callers (including an EXE exporting its own allocation hooks).

`./scarlett/tests/queue-fence.sh` builds a synthetic PE importing `d3d12_xs.dll`. It serializes the empty version 1.1 local root signature seen at retail RVA `+0x247fc80`, creates a desktop D3D12 root signature from the resulting blob, and verifies queue/fence synchronization. It runs in an isolated Wine prefix on the NixOS/NVIDIA test host. The [Xbox device identity test](native/d3d12_device.md) covers the separate console factory. Neither test loads retail shaders. `SCARLETT_RECORD=1` enables recording of the private test display.
