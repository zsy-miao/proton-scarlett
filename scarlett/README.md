# scarlett-proton: Steam setup and testing

This x86-64 Steam tool starts `game_scarlett_master.exe` through [XCL](https://github.com/ivall/xcl)
inside Proton 11. XCL maps the console PE, initializes title DLLs and TLS, and translates Scarlett
graphics calls to desktop D3D12. Proton supplies Wine and vkd3d-proton. The game is not playable
in the tested configuration.

## Use in Steam

1. Restart Steam after installing the tool.
2. Add the **package directory's** `game_scarlett_master.exe` as a non-Steam game. Set **Start In**
   to its directory, alongside `MicrosoftGame.Config`, the DLLs and the game data.
3. In **Properties → Compatibility**, force **scarlett-proton** and launch.

No launch options are required. For a Proton log set `PROTON_LOG=1 %command%`. XCL writes
`xcl.log` and `report.json` under the shortcut's
`steamapps/compatdata/<id>/pfx/drive_c/users/steamuser/AppData/Local/XCL/<title>/logs/`.
Network calls through XCL are disabled for this title because its crash reporter uploads minidumps.
That setting is not an OS sandbox.

## Reproduce the build

From the fork root on x86-64 Linux, with rustup, an x86-64 MinGW GCC toolchain, git, curl,
Python 3 and GNU build utilities:

```bash
rustup toolchain install 1.91.1 --profile minimal --target x86_64-pc-windows-gnu
./scarlett/build-runtime.sh "$PWD/scarlett/runtime"
steam="$HOME/.local/share/Steam"
./scarlett/install.sh "$steam/steamapps/common/Proton 11.0" "$PWD/scarlett/runtime" \
    "$steam/compatibilitytools.d/scarlett-proton"
```

The installer expects the matching Valve 11.0-2e depot and a **new** destination. It makes an
independent copy; updates to stock Proton do not change the fork. For a full SDK build instead,
build `scarlett/runtime` as above, initialize Valve's submodules and follow the upstream
[Proton build instructions](../README.md). The x86-64 distribution target includes the staged runtime.

For the XAudio2 fixes, apply `git -C wine apply ../scarlett/proton11-faudio.patch` to the
pinned Wine submodule **before** the SDK build. For depot installs, put the resulting
x64 Wine builtin `xaudio2_9.dll` in `scarlett/runtime/` before running `install.sh`.
Without that file, the depot installer retains Valve's original audio module. To build just
that DLL, run `./scarlett/rebuild-xaudio.sh /path/to/new-build-directory` and copy
`build/dlls/xaudio2_9/x86_64-windows/xaudio2_9.dll` from that directory into `scarlett/runtime/`.

The build pins Proton at `1f089f5610a882c8bfcb4b58a3c86ee88bb4b9f1`, XCL at
`7ebf1b8044bef8f0ef296eb36e85010cceb4633a`, and includes the [XCL Wine crash-report fix](https://github.com/ivall/xcl/pull/147).
DXC comes from Microsoft's checksum-verified `v1.9.2609` release. XCL, DXC and Proton retain
their separate upstream licenses. No game files are distributed.

## Local verification

The title executable has SHA-256 `e95020b2fa4a721db3df59fdecf75621730ac559d8835252f6c3b658d5a08993`
(build 1.0.2.1). Stock Proton stopped before entry with `0xc0000135`. This fork, using
`waitforexitandrun`, loaded the title DLLs and TLS, created a real D3D12 backend on an RTX 3070
and executed the game's entry point (`+0x56a1ec8`) with the package mounted read-only.
The first run stopped with exit 3 when vkd3d-proton detected a modified
heap vtable. Preserving the native heap vtable got past that assertion, XGameRuntime startup
and GameInput registration. The game's own crash log then reported **System Init, frame 0**,
with a breakpoint at `+0x2237178`. A Wine file trace showed `G:\appdata0.rpf` missing from
both the package and XCL's overlay just before the breakpoint. The [1.0.2.1 package inventory](https://github.com/ItsNotRexed/Leonida/blob/8575d36edc479218972497a7bded8651a42856c0/game/xbox-1.0.2.1.json)
lists that archive as 8,880 bytes. With its SHA-256 verified, the next run opened the archive
and reached a later breakpoint (`+0x1ce6e13`), still in System Init with zero presents.
With `data.rpf` subsequently verified, the retail host reported 93 presents before a breakpoint
at `+0x1dac734`; the game's own crash log reported `Game Init`, frame 0. Frame contents and
gameplay were not verified in that run. With `core_bvh4.rpf` verified, a later run reached
1,360 presents and then faulted in `d3d12core.dll+0x219546`. The sampled private display
remained black apart from the cursor. `python3 scarlett/test_route.py` checks path and argument handling.

To repeat the actual test on this NixOS/NVIDIA host without restarting Steam:

```bash
./scarlett/test-launch-nixos.sh "/path/to/package"
```

This uses `steam-run`'s filesystem, Xvfb, a fresh prefix and no network. It prints the private
test directory containing `logs/steam-999001.log` and the XCL report. It returns the process
status, or 124 if the 90-second test window expires.
For visual checks, `SCARLETT_TEST_SECONDS=360 XCL_FRAME_DUMP=120` allows six minutes and
saves sampled back buffers as `frame_*.bmp` in the title's XCL logs directory.
Set `SCARLETT_RECORD=1` to record the sandbox's private Xvfb framebuffer to `display.mp4`
(silent H.264, 12 fps) and update `display.png` once a second in the printed test directory.
This uses the host FFmpeg inside the sandbox; the host display is never opened.

`./scarlett/tests/input-frame.sh` launches a synthetic window in the private Xvfb harness.
It sends one Space press/release to the focused window and checks that the screen pixel
changes from dark gray to white. This checks Win32 input delivery, not GameInput mapping.

`./scarlett/tests/queue-fence.sh` runs the hardware synchronization test without retail files.
`./scarlett/tests/gameinput.sh` holds Space on a synthetic window for 400 ms and checks
XCL GameInput's neutral → A → neutral readings. Its helper takes `KEYSYM HOLD_MS WINDOW_XID`
inside the harness and verifies that its Unix socket peer is the private Xvfb before sending input.
On the RTX 3070, the console title's CPU signal, GPU queue wait/signal, and completion event
passed. The broader XCL test title also creates fallback graphics pipelines for two console
shader fixtures. The descriptor-heap test verifies that the console sampler handles share their
allocation and that it remains valid while the driver holds the heap. The resource test
verifies native COM identity, title and host mappings, and cleanup after final release.
The broader test renders a triangle on the RTX 3070, verifies the pixels by readback, and
presents one 640×360 frame. With the patched XAudio2 builtin and a private PulseAudio sink,
the fixture also decodes XMA2 to PCM and receives buffer-end callbacks. It finishes with
50 checks passed and exit code 0 when launched under its upstream fixture name,
`xcl_testtitle.exe`. Opus callbacks used the silence fallback; Opus decoding remains unverified.
