# Device +0x428: private backing capture

`ScarlettCapturePlacedV1` takes the exact code base and 174 MiB width. The
shared XMem page lock pins three complete views of one 2,960-page allocation
in VA order: code (27 MiB), WC data (47 MiB), WB data (100 MiB). The other
two extents need not be mapped. Use XMem to unmap before editing the title
reservations, as required by the existing page API. GPU imports use only the
pinned handles after unlocking; there is no subsequent title VA lookup.

`ScarlettPlacedDataV1` returns a borrowed data resource and checked
extent-relative byte range. A trusted recorder must use that range, check
its operation-specific constraints (e.g. alignment, state and device), and
keep the capture alive through the last queue fence. Empty ranges, crossings,
code and out-of-bounds requests return no operand. The code resource
stays inside XMem. The production factory still fails: a whole-arena
descriptor has no checked consumer on this baseline.

`ScarlettPlacedCopyToV1(capture, sourceOffset, bytes, list, dst, dstOffset,
&retained)` accepts any nonempty in-bounds buffer copy in the captured
offset space. It checks device identity and both ranges before recording,
then splits at 27 and 74 MiB. In particular `8404` is a data source only.
The recorder supplies an open direct, compute or copy list, compatible
resource states and an independently backed, non-aliasing destination.
Keep the destination and the returned capture reference through the last
execution fence (or discard the unsubmitted recording); release the reference
with `ScarlettReleasePlacedV1`. Failure clears the output and records nothing.

Build `test-placed-capture.cpp` like `test-descriptor-pages.cpp`; it needs
no DXIL files. Launch the synthetic with `SCARLETT_TEST_GPU=radv`. It
rejects missing and mixed runs, copies across both boundaries and reads the
last byte of two generations after both runs were freed and the VAs reused.
A second independent device on the same adapter checks both device mismatches.

`test-arena-data.cpp` obtains both writable resources from one capture.
Each extent has one UAV-capable buffer over its original heap, created
in `COMMON` state. On RADV, a synthetic shader verified 38,535,168 words
through the live title mappings, then again after freeing and reusing
both addresses before submission. Both replacement mappings stayed zero.
The test does not bind the code region as a shader resource.

To reproduce, build `test-arena-data.cpp` like `test-placed-capture.cpp`
with the extra link flag `-ldxgi`. Compile the adjacent HLSL with
`dxc -T cs_6_0 -E main -Fo arena-data.dxil test-arena-data.hlsl` and place
`arena-data.dxil` beside the test executable.
