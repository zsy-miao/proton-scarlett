# Release on the successor Direct list

Build the three-display regression with one of these token-2 variants:

```sh
TEST_SAME_BATCH_RELEASE=1 scarlett/tests/build-ordered-display.sh pair-game
TEST_PAIR_SPLIT=1 scarlett/tests/build-ordered-display.sh split-game
python3 scarlett/tests/test-same-batch-admission.py
python3 scarlett/tests/test-pair-segments.py
```

Use the private launcher and unchanged three-frame `ordered-display.py` from
[ordered-display.md](ordered-display.md) for **each** image on RADV and NVIDIA.
Verify each image's SHA256SUMS on both runs. Require exit 0, WSI 0→1→2→3,
A/B/A key reuse and the decoded red/blue → green/magenta → yellow/cyan
sequence. Require `PASS same-batch admission:` in the untouched log; for
`split-game`, also require `PASS split flags-3:`. The older two-frame captures
do not qualify these builds.

Both variants passed on RADV and RTX with the same executable per pair
(`9ca53b10…` unsplit, `ef440d05…` split). All four runs counted three
flips and showed the three patterns in order. The recycled primary had
RTV serial 9 and a new end fence. Both split runs kept the tail blocked
until the original counter opened.

The producer acquires the registered primary, clears it, copies the test image,
restores RT and freezes the RTV at its final slot and serial. The successor
owns a reference to that **specific immutable predecessor**. It separately
pins the key and host and freezes the supplied slot/serial. Its saved whole-RT
`2→1f` wire records intent only. Neither recorder publishes a display release.

The candidate accepts exactly `[producer, successor]` on the same Direct
queue, token 2, after the normal sync. The queue lock protects generation
membership, single use and order; the frame lock protects token association.
The registry commit checks the live key/host/slot/serial, no multi-writer
history or active flip, same (or unclaimed) writer, and the writer epoch
sampled when the producer sealed. A later tracked write on the *same* queue
also invalidates the pair. A successful claim excludes unordered writers;
native exposure is refused through retirement. Failed checks consume neither
generation. The dedicated intent-only successor is lowered into a private
RT→COMMON list in its original array position. Both generations retire on the
single ordered batch end used for Present. The split control drains after
all rejections. Opening the queue gate admits prefix/head/timestamp/post, but
with the counter at zero the sentinel pixels, lease and end must stay pending.
Opening the counter admits the tail and successor; verify the green readback
and completed end before Present.

The fixture checks wrong predecessor, reversed, duplicate and intervening
members, foreign queue, a sibling's valid registration, malformed range,
same-slot/same-key rewrite, and an actual intervening UAV→RT clear→UAV
submission. Those candidates must make zero ordered host submissions. The
fresh pair is gated, drops caller refs and checks the original end fence,
WSI count and visible green/magenta output. Its pixels come from known copy
work. There is no invented composite draw.

**Retail boundary.** These private recordings reject `offered` and `deferred`;
they neither relax nor exercise the retail receipt matcher. Both generations
use private allocators and the successor contains only release intent. The
RTX retail request shared an allocator and a receipt. Both adapters requested
a timestamp *after* the release. Neither the calibrated synthetic split
nor its copy pixels qualify those commands, table/VB/chunk lifetimes or an
ordinary two-member submission.
