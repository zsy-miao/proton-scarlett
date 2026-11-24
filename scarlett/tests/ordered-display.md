# Ordered private display regression

Build once from the repository root with x86-64 MinGW and `dxc` on PATH (`DXC` can override it):

```sh
scarlett/tests/build-ordered-display.sh ordered-game
```

The build compiles the project's `test-primary-barrier.hlsl` and writes hashes for the PE and shader. Use a staged bridge-capable tool and the locked launcher. Run serially, with `TOOL` set to that tool's directory:

```sh
(cd ordered-game && sha256sum -c SHA256SUMS)
SCARLETT_TEST_GPU=radv SCARLETT_TEST_DISPLAY=sway SCARLETT_RECORD=1 \
    scarlett/test-launch-nixos.sh ordered-game "$TOOL"
python3 scarlett/tests/ordered-display.py "$RADV_RUN" radv
(cd ordered-game && sha256sum -c SHA256SUMS)
SCARLETT_TEST_GPU=nvidia SCARLETT_TEST_DISPLAY=xvfb SCARLETT_RECORD=1 \
    scarlett/test-launch-nixos.sh ordered-game "$TOOL"
python3 scarlett/tests/ordered-display.py "$NVIDIA_RUN" nvidia
```

Set `RADV_RUN` and `NVIDIA_RUN` to the respective launcher output directories. The PE resolves the D3D12 device LUID: `1002:164e` requires the WB allocation and original-section alias; `10de:2484` requires the CPU-control view. Unknown adapters and failed setup stop. The head marker and timestamp must arrive while the tail remains gated.

Token 1 presents red/blue. For token 2 the first closed Direct generation fills an intermediate, and the second copies it to a different registered primary. Reversing them must submit neither. The test drops caller references behind a gate, rejects the sibling key and an unrelated completed fence, then checks retirement, the original end, same captured window generation, two WSI count advances and both decoded spatial patterns.

Token 3 exercises reuse. After the DXGI backbuffer index wraps, rewrite A's original RTV slot to B and back to A, record a new singleton on the same producer, and present yellow/cyan. On RADV and RTX the common fixture (`92425fda…`) reused A with RTV serial 8 instead of 5 and a new batch fence. The count advanced 0→1→2→3 on the same window generation, and the decoded captures contained red/blue, green/magenta and yellow/cyan in that order.

Only the fixture's `testq.c` compiles the ordered factory and submit helper. They require a producer lease protecting command storage against reuse. A current or previous colour VB (resource or placed backing), a pending table, and queue-side split operations each disqualify a recording. The ordinary provider has no ordered entry point or queue-dispatch branch.

In the frame-feedback regression an early flagged poll supplied the token
for the first real flip. After the raw display count reached 2, the clock
still confirmed token 1 from an exact observation on that same swapchain.
It likewise confirmed token 2 after count 3. Both adapters passed. A later
count alone cannot establish an earlier presentation.

**Qualification:** Both adapters passed the fixture-only PE built on `e94ccab7` (SHA-256 `1a0fe844482178ec5a89b02e9ffe400ddaabce58681b5bd202637f48608be0db`). Both rejected the unrelated fence, advanced WSI 0→1→2 on one window generation and displayed red/blue followed by green/magenta. The earlier common PE (`b04e501f…`) also passed; its private recordings show the two patterns at samples 26→50 and 42→65 on RADV and RTX, respectively.

**Attribution:** The ordered generation and two-list fixture derive from the project's scratch commit `86bb7701`. Presentation, window leasing, tail calibration and pixel decoding reuse the project bridge fixtures. Behavioral references were [XCL queue translation](https://github.com/ivall/xcl/blob/930e8b49c1c2ec683d22c724a398b39111cd0e56/crates/xcl/src/hle/d3d12x/real.rs#L5129-L5139) (GPL-2.0-only) and [VIctory submission](https://github.com/Marvisak/VIctory/blob/ffe310eb2098e52169f4c44db5ffdabf43a72622/crates/victory-gpu/src/queue.rs#L500-L546) (GPL-3.0-or-later); no source from either was incorporated.
